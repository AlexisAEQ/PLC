/**
 * @file Steppermotor.cpp
 * @brief Implémentation de StepperMotor (axe pas-à-pas FastAccelStepper, interface ServoDrive).
 *
 * Voir StepperMotor.h et docs/axes/stepper.md.
 *
 * Compatibilité FastAccelStepper :
 * - 0.33.x : sur ESP-IDF 5, seul le générateur RMT existe (pas de choix du générateur) ;
 * - 2.0.x  : MCPWM/PCNT + RMT (+ I2S) sur ESP-IDF >= 5.3, choix par stepperConnectToPin(pin, driver).
 * Seule l'API commune aux deux versions est utilisée (init, stepperConnectToPin, setDirectionPin,
 * setEnablePin, setSpeedInHz, setAcceleration, moveTo, runForward/runBackward, stopMove, forceStop,
 * isRunning, getCurrentPosition, setCurrentPosition, enableOutputs/disableOutputs,
 * getCurrentSpeedInMilliHz).
 */

#include <Arduino.h>
#include <FastAccelStepper.h>   // avant les en-têtes du projet (macros ERROR/INFO/...)
#include <stdarg.h>
#include <math.h>
#include <type_traits>
#include <utility>

#include "StepperMotor/StepperMotor.h"
#include "BorneUniverselle/borneUniverselle.h"
#include "PLC_Tools/PLC_Tools.h"

// ========================================
// MOTEUR PARTAGÉ ET REGISTRE DES AXES
// ========================================

namespace {

// Un FastAccelStepper ne peut pas être libéré : il reste attaché à sa broche STEP. Le registre
// permet de le réutiliser si un axe est détruit puis recréé sur la même broche (rechargement de
// configuration) et de refuser deux axes sur une même broche.
struct StepperSlot {
    uint8_t stepPin;
    FastAccelStepper* stepper;
    const StepperMotor* owner;   // nullptr : libre (axe détruit)
    const char* ownerId;
};

StepperSlot g_stepperSlots[StepperMotor::MAX_STEPPERS] = {};
uint8_t g_stepperSlotCount = 0;

// Phases de la prise d'origine
enum : uint8_t {
    HOMING_START = 0,
    HOMING_CLEAR_SWITCH,      // déjà sur le capteur : on s'en dégage
    HOMING_CLEAR_STOP,        // arrêt après dégagement
    HOMING_SEEK,              // recherche du capteur
    HOMING_SEEK_STOP,         // arrêt décéléré après détection
    HOMING_BACKOFF            // dégagement final
};

uint32_t clampU32(int64_t value, uint32_t minValue, uint32_t maxValue) {
    if (value < (int64_t)minValue) return minValue;
    if (value > (int64_t)maxValue) return maxValue;
    return (uint32_t)value;
}

bool timeReached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

// Étage de sortie inversé déclaré dans le fichier matériel (HardwareBooleanOutputNode::isActiveLow()).
// FastAccelStepper écrit la GPIO directement, sans passer par le nœud : l'inversion est donc
// reportée sur la polarité DIR/ENABLE. Détection à la compilation pour rester compatible avec
// une version de Node.h sans isActiveLow().
template <typename T, typename = void>
struct HasIsActiveLow : std::false_type {};
template <typename T>
struct HasIsActiveLow<T, std::void_t<decltype(std::declval<const T&>().isActiveLow())>> : std::true_type {};

template <typename T>
bool outputActiveLow(const T* node) {
    if constexpr (HasIsActiveLow<T>::value) {
        return node && node->isActiveLow();
    } else {
        (void)node;
        return false;
    }
}

}  // namespace

FastAccelStepperEngine& StepperMotor::sharedEngine() {
    // Construit et initialisé une seule fois, au premier axe connecté.
    static FastAccelStepperEngine engine;
    static const bool started = []() {
        engine.init();
        Serial.println("[STEPPER] FastAccelStepperEngine initialisé (partagé par tous les axes)");
        return true;
    }();
    (void)started;
    return engine;
}

// ========================================
// CONSTRUCTEUR / DESTRUCTEUR
// ========================================

StepperMotor::StepperMotor(uint32_t stepsPerUnitParam,
                           const StepperNodes& nodesParam,
                           uint32_t maxRangeStepsParam,
                           const char* id,
                           uint32_t accelerationStepsPerS2)
    : ServoDrive(id),
      stepsPerUnit(stepsPerUnitParam ? stepsPerUnitParam : 1),
      maxRangeSteps(maxRangeStepsParam),
      nodes(nodesParam),
      accelerationHz2(clampU32(accelerationStepsPerS2, MIN_ACCELERATION, MAX_ACCELERATION)) {

    if (stepsPerUnitParam == 0) {
        axisLog("stepsPerUnit = 0 remplacé par 1");
    }
    if (accelerationHz2 != accelerationStepsPerS2) {
        axisLog("Accélération %lu pas/s² bornée à %lu", (unsigned long)accelerationStepsPerS2,
                (unsigned long)accelerationHz2);
    }

    if (maxRangeSteps > 0) {
        softLimitsEnabled = true;
        softLimitMin = 0;
        softLimitMax = (int32_t)clampU32(maxRangeSteps, 0, INT32_MAX);
    }

    // Broches : lues sur les nœuds GPIO, jamais écrites par la classe.
    pinsValid = extractPinNumber(nodes.stepPin, "STEP", stepPinNumber) &&
                extractPinNumber(nodes.dirPin, "DIR", dirPinNumber);

    if (pinsValid && stepPinNumber == dirPinNumber) {
        setError("STEP et DIR sur la même GPIO %u", (unsigned)stepPinNumber);
        pinsValid = false;
    }

    if (pinsValid) {
        dirOutputInverted = outputActiveLow(nodes.dirPin);
        if (outputActiveLow(nodes.stepPin)) {
            axisLog("Attention : sortie STEP déclarée active basse, impulsions inversées à la borne");
        }
    }

    if (nodes.enable) {
        if (nodes.enable->classType() == CLASS_HW_BOOLEAN_OUTPUT_NODE) {
            HardwareBooleanOutputNode* hwEnable = static_cast<HardwareBooleanOutputNode*>(nodes.enable);
            enablePinNumber = hwEnable->getPinNumber();
            enableOutputInverted = outputActiveLow(hwEnable);
            if (pinsValid && (enablePinNumber == stepPinNumber || enablePinNumber == dirPinNumber)) {
                setError("ENABLE (GPIO %u) partage une broche avec STEP/DIR", (unsigned)enablePinNumber);
                pinsValid = false;
            }
        } else {
            enableViaNode = true;
        }
    }

    if (!pinsValid) {
        configError = lastError;
    }

    char enableText[16];
    if (enablePinNumber != PIN_NONE) {
        snprintf(enableText, sizeof(enableText), "GPIO%u", (unsigned)enablePinNumber);
    } else {
        snprintf(enableText, sizeof(enableText), "%s", enableViaNode ? "nœud" : "aucun");
    }
    axisLog("Axe pas-à-pas créé : STEP=GPIO%u DIR=GPIO%u ENABLE=%s, accélération %lu pas/s², course %lu pas, "
            "%u nœud(s) optionnel(s)%s",
            (unsigned)stepPinNumber, (unsigned)dirPinNumber, enableText,
            (unsigned long)accelerationHz2, (unsigned long)maxRangeSteps,
            (unsigned)nodes.countOptional(), pinsValid ? "" : " - CONFIGURATION INVALIDE");
}

StepperMotor::~StepperMotor() {
    // Ne touche pas aux nœuds (ils peuvent déjà être détruits) : seulement FastAccelStepper.
    if (fasMotor) {
        if (fasMotor->isRunning()) {
            fasMotor->forceStop();
        }
        if (enablePinNumber != PIN_NONE) {
            fasMotor->disableOutputs();
        }
    }
    for (uint8_t i = 0; i < g_stepperSlotCount; i++) {
        if (g_stepperSlots[i].owner == this) {
            g_stepperSlots[i].owner = nullptr;
            g_stepperSlots[i].ownerId = nullptr;
        }
    }
    axisLog("Axe détruit");
}

// ========================================
// OUTILS DE TRACE
// ========================================

void StepperMotor::axisLog(const char* fmt, ...) const {
    char buf[224];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[%s] %s\n", servoId.c_str(), buf);
}

void StepperMotor::setError(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    const uint32_t now = millis();
    const bool same = (lastError == buf);
    lastError = buf;
    // Pas de rafale sur la liaison série quand une commande refusée est répétée à chaque cycle.
    if (!same || now - lastErrorLogMs > 2000) {
        Serial.printf("[%s] ERREUR : %s\n", servoId.c_str(), buf);
        lastErrorLogMs = now;
    }
}

void StepperMotor::userMessage(uint8_t type, const char* fmt, ...) const {
    char text[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    char msg[200];
    snprintf(msg, sizeof(msg), "[%s] %s", servoId.c_str(), text);
    Serial.println(msg);
    BorneUniverselle::prepareMessage(type, msg);
}

// ========================================
// BROCHES ET CONNEXION
// ========================================

bool StepperMotor::extractPinNumber(HardwareBooleanOutputNode* node, const char* pinName, uint8_t& pinNumber) {
    if (!node) {
        setError("Nœud %s absent (nœud GPIO \"tx-bool\" obligatoire)", pinName);
        return false;
    }
    if (node->classType() != CLASS_HW_BOOLEAN_OUTPUT_NODE) {
        setError("Nœud %s : HardwareBooleanOutputNode (GPIO \"tx-bool\") attendu, type %d reçu",
                 pinName, node->classType());
        return false;
    }
    const uint8_t pin = node->getPinNumber();
    bool valid = pin < 64;
#ifdef GPIO_IS_VALID_OUTPUT_GPIO
    valid = valid && GPIO_IS_VALID_OUTPUT_GPIO(pin);
#endif
    if (!valid) {
        setError("Nœud %s : GPIO %u inutilisable en sortie", pinName, (unsigned)pin);
        return false;
    }
    pinNumber = pin;
    return true;
}

FastAccelStepper* StepperMotor::acquireStepper(uint8_t pin) {
    // Broche déjà connue : réutilisation (axe recréé) ou conflit.
    for (uint8_t i = 0; i < g_stepperSlotCount; i++) {
        StepperSlot& slot = g_stepperSlots[i];
        if (slot.stepPin != pin) continue;
        if (slot.owner && slot.owner != this) {
            setError("GPIO STEP %u déjà utilisée par l'axe %s", (unsigned)pin, slot.ownerId ? slot.ownerId : "?");
            return nullptr;
        }
        slot.owner = this;
        slot.ownerId = servoId.c_str();
        driverName = "réutilisé";
        return slot.stepper;
    }

    if (g_stepperSlotCount >= MAX_STEPPERS) {
        setError("Trop d'axes pas-à-pas (max %u)", (unsigned)MAX_STEPPERS);
        return nullptr;
    }

    FastAccelStepperEngine& engine = sharedEngine();
    FastAccelStepper* stepper = nullptr;

#if defined(SUPPORT_SELECT_DRIVER_TYPE)
#if defined(QUEUES_MCPWM_PCNT) && (QUEUES_MCPWM_PCNT > 0)
    if (!stepper && (driverType == DriverType::AUTO || driverType == DriverType::MCPWM_PCNT)) {
        stepper = engine.stepperConnectToPin(pin, FasDriver::MCPWM_PCNT);
        if (stepper) driverName = "MCPWM/PCNT";
    }
#endif
#if defined(QUEUES_RMT) && (QUEUES_RMT > 0)
    if (!stepper && (driverType == DriverType::AUTO || driverType == DriverType::RMT)) {
        stepper = engine.stepperConnectToPin(pin, FasDriver::RMT);
        if (stepper) driverName = "RMT";
    }
#endif
    if (!stepper && driverType == DriverType::AUTO) {
        stepper = engine.stepperConnectToPin(pin, FasDriver::DONT_CARE);
        if (stepper) driverName = "autre (I2S...)";
    }
#else
    // Version/cible sans choix du générateur (ex. FastAccelStepper 0.33 sur ESP-IDF 5 : RMT seul).
    if (driverType != DriverType::AUTO) {
        axisLog("Choix du générateur non supporté par cette version de FastAccelStepper : ignoré");
    }
    stepper = engine.stepperConnectToPin(pin);
    if (stepper) driverName = "défaut bibliothèque";
#endif

    if (!stepper) {
        setError("FastAccelStepper refuse la GPIO STEP %u (broche invalide ou plus de générateur libre)",
                 (unsigned)pin);
        return nullptr;
    }

    StepperSlot& slot = g_stepperSlots[g_stepperSlotCount++];
    slot.stepPin = pin;
    slot.stepper = stepper;
    slot.owner = this;
    slot.ownerId = servoId.c_str();
    return stepper;
}

bool StepperMotor::connectStepper() {
    if (!pinsValid) {
        setError("Configuration invalide : %s", configError.c_str());
        return false;
    }

    // Conflits avec les autres axes (une broche STEP ne peut servir qu'une fois).
    for (uint8_t i = 0; i < g_stepperSlotCount; i++) {
        const StepperSlot& slot = g_stepperSlots[i];
        if (!slot.owner || slot.owner == this) continue;
        if (slot.stepPin == dirPinNumber || (enablePinNumber != PIN_NONE && slot.stepPin == enablePinNumber)) {
            setError("GPIO %u déjà utilisée comme STEP par l'axe %s",
                     (unsigned)slot.stepPin, slot.ownerId ? slot.ownerId : "?");
            return false;
        }
        if (slot.stepper && slot.stepper->getDirectionPin() == dirPinNumber) {
            axisLog("Attention : DIR (GPIO %u) partagée avec l'axe %s", (unsigned)dirPinNumber,
                    slot.ownerId ? slot.ownerId : "?");
        }
    }

    if (!fasMotor) {
        fasMotor = acquireStepper(stepPinNumber);
        if (!fasMotor) return false;
    }

    fasMotor->setDirectionPin(dirPinNumber, dirHighCountsUp());
    if (enablePinNumber != PIN_NONE) {
        // "Actif bas" par défaut : driver activé quand la sortie ENABLE est au repos.
        // setEnablePin() place immédiatement le driver à l'état désactivé.
        fasMotor->setEnablePin(enablePinNumber, enableActiveLow != enableOutputInverted);
        fasMotor->setAutoEnable(false);
    }
    fasMotor->setAcceleration((int32_t)accelerationHz2);
    fasMotor->setSpeedInHz(moveSpeedHz);

    axisLog("Connecté à FastAccelStepper : STEP=GPIO%u (générateur %s), DIR=GPIO%u%s, ENABLE=%s",
            (unsigned)stepPinNumber, driverName, (unsigned)dirPinNumber, directionInverted ? " (inversé)" : "",
            enablePinNumber != PIN_NONE ? (enableActiveLow ? "GPIO actif bas" : "GPIO actif haut")
                                        : (enableViaNode ? "nœud (setValue)" : "aucun"));
    return true;
}

// ========================================
// ACTIVATION DU DRIVER
// ========================================

void StepperMotor::applyEnableOutput(bool on) {
    const bool hasEnableOutput = (enablePinNumber != PIN_NONE) || enableViaNode;

    if (fasMotor && enablePinNumber != PIN_NONE) {
        if (on) {
            fasMotor->enableOutputs();
        } else {
            fasMotor->disableOutputs();
        }
    } else if (enableViaNode && nodes.enable) {
        // Même convention que la GPIO : "actif bas" = sortie inactive (false) pour activer.
        nodes.enable->setValue(on ? !enableActiveLow : enableActiveLow);
    }

    if (on && !enabled) {
        enabledSinceMs = millis();
    }
    if (!on && enabled && hasEnableOutput && homeLostOnDisable) {
        invalidateHome("driver désactivé (couple de maintien perdu)");
    }
    enabled = on;
}

bool StepperMotor::enableSettled(uint32_t now) const {
    if (!enabled) return false;
    const bool hasEnableOutput = (enablePinNumber != PIN_NONE) || enableViaNode;
    return !hasEnableOutput || (now - enabledSinceMs >= enableDelayMs);
}

void StepperMotor::invalidateHome(const char* reason) {
    if (homingDone) {
        homingDone = false;
        axisLog("Origine perdue : %s - nouvelle prise d'origine nécessaire", reason);
    }
}

bool StepperMotor::setServoEnabled(bool enable) {
    if (enable) {
        if (!fasMotor) {
            setError("Activation impossible : axe non initialisé");
            return false;
        }
        if (!enabled) {
            applyEnableOutput(true);
            axisLog("Driver activé");
        }
        return true;
    }

    if (enabled) {
        if (fasMotor && fasMotor->isRunning()) {
            fasMotor->forceStop();
            if (homeLostOnDisable) invalidateHome("arrêt brutal (désactivation en mouvement)");
        }
        jogDirection = 0;
        jogStarted = false;
        if (moveState == OperationState::DRIVE_IN_PROGRESS || homingState == OperationState::DRIVE_IN_PROGRESS) {
            setError("Driver désactivé pendant un mouvement");
            failActiveOperations("Driver désactivé pendant un mouvement");
        }
        applyEnableOutput(false);
        axisLog("Driver désactivé");
    }
    return true;
}

// ========================================
// VITESSES / ACCÉLÉRATION
// ========================================

bool StepperMotor::setMoveSpeed(int32_t stepsPerSecond) {
    const uint32_t speed = clampU32(stepsPerSecond, MIN_SPEED_HZ, MAX_SPEED_HZ);
    if (speed != moveSpeedHz) {
        moveSpeedHz = speed;
        axisLog("Vitesse des déplacements : %lu pas/s%s (prochain déplacement)", (unsigned long)speed,
                (int64_t)speed != (int64_t)stepsPerSecond ? " (valeur demandée bornée)" : "");
    }
    return true;
}

bool StepperMotor::setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) {
    (void)rampIndex;   // l'accélération vient du constructeur / setAcceleration()
    if (speed == 0) {
        setError("setSpeedAndRamp : vitesse 0 refusée");
        return false;
    }
    return setMoveSpeed((int32_t)speed * 1000);
}

bool StepperMotor::setJogSpeed(uint32_t stepsPerSecond) {
    if (stepsPerSecond == 0) {
        setError("Vitesse de jog nulle refusée");
        return false;
    }
    const uint32_t speed = clampU32(stepsPerSecond, MIN_SPEED_HZ, MAX_SPEED_HZ);
    if (speed != jogSpeedHz) {
        jogSpeedHz = speed;
        axisLog("Vitesse de jog : %lu pas/s", (unsigned long)speed);
    }
    return true;   // un jog en cours prend la nouvelle vitesse au prochain appel de jogForward/Reverse
}

bool StepperMotor::setAcceleration(uint32_t stepsPerS2) {
    const uint32_t accel = clampU32(stepsPerS2, MIN_ACCELERATION, MAX_ACCELERATION);
    if (accel != accelerationHz2) {
        accelerationHz2 = accel;
        axisLog("Accélération : %lu pas/s²", (unsigned long)accel);
    }
    if (fasMotor) {
        fasMotor->setAcceleration((int32_t)accelerationHz2);   // appliquée à la prochaine commande
    }
    return accel == stepsPerS2;
}

bool StepperMotor::setAccelerationProfile(uint32_t maxSpeedStepsPerSec, uint32_t accelStepsPerSec2) {
    setMoveSpeed((int32_t)clampU32(maxSpeedStepsPerSec, MIN_SPEED_HZ, MAX_SPEED_HZ));
    return setAcceleration(accelStepsPerSec2);
}

bool StepperMotor::setMaxTorque(uint8_t torquePercent) {
    (void)torquePercent;   // sans objet pour un pas-à-pas (réglage du courant sur le driver)
    return true;
}

bool StepperMotor::saveParameters() {
    return true;   // aucun paramètre stocké dans le driver
}

bool StepperMotor::applySpeed(uint32_t speedHz) {
    if (!fasMotor) return false;
    const int8_t rc = fasMotor->setSpeedInHz(clampU32(speedHz, MIN_SPEED_HZ, MAX_SPEED_HZ));
    if (rc < 0) {
        setError("Vitesse %lu pas/s refusée par FastAccelStepper (code %d)", (unsigned long)speedHz, (int)rc);
        return false;
    }
    fasMotor->setAcceleration((int32_t)accelerationHz2);
    return true;
}

bool StepperMotor::issueMoveTo(int32_t target, uint32_t speedHz) {
    if (!applySpeed(speedHz)) return false;
    const MoveResultCode rc = fasMotor->moveTo(target);
    if (!moveIsOk(rc)) {
        setError("moveTo(%ld) refusé par FastAccelStepper : %s", (long)target, toString(rc));
        return false;
    }
    const int32_t pos = fasMotor->getCurrentPosition();
    commandedDirection = (target > pos) ? 1 : ((target < pos) ? -1 : 0);
    return true;
}

bool StepperMotor::issueRun(int8_t direction, uint32_t speedHz) {
    if (!applySpeed(speedHz)) return false;
    const MoveResultCode rc = (direction > 0) ? fasMotor->runForward() : fasMotor->runBackward();
    if (!moveIsOk(rc)) {
        setError("Marche %c refusée par FastAccelStepper : %s", direction > 0 ? '+' : '-', toString(rc));
        return false;
    }
    commandedDirection = direction > 0 ? 1 : -1;
    return true;
}

void StepperMotor::setHomingParameters(int8_t direction, uint32_t speedHz, uint32_t backoffSteps) {
    homingDirection = (direction >= 0) ? 1 : -1;
    homingSpeedHz = speedHz ? clampU32(speedHz, MIN_SPEED_HZ, MAX_SPEED_HZ) : 0;
    homingBackoffSteps = (uint32_t)clampU32(backoffSteps, 0, INT32_MAX / 2);
}

void StepperMotor::setSoftLimits(int32_t minSteps, int32_t maxSteps) {
    if (minSteps > maxSteps) {
        const int32_t tmp = minSteps;
        minSteps = maxSteps;
        maxSteps = tmp;
    }
    softLimitMin = minSteps;
    softLimitMax = maxSteps;
    softLimitsEnabled = true;
}

void StepperMotor::setDirectionInverted(bool inverted) {
    directionInverted = inverted;
    if (fasMotor && !fasMotor->isRunning()) {
        fasMotor->setDirectionPin(dirPinNumber, dirHighCountsUp());
    }
}

void StepperMotor::setEnableActiveLow(bool activeLow) {
    if (fasMotor && activeLow != enableActiveLow) {
        axisLog("Polarité ENABLE modifiée après connexion : prise en compte à la prochaine initialisation");
    }
    enableActiveLow = activeLow;
}

// ========================================
// ÉTAT
// ========================================

int32_t StepperMotor::getPosition() const {
    return fasMotor ? fasMotor->getCurrentPosition() : 0;
}

int32_t StepperMotor::getCurrentSpeed() const {
    return fasMotor ? fasMotor->getCurrentSpeedInMilliHz() / 1000 : 0;
}

bool StepperMotor::isMoving() const {
    return fasMotor && fasMotor->isRunning();
}

bool StepperMotor::getIsReady() const {
    return initialized && fasMotor && enabled && alarmCode == ALARM_NONE &&
           initState != OperationState::DRIVE_FAILED && initState != OperationState::DRIVE_TIMEOUT;
}

bool StepperMotor::isImmediateStopActive() const {
    if (!stopRequested) return false;
    if (!timeReached(millis(), stopLockUntilMs)) return true;
    return isMoving();
}

const char* StepperMotor::getAlarmDescription() const {
    switch (alarmCode) {
        case ALARM_NONE:           return "";
        case ALARM_DRIVER:         return "Alarme du driver pas-à-pas (entrée ALM)";
        case ALARM_LIMIT_POSITIVE: return "Fin de course + atteinte";
        case ALARM_LIMIT_NEGATIVE: return "Fin de course - atteinte";
        default:                   return "Alarme inconnue";
    }
}

int8_t StepperMotor::motionDirection() const {
    if (!isMoving()) return 0;
    const int32_t mhz = fasMotor->getCurrentSpeedInMilliHz();
    if (mhz > 0) return 1;
    if (mhz < 0) return -1;
    return commandedDirection;   // juste après la commande, avant le premier pas
}

bool StepperMotor::limitActive(int8_t direction) const {
    BooleanInputNode* sw = (direction > 0) ? nodes.limitSwitchPositive : nodes.limitSwitchNegative;
    return sw && sw->getValue();
}

bool StepperMotor::targetWithinSoftLimits(int32_t target) const {
    if (!softLimitsEnabled || !homingDone) return true;
    return target >= softLimitMin && target <= softLimitMax;
}

uint32_t StepperMotor::estimateMoveMs(int64_t distance, uint32_t speedHz) const {
    const double d = fabs((double)distance);
    const double v = speedHz ? (double)speedHz : 1.0;
    const double a = accelerationHz2 ? (double)accelerationHz2 : 1.0;
    // Trapèze : d/v + v/a ; triangle (vitesse non atteinte) : 2*sqrt(d/a).
    const double seconds = (d >= v * v / a) ? (d / v + v / a) : 2.0 * sqrt(d / a);
    const double ms = seconds * 1000.0;
    return ms > 3.6e6 ? 3600000UL : (uint32_t)ms;
}

// ========================================
// PROCESS
// ========================================

void StepperMotor::process() {
    const uint32_t now = millis();

    syncParametersFromNodes();
    monitorDriverAlarm();
    monitorLimitSwitches();

    if (stopRequested && timeReached(now, stopLockUntilMs) && !isMoving()) {
        stopRequested = false;
    }

    if (initState == OperationState::DRIVE_IN_PROGRESS) processInitialize(now);
    if (resetState == OperationState::DRIVE_IN_PROGRESS) processReset(now);
    if (moveState == OperationState::DRIVE_IN_PROGRESS) processMove(now);
    if (homingState == OperationState::DRIVE_IN_PROGRESS) processHoming(now);
    if (jogDirection != 0) processJog(now);

    updateStatusNodes(now);

    const uint32_t elapsed = millis() - now;
    if (elapsed > 50) {
        axisLog("process() a duré %lu ms", (unsigned long)elapsed);
    }
}

// ========================================
// INITIALISATION
// ========================================

bool StepperMotor::startInitialize() {
    if (initState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Initialisation déjà en cours");
        return false;
    }
    initState = OperationState::DRIVE_IN_PROGRESS;
    initPhase = 0;
    initStartMs = millis();
    lastError = "";
    axisLog("Initialisation démarrée");
    return true;
}

void StepperMotor::processInitialize(uint32_t now) {
    if (now - initStartMs > initTimeoutMs) {
        if (nodes.hardwareStepperAlarm && nodes.hardwareStepperAlarm->getValue()) {
            setError("alarme du driver active");
        } else {
            setError("délai d'initialisation dépassé (%lu ms)", (unsigned long)initTimeoutMs);
        }
        initState = OperationState::DRIVE_FAILED;
        initPhase = 0;
        return;
    }

    switch (initPhase) {
        case 0:
            if (!connectStepper()) {
                initState = OperationState::DRIVE_FAILED;
                initPhase = 0;
                return;
            }
            applyEnableOutput(true);
            initPhase = 1;
            break;

        case 1:
            if (!enableSettled(now)) return;
            // Un driver qui signale une alarme n'est pas prêt : on attend jusqu'au délai.
            if (nodes.hardwareStepperAlarm && nodes.hardwareStepperAlarm->getValue()) return;
            initialized = true;
            initState = OperationState::DRIVE_IDLE;
            initPhase = 0;
            axisLog("Initialisation terminée en %lu ms (position %ld pas)",
                    (unsigned long)(now - initStartMs), (long)getPosition());
            break;

        default:
            initPhase = 0;
            break;
    }
}

bool StepperMotor::handleInitializing(OperationStatus& status) {
    if (!initHandlerStarted) {
        initFailureReported = false;
        if (!startInitialize()) {
            userMessage(ERROR, "Impossible de démarrer l'initialisation : %s", getLastError());
            status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
        initHandlerStarted = true;
        status = OperationStatus::IN_PROGRESS;
        return true;
    }

    switch (initState) {
        case OperationState::DRIVE_IDLE:
            initHandlerStarted = false;
            status = initialized ? OperationStatus::COMPLETED : OperationStatus::IN_PROGRESS;
            return true;

        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT:
            // initHandlerStarted reste vrai jusqu'au réarmement : l'erreur est signalée une fois.
            if (!initFailureReported) {
                userMessage(ERROR, "Échec de l'initialisation : %s", getLastError());
                initFailureReported = true;
            }
            status = OperationStatus::SERVO_DRIVE_ERROR;
            return true;

        case OperationState::DRIVE_IN_PROGRESS:
        default:
            status = OperationStatus::IN_PROGRESS;
            return true;
    }
}

// ========================================
// RÉARMEMENT
// ========================================

bool StepperMotor::startReset() {
    if (resetState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Réarmement déjà en cours");
        return false;
    }
    resetState = OperationState::DRIVE_IN_PROGRESS;
    resetPhase = 0;
    resetStartMs = millis();
    resetPhaseMs = resetStartMs;
    axisLog("Réarmement démarré");
    return true;
}

void StepperMotor::processReset(uint32_t now) {
    const bool driverAlarmInput = nodes.hardwareStepperAlarm && nodes.hardwareStepperAlarm->getValue();

    if (now - resetStartMs > resetTimeoutMs) {
        if (driverAlarmInput) {
            setError("Réarmement impossible : alarme du driver toujours active");
        } else {
            setError("délai de réarmement dépassé (%lu ms)", (unsigned long)resetTimeoutMs);
        }
        if (nodes.alarmsReset) nodes.alarmsReset->setValue(false);
        resetState = OperationState::DRIVE_FAILED;
        resetPhase = 0;
        return;
    }

    switch (resetPhase) {
        case 0:
            // Arrêt de tout mouvement, puis impulsion d'acquittement / coupure ENABLE.
            jogDirection = 0;
            jogStarted = false;
            if (fasMotor && fasMotor->isRunning()) {
                fasMotor->forceStop();
            }
            failActiveOperations("Réarmement");
            resetNeedsEnableCycle = (alarmCode == ALARM_DRIVER) || driverAlarmInput;
            if (nodes.alarmsReset) nodes.alarmsReset->setValue(true);
            if (resetNeedsEnableCycle && enabled) applyEnableOutput(false);
            resetPhaseMs = now;
            resetPhase = 1;
            break;

        case 1:
            if ((nodes.alarmsReset || resetNeedsEnableCycle) && now - resetPhaseMs < RESET_PULSE_MS) return;
            if (nodes.alarmsReset) nodes.alarmsReset->setValue(false);
            if (!fasMotor && !connectStepper()) {
                resetState = OperationState::DRIVE_FAILED;
                resetPhase = 0;
                return;
            }
            if (!enabled) applyEnableOutput(true);
            resetPhaseMs = now;
            resetPhase = 2;
            break;

        case 2:
            if (!enableSettled(now) || isMoving()) return;
            if (driverAlarmInput) return;   // attend la retombée de l'alarme (jusqu'au délai)
            finishReset();
            resetState = OperationState::DRIVE_IDLE;
            resetPhase = 0;
            axisLog("Réarmement terminé en %lu ms (origine %s)", (unsigned long)(now - resetStartMs),
                    homingDone ? "conservée" : "à refaire");
            break;

        default:
            resetPhase = 0;
            break;
    }
}

void StepperMotor::finishReset() {
    alarmCode = ALARM_NONE;
    driverAlarmPrev = false;
    resetNeedsEnableCycle = false;
    lastError = "";

    moveState = OperationState::DRIVE_IDLE;
    movePhase = 0;
    homingState = OperationState::DRIVE_IDLE;
    homingPhase = 0;
    homingSpeedOverrideHz = 0;
    lastMoveOk = false;
    commandedDirection = 0;

    if (initState != OperationState::DRIVE_IN_PROGRESS) {
        initialized = (fasMotor != nullptr);
        initState = initialized ? OperationState::DRIVE_IDLE : OperationState::DRIVE_FAILED;
        initPhase = 0;
    }
    initHandlerStarted = false;
    initFailureReported = false;
    homingHandlerStarted = false;
    moveFailureReported = false;

    stopRequested = false;
    stopLockUntilMs = 0;
}

void StepperMotor::clearMovementLock() {
    stopRequested = false;
    stopLockUntilMs = 0;

    if (moveState == OperationState::DRIVE_FAILED || moveState == OperationState::DRIVE_TIMEOUT) {
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
    }
    if (homingState != OperationState::DRIVE_IDLE) {
        if (homingState == OperationState::DRIVE_IN_PROGRESS && isMoving()) {
            fasMotor->stopMove();
        }
        homingState = OperationState::DRIVE_IDLE;
        homingPhase = 0;
    }
    homingHandlerStarted = false;

    if (initState == OperationState::DRIVE_FAILED || initState == OperationState::DRIVE_TIMEOUT) {
        initState = OperationState::DRIVE_IDLE;
        initPhase = 0;
        initHandlerStarted = false;
        initFailureReported = false;
        if (!initialized) {
            startInitialize();
        }
    }

    if (!enabled && fasMotor && alarmCode == ALARM_NONE) {
        applyEnableOutput(true);
    }
}

// ========================================
// DÉPLACEMENT
// ========================================

bool StepperMotor::startGoToPosition(int32_t positionSteps, bool waitForSync, uint32_t timeout) {
    (void)waitForSync;   // la position d'un pas-à-pas est toujours celle du compteur
    const uint32_t now = millis();

    if (!initialized || !fasMotor) {
        setError("Déplacement refusé : axe non initialisé");
        return false;
    }
    if (isImmediateStopActive()) {
        setError("Déplacement refusé : arrêt en cours");
        return false;
    }
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Déplacement refusé : mouvement déjà en cours");
        return false;
    }
    if (moveState == OperationState::DRIVE_FAILED || moveState == OperationState::DRIVE_TIMEOUT) {
        setError("Déplacement refusé : erreur précédente, réarmement nécessaire");
        return false;
    }
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Déplacement refusé : prise d'origine en cours");
        return false;
    }
    if (jogDirection != 0) {
        setError("Déplacement refusé : jog en cours");
        return false;
    }
    if (alarmCode != ALARM_NONE) {
        setError("Déplacement refusé : %s", getAlarmDescription());
        return false;
    }
    if (!enabled) {
        setError("Déplacement refusé : driver désactivé");
        return false;
    }
    if (!targetWithinSoftLimits(positionSteps)) {
        setError("Cible %ld hors course [%ld, %ld]", (long)positionSteps, (long)softLimitMin, (long)softLimitMax);
        return false;
    }

    const int32_t pos = getPosition();
    const int8_t dir = (positionSteps > pos) ? 1 : ((positionSteps < pos) ? -1 : 0);
    if (dir != 0 && limitActive(dir)) {
        setError("Déplacement vers %ld refusé : fin de course %c active", (long)positionSteps, dir > 0 ? '+' : '-');
        return false;
    }

    lastError = "";
    targetPosition = positionSteps;
    lastMoveOk = false;
    moveFailureReported = false;

    // Délai : demandé (0 = défaut), allongé si le trajet estimé est plus long.
    const uint64_t requested = timeout ? timeout : moveTimeoutDefaultMs;
    const uint32_t estimate = estimateMoveMs((int64_t)positionSteps - (int64_t)pos, moveSpeedHz);
    const uint64_t minimum = (uint64_t)estimate * 3 / 2 + 2000;
    const uint64_t effective = requested > minimum ? requested : minimum;
    moveTimeoutMs = (uint32_t)(effective > (uint64_t)INT32_MAX ? (uint64_t)INT32_MAX : effective);
    moveStartMs = now;
    lastMoveProgressLogMs = now;
    moveState = OperationState::DRIVE_IN_PROGRESS;

    if (enableSettled(now)) {
        if (!issueMoveTo(positionSteps, moveSpeedHz)) {
            moveState = OperationState::DRIVE_IDLE;
            return false;
        }
        movePhase = 1;
    } else {
        movePhase = 0;   // départ dès que le driver est prêt (délai ENABLE)
    }

    axisLog("Déplacement %ld -> %ld pas à %lu pas/s (délai %lu ms)", (long)pos, (long)positionSteps,
            (unsigned long)moveSpeedHz, (unsigned long)moveTimeoutMs);
    return true;
}

void StepperMotor::processMove(uint32_t now) {
    if (!fasMotor) {
        setError("FastAccelStepper absent");
        moveState = OperationState::DRIVE_FAILED;
        return;
    }

    if (now - moveStartMs > moveTimeoutMs) {
        if (isMoving()) fasMotor->stopMove();
        setError("délai de déplacement dépassé (%lu ms), position %ld, cible %ld",
                 (unsigned long)moveTimeoutMs, (long)getPosition(), (long)targetPosition);
        moveState = OperationState::DRIVE_FAILED;
        movePhase = 0;
        return;
    }

    if (movePhase == 0) {
        if (!enabled) {
            setError("driver désactivé avant le départ du déplacement");
            moveState = OperationState::DRIVE_FAILED;
            return;
        }
        if (!enableSettled(now)) return;
        if (!issueMoveTo(targetPosition, moveSpeedHz)) {
            moveState = OperationState::DRIVE_FAILED;
            return;
        }
        movePhase = 1;
        return;
    }

    if (fasMotor->isRunning()) return;

    const int32_t pos = fasMotor->getCurrentPosition();
    movePhase = 0;
    commandedDirection = 0;
    if (pos == targetPosition) {
        moveState = OperationState::DRIVE_IDLE;
        lastMoveOk = true;
        axisLog("Position %ld atteinte en %lu ms", (long)pos, (unsigned long)(now - moveStartMs));
    } else {
        setError("déplacement interrompu à %ld (cible %ld)", (long)pos, (long)targetPosition);
        moveState = OperationState::DRIVE_FAILED;
    }
}

bool StepperMotor::handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync, uint32_t timeoutMs) {
    if (!initialized || !fasMotor) {
        userMessage(ERROR, "Déplacement vers %ld impossible : axe non initialisé", (long)pos);
        status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
        return false;
    }

    switch (moveState) {
        case OperationState::DRIVE_IN_PROGRESS: {
            const uint32_t now = millis();
            if (now - lastMoveProgressLogMs > 1000) {
                axisLog("Déplacement en cours vers %ld (position %ld)", (long)targetPosition, (long)getPosition());
                lastMoveProgressLogMs = now;
            }
            status = OperationStatus::IN_PROGRESS;
            return true;
        }

        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT:
            if (!moveFailureReported) {
                userMessage(ERROR, "Échec du déplacement vers %ld : %s", (long)targetPosition, getLastError());
                moveFailureReported = true;
            }
            status = OperationStatus::SERVO_DRIVE_ERROR;
            return true;

        case OperationState::DRIVE_IDLE:
        default:
            if (lastMoveOk && targetPosition == pos) {
                status = OperationStatus::COMPLETED;
                return true;
            }
            if (isImmediateStopActive()) {   // attend la fin de l'arrêt précédent
                status = OperationStatus::IN_PROGRESS;
                return true;
            }
            if (!startGoToPosition(pos, waitForSync, timeoutMs)) {
                userMessage(ERROR, "Déplacement vers %ld refusé : %s", (long)pos, getLastError());
                status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
                return false;
            }
            status = OperationStatus::IN_PROGRESS;
            return true;
    }
}

bool StepperMotor::stopMovement() {
    axisLog("Arrêt décéléré demandé");
    if (isMoving()) {
        fasMotor->stopMove();
    }
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
    }
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        // Comme SureServo : homingHandlerStarted n'est pas réarmé ; le prochain handleHoming()
        // consomme l'annulation (statut ABORTED) au lieu de relancer une prise d'origine.
        homingState = OperationState::DRIVE_IDLE;
        homingPhase = 0;
        homingSpeedOverrideHz = 0;
    }
    lastMoveOk = false;
    jogDirection = 0;
    jogStarted = false;
    stopRequested = true;
    stopLockUntilMs = millis() + STOP_LOCK_MIN_MS;
    return true;
}

bool StepperMotor::emergencyStop() {
    axisLog("ARRÊT D'URGENCE");
    if (fasMotor) {
        if (fasMotor->isRunning() && homeLostOnDisable) {
            invalidateHome("arrêt brutal en mouvement");
        }
        fasMotor->forceStop();
    }
    jogDirection = 0;
    jogStarted = false;
    commandedDirection = 0;
    applyEnableOutput(false);

    initState = OperationState::DRIVE_FAILED;
    resetState = OperationState::DRIVE_FAILED;
    moveState = OperationState::DRIVE_FAILED;
    homingState = OperationState::DRIVE_FAILED;
    initPhase = resetPhase = movePhase = homingPhase = 0;
    homingSpeedOverrideHz = 0;
    lastMoveOk = false;
    if (nodes.alarmsReset) nodes.alarmsReset->setValue(false);

    stopRequested = true;
    stopLockUntilMs = millis() + EMERGENCY_LOCK_MS;
    return true;
}

// ========================================
// PRISE D'ORIGINE
// ========================================

bool StepperMotor::startHoming(uint32_t timeoutMs) {
    if (!initialized || !fasMotor) {
        setError("Prise d'origine refusée : axe non initialisé");
        return false;
    }
    if (alarmCode != ALARM_NONE) {
        setError("Prise d'origine refusée : %s", getAlarmDescription());
        return false;
    }
    if (!enabled) {
        setError("Prise d'origine refusée : driver désactivé");
        return false;
    }
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Prise d'origine déjà en cours");
        return false;
    }
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Prise d'origine refusée : déplacement en cours");
        return false;
    }
    if (jogDirection != 0) {
        setError("Prise d'origine refusée : jog en cours");
        return false;
    }

    lastError = "";
    homingActiveTimeoutMs = timeoutMs ? timeoutMs : homingTimeoutMs;
    homingDone = false;   // l'origine est refaite (perdue si la prise d'origine est annulée)
    lastMoveOk = false;
    homingState = OperationState::DRIVE_IN_PROGRESS;
    homingPhase = HOMING_START;
    homingStartMs = millis();
    lastHomingProgressLogMs = homingStartMs;

    const uint32_t speed = homingSpeedOverrideHz ? homingSpeedOverrideHz : (homingSpeedHz ? homingSpeedHz : jogSpeedHz);
    if (nodes.homeSwitch) {
        axisLog("Prise d'origine : recherche du capteur vers %c à %lu pas/s, dégagement %lu pas, délai %lu ms",
                homingDirection > 0 ? '+' : '-', (unsigned long)speed, (unsigned long)homingBackoffSteps,
                (unsigned long)homingActiveTimeoutMs);
    } else {
        axisLog("Prise d'origine sans capteur : la position courante devient 0");
    }
    return true;
}

void StepperMotor::homingFail(const char* reason) {
    if (isMoving()) fasMotor->stopMove();
    setError("Prise d'origine : %s", reason);
    homingState = OperationState::DRIVE_FAILED;
    homingPhase = 0;
    homingSpeedOverrideHz = 0;
    commandedDirection = 0;
}

void StepperMotor::homingFinish() {
    homingDone = true;
    homingState = OperationState::DRIVE_IDLE;
    homingPhase = 0;
    homingSpeedOverrideHz = 0;
    commandedDirection = 0;
    axisLog("Prise d'origine terminée en %lu ms : position %ld pas",
            (unsigned long)(millis() - homingStartMs), (long)getPosition());
}

void StepperMotor::processHoming(uint32_t now) {
    if (!fasMotor) {
        homingFail("FastAccelStepper absent");
        return;
    }
    if (now - homingStartMs > homingActiveTimeoutMs) {
        char reason[96];
        snprintf(reason, sizeof(reason), "délai dépassé (%lu ms), capteur d'origine non détecté",
                 (unsigned long)homingActiveTimeoutMs);
        homingFail(reason);
        return;
    }
    if (!enabled) {
        homingFail("driver désactivé");
        return;
    }

    const uint32_t speed = homingSpeedOverrideHz ? homingSpeedOverrideHz : (homingSpeedHz ? homingSpeedHz : jogSpeedHz);
    const bool onSwitch = nodes.homeSwitch && nodes.homeSwitch->getValue();

    switch (homingPhase) {
        case HOMING_START:
            // Attend le délai ENABLE et la fin d'un éventuel arrêt précédent.
            if (!enableSettled(now) || fasMotor->isRunning()) return;
            if (!nodes.homeSwitch) {
                fasMotor->setCurrentPosition(0);
                homingFinish();
                return;
            }
            if (onSwitch) {
                if (!issueRun((int8_t)-homingDirection, speed)) { homingFail(getLastError()); return; }
                homingPhase = HOMING_CLEAR_SWITCH;
                axisLog("Prise d'origine : déjà sur le capteur, dégagement");
            } else {
                if (!issueRun(homingDirection, speed)) { homingFail(getLastError()); return; }
                homingPhase = HOMING_SEEK;
            }
            break;

        case HOMING_CLEAR_SWITCH:
            if (!onSwitch) {
                fasMotor->stopMove();
                homingPhase = HOMING_CLEAR_STOP;
            } else if (!fasMotor->isRunning()) {
                homingFail("mouvement interrompu pendant le dégagement du capteur");
            }
            break;

        case HOMING_CLEAR_STOP:
            if (fasMotor->isRunning()) return;
            if (!issueRun(homingDirection, speed)) { homingFail(getLastError()); return; }
            homingPhase = HOMING_SEEK;
            break;

        case HOMING_SEEK:
            if (onSwitch) {
                // Position mémorisée à la détection : le zéro sera ce point, indépendamment de la
                // distance de freinage (v²/2a), donc de la vitesse atteinte au moment de la détection.
                homingSwitchPosition = fasMotor->getCurrentPosition();
                fasMotor->stopMove();   // arrêt décéléré : pas de pas perdus
                homingPhase = HOMING_SEEK_STOP;
                axisLog("Prise d'origine : capteur détecté après %lu ms", (unsigned long)(now - homingStartMs));
            } else if (!fasMotor->isRunning()) {
                homingFail("mouvement interrompu avant le capteur d'origine");
            }
            break;

        case HOMING_SEEK_STOP: {
            if (fasMotor->isRunning()) return;
            // Zéro = point de détection du capteur ; l'axe est au-delà de la distance de freinage.
            const int32_t overshoot = fasMotor->getCurrentPosition() - homingSwitchPosition;
            fasMotor->setCurrentPosition(overshoot);
            // Position finale : dégagement depuis le zéro, dans le sens opposé à la recherche
            // (0 si aucun dégagement : retour sur le point de détection).
            const int32_t finalTarget = (int32_t)(-homingDirection) * (int32_t)homingBackoffSteps;
            if (!issueMoveTo(finalTarget, speed)) { homingFail(getLastError()); return; }
            homingPhase = HOMING_BACKOFF;
            break;
        }

        case HOMING_BACKOFF: {
            if (fasMotor->isRunning()) return;
            const int32_t expected = (int32_t)(-homingDirection) * (int32_t)homingBackoffSteps;
            if (fasMotor->getCurrentPosition() != expected) {
                homingFail("dégagement interrompu");
                return;
            }
            if (onSwitch && homingBackoffSteps > 0) {
                axisLog("Attention : capteur d'origine toujours actif après le dégagement (augmenter le dégagement)");
            }
            homingFinish();
            break;
        }

        default:
            homingFail("phase invalide");
            break;
    }
}

bool StepperMotor::handleHoming(OperationStatus& status, uint32_t homingSpeed, uint32_t timeoutMs) {
    if (!homingHandlerStarted) {
        homingSpeedOverrideHz = homingSpeed ? clampU32(homingSpeed, MIN_SPEED_HZ, MAX_SPEED_HZ) : 0;
        if (!startHoming(timeoutMs)) {
            homingSpeedOverrideHz = 0;
            userMessage(ERROR, "Impossible de démarrer la prise d'origine : %s", getLastError());
            status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
        homingHandlerStarted = true;
        status = OperationStatus::IN_PROGRESS;
        return true;
    }

    switch (homingState) {
        case OperationState::DRIVE_IN_PROGRESS: {
            const uint32_t now = millis();
            if (now - lastHomingProgressLogMs > 5000) {
                axisLog("Prise d'origine en cours (%lu s, position %ld)",
                        (unsigned long)((now - homingStartMs) / 1000), (long)getPosition());
                lastHomingProgressLogMs = now;
            }
            status = OperationStatus::IN_PROGRESS;
            return true;
        }

        case OperationState::DRIVE_IDLE:
            homingHandlerStarted = false;
            // Terminé, ou annulé par stopMovement() (origine non faite).
            status = homingDone ? OperationStatus::COMPLETED : OperationStatus::ABORTED;
            return true;

        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT:
        default:
            homingHandlerStarted = false;
            userMessage(ERROR, "%s", getLastError());
            status = OperationStatus::SERVO_DRIVE_ERROR;
            return true;
    }
}

// ========================================
// JOG
// ========================================

bool StepperMotor::jogForward(int32_t limitSteps, uint32_t slowZoneSteps, uint16_t slowSpeed) {
    return jog(1, limitSteps, slowZoneSteps, slowSpeed);
}

bool StepperMotor::jogReverse(int32_t limitSteps, uint32_t slowZoneSteps, uint16_t slowSpeed) {
    return jog(-1, limitSteps, slowZoneSteps, slowSpeed);
}

bool StepperMotor::jog(int8_t direction, int32_t limitSteps, uint32_t slowZoneSteps, uint16_t slowSpeed) {
    const uint32_t now = millis();
    const char sign = direction > 0 ? '+' : '-';

    if (!initialized || !fasMotor) {
        setError("Jog %c refusé : axe non initialisé", sign);
        return false;
    }
    if (alarmCode != ALARM_NONE) {
        setError("Jog %c refusé : %s", sign, getAlarmDescription());
        return false;
    }
    if (moveState == OperationState::DRIVE_IN_PROGRESS || homingState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Jog %c refusé : mouvement automatique en cours", sign);
        return false;
    }
    if (isImmediateStopActive()) {
        setError("Jog %c refusé : arrêt en cours", sign);
        return false;
    }
    if (!enabled) {
        setError("Jog %c refusé : driver désactivé", sign);
        return false;
    }
    if (limitActive(direction)) {
        if (jogDirection == direction) stopJogInternal();
        setError("Jog %c refusé : fin de course %c active", sign, sign);
        return false;
    }

    // Limite de position : celle de l'appelant et les butées logicielles, après la prise d'origine.
    bool limited = false;
    int32_t limit = 0;
    if (homingDone) {
        if (direction > 0) {
            if (limitSteps != INT32_MAX) { limited = true; limit = limitSteps; }
            if (softLimitsEnabled && (!limited || softLimitMax < limit)) { limited = true; limit = softLimitMax; }
        } else {
            if (limitSteps != INT32_MIN) { limited = true; limit = limitSteps; }
            if (softLimitsEnabled && (!limited || softLimitMin > limit)) { limited = true; limit = softLimitMin; }
        }
    }

    const int32_t pos = fasMotor->getCurrentPosition();
    if (limited && ((direction > 0 && pos >= limit) || (direction < 0 && pos <= limit))) {
        if (jogDirection != 0) {
            if (isMoving()) fasMotor->stopMove();
            jogDirection = 0;
            jogStarted = false;
        }
        setError("Jog %c : limite %ld atteinte (position %ld)", sign, (long)limit, (long)pos);
        return false;
    }

    uint32_t speed = jogSpeedHz;
    if (limited && slowZoneSteps > 0) {
        const int64_t remaining = (direction > 0) ? (int64_t)limit - pos : (int64_t)pos - limit;
        if (remaining <= (int64_t)slowZoneSteps) {
            speed = clampU32(slowSpeed, MIN_SPEED_HZ, jogSpeedHz);
        }
    }

    lastJogCallMs = now;
    if (jogDirection != direction) {
        jogDirection = direction;
        jogStarted = false;
    }
    if (!enableSettled(now)) {
        return true;   // départ à l'un des prochains appels, une fois le driver prêt
    }

    const bool changed = !jogStarted || speed != jogAppliedSpeedHz || limited != jogAppliedLimited ||
                         (limited && limit != jogAppliedLimit);
    if (changed) {
        const bool ok = limited ? issueMoveTo(limit, speed) : issueRun(direction, speed);
        if (!ok) {
            jogDirection = 0;
            jogStarted = false;
            return false;
        }
        if (!jogStarted) {
            if (limited) {
                axisLog("Jog %c à %lu pas/s jusqu'à %ld", sign, (unsigned long)speed, (long)limit);
            } else {
                axisLog("Jog %c à %lu pas/s", sign, (unsigned long)speed);
            }
        }
        jogStarted = true;
        jogAppliedSpeedHz = speed;
        jogAppliedLimited = limited;
        jogAppliedLimit = limit;
        lastMoveOk = false;
    }
    return true;
}

void StepperMotor::stopJogInternal() {
    if (jogDirection != 0 && jogStarted && isMoving()) {
        fasMotor->stopMove();
    }
    jogDirection = 0;
    jogStarted = false;
}

bool StepperMotor::jogStop() {
    if (jogDirection != 0) {
        axisLog("Jog arrêté (position %ld)", (long)getPosition());
        stopJogInternal();
    }
    return true;
}

void StepperMotor::processJog(uint32_t now) {
    // Homme mort : le jog doit être rafraîchi à chaque cycle tant que le bouton est maintenu.
    if (jogWatchdogMs > 0 && now - lastJogCallMs > jogWatchdogMs) {
        axisLog("Jog arrêté : plus de commande depuis %lu ms", (unsigned long)(now - lastJogCallMs));
        stopJogInternal();
    }
}

bool StepperMotor::handleJogging(OperationStatus& status) {
    if (alarmCode != ALARM_NONE) {
        status = OperationStatus::SERVO_DRIVE_ERROR;
        return true;
    }
    status = (jogDirection != 0 || isMoving()) ? OperationStatus::IN_PROGRESS : OperationStatus::COMPLETED;
    return true;
}

// ========================================
// ALARMES ET FINS DE COURSE
// ========================================

void StepperMotor::failActiveOperations(const char* reason) {
    bool failed = false;
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        moveState = OperationState::DRIVE_FAILED;
        movePhase = 0;
        failed = true;
    }
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        homingState = OperationState::DRIVE_FAILED;
        homingPhase = 0;
        homingSpeedOverrideHz = 0;
        failed = true;
    }
    lastMoveOk = false;
    commandedDirection = 0;
    if (failed) {
        axisLog("Opération en cours interrompue : %s", reason);
    }
}

void StepperMotor::raiseAlarm(uint16_t code, const char* reason) {
    const bool wasMoving = isMoving();
    if (fasMotor) {
        fasMotor->forceStop();
    }
    jogDirection = 0;
    jogStarted = false;
    failActiveOperations(reason);

    if (code == ALARM_DRIVER) {
        invalidateHome("alarme du driver");
    } else if (wasMoving && homeLostOnDisable) {
        invalidateHome("arrêt brutal sur fin de course");
    }

    if (alarmCode != ALARM_NONE) return;   // première alarme conservée jusqu'au réarmement

    alarmCode = code;
    setError("%s", reason);

    char msg[192];
    snprintf(msg, sizeof(msg), "[%s] ALARME AL%03u : %s", servoId.c_str(), (unsigned)code, reason);
    Serial.println(msg);
    BorneUniverselle::prepareMessage(ERROR, msg);
    PLC_Tools::logDiagnostic(msg);
    if (onAlarmCallback) {
        onAlarmCallback(code, getAlarmDescription());
    }
}

void StepperMotor::monitorDriverAlarm() {
    if (!nodes.hardwareStepperAlarm) return;
    const bool active = nodes.hardwareStepperAlarm->getValue();

    char msg[128];
    snprintf(msg, sizeof(msg), "[%s] Alarme du driver active - réarmez l'axe", servoId.c_str());

    if (active && !driverAlarmPrev) {
        raiseAlarm(ALARM_DRIVER, "Alarme du driver (entrée ALM active)");
    } else if (!active && driverAlarmPrev) {
        axisLog("Entrée ALM du driver retombée : alarme mémorisée jusqu'au réarmement");
        PLC_Tools::sendPeriodicMessage(false, ERROR, msg);
    }
    driverAlarmPrev = active;

    if (active) {
        PLC_Tools::sendPeriodicMessage(true, ERROR, msg);
    }
}

void StepperMotor::monitorLimitSwitches() {
    if (!fasMotor) return;
    const int8_t motion = motionDirection();
    if (motion == 0) return;

    // Pendant la prise d'origine, un capteur commun origine/fin de course n'est pas une alarme.
    const bool homing = homingState == OperationState::DRIVE_IN_PROGRESS;
    BooleanInputNode* sw = (motion > 0) ? nodes.limitSwitchPositive : nodes.limitSwitchNegative;
    if (!sw || (homing && sw == nodes.homeSwitch)) return;
    if (!sw->getValue()) return;

    if (motion > 0) {
        raiseAlarm(ALARM_LIMIT_POSITIVE, "Fin de course + atteinte en mouvement");
    } else {
        raiseAlarm(ALARM_LIMIT_NEGATIVE, "Fin de course - atteinte en mouvement");
    }
}

// ========================================
// NŒUDS : RÉGLAGES ET RECOPIES
// ========================================

void StepperMotor::syncParametersFromNodes() {
    if (nodes.maxSpeed) {
        const uint32_t v = nodes.maxSpeed->getValue();
        if (v != lastNodeMaxSpeed) {
            lastNodeMaxSpeed = v;
            if (v > 0) setMoveSpeed((int32_t)clampU32(v, MIN_SPEED_HZ, MAX_SPEED_HZ));
        }
    }
    if (nodes.jogSpeed) {
        const uint32_t v = nodes.jogSpeed->getValue();
        if (v != lastNodeJogSpeed) {
            lastNodeJogSpeed = v;
            if (v > 0) setJogSpeed(v);
        }
    }
    if (nodes.acceleration) {
        const uint32_t v = nodes.acceleration->getValue();
        if (v != lastNodeAcceleration) {
            lastNodeAcceleration = v;
            if (v > 0) setAcceleration(v);
        }
    }
}

void StepperMotor::updateStatusNodes(uint32_t now) {
    const bool moving = isMoving();

    if (nodes.position) {
        // Uint32OutputNode::setValue() trace chaque changement : rafraîchissement limité en mouvement.
        const int32_t pos = getPosition();
        if (!positionPublished ||
            (pos != lastPublishedPosition && (!moving || now - lastPositionPublishMs >= POSITION_PUBLISH_MS))) {
            nodes.position->setValue(static_cast<uint32_t>(pos));   // int32 en complément à deux
            lastPublishedPosition = pos;
            lastPositionPublishMs = now;
            positionPublished = true;
        }
    }

    if (nodes.targetPositionReached) nodes.targetPositionReached->setValue(lastMoveOk && !moving);
    if (nodes.stepperReady) nodes.stepperReady->setValue(getIsReady());
    if (nodes.stepperActivated) nodes.stepperActivated->setValue(enabled);
    if (nodes.homeDone) nodes.homeDone->setValue(homingDone);
    if (nodes.limitError) {
        nodes.limitError->setValue(alarmCode == ALARM_LIMIT_POSITIVE || alarmCode == ALARM_LIMIT_NEGATIVE);
    }
    if (nodes.stepperAlarm) nodes.stepperAlarm->setValue(alarmCode != ALARM_NONE);
    if (nodes.zeroSpeed) nodes.zeroSpeed->setValue(!moving);
    if (nodes.driveInitialised) nodes.driveInitialised->setValue(initialized);
}

void StepperMotor::printConfiguration() const {
    axisLog("=== Configuration de l'axe pas-à-pas ===");
    axisLog("STEP=GPIO%u DIR=GPIO%u%s ENABLE=%s (actif %s, délai %lu ms), générateur %s",
            (unsigned)stepPinNumber, (unsigned)dirPinNumber, directionInverted ? " (inversé)" : "",
            enablePinNumber != PIN_NONE ? "GPIO" : (enableViaNode ? "nœud" : "aucun"),
            enableActiveLow ? "bas" : "haut", (unsigned long)enableDelayMs, driverName);
    axisLog("Vitesse %lu pas/s, jog %lu pas/s, accélération %lu pas/s², %lu pas/unité",
            (unsigned long)moveSpeedHz, (unsigned long)jogSpeedHz, (unsigned long)accelerationHz2,
            (unsigned long)stepsPerUnit);
    axisLog("Origine : capteur %s, sens %c, vitesse %lu pas/s, dégagement %lu pas, délai %lu ms",
            nodes.homeSwitch ? "oui" : "non", homingDirection > 0 ? '+' : '-',
            (unsigned long)(homingSpeedHz ? homingSpeedHz : jogSpeedHz), (unsigned long)homingBackoffSteps,
            (unsigned long)homingTimeoutMs);
    if (softLimitsEnabled) {
        axisLog("Butées logicielles [%ld, %ld] pas (après prise d'origine)", (long)softLimitMin, (long)softLimitMax);
    }
    axisLog("Fins de course : + %s, - %s ; alarme driver %s ; %u nœud(s) optionnel(s)",
            nodes.limitSwitchPositive ? "oui" : "non", nodes.limitSwitchNegative ? "oui" : "non",
            nodes.hardwareStepperAlarm ? "oui" : "non", (unsigned)nodes.countOptional());
}

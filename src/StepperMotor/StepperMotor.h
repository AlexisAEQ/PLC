/**
 * @file StepperMotor.h
 * @brief Axe pas-à-pas STEP/DIR(/ENABLE) piloté par FastAccelStepper, interface ServoDrive.
 *
 * Plusieurs axes (et des servos ServoDrive) peuvent cohabiter dans une même application :
 * - un seul FastAccelStepperEngine pour tout le programme (sharedEngine(), init() appelé une fois) ;
 * - chaque StepperMotor obtient son propre FastAccelStepper* (un générateur d'impulsions
 *   MCPWM/PCNT, RMT ou I2S de l'ESP32) ;
 * - aucun état statique de fonction : tout l'état est porté par l'instance.
 *
 * Unités : positions en pas (int32 signé), vitesses en pas/s, accélérations en pas/s².
 *
 * Broches : STEP, DIR et (option) ENABLE sont des nœuds HardwareBooleanOutputNode déclarés
 * en GPIO "tx-bool" dans le fichier matériel de la carte. La classe lit leur numéro de GPIO
 * (getPinNumber()) et confie ces broches à FastAccelStepper : elle n'appelle jamais setValue()
 * sur les nœuds STEP/DIR, et la logique métier / l'IHM ne doivent pas les écrire non plus.
 *
 * Documentation : docs/axes/stepper.md
 *
 * Compatible FastAccelStepper 0.33.x et 2.0.x (2.0.0 recommandée sur ESP-IDF 5).
 */

#ifndef STEPPER_MOTOR_H
#define STEPPER_MOTOR_H

#include <Arduino.h>
#include "ServoDrive/ServoDrive.h"
#include "Node/Node.h"

// Déclarations anticipées : seul Steppermotor.cpp inclut <FastAccelStepper.h>.
class FastAccelStepper;
class FastAccelStepperEngine;

// ========================================
// NŒUDS D'UN AXE PAS-À-PAS
// ========================================

/**
 * @brief Pointeurs vers les nœuds d'un axe (créés par BorneUniverselle depuis config.json).
 *
 * Seuls stepPin et dirPin sont obligatoires. Tous les autres champs peuvent rester nullptr.
 */
struct StepperNodes {
    // ---- Obligatoires : broches confiées à FastAccelStepper (GPIO "tx-bool") ----
    HardwareBooleanOutputNode* stepPin = nullptr;   // STEP / PUL
    HardwareBooleanOutputNode* dirPin  = nullptr;   // DIR

    // ---- Optionnel : activation du driver ----
    // HardwareBooleanOutputNode (GPIO "tx-bool") -> piloté par FastAccelStepper (setEnablePin).
    // Autre BooleanOutputNode (relais PCF8574/PCA9554, bobine Modbus, nœud virtuel) -> écrit par
    // la classe via setValue(). Polarité : voir setEnableActiveLow().
    BooleanOutputNode* enable = nullptr;
    // Sortie d'acquittement du driver : impulsion de RESET_PULSE_MS pendant startReset().
    BooleanOutputNode* alarmsReset = nullptr;

    // ---- Optionnel : entrées (true = actif ; régler "inverted" dans config.json si besoin) ----
    BooleanInputNode* homeSwitch = nullptr;            // capteur d'origine
    BooleanInputNode* limitSwitchPositive = nullptr;   // fin de course côté +
    BooleanInputNode* limitSwitchNegative = nullptr;   // fin de course côté -
    BooleanInputNode* hardwareStepperAlarm = nullptr;  // sortie ALM/FAULT du driver

    // ---- Optionnel : recopies d'état pour l'IHM (écrites par la classe) ----
    // Position en pas, int32 signé stocké en complément à deux : lire avec getValueAsInt32().
    Uint32OutputNode*  position = nullptr;
    BooleanOutputNode* targetPositionReached = nullptr;  // dernier déplacement terminé sur la cible
    BooleanOutputNode* stepperReady = nullptr;           // getIsReady()
    BooleanOutputNode* stepperActivated = nullptr;       // driver activé (enable)
    BooleanOutputNode* homeDone = nullptr;               // getHomeDone()
    BooleanOutputNode* limitError = nullptr;             // alarme fin de course mémorisée
    BooleanOutputNode* stepperAlarm = nullptr;           // getHasAlarms()
    BooleanOutputNode* zeroSpeed = nullptr;              // moteur à l'arrêt
    BooleanOutputNode* driveInitialised = nullptr;       // getIsInitialized()

    // ---- Optionnel : réglages IHM, appliqués quand leur valeur change (0 = ignoré) ----
    Uint32InputNode* maxSpeed = nullptr;       // vitesse des déplacements (pas/s) -> setMoveSpeed()
    Uint32InputNode* jogSpeed = nullptr;       // vitesse du jog (pas/s)           -> setJogSpeed()
    Uint32InputNode* acceleration = nullptr;   // accélération (pas/s²)            -> setAcceleration()

    /** @brief true si les nœuds obligatoires (STEP, DIR) sont présents. */
    bool validateRequired() const {
        return stepPin != nullptr && dirPin != nullptr;
    }

    /** @brief Nombre de nœuds optionnels renseignés (diagnostic). */
    size_t countOptional() const {
        const void* opts[] = {enable, alarmsReset, homeSwitch, limitSwitchPositive, limitSwitchNegative,
                              hardwareStepperAlarm, position, targetPositionReached, stepperReady,
                              stepperActivated, homeDone, limitError, stepperAlarm, zeroSpeed,
                              driveInitialised, maxSpeed, jogSpeed, acceleration};
        size_t count = 0;
        for (const void* p : opts) {
            if (p) count++;
        }
        return count;
    }
};

// ========================================
// CLASSE STEPPERMOTOR
// ========================================

class StepperMotor : public ServoDrive {
public:
    // ---- Codes d'alarme (getAlarmCode()) ----
    static constexpr uint16_t ALARM_NONE           = 0x0000;
    static constexpr uint16_t ALARM_DRIVER         = 0x0001;  // entrée ALM du driver active
    static constexpr uint16_t ALARM_LIMIT_POSITIVE = 0x0002;  // fin de course + atteinte en allant vers +
    static constexpr uint16_t ALARM_LIMIT_NEGATIVE = 0x0003;  // fin de course - atteinte en allant vers -

    // ---- Bornes et valeurs par défaut (pas/s, pas/s², ms) ----
    static constexpr uint32_t MIN_SPEED_HZ              = 1;
    static constexpr uint32_t MAX_SPEED_HZ              = 200000;   // plafond FastAccelStepper ESP32
    static constexpr uint32_t MIN_ACCELERATION          = 1;
    static constexpr uint32_t MAX_ACCELERATION          = 10000000;
    static constexpr uint32_t DEFAULT_ACCELERATION      = 5000;
    static constexpr uint32_t DEFAULT_MOVE_SPEED        = 2000;
    static constexpr uint32_t DEFAULT_JOG_SPEED         = 1000;
    static constexpr uint32_t DEFAULT_MOVE_TIMEOUT_MS   = 60000;
    static constexpr uint32_t DEFAULT_HOMING_TIMEOUT_MS = 30000;
    static constexpr uint32_t DEFAULT_INIT_TIMEOUT_MS   = 5000;
    static constexpr uint32_t DEFAULT_RESET_TIMEOUT_MS  = 5000;
    static constexpr uint32_t DEFAULT_ENABLE_DELAY_MS   = 100;   // ENABLE actif -> premier pas
    static constexpr uint32_t DEFAULT_JOG_WATCHDOG_MS   = 500;   // jog arrêté si plus rafraîchi
    static constexpr uint32_t STOP_LOCK_MIN_MS          = 100;   // verrou après stopMovement()
    static constexpr uint32_t EMERGENCY_LOCK_MS         = 500;   // verrou après emergencyStop()
    static constexpr uint32_t RESET_PULSE_MS            = 200;   // impulsion alarmsReset / coupure ENABLE
    static constexpr uint32_t POSITION_PUBLISH_MS       = 100;   // rafraîchissement du nœud position
    static constexpr uint8_t  MAX_STEPPERS              = 24;    // axes enregistrés dans le programme

    /** @brief Générateur d'impulsions ESP32 demandé à FastAccelStepper. */
    enum class DriverType : uint8_t {
        AUTO = 0,     // MCPWM/PCNT d'abord (laisse le RMT à la LED WS2812), puis RMT, puis autre
        MCPWM_PCNT,   // uniquement MCPWM/PCNT (ESP32 : 6, ESP32-S3 : 4)
        RMT           // uniquement RMT (ESP32 : 8, ESP32-S3 : 4, partagé avec la LED WS2812)
    };

    /**
     * @param stepsPerUnit           pas par unité mécanique (information/convertisseur, 0 -> 1)
     * @param nodes                  nœuds de l'axe (stepPin et dirPin obligatoires)
     * @param maxRangeSteps          course utile en pas : si > 0, butées logicielles [0, maxRangeSteps]
     *                               appliquées après la prise d'origine (voir setSoftLimits())
     * @param servoId                identifiant de l'axe, préfixe de tous les messages
     * @param accelerationStepsPerS2 accélération/décélération des mouvements (pas/s²)
     */
    StepperMotor(uint32_t stepsPerUnit,
                 const StepperNodes& nodes,
                 uint32_t maxRangeSteps = 0,
                 const char* servoId = "STEPPER",
                 uint32_t accelerationStepsPerS2 = DEFAULT_ACCELERATION);

    ~StepperMotor() override;

    StepperMotor(const StepperMotor&) = delete;
    StepperMotor& operator=(const StepperMotor&) = delete;

    /** @brief Moteur FastAccelStepper partagé par tous les axes (init() appelé une seule fois). */
    static FastAccelStepperEngine& sharedEngine();

    // ========================================
    // INTERFACE SERVODRIVE
    // ========================================

    void process() override;

    bool startInitialize() override;
    bool startReset() override;
    using ServoDrive::startGoToPosition;  // garde la surcharge float de ServoDrive visible
    // waitForSync est ignoré (la position d'un pas-à-pas est toujours celle du compteur).
    // timeout = 0 -> setMoveTimeout() ; il est allongé si la durée estimée du trajet le dépasse.
    bool startGoToPosition(int32_t positionSteps, bool waitForSync = false, uint32_t timeout = 60000) override;
    bool startHoming(uint32_t timeoutMs = 0) override;
    bool stopMovement() override;

    OperationState getInitializeState() const override { return initState; }
    OperationState getResetState() const override { return resetState; }
    OperationState getMoveState() const override { return moveState; }
    OperationState getHomingState() const override { return homingState; }

    // limit = INT32_MAX / INT32_MIN : pas de limite. Limites, zone lente (slowZone en pas,
    // slowSpeed en pas/s) et butées logicielles appliquées seulement après la prise d'origine.
    bool jogForward(int32_t limitSteps = INT32_MAX, uint32_t slowZoneSteps = 0, uint16_t slowSpeed = 10) override;
    bool jogReverse(int32_t limitSteps = INT32_MIN, uint32_t slowZoneSteps = 0, uint16_t slowSpeed = 10) override;
    bool jogStop() override;
    bool setJogSpeed(uint32_t stepsPerSecond) override;
    uint32_t getJogSpeed() const override { return jogSpeedHz; }
    bool setServoEnabled(bool enable) override;
    // Compatibilité SureServo : vitesse = speed x 1000 pas/s, rampIndex ignoré
    // (l'accélération vient du constructeur / setAcceleration()). Préférer setMoveSpeed().
    bool setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) override;
    bool setMaxTorque(uint8_t torquePercent) override;   // sans objet : renvoie true
    bool emergencyStop() override;
    bool saveParameters() override;                      // sans objet : renvoie true
    void clearMovementLock() override;

    int32_t getPosition() const override;                // pas, sans trace Serial
    bool getIsReady() const override;
    bool getHasAlarms() const override { return alarmCode != ALARM_NONE; }
    uint16_t getAlarmCode() const override { return alarmCode; }
    bool getIsInitialized() const override { return initialized; }
    const char* getLastError() const override { return lastError.c_str(); }
    bool getHomeDone() const override { return homingDone; }

    int32_t getActualPosition() const override { return getPosition(); }
    // Vitesse des prochains déplacements en pas/s, bornée à [MIN_SPEED_HZ, MAX_SPEED_HZ].
    bool setMoveSpeed(int32_t stepsPerSecond) override;
    // Vrai pendant l'arrêt décéléré qui suit stopMovement()/emergencyStop() (au moins
    // STOP_LOCK_MIN_MS) : startGoToPosition() est alors refusé.
    bool isImmediateStopActive() const override;
    const char* getAlarmDescription() const override;

    bool handleInitializing(OperationStatus& status) override;
    // homingSpeed en pas/s (0 = setHomingParameters() ou vitesse de jog), timeoutMs 0 = défaut.
    bool handleHoming(OperationStatus& status, uint32_t homingSpeed = 0, uint32_t timeoutMs = 0) override;
    bool handleJogging(OperationStatus& status) override;
    bool handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync = true, uint32_t timeoutMs = 60000) override;

    // ========================================
    // CONFIGURATION SPÉCIFIQUE PAS-À-PAS
    // ========================================

    bool setAcceleration(uint32_t stepsPerS2);
    uint32_t getAcceleration() const { return accelerationHz2; }
    uint32_t getMoveSpeed() const { return moveSpeedHz; }
    // Ancienne API : vitesse des déplacements + accélération.
    bool setAccelerationProfile(uint32_t maxSpeedStepsPerSec, uint32_t accelStepsPerSec2);

    /**
     * Le zéro est le point où le capteur d'origine est vu actif ; l'axe finit à
     * -direction x backoffSteps (0 sans dégagement), après un arrêt décéléré et un retour.
     * @param direction    -1 : recherche vers les positions négatives (défaut), +1 : vers les positives
     * @param speedHz      vitesse de recherche en pas/s (0 = vitesse de jog)
     * @param backoffSteps dégagement final depuis le zéro, sens opposé à la recherche (0 = aucun)
     */
    void setHomingParameters(int8_t direction, uint32_t speedHz, uint32_t backoffSteps);
    void setHomingTimeout(uint32_t timeoutMs) { homingTimeoutMs = timeoutMs ? timeoutMs : DEFAULT_HOMING_TIMEOUT_MS; }
    uint32_t getHomingTimeout() const { return homingTimeoutMs; }
    void setInitTimeout(uint32_t timeoutMs) { initTimeoutMs = timeoutMs ? timeoutMs : DEFAULT_INIT_TIMEOUT_MS; }
    void setResetTimeout(uint32_t timeoutMs) { resetTimeoutMs = timeoutMs ? timeoutMs : DEFAULT_RESET_TIMEOUT_MS; }
    // Délai utilisé quand startGoToPosition() reçoit timeout = 0.
    void setMoveTimeout(uint32_t timeoutMs) { moveTimeoutDefaultMs = timeoutMs ? timeoutMs : DEFAULT_MOVE_TIMEOUT_MS; }

    // Butées logicielles (pas), actives après la prise d'origine. clearSoftLimits() les retire.
    void setSoftLimits(int32_t minSteps, int32_t maxSteps);
    void clearSoftLimits() { softLimitsEnabled = false; }

    // À appeler avant l'initialisation (avant le premier handleInitializing()).
    void setDirectionInverted(bool inverted);                  // DIR haut = sens négatif
    void setEnableActiveLow(bool activeLow);                   // défaut true : ENABLE bas/inactif = driver activé
    void setEnableDelay(uint32_t ms) { enableDelayMs = ms; }   // attente ENABLE -> premier pas
    void setDriverType(DriverType type) { driverType = type; }
    // true (défaut) : couper le driver (ENABLE) ou un arrêt brutal en mouvement invalide l'origine.
    void setHomeLostOnDisable(bool lost) { homeLostOnDisable = lost; }
    void setJogWatchdog(uint32_t ms) { jogWatchdogMs = ms; }   // 0 = désactivé

    // ========================================
    // ÉTAT
    // ========================================

    int32_t getCurrentSpeed() const;                 // pas/s signés (0 à l'arrêt)
    int32_t getTargetPosition() const { return targetPosition; }
    bool isMoving() const;
    bool getServoActivated() const { return enabled; }
    bool getTargetPositionReached() const { return lastMoveOk && !isMoving(); }
    uint32_t getStepsPerUnit() const { return stepsPerUnit; }
    uint8_t getStepPinNumber() const { return stepPinNumber; }
    uint8_t getDirPinNumber() const { return dirPinNumber; }
    void printConfiguration() const;

private:
    static constexpr uint8_t PIN_NONE = 0xFF;

    // ---- Configuration ----
    const uint32_t stepsPerUnit;
    const uint32_t maxRangeSteps;
    const StepperNodes nodes;

    FastAccelStepper* fasMotor = nullptr;
    const char* driverName = "-";
    DriverType driverType = DriverType::AUTO;

    uint8_t stepPinNumber = PIN_NONE;
    uint8_t dirPinNumber = PIN_NONE;
    uint8_t enablePinNumber = PIN_NONE;   // GPIO si l'ENABLE est confié à FastAccelStepper
    bool enableViaNode = false;           // ENABLE écrit par setValue() sur un nœud non GPIO
    bool pinsValid = false;
    bool directionInverted = false;
    bool enableActiveLow = true;
    bool dirOutputInverted = false;       // nœud DIR déclaré actif bas dans le fichier matériel
    bool enableOutputInverted = false;    // nœud ENABLE GPIO déclaré actif bas
    bool homeLostOnDisable = true;

    uint32_t accelerationHz2;
    uint32_t moveSpeedHz = DEFAULT_MOVE_SPEED;
    uint32_t jogSpeedHz = DEFAULT_JOG_SPEED;
    uint32_t homingSpeedHz = 0;           // 0 = jogSpeedHz
    uint32_t homingSpeedOverrideHz = 0;   // vitesse passée à handleHoming()
    uint32_t homingBackoffSteps = 0;
    int8_t homingDirection = -1;
    int32_t homingSwitchPosition = 0;     // position lue à la détection du capteur

    bool softLimitsEnabled = false;
    int32_t softLimitMin = 0;
    int32_t softLimitMax = 0;

    uint32_t enableDelayMs = DEFAULT_ENABLE_DELAY_MS;
    uint32_t jogWatchdogMs = DEFAULT_JOG_WATCHDOG_MS;
    uint32_t initTimeoutMs = DEFAULT_INIT_TIMEOUT_MS;
    uint32_t resetTimeoutMs = DEFAULT_RESET_TIMEOUT_MS;
    uint32_t moveTimeoutDefaultMs = DEFAULT_MOVE_TIMEOUT_MS;
    uint32_t homingTimeoutMs = DEFAULT_HOMING_TIMEOUT_MS;

    // ---- États des opérations ----
    OperationState initState = OperationState::DRIVE_IDLE;
    OperationState resetState = OperationState::DRIVE_IDLE;
    OperationState moveState = OperationState::DRIVE_IDLE;
    OperationState homingState = OperationState::DRIVE_IDLE;
    uint8_t initPhase = 0;
    uint8_t resetPhase = 0;
    uint8_t movePhase = 0;
    uint8_t homingPhase = 0;

    uint32_t initStartMs = 0;
    uint32_t resetStartMs = 0;
    uint32_t resetPhaseMs = 0;
    uint32_t moveStartMs = 0;
    uint32_t moveTimeoutMs = DEFAULT_MOVE_TIMEOUT_MS;
    uint32_t homingStartMs = 0;
    uint32_t homingActiveTimeoutMs = DEFAULT_HOMING_TIMEOUT_MS;

    bool initialized = false;
    bool enabled = false;
    uint32_t enabledSinceMs = 0;
    bool homingDone = false;

    int32_t targetPosition = 0;
    bool lastMoveOk = false;              // dernier déplacement terminé sur la cible
    int8_t commandedDirection = 0;        // sens de la dernière commande (+1/-1/0)

    // ---- Jog ----
    int8_t jogDirection = 0;              // 0 = pas de jog
    bool jogStarted = false;              // commande envoyée à FastAccelStepper
    uint32_t jogAppliedSpeedHz = 0;
    int32_t jogAppliedLimit = 0;
    bool jogAppliedLimited = false;
    uint32_t lastJogCallMs = 0;

    // ---- Arrêt ----
    bool stopRequested = false;
    uint32_t stopLockUntilMs = 0;

    // ---- Alarmes (mémorisées jusqu'au réarmement) ----
    uint16_t alarmCode = ALARM_NONE;
    bool driverAlarmPrev = false;
    bool resetNeedsEnableCycle = false;

    // ---- Handlers (remplacent les anciennes variables statiques de fonction) ----
    bool initHandlerStarted = false;
    bool initFailureReported = false;
    bool homingHandlerStarted = false;
    bool moveFailureReported = false;
    uint32_t lastHomingProgressLogMs = 0;
    uint32_t lastMoveProgressLogMs = 0;

    // ---- Recopie IHM / synchronisation des réglages ----
    int32_t lastPublishedPosition = 0;
    bool positionPublished = false;
    uint32_t lastPositionPublishMs = 0;
    uint32_t lastNodeMaxSpeed = 0;
    uint32_t lastNodeJogSpeed = 0;
    uint32_t lastNodeAcceleration = 0;

    String lastError;
    String configError;                   // erreur de configuration des broches (constructeur)
    uint32_t lastErrorLogMs = 0;

    // ---- Méthodes privées ----
    void axisLog(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void setError(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void userMessage(uint8_t type, const char* fmt, ...) const __attribute__((format(printf, 3, 4)));

    bool extractPinNumber(HardwareBooleanOutputNode* node, const char* pinName, uint8_t& pinNumber);
    bool dirHighCountsUp() const { return directionInverted == dirOutputInverted; }
    bool connectStepper();
    FastAccelStepper* acquireStepper(uint8_t pin);
    void applyEnableOutput(bool enable);
    bool enableSettled(uint32_t now) const;
    void invalidateHome(const char* reason);

    bool applySpeed(uint32_t speedHz);
    bool issueMoveTo(int32_t target, uint32_t speedHz);
    bool issueRun(int8_t direction, uint32_t speedHz);
    bool jog(int8_t direction, int32_t limitSteps, uint32_t slowZoneSteps, uint16_t slowSpeed);
    void stopJogInternal();
    int8_t motionDirection() const;
    bool limitActive(int8_t direction) const;
    bool targetWithinSoftLimits(int32_t target) const;
    uint32_t estimateMoveMs(int64_t distance, uint32_t speedHz) const;

    void processInitialize(uint32_t now);
    void processReset(uint32_t now);
    void processMove(uint32_t now);
    void processHoming(uint32_t now);
    void homingFail(const char* reason);
    void homingFinish();
    void processJog(uint32_t now);
    void monitorDriverAlarm();
    void monitorLimitSwitches();
    void raiseAlarm(uint16_t code, const char* reason);
    void failActiveOperations(const char* reason);
    void finishReset();
    void syncParametersFromNodes();
    void updateStatusNodes(uint32_t now);
};

#endif // STEPPER_MOTOR_H

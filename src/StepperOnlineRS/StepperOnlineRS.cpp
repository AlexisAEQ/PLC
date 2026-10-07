#include "StepperOnlineRS.h"

#include <stdarg.h>
#include <stdio.h>
#include "BorneUniverselle/borneUniverselle.h"
#include "MyModbus/MyModbus.h"

// ============================================================================
// ALARMES (registre 0x2203)
// ============================================================================
namespace {

struct StepperOnlineRSAlarmText {
    uint16_t code;
    const char* text;
};

// Codes du manuel des variateurs RS (protocole Leadshine RS, § « Drive alarm codes ») : les
// valeurs sont des puissances de 2, plusieurs défauts peuvent être présents à la fois.
const StepperOnlineRSAlarmText SO_RS_ALARMS[] = {
    {0x0001, "Surintensité (court-circuit moteur ?)"},
    {0x0002, "Surtension (alimentation)"},
    {0x0040, "Défaut du circuit de mesure de courant"},
    {0x0080, "Échec du blocage de l'arbre (moteur débranché ?)"},
    {0x0100, "Défaut d'auto-réglage"},
    {0x0200, "Défaut EEPROM"},
};

const char* lookupAlarmBit(uint16_t bit) {
    for (const auto& a : SO_RS_ALARMS) {
        if (a.code == bit) return a.text;
    }
    return nullptr;
}

int32_t clampToInt32(int64_t v) {
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

}  // namespace

// ============================================================================
// OPTIONS PAR MODÈLE
// ============================================================================
StepperOnlineRSOptions StepperOnlineRSOptions::defaultsFor(StepperOnlineRSModel model) {
    StepperOnlineRSOptions o;
    switch (model) {
        case StepperOnlineRSModel::DM556RS:
            o.maxPeakCurrent = 56;          // 5,6 A
            o.requireEnableBit = false;     // boucle ouverte : alimenté sauf entrée ENA (pas de Pr0.07)
            break;
        case StepperOnlineRSModel::DM882RS:
            o.maxPeakCurrent = 82;          // 8,2 A
            o.requireEnableBit = false;
            break;
        case StepperOnlineRSModel::CL57RS:
            o.maxPeakCurrent = 70;          // 7,0 A
            o.requireEnableBit = true;      // boucle fermée : Pr0.07 = 1 écrit à l'initialisation
            break;
        case StepperOnlineRSModel::CL86RS:
            o.maxPeakCurrent = 80;          // 8,0 A
            o.requireEnableBit = true;
            break;
    }
    return o;
}

// ============================================================================
// CONSTRUCTION
// ============================================================================
StepperOnlineRS::StepperOnlineRS(StepperOnlineRSModel model_, const StepperOnlineRSNodes& nodes_, const char* servoId_)
    : ServoDrive(servoId_),
      model(model_),
      opt(StepperOnlineRSOptions::defaultsFor(model_)),
      nodes(nodes_) {
    construct();
}

StepperOnlineRS::StepperOnlineRS(StepperOnlineRSModel model_, const StepperOnlineRSNodes& nodes_,
                                 const StepperOnlineRSOptions& options_, const char* servoId_)
    : ServoDrive(servoId_),
      model(model_),
      opt(options_),
      nodes(nodes_) {
    construct();
}

void StepperOnlineRS::construct() {
    alarmDescBuf[0] = '\0';
    moveSpeedRpm = opt.defaultMoveSpeedRpm ? opt.defaultMoveSpeedRpm : 1;
    jogSpeedRpm = opt.defaultJogSpeedRpm ? opt.defaultJogSpeedRpm : 1;
    if (moveSpeedRpm > opt.maxSpeedRpm) moveSpeedRpm = opt.maxSpeedRpm;
    if (jogSpeedRpm > opt.maxSpeedRpm) jogSpeedRpm = opt.maxSpeedRpm;
    accelTimeMs = opt.defaultAccelMs > opt.maxAccelMs ? opt.maxAccelMs : opt.defaultAccelMs;
    torqueRefCurrent = opt.torqueRefCurrent;

    if (!nodes.validateRequired()) {
        String msg = "Nodes obligatoires manquants :";
        if (!nodes.prMode) msg += " prMode";
        if (!nodes.moveSpeed) msg += " moveSpeed";
        if (!nodes.trigger) msg += " trigger";
        if (!nodes.controlWord) msg += " controlWord";
        if (!nodes.targetPosition) msg += " targetPosition";
        if (!nodes.status) msg += " status";
        if (!nodes.alarmCode) msg += " alarmCode";
        if (!nodes.triggerStatus) msg += " triggerStatus";
        if (!nodes.position) msg += " position";
        setError(msg.c_str());
        nodesOk = false;
        return;
    }
    nodesOk = true;
    logf("StepperOnline %s créé (%u nodes optionnels)", getModelName(model), (unsigned)nodes.countOptional());
}

const char* StepperOnlineRS::getModelName(StepperOnlineRSModel m) {
    switch (m) {
        case StepperOnlineRSModel::DM556RS: return "DM556RS";
        case StepperOnlineRSModel::DM882RS: return "DM882RS";
        case StepperOnlineRSModel::CL57RS:  return "CL57RS";
        case StepperOnlineRSModel::CL86RS:  return "CL86RS";
    }
    return "RS";
}

// ============================================================================
// BOUCLE PRINCIPALE
// ============================================================================
void StepperOnlineRS::process() {
    if (!nodesOk) return;
    uint32_t now = millis();

    updateStatus(now);
    supervise(now);

    if (initState == OperationState::DRIVE_IN_PROGRESS) processInitialize(now);
    if (resetState == OperationState::DRIVE_IN_PROGRESS) processReset(now);
    if (moveState == OperationState::DRIVE_IN_PROGRESS) processMove(now);
    if (homingState == OperationState::DRIVE_IN_PROGRESS) processHoming(now);
    processJog(now);
    processStop(now);

    updateFastPolling();
    updateOutputNodes();
}

bool StepperOnlineRS::statusBit(uint8_t bit, bool whenUnused) const {
    if (bit >= 16) return whenUnused;
    return (statusWord & (uint16_t)(1u << bit)) != 0;
}

// Lecture et décodage des registres d'état (aucun accès bus : valeurs du dernier refresh)
void StepperOnlineRS::updateStatus(uint32_t now) {
    statusWord = nodes.status->getValue();
    alarmCodeValue = nodes.alarmCode->getValue();
    triggerStatusValue = nodes.triggerStatus->getValue();

    faultBit = statusBit(opt.stFault, false);
    enabledBit = statusBit(opt.stEnabled, true);
    runningBit = statusBit(opt.stRunning, false);
    homeDoneBit = statusBit(opt.stHomeDone, false);
    // Registre de déclenchement relu : 0x000P = trajet P terminé, nouvelle commande acceptée ;
    // 0x001P / 0x0020 / 0x0040 = commande pas encore prise en compte ; 0x010P = trajet P en
    // cours ; 0x0200 = commande terminée, attente du positionnement.
    bool busy = (triggerStatusValue & 0xFFF0) == 0x0010 || (triggerStatusValue & 0xFF00) == 0x0100 ||
                triggerStatusValue == 0x0200;
    inPosBit = !runningBit && !busy;

    // Front montant du bit de défaut : lecture immédiate du code (libellé correct dès le signalement)
    if (faultBit && !prevFaultBit) {
        faultBitSince = now ? now : 1;
        if (!alarmCodeFast) {
            savedAlarmInterval = nodes.alarmCode->getRefreshInterval();
            nodes.alarmCode->setRefreshInterval(0);
            alarmCodeFast = true;
        }
    }
    if (alarmCodeFast && (!faultBit || fresh(nodes.alarmCode, faultBitSince))) {
        nodes.alarmCode->setRefreshInterval((uint16_t)savedAlarmInterval);
        alarmCodeFast = false;
    }
    prevFaultBit = faultBit;

    // Confirmation sur le bus de Pr0.07 = 1 (référence de la détection de redémarrage)
    if (enableWritten && enableConfirmedAt == 0 && fresh(nodes.softwareEnable, settingsWriteTime)) {
        enableConfirmedAt = nodes.softwareEnable->getLastRefresh();
    }
}

// Surveillance : communication, redémarrage du variateur, front d'alarme
void StepperOnlineRS::supervise(uint32_t now) {
    // ---- Communication ----
    uint16_t addr = getSlaveAddress();
    bool slaveDead = addr != 0 && MyModbus::getInstance().isSlaveConsideredDead(addr);
    uint32_t lastStatus = nodes.status->getLastRefresh();
    uint32_t baseInterval = fastPolling ? savedStatusInterval : nodes.status->getRefreshInterval();
    bool stale = lastStatus == 0 || (now - lastStatus) > COMM_LOSS_MS + 2 * baseInterval;

    if (initialized) {
        if (!commLost && (slaveDead || stale)) {
            commLost = true;
            logf("Perte de communication Modbus (adresse %u)", (unsigned)addr);
            if (!nodes.softwareEnableReadback) homeLatched = false;  // redémarrage du variateur non détectable
            failMotion("Perte de communication Modbus avec le variateur");
            if (onAlarmCallback) onAlarmCallback(0, "Perte de communication Modbus avec le variateur");
        }
        if (commLost && (now - lastCommMessage) > COMM_MESSAGE_INTERVAL_MS) {
            char msg[128];
            snprintf(msg, sizeof(msg), "[%s] Perte de communication avec le variateur StepperOnline - vérifiez le bus RS485",
                     servoId.c_str());
            BorneUniverselle::prepareMessage(ERROR, msg);
            lastCommMessage = now;
        }
        if (commLost && !slaveDead && !stale) {
            commLost = false;
            logf("Communication rétablie");
            writeRuntimeSettings(now);   // le variateur a pu redémarrer (réglages en RAM perdus)
        }
    } else {
        commLost = false;
    }

    // ---- Redémarrage du variateur : Pr0.07 relu différent de 1 ----
    if (initialized && !commLost && enableConfirmedAt != 0 && nodes.softwareEnableReadback &&
        fresh(nodes.softwareEnableReadback, enableConfirmedAt) && nodes.softwareEnableReadback->getValue() != 1) {
        logf("Pr0.07 relu à %u au lieu de 1", (unsigned)nodes.softwareEnableReadback->getValue());
        homeLatched = false;
        notifyDriveFault("Variateur redémarré (Pr0.07 perdu) - refaire l'initialisation");
        writeRuntimeSettings(now);
    }

    // ---- Front montant d'alarme ----
    bool has = getHasAlarms();
    if (has && !lastHasAlarms) {
        logf("ALARME 0x%04X : %s", (unsigned)alarmCodeValue, getAlarmDescription());
        if (hasAlarmRaw() && onAlarmCallback) onAlarmCallback(alarmCodeValue, getAlarmDescription());
    } else if (!has && lastHasAlarms) {
        logf("Alarme effacée");
    }
    lastHasAlarms = has;
}

// Réglages de fonctionnement écrits en RAM (jamais en EEPROM) : alimentation forcée par
// logiciel Pr0.07 et décélération de l'arrêt rapide Pr8.23.
void StepperOnlineRS::writeRuntimeSettings(uint32_t now) {
    settingsWriteTime = now;
    enableConfirmedAt = 0;
    enableWritten = false;
    quickStopWritten = false;
    if (nodes.softwareEnable) {
        forceWrite16(nodes.softwareEnable, 1);
        enableWritten = true;
    }
    if (nodes.quickStopTime && opt.quickStopMs > 0) {
        forceWrite16(nodes.quickStopTime, opt.quickStopMs);
        quickStopWritten = true;
    }
}

bool StepperOnlineRS::runtimeSettingsOnWire() const {
    if (enableWritten && !fresh(nodes.softwareEnable, settingsWriteTime)) return false;
    if (quickStopWritten && !fresh(nodes.quickStopTime, settingsWriteTime)) return false;
    return true;
}

bool StepperOnlineRS::motionActive() const {
    return moveState == OperationState::DRIVE_IN_PROGRESS ||
           homingState == OperationState::DRIVE_IN_PROGRESS ||
           jogDir != 0 || settleActive;
}

void StepperOnlineRS::updateFastPolling() {
    if (!opt.fastPollingDuringMotion) return;
    bool want = motionActive();
    if (want && !fastPolling) {
        savedStatusInterval = nodes.status->getRefreshInterval();
        savedPositionInterval = nodes.position->getRefreshInterval();
        savedTriggerInterval = nodes.triggerStatus->getRefreshInterval();
        nodes.status->setRefreshInterval(0);
        nodes.position->setRefreshInterval(0);
        nodes.triggerStatus->setRefreshInterval(0);
        fastPolling = true;
    } else if (!want && fastPolling) {
        nodes.status->setRefreshInterval((uint16_t)savedStatusInterval);
        nodes.position->setRefreshInterval((uint16_t)savedPositionInterval);
        nodes.triggerStatus->setRefreshInterval((uint16_t)savedTriggerInterval);
        fastPolling = false;
    }
}

void StepperOnlineRS::updateOutputNodes() {
    if (nodes.servoReady) nodes.servoReady->setValue(getIsReady());
    if (nodes.inPosition) nodes.inPosition->setValue(inPosBit);
    if (nodes.zeroSpeed) nodes.zeroSpeed->setValue(!runningBit);
    if (nodes.homeDone) nodes.homeDone->setValue(getHomeDone());
    if (nodes.servoAlarm) nodes.servoAlarm->setValue(getHasAlarms());
    if (nodes.driveInitialised) nodes.driveInitialised->setValue(initialized);
    if (nodes.modbusError) nodes.modbusError->setValue(commLost);
}

// ============================================================================
// ÉCRITURES
// ============================================================================
void StepperOnlineRS::forceWrite16(Uint16OutputNode* node, uint16_t value) {
    if (node) node->setValue(value, true);
}

// Uint32OutputNode n'a pas de paramètre « force » : une valeur intermédiaire différente
// garantit l'écriture (seule la valeur finale part sur le bus).
void StepperOnlineRS::forceWrite32(Uint32OutputNode* node, uint32_t value) {
    if (!node) return;
    if (node->getValue() == value) node->setValue(value ^ 1u);
    node->setValue(value);
}

// Le node « Dobble » écrit (et lit) le mot de poids faible à l'adresse basse ; le variateur
// attend le poids fort à l'adresse basse : on échange les deux mots.
uint32_t StepperOnlineRS::toWire(int32_t value) const {
    uint32_t v = (uint32_t)value;
    return opt.highWordFirst ? ((v << 16) | (v >> 16)) : v;
}

int32_t StepperOnlineRS::fromWire(uint32_t raw) const {
    return (int32_t)(opt.highWordFirst ? ((raw << 16) | (raw >> 16)) : raw);
}

// ============================================================================
// SÉQUENCEUR DE CHARGEMENT DU TRAJET PR0
// ============================================================================
// Toutes les écritures sont différées au prochain BorneUniverselle::refresh(), dans
// l'ordre des nodes : le déclenchement 0x10 n'est écrit que lorsque mode, cible, vitesse
// et rampes sont confirmés sur le bus (getLastRefresh() postérieur). Tout est ré-écrit de
// force : un variateur redémarré ne repart pas avec une ancienne cible.
void StepperOnlineRS::beginLoad(int32_t target, uint16_t rpm, uint32_t now) {
    if (rpm < 1) rpm = 1;
    if (rpm > opt.maxSpeedRpm) rpm = opt.maxSpeedRpm;
    forceWrite16(nodes.prMode, opt.prMode);
    forceWrite32(nodes.targetPosition, toWire(target));
    forceWrite16(nodes.moveSpeed, rpm);
    loadAccelWritten = false;
    if (accelTimeMs > 0) {
        if (nodes.accelTime) { forceWrite16(nodes.accelTime, accelTimeMs); loadAccelWritten = true; }
        if (nodes.decelTime) { forceWrite16(nodes.decelTime, accelTimeMs); loadAccelWritten = true; }
    }
    lastLoadSpeedRpm = rpm;
    loadWriteTime = now;
    loadTriggerTime = 0;
    loadOnWireTime = 0;
    loadState = LoadState::WRITING;
}

StepperOnlineRS::LoadState StepperOnlineRS::serviceLoad(uint32_t now) {
    switch (loadState) {
        case LoadState::WRITING: {
            bool written = fresh(nodes.prMode, loadWriteTime) && fresh(nodes.targetPosition, loadWriteTime) &&
                           fresh(nodes.moveSpeed, loadWriteTime);
            if (written && loadAccelWritten) {
                if (nodes.accelTime && !fresh(nodes.accelTime, loadWriteTime)) written = false;
                if (nodes.decelTime && !fresh(nodes.decelTime, loadWriteTime)) written = false;
            }
            if (!written) {
                if (now - loadWriteTime > WRITE_CONFIRM_TIMEOUT_MS) loadState = LoadState::FAILED;
                break;
            }
            forceWrite16(nodes.trigger, TRIG_PR0);
            loadTriggerTime = now;
            loadState = LoadState::TRIGGER;
            break;
        }

        case LoadState::TRIGGER:
            if (fresh(nodes.trigger, loadTriggerTime)) {
                loadOnWireTime = nodes.trigger->getLastRefresh();
                loadState = LoadState::DONE;
            } else if (now - loadTriggerTime > WRITE_CONFIRM_TIMEOUT_MS) {
                loadState = LoadState::FAILED;
            }
            break;

        default:
            break;
    }
    return loadState;
}

// ============================================================================
// INITIALISATION
// ============================================================================
bool StepperOnlineRS::startInitialize() {
    if (!nodesOk) {
        setError("Nodes obligatoires manquants pour l'initialisation");
        return false;
    }
    if (initState != OperationState::DRIVE_IDLE) {
        setError("Initialisation déjà en cours ou terminée");
        return false;
    }
    uint32_t now = millis();
    lastError = "";
    lastErrorRaw = "";
    initialized = false;

    if (hasAlarmRaw()) {
        logf("Variateur en alarme (0x%04X) - acquittement automatique avant initialisation",
             (unsigned)alarmCodeValue);
        if (resetState == OperationState::DRIVE_IN_PROGRESS) {
            setError("Variateur en alarme et reset déjà en cours - initialisation impossible");
            return false;
        }
        if (!startReset()) {
            setError("Échec du démarrage du reset automatique");
            return false;
        }
        initState = OperationState::DRIVE_IN_PROGRESS;
        initPhase = 100;
        initStartTime = now;
        initPhaseTime = now;
        return true;
    }

    initState = OperationState::DRIVE_IN_PROGRESS;
    initPhase = 0;
    initStartTime = now;
    initPhaseTime = now;
    logf("Initialisation démarrée");
    return true;
}

void StepperOnlineRS::processInitialize(uint32_t now) {
    // Attente du variateur : le budget de temps est gelé tant que l'esclave ne répond pas
    uint16_t addr = getSlaveAddress();
    if (addr != 0 && MyModbus::getInstance().isSlaveConsideredDead(addr)) {
        initStartTime = now;
        if (waitModbusStartTime == 0) {
            waitModbusStartTime = now;
            lastWaitLog = now;
            logf("Attente du variateur (adresse Modbus %u)...", (unsigned)addr);
        } else if (now - lastWaitLog > 5000) {
            logf("Variateur (adresse %u) toujours injoignable", (unsigned)addr);
            lastWaitLog = now;
        }
        return;
    }
    if (waitModbusStartTime != 0) {
        logf("Variateur (adresse %u) joignable", (unsigned)addr);
        waitModbusStartTime = 0;
    }

    if (now - initStartTime > INIT_TIMEOUT_MS) {
        setError("Timeout initialisation");
        initState = OperationState::DRIVE_TIMEOUT;
        return;
    }

    // Acquittement automatique préalable (alarme présente au démarrage)
    if (initPhase == 100) {
        switch (resetState) {
            case OperationState::DRIVE_IN_PROGRESS:
                return;
            case OperationState::DRIVE_IDLE:
                logf("Acquittement automatique réussi");
                initPhase = 0;
                initPhaseTime = now;
                break;
            default:
                setError("Reset automatique échoué - variateur toujours en alarme");
                initState = OperationState::DRIVE_FAILED;
                return;
        }
    }

    switch (initPhase) {
        case 0:
            // Communication établie : au moins une lecture de chaque registre d'état
            if (nodes.status->getLastRefresh() == 0 || nodes.position->getLastRefresh() == 0 ||
                nodes.alarmCode->getLastRefresh() == 0 || nodes.triggerStatus->getLastRefresh() == 0) {
                if (now - initPhaseTime > 5000 && now - lastInitLog > 2000) {
                    logf("Attente de la première lecture de l'état du variateur");
                    lastInitLog = now;
                }
                break;
            }
            if (hasAlarmRaw()) {
                setError("Variateur en alarme - vérifier manuellement");
                initState = OperationState::DRIVE_FAILED;
                return;
            }
            writeRuntimeSettings(now);
            forceWrite16(nodes.prMode, opt.prMode);
            initStartTime = now;   // le timeout démarre après les écritures
            initPhaseTime = now;
            initPhase = 1;
            break;

        case 1:
            if (runtimeSettingsOnWire() && fresh(nodes.prMode, initPhaseTime) &&
                now - initPhaseTime >= ENABLE_SETTLE_MS) {
                initPhaseTime = now;
                initPhase = 2;
            }
            break;

        case 2:
            if (fresh(nodes.status, initPhaseTime) && fresh(nodes.position, initPhaseTime)) {
                if (hasAlarmRaw()) {
                    setError("Alarme variateur pendant l'initialisation");
                    initState = OperationState::DRIVE_FAILED;
                    return;
                }
                if (enabledBit || !opt.requireEnableBit) {
                    initialized = true;
                    initState = OperationState::DRIVE_IDLE;
                    initPhase = 0;
                    logf("Initialisation réussie (position %ld, origine %s)",
                         (long)getActualPosition(), getHomeDone() ? "faite" : "à faire");
                    return;
                }
            }
            if (now - initPhaseTime > 5000 && now - lastInitLog > 2000) {
                logf("Attente moteur alimenté (état 0x%04X, alarme 0x%04X) - entrée ENA / Pr0.07 ?",
                     (unsigned)statusWord, (unsigned)alarmCodeValue);
                lastInitLog = now;
            }
            break;

        default:
            setError("Phase d'initialisation inconnue");
            initState = OperationState::DRIVE_FAILED;
            break;
    }
}

// ============================================================================
// ACQUITTEMENT (RESET)
// ============================================================================
bool StepperOnlineRS::startReset() {
    if (!nodesOk) {
        setError("Nodes obligatoires manquants");
        return false;
    }
    if (isPlcSuspended()) {
        setError("Reset refusé - PLC suspendu");
        return false;
    }
    switch (resetState) {
        case OperationState::DRIVE_IN_PROGRESS:
            setError("Reset déjà en cours");
            return false;
        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT:
            logf("Reset après erreur - nouvel essai");
            break;
        default:
            break;
    }
    uint32_t now = millis();
    resetState = OperationState::DRIVE_IN_PROGRESS;
    resetPhase = 0;
    resetStartTime = now;
    resetPhaseTime = now;
    resetSuccessSince = 0;
    lastError = "";
    lastErrorRaw = "";
    logf("Reset démarré");
    return true;
}

void StepperOnlineRS::processReset(uint32_t now) {
    if (now - resetStartTime > RESET_TIMEOUT_MS) {
        setError("Timeout reset après 5 secondes");
        resetState = OperationState::DRIVE_TIMEOUT;
        return;
    }

    switch (resetPhase) {
        case 0:
            // Le reset acquitte aussi les défauts logiciels (perte de com., redémarrage)
            driveFaultPending = false;
            driveFaultReason = "";
            cancelLoad();
            settleActive = false;
            stopResetPending = false;
            jogDir = 0;
            jogPhase = 0;
            forceWrite16(nodes.controlWord, CW_ALARM_RESET);
            resetPhaseTime = now;
            resetPhase = 1;
            break;

        case 1:
            if (fresh(nodes.controlWord, resetPhaseTime) && now - resetPhaseTime >= RESET_STABILIZE_MS) {
                // Le variateur a pu redémarrer : réglages en RAM ré-imposés
                writeRuntimeSettings(now);
                resetPhaseTime = now;
                resetSuccessSince = 0;
                resetPhase = 2;
            }
            break;

        case 2: {
            bool freshRead = fresh(nodes.status, resetPhaseTime) && fresh(nodes.alarmCode, resetPhaseTime);
            bool ok = freshRead && !commLost && !hasAlarmRaw() && !driveFaultPending && runtimeSettingsOnWire() &&
                      (enabledBit || !opt.requireEnableBit);
            if (ok) {
                if (resetSuccessSince == 0) resetSuccessSince = now ? now : 1;
                if (now - resetSuccessSince >= RESET_STABLE_MS) {
                    logf("Reset réussi");
                    resetState = OperationState::DRIVE_IDLE;
                    resetPhase = 0;
                    // Un reset réussi débloque les états (et relance l'init après emergencyStop)
                    clearMovementLock();
                }
            } else {
                resetSuccessSince = 0;
                if (now - resetPhaseTime > RESET_VERIFY_MS) {
                    char msg[160];
                    snprintf(msg, sizeof(msg),
                             "Reset échoué : alimenté=%d, alarme=%d, code=0x%04X%s%s",
                             enabledBit ? 1 : 0, hasAlarmRaw() ? 1 : 0, (unsigned)alarmCodeValue,
                             commLost ? ", communication perdue" : "",
                             hasAlarmRaw() ? " (certaines alarmes exigent de couper l'alimentation du variateur)" : "");
                    setError(msg);
                    resetState = OperationState::DRIVE_FAILED;
                }
            }
            break;
        }

        default:
            setError("Phase de reset inconnue");
            resetState = OperationState::DRIVE_FAILED;
            break;
    }
}

// ============================================================================
// DÉPLACEMENT
// ============================================================================
bool StepperOnlineRS::startGoToPosition(int32_t positionPuu, bool waitForSync, uint32_t timeout) {
    if (!nodesOk) {
        setError("Nodes obligatoires manquants");
        return false;
    }
    if (isPlcSuspended()) {
        setError("Mouvement refusé - PLC suspendu");
        return false;
    }
    if (movementCancelled) {
        setError("Mouvement bloqué - annulation récente (attendez la fin de l'arrêt)");
        return false;
    }
    switch (moveState) {
        case OperationState::DRIVE_IN_PROGRESS:
            setError("Mouvement déjà en cours");
            return false;
        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT:
            setError("Erreur précédente - utilisez RESET d'abord");
            return false;
        default:
            break;
    }
    if (!getIsReady()) {
        setError("Variateur StepperOnline pas prêt pour un mouvement");
        return false;
    }
    if (faultNow()) {
        setError("Variateur en alarme - mouvement impossible");
        return false;
    }
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Prise d'origine en cours - mouvement impossible");
        return false;
    }
    if (jogDir != 0) {
        setError("Jog en cours - mouvement impossible");
        return false;
    }
    if (loadBusy() || settleActive || stopResetPending) {
        setError("Arrêt en cours - mouvement impossible");
        return false;
    }

    uint32_t now = millis();
    // Déjà en position (lecture récente) : succès immédiat si une synchronisation est demandée
    if (waitForSync && !runningBit && isAtPosition(positionPuu) &&
        nodes.position->getLastRefresh() != 0 && now - nodes.position->getLastRefresh() < 500) {
        targetPosition = positionPuu;
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
        waitForSyncRequested = waitForSync;
        return true;
    }

    targetPosition = positionPuu;
    waitForSyncRequested = waitForSync;
    currentMoveTimeoutMs = timeout > 0 ? timeout : MOVE_TIMEOUT_MS;
    moveState = OperationState::DRIVE_IN_PROGRESS;
    movePhase = 0;
    moveStartTime = now;
    moveStableCount = 0;
    lastError = "";
    lastErrorRaw = "";
    logf("Déplacement vers %ld à %u tr/min", (long)positionPuu, (unsigned)moveSpeedRpm);
    return true;
}

void StepperOnlineRS::processMove(uint32_t now) {
    if (now - moveStartTime > currentMoveTimeoutMs) {
        char msg[112];
        snprintf(msg, sizeof(msg), "Timeout mouvement après %lu s (position %ld, cible %ld, déclenchement 0x%04X)",
                 (unsigned long)(currentMoveTimeoutMs / 1000), (long)getActualPosition(), (long)targetPosition,
                 (unsigned)triggerStatusValue);
        setError(msg);
        cancelLoad();
        moveState = OperationState::DRIVE_TIMEOUT;
        movePhase = 0;
        return;
    }
    if (faultNow()) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Alarme pendant le mouvement (0x%04X)", (unsigned)alarmCodeValue);
        setError(msg);
        cancelLoad();
        moveState = OperationState::DRIVE_FAILED;
        movePhase = 0;
        return;
    }

    switch (movePhase) {
        case 0:
            beginLoad(targetPosition, moveSpeedRpm, now);
            movePhase = 1;
            break;

        case 1: {
            LoadState r = serviceLoad(now);
            if (r == LoadState::FAILED) {
                cancelLoad();
                setError("Écriture du trajet PR0 non confirmée sur le bus");
                moveState = OperationState::DRIVE_FAILED;
                movePhase = 0;
            } else if (r == LoadState::DONE) {
                moveCmdOnWireTime = loadOnWireTime;
                cancelLoad();
                moveStableCount = 0;
                movePrevPosTime = 0;
                moveIdleOffTargetSince = 0;
                movePhase = 2;
            }
            break;
        }

        case 2: {
            // Fin du mouvement : lectures postérieures au déclenchement, axe arrêté, trajet
            // pas en cours côté variateur et cible atteinte.
            if (!fresh(nodes.status, moveCmdOnWireTime) || !fresh(nodes.position, moveCmdOnWireTime) ||
                !fresh(nodes.triggerStatus, moveCmdOnWireTime)) {
                break;
            }
            if (now - moveCmdOnWireTime < MIN_DONE_MS) break;

            // Avertissement PR (fin de course pendant le trajet...) : le trajet est abandonné
            if (nodes.prWarning && fresh(nodes.prWarning, moveCmdOnWireTime) && nodes.prWarning->getValue() != 0) {
                char msg[96];
                snprintf(msg, sizeof(msg), "Trajet interrompu par le variateur (avertissement PR 0x%04X)",
                         (unsigned)nodes.prWarning->getValue());
                setError(msg);
                moveState = OperationState::DRIVE_FAILED;
                movePhase = 0;
                return;
            }

            int32_t pos = getActualPosition();
            bool stopped = inPosBit;   // pas de mouvement, trajet ni en attente ni en cours
            bool done = stopped && isAtPosition(targetPosition);

            // Position stable sur plusieurs lectures (synchronisation demandée, ou détection
            // d'un trajet abandonné hors de la cible : fin de course, axe désactivé...)
            uint32_t pr = nodes.position->getLastRefresh();
            if (pr != movePrevPosTime) {
                int64_t d = (int64_t)pos - (int64_t)movePrevPos;
                if (d < 0) d = -d;
                if (movePrevPosTime != 0 && d <= (int64_t)opt.positionTolerance) {
                    if (moveStableCount < 255) moveStableCount++;
                } else {
                    moveStableCount = 0;
                    moveIdleOffTargetSince = 0;
                }
                movePrevPos = pos;
                movePrevPosTime = pr;
            }

            if (done && waitForSyncRequested) done = moveStableCount >= STABLE_READINGS;

            if (done) {
                moveState = OperationState::DRIVE_IDLE;
                movePhase = 0;
                logf("Position %ld atteinte (%ld)", (long)targetPosition, (long)pos);
                return;
            }

            if (stopped && !isAtPosition(targetPosition) && moveStableCount >= STABLE_READINGS) {
                if (moveIdleOffTargetSince == 0) moveIdleOffTargetSince = now ? now : 1;
                if (now - moveIdleOffTargetSince > IDLE_OFF_TARGET_MS) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Axe arrêté hors cible (position %ld, cible %ld, état 0x%04X, déclenchement 0x%04X)",
                             (long)pos, (long)targetPosition, (unsigned)statusWord, (unsigned)triggerStatusValue);
                    setError(msg);
                    moveState = OperationState::DRIVE_FAILED;
                    movePhase = 0;
                    return;
                }
            } else {
                moveIdleOffTargetSince = 0;
            }

            if (now - lastMoveLog > 2000) {
                logf("Déplacement en cours : position %ld / cible %ld (état 0x%04X, déclenchement 0x%04X)",
                     (long)pos, (long)targetPosition, (unsigned)statusWord, (unsigned)triggerStatusValue);
                lastMoveLog = now;
            }
            break;
        }

        default:
            setError("Phase de mouvement invalide");
            moveState = OperationState::DRIVE_FAILED;
            movePhase = 0;
            break;
    }
}

// ============================================================================
// PRISE D'ORIGINE
// ============================================================================
// 0x20 : prise d'origine selon Pr8.10 (capteur d'origine ou fin de course, sens, vitesses
// Pr8.15/8.16) ; 0x21 (option homingSetsCurrentPosition) : la position actuelle devient 0.
bool StepperOnlineRS::startHoming(uint32_t timeoutMs) {
    if (!nodesOk) {
        setError("Nodes obligatoires manquants");
        return false;
    }
    if (isPlcSuspended()) {
        setError("Homing refusé - PLC suspendu");
        return false;
    }
    if (!getIsReady()) {
        setError("Variateur StepperOnline pas prêt pour la prise d'origine");
        return false;
    }
    if (faultNow()) {
        setError("Variateur en alarme - prise d'origine impossible");
        return false;
    }
    if (moveState == OperationState::DRIVE_IN_PROGRESS || jogDir != 0 || loadBusy() || settleActive ||
        stopResetPending) {
        setError("Mouvement en cours - prise d'origine impossible");
        return false;
    }
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Prise d'origine déjà en cours");
        return false;
    }

    uint32_t now = millis();
    currentHomingTimeoutMs = timeoutMs > 0 ? timeoutMs : HOMING_TIMEOUT_MS;
    homeLatched = false;
    forceWrite16(nodes.trigger, opt.homingSetsCurrentPosition ? TRIG_SET_ZERO : TRIG_HOMING);
    homingWriteTime = now;
    homingState = OperationState::DRIVE_IN_PROGRESS;
    homingPhase = 0;
    homingStartTime = now;
    homingSeenHomeLow = false;
    homingSeenMotion = false;
    homingStableCount = 0;
    homingPrevPosTime = 0;
    lastError = "";
    lastErrorRaw = "";
    logf("Prise d'origine démarrée (%s)", opt.homingSetsCurrentPosition ? "position actuelle = 0" : "Pr8.10");
    return true;
}

void StepperOnlineRS::processHoming(uint32_t now) {
    uint32_t elapsed = now - homingStartTime;
    if (elapsed > currentHomingTimeoutMs) {
        setError("Timeout prise d'origine");
        homingState = OperationState::DRIVE_TIMEOUT;
        homingPhase = 0;
        return;
    }
    if (faultNow()) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Alarme pendant la prise d'origine (0x%04X)", (unsigned)alarmCodeValue);
        setError(msg);
        homingState = OperationState::DRIVE_FAILED;
        homingPhase = 0;
        return;
    }

    switch (homingPhase) {
        case 0:
            if (fresh(nodes.trigger, homingWriteTime)) {
                homingOnWireTime = nodes.trigger->getLastRefresh();
                homingStartPos = getActualPosition();
                homingPhase = 1;
            } else if (now - homingWriteTime > WRITE_CONFIRM_TIMEOUT_MS) {
                setError("Commande de prise d'origine non confirmée sur le bus");
                homingState = OperationState::DRIVE_FAILED;
                homingPhase = 0;
            }
            break;

        case 1: {
            if (!fresh(nodes.status, homingOnWireTime) || !fresh(nodes.position, homingOnWireTime)) break;

            // Pr8.29 : 0x100 fin de course pendant la prise d'origine, 0x102 dépassement de course
            if (nodes.prWarning && fresh(nodes.prWarning, homingOnWireTime) && nodes.prWarning->getValue() != 0) {
                uint16_t w = nodes.prWarning->getValue();
                char msg[112];
                snprintf(msg, sizeof(msg), "Prise d'origine en défaut (avertissement PR 0x%04X%s)", (unsigned)w,
                         w == 0x100 ? " : fin de course" : (w == 0x102 ? " : dépassement de course" : ""));
                setError(msg);
                homingState = OperationState::DRIVE_FAILED;
                homingPhase = 0;
                return;
            }

            int32_t pos = getActualPosition();
            uint32_t sinceStart = now - homingOnWireTime;
            bool complete = false;

            // Stabilité de la position sur plusieurs lectures
            uint32_t pr = nodes.position->getLastRefresh();
            if (pr != homingPrevPosTime) {
                int64_t d = (int64_t)pos - (int64_t)homingPrevPos;
                if (d < 0) d = -d;
                if (homingPrevPosTime != 0 && d <= (int64_t)opt.positionTolerance) {
                    if (homingStableCount < 255) homingStableCount++;
                } else {
                    homingStableCount = 0;
                }
                homingPrevPos = pos;
                homingPrevPosTime = pr;
            }

            if (opt.homingSetsCurrentPosition) {
                // Pas de mouvement : terminé quand la position relue vaut 0
                complete = !runningBit && isAtPosition(0) && homingStableCount >= 1;
            } else {
                int64_t moved = (int64_t)pos - (int64_t)homingStartPos;
                if (moved < 0) moved = -moved;
                if (!homeDoneBit) homingSeenHomeLow = true;
                if (runningBit || moved > (int64_t)opt.positionTolerance) homingSeenMotion = true;
                bool candidate = homeDoneBit && !runningBit && sinceStart >= HOMING_MIN_MS;
                if (candidate && (homingSeenHomeLow || homingSeenMotion)) {
                    complete = homingStableCount >= STABLE_READINGS;
                } else if (candidate && sinceStart >= HOMING_NO_MOTION_ACCEPT_MS) {
                    // Bit « origine faite » resté vrai sans aucun mouvement : origine déjà valide
                    logf("Prise d'origine sans mouvement détecté : origine déjà valide côté variateur");
                    complete = true;
                }
            }

            if (complete) {
                homeLatched = true;
                homingState = OperationState::DRIVE_IDLE;
                homingPhase = 0;
                logf("Prise d'origine terminée (position %ld, %lu ms)", (long)pos, (unsigned long)elapsed);
                return;
            }
            if (now - lastHomingLog > 2000) {
                logf("Prise d'origine en cours (%lu s, position %ld, état 0x%04X, déclenchement 0x%04X)",
                     (unsigned long)(elapsed / 1000), (long)pos, (unsigned)statusWord, (unsigned)triggerStatusValue);
                lastHomingLog = now;
            }
            break;
        }

        default:
            setError("Phase de prise d'origine invalide");
            homingState = OperationState::DRIVE_FAILED;
            homingPhase = 0;
            break;
    }
}

// ============================================================================
// JOG
// ============================================================================
// Jog par trajet PR0 : consigne absolue vers la butée (ou fenêtre glissante si la butée
// n'est pas applicable) à la vitesse de jog, prolongée avant d'arriver au bout ; arrêt =
// arrêt rapide 0x40 (décélération Pr8.23). Le jog RS485 natif du variateur (0x4001/0x4002
// dans 0x1801) n'est pas utilisé : il exige une ré-écriture toutes les 50 ms.
bool StepperOnlineRS::jogForward(int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    return jogCommand(1, limitPuu, slowZonePuu, slowSpeed);
}

bool StepperOnlineRS::jogReverse(int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    return jogCommand(-1, limitPuu, slowZonePuu, slowSpeed);
}

bool StepperOnlineRS::jogCommand(int8_t dir, int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    if (!nodesOk) {
        setError("Nodes obligatoires manquants");
        return false;
    }
    if (!initialized) {
        setError("Variateur non initialisé - jog impossible");
        return false;
    }
    if (faultNow()) {
        setError("Variateur en alarme - jog impossible");
        return false;
    }
    if (isPlcSuspended()) {
        setError("Commande refusée - PLC suspendu");
        return false;
    }
    if (moveState == OperationState::DRIVE_IN_PROGRESS || homingState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Opération en cours - jog impossible");
        return false;
    }

    int32_t pos = getActualPosition();
    bool homed = getHomeDone();

    // Butées : mêmes règles que SureServo / Lichuan (butée avant seulement si l'origine est faite)
    bool enforced;
    if (dir > 0) {
        enforced = limitPuu != INT32_MAX && homed;
        if (enforced && pos >= limitPuu) {
            jogStop();
            logf("Limite jog avant atteinte (%ld >= %ld)", (long)pos, (long)limitPuu);
            return false;
        }
    } else {
        enforced = limitPuu != INT32_MIN;
        if (enforced && pos <= limitPuu) {
            jogStop();
            logf("Limite jog arrière atteinte (%ld <= %ld)", (long)pos, (long)limitPuu);
            return false;
        }
    }

    // Changement de sens : arrêt d'abord, le nouveau sens part quand l'axe est arrêté
    if (jogDir != 0 && jogDir != dir) {
        jogStop();
        return true;
    }
    // Arrêt précédent pas terminé : on patiente
    if (jogDir == 0 && isImmediateStopActive()) return true;

    // Vitesse (zone lente proche de la butée)
    uint16_t speed = jogSpeedRpm;
    if (slowZonePuu > 0 && homed) {
        if (dir > 0 && limitPuu != INT32_MAX && (int64_t)pos >= (int64_t)limitPuu - (int64_t)slowZonePuu) speed = slowSpeed;
        if (dir < 0 && limitPuu != INT32_MIN && (int64_t)pos <= (int64_t)limitPuu + (int64_t)slowZonePuu) speed = slowSpeed;
    }
    if (speed < 1) speed = 1;
    if (speed > opt.maxSpeedRpm) speed = opt.maxSpeedRpm;

    jogLimit = limitPuu;
    jogLimitEnforced = enforced;
    jogCmdSpeed = speed;
    if (jogDir == 0) {
        jogDir = dir;
        jogPhase = 0;
        jogAppliedSpeed = 0;
        logf("Jog %s à %u tr/min", dir > 0 ? "avant" : "arrière", (unsigned)speed);
    }
    return true;
}

// Fenêtre glissante : distance parcourue en jogWindowMs à la vitesse de jog (au moins jogWindowMinSteps)
int64_t StepperOnlineRS::jogWindow() const {
    int64_t window = (int64_t)jogCmdSpeed * (int64_t)opt.stepsPerRev / 60 * (int64_t)opt.jogWindowMs / 1000;
    if (window < (int64_t)opt.jogWindowMinSteps) window = opt.jogWindowMinSteps;
    if (window < 1000) window = 1000;
    return window;
}

int32_t StepperOnlineRS::computeJogTarget() const {
    int64_t pos = getActualPosition();
    int64_t window = jogWindow();
    int64_t t = jogDir > 0 ? pos + window : pos - window;
    if (jogLimitEnforced) {
        if (jogDir > 0 && t > jogLimit) t = jogLimit;
        if (jogDir < 0 && t < jogLimit) t = jogLimit;
    }
    return clampToInt32(t);
}

void StepperOnlineRS::processJog(uint32_t now) {
    if (jogDir == 0) return;

    switch (jogPhase) {
        case 0:
            jogTarget = computeJogTarget();
            beginLoad(jogTarget, jogCmdSpeed, now);
            jogAppliedSpeed = jogCmdSpeed;
            jogPhase = 1;
            break;

        case 1: {
            LoadState r = serviceLoad(now);
            if (r == LoadState::FAILED) {
                cancelLoad();
                setError("Consigne de jog non confirmée sur le bus");
                stopJogMotion(now);
            } else if (r == LoadState::DONE) {
                cancelLoad();
                jogPhase = 2;
            }
            break;
        }

        default: {
            // Fenêtre glissante : prolonger la consigne avant d'arriver au bout, ou appliquer
            // une nouvelle vitesse (zone lente). Un nouveau déclenchement interrompt le trajet en cours.
            int64_t remaining = (int64_t)jogTarget - (int64_t)getActualPosition();
            if (remaining < 0) remaining = -remaining;
            bool atLimitTarget = jogLimitEnforced && jogTarget == jogLimit;
            bool extend = !atLimitTarget && remaining < jogWindow() / 2;
            if (extend || jogCmdSpeed != jogAppliedSpeed) {
                jogTarget = computeJogTarget();
                beginLoad(jogTarget, jogCmdSpeed, now);
                jogAppliedSpeed = jogCmdSpeed;
                jogPhase = 1;
            }
            break;
        }
    }
}

// Arrête le jog en cours (sans toucher aux autres opérations)
void StepperOnlineRS::stopJogMotion(uint32_t now) {
    if (jogDir == 0) return;
    bool sent = jogPhase >= 1;
    cancelLoad();
    if (sent || runningBit) beginQuickStop(now);
    jogDir = 0;
    jogPhase = 0;
}

bool StepperOnlineRS::jogStop() {
    if (!nodesOk) return false;
    uint32_t now = millis();
    bool wasJogging = jogDir != 0;
    stopJogMotion(now);
    if (wasJogging) logf("Jog arrêté");

    if (moveState != OperationState::DRIVE_IDLE && moveState != OperationState::DRIVE_FAILED) {
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
    }
    return true;
}

bool StepperOnlineRS::setJogSpeed(uint32_t speed) {
    if (speed > opt.maxSpeedRpm) {
        setError("Vitesse de jog trop élevée");
        return false;
    }
    jogSpeedRpm = speed < 1 ? 1 : (uint16_t)speed;
    if (jogDir != 0) jogCmdSpeed = jogSpeedRpm;
    return true;
}

// ============================================================================
// ARRÊTS
// ============================================================================
void StepperOnlineRS::beginQuickStop(uint32_t now) {
    forceWrite16(nodes.trigger, TRIG_QUICK_STOP);
    settleActive = true;
    settleStart = now;
    settleWireTime = 0;
}

void StepperOnlineRS::processStop(uint32_t now) {
    // 1. Arrêt de l'axe constaté (ou délai dépassé)
    if (settleActive) {
        if (settleWireTime == 0) {
            if (fresh(nodes.trigger, settleStart)) settleWireTime = nodes.trigger->getLastRefresh();
        } else if (fresh(nodes.status, settleWireTime) && fresh(nodes.triggerStatus, settleWireTime) && !runningBit) {
            settleActive = false;
            // Registre de déclenchement resté à 0x40 : état d'arrêt rapide verrouillé => acquittement
            if (opt.resetAfterQuickStop && triggerStatusValue == TRIG_QUICK_STOP && !hasAlarmRaw()) {
                forceWrite16(nodes.controlWord, CW_ALARM_RESET);
                stopResetPending = true;
                stopResetTime = now;
                logf("Arrêt rapide acquitté (0x1111)");
            }
        }
        // Délai : 3 s + décélération Pr8.23 (connue si écrite par la classe, sinon 2 s)
        uint32_t limit = STOP_SETTLE_TIMEOUT_MS + (opt.quickStopMs > 0 ? opt.quickStopMs : 2000);
        if (limit > 15000) limit = 15000;
        if (settleActive && now - settleStart > limit) {
            settleActive = false;
            if (fresh(nodes.status, settleStart) && runningBit) {
                // L'ordre d'arrêt n'a pas été exécuté : défaut => la classe métier passe en urgence
                notifyDriveFault("Arrêt non exécuté par le variateur : axe toujours en mouvement");
            } else {
                logf("Arrêt : fin de mouvement non constatée après %lu ms", (unsigned long)limit);
                if (onWarningCallback) onWarningCallback(true, "Arrêt StepperOnline : fin de mouvement non constatée");
            }
        }
    }

    // 2. Fin de l'acquittement après arrêt rapide
    if (stopResetPending && (fresh(nodes.controlWord, stopResetTime) || now - stopResetTime > WRITE_CONFIRM_TIMEOUT_MS)) {
        stopResetPending = false;
    }

    // 3. Fin du verrou : 500 ms minimum et axe arrêté
    if (pendingReleaseImmediateStop && !settleActive && !stopResetPending &&
        now - immediateStopTimestamp >= IMMEDIATE_STOP_DURATION_MS) {
        pendingReleaseImmediateStop = false;
        movementCancelled = false;
        logf("Arrêt terminé - mouvements autorisés");
    }
}

bool StepperOnlineRS::stopMovement() {
    if (!nodesOk) return false;
    uint32_t now = millis();
    logf("Arrêt demandé");

    bool moving = (moveState == OperationState::DRIVE_IN_PROGRESS && movePhase >= 1) ||
                  homingState == OperationState::DRIVE_IN_PROGRESS ||
                  (jogDir != 0 && jogPhase >= 1) || runningBit;

    // 1. Commandes en cours retirées
    cancelLoad();
    jogDir = 0;
    jogPhase = 0;

    // 2. Arrêt physique : arrêt rapide 0x40 (décélération Pr8.23)
    if (moving) beginQuickStop(now);

    // 3. Annulation des opérations (retour à IDLE, pas FAILED)
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
        targetPosition = 0;
    }
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        homingState = OperationState::DRIVE_IDLE;   // driveHomingStarted conservé (consommé par handleHoming)
        homingPhase = 0;
    }

    // 4. Blocage des nouveaux mouvements : 500 ms minimum et jusqu'à l'arrêt de l'axe
    movementCancelled = true;
    pendingReleaseImmediateStop = true;
    immediateStopTimestamp = now;
    return true;
}

bool StepperOnlineRS::emergencyStop() {
    if (!nodesOk) return false;
    uint32_t now = millis();
    cancelLoad();
    settleActive = false;
    stopResetPending = false;
    jogDir = 0;
    jogPhase = 0;

    // Arrêt rapide systématique ; coupure du courant seulement si demandé (un axe pas-à-pas
    // non alimenté perd son couple de maintien : axe vertical !)
    forceWrite16(nodes.trigger, TRIG_QUICK_STOP);
    if (opt.disableOnEmergency && nodes.softwareEnable) {
        forceWrite16(nodes.softwareEnable, 0);
        enableWritten = false;
        enableConfirmedAt = 0;
    }

    initState   = OperationState::DRIVE_FAILED;
    resetState  = OperationState::DRIVE_FAILED;
    moveState   = OperationState::DRIVE_FAILED;
    homingState = OperationState::DRIVE_FAILED;
    initialized = false;

    pendingReleaseImmediateStop = true;
    immediateStopTimestamp = now;
    logf("Arrêt d'urgence : arrêt rapide%s", opt.disableOnEmergency && nodes.softwareEnable ? " et moteur non alimenté" : "");
    return true;
}

bool StepperOnlineRS::isImmediateStopActive() const {
    return pendingReleaseImmediateStop || settleActive || stopResetPending;
}

void StepperOnlineRS::clearMovementLock() {
    movementCancelled = false;

    if (moveState == OperationState::DRIVE_FAILED || moveState == OperationState::DRIVE_TIMEOUT) {
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
    }

    // Après emergencyStop() : relance d'une vraie initialisation
    if (initState == OperationState::DRIVE_FAILED || initState == OperationState::DRIVE_TIMEOUT) {
        initState = OperationState::DRIVE_IDLE;
        initPhase = 0;
        driveInitStarted = false;
        initFailureReported = false;
        if (!startInitialize()) {
            logf("Échec de la relance de l'initialisation : %s", lastErrorRaw.c_str());
        }
    }

    if (homingState != OperationState::DRIVE_IDLE) {
        homingState = OperationState::DRIVE_IDLE;
        homingPhase = 0;
        driveHomingStarted = false;
    }

    targetPosition = 0;
}

void StepperOnlineRS::failMotion(const char* reason) {
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        cancelLoad();
        moveState = OperationState::DRIVE_FAILED;
        movePhase = 0;
        setError(reason);
    }
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        homingState = OperationState::DRIVE_FAILED;
        homingPhase = 0;
        setError(reason);
    }
    if (jogDir != 0) {
        cancelLoad();
        jogDir = 0;
        jogPhase = 0;
    }
}

void StepperOnlineRS::notifyDriveFault(const char* reason) {
    setError(reason);
    driveFaultPending = true;
    driveFaultReason = reason;
    failMotion(reason);
    char msg[160];
    snprintf(msg, sizeof(msg), "[%s] %s", servoId.c_str(), reason);
    BorneUniverselle::prepareMessage(ERROR, msg);
    if (onAlarmCallback) onAlarmCallback(0, reason);
}

// ============================================================================
// RÉGLAGES
// ============================================================================
bool StepperOnlineRS::setServoEnabled(bool enable) {
    if (!nodesOk) return false;
    if (!nodes.softwareEnable) {
        setError("Alimentation moteur non pilotable (node softwareEnable absent : entrée ENA du variateur)");
        return false;
    }
    forceWrite16(nodes.softwareEnable, enable ? 1 : 0);
    settingsWriteTime = millis();
    enableWritten = enable;
    enableConfirmedAt = 0;
    logf("Moteur %s", enable ? "alimenté (Pr0.07 = 1)" : "non alimenté par logiciel (Pr0.07 = 0)");
    return true;
}

bool StepperOnlineRS::setMoveSpeed(int32_t speed) {
    if (speed < 1) speed = 1;
    if (speed > (int32_t)opt.maxSpeedRpm) speed = opt.maxSpeedRpm;
    moveSpeedRpm = (uint16_t)speed;   // écrit avec le trajet au début de chaque déplacement
    return true;
}

bool StepperOnlineRS::setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) {
    (void)rampIndex;   // pas de table de rampes : voir setAccelTime()
    return setMoveSpeed(speed);
}

bool StepperOnlineRS::setAccelTime(uint16_t ms) {
    if (!nodes.accelTime && !nodes.decelTime) {
        setError("Temps de rampe non configurable (nodes accelTime/decelTime absents)");
        return false;
    }
    if (ms > opt.maxAccelMs) ms = opt.maxAccelMs;
    accelTimeMs = ms;
    return true;
}

bool StepperOnlineRS::setTorqueReferenceCurrent(uint16_t deciAmps) {
    if (deciAmps > opt.maxPeakCurrent) deciAmps = opt.maxPeakCurrent;
    torqueRefCurrent = deciAmps;
    return true;
}

// Un pas-à-pas n'a pas de limite de couple : on réduit le courant crête Pr5.00 (en RAM) en
// proportion du courant de référence du moteur. Jamais au-dessus de ce courant de référence.
bool StepperOnlineRS::setMaxTorque(uint8_t torquePercent) {
    if (!nodes.peakCurrent || torqueRefCurrent == 0) {
        setError("Limite de couple non gérée (node peakCurrent ou courant de référence absent)");
        return false;
    }
    uint32_t pct = torquePercent;
    if (pct < opt.minTorquePercent) pct = opt.minTorquePercent;
    if (pct > 100) pct = 100;
    uint32_t v = ((uint32_t)torqueRefCurrent * pct + 50) / 100;
    if (v < 1) v = 1;
    if (v > opt.maxPeakCurrent) v = opt.maxPeakCurrent;
    nodes.peakCurrent->setValue((uint16_t)v);
    logf("Couple max %u %% : courant crête %u,%u A", (unsigned)pct, (unsigned)(v / 10), (unsigned)(v % 10));
    return true;
}

bool StepperOnlineRS::saveParameters() {
    // Volontairement non implémenté : la sauvegarde EEPROM (0x2211 dans 0x1801) use la mémoire
    // du variateur. Le paramétrage se fait une fois à la mise en service.
    setError("Sauvegarde EEPROM non utilisée (paramétrage unique, voir docs/axes/stepperonline-rs.md)");
    return false;
}

bool StepperOnlineRS::clearError() {
    if (moveState == OperationState::DRIVE_FAILED || moveState == OperationState::DRIVE_TIMEOUT) {
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
    }
    if (initState == OperationState::DRIVE_FAILED || initState == OperationState::DRIVE_TIMEOUT) {
        initState = OperationState::DRIVE_IDLE;
        initPhase = 0;
    }
    if (resetState == OperationState::DRIVE_FAILED || resetState == OperationState::DRIVE_TIMEOUT) {
        resetState = OperationState::DRIVE_IDLE;
        resetPhase = 0;
    }
    lastError = "";
    lastErrorRaw = "";
    return true;
}

// ============================================================================
// ÉTAT
// ============================================================================
int32_t StepperOnlineRS::getActualPosition() const {
    return nodes.position ? fromWire(nodes.position->getValue()) : 0;
}

int32_t StepperOnlineRS::getFeedbackPosition() const {
    return nodes.feedbackPosition ? fromWire(nodes.feedbackPosition->getValue()) : getActualPosition();
}

bool StepperOnlineRS::hasAlarmRaw() const {
    if (!nodesOk) return false;
    return faultBit || alarmCodeValue != 0;
}

bool StepperOnlineRS::faultNow() const {
    return hasAlarmRaw() || driveFaultPending || (initialized && commLost);
}

bool StepperOnlineRS::alarmCodePending() const {
    return faultBit && alarmCodeValue == 0 && faultBitSince != 0 && !fresh(nodes.alarmCode, faultBitSince) &&
           (millis() - faultBitSince) < ALARM_CODE_WAIT_MS;
}

// Signalement public : si le bit de défaut est arrivé avant le code, on attend la relecture du
// code (500 ms au plus) pour que getAlarmDescription() soit juste dès le premier appel.
// Les contrôles internes (mouvement, homing) utilisent faultNow(), sans délai.
bool StepperOnlineRS::getHasAlarms() const {
    if (!nodesOk) return true;
    if (alarmCodePending()) return driveFaultPending || (initialized && commLost);
    return faultNow();
}

bool StepperOnlineRS::getIsReady() const {
    return nodesOk && initialized && !commLost && !hasAlarmRaw() && (enabledBit || !opt.requireEnableBit);
}

bool StepperOnlineRS::getHomeDone() const {
    if (!nodesOk || homingState == OperationState::DRIVE_IN_PROGRESS) return false;
    return homeLatched || homeDoneBit;
}

bool StepperOnlineRS::isAtPosition(int32_t target) const {
    int64_t d = (int64_t)getActualPosition() - (int64_t)target;
    if (d < 0) d = -d;
    return d <= (int64_t)opt.positionTolerance;
}

const char* StepperOnlineRS::getAlarmDescription() const {
    if (!nodesOk) return "Nodes StepperOnline obligatoires manquants";
    if (alarmCodeValue != 0) {
        // Code = champ de bits : libellés connus séparés par « + », reste en hexadécimal
        size_t len = 0;
        alarmDescBuf[0] = '\0';
        uint16_t unknown = 0;
        for (uint8_t b = 0; b < 16; b++) {
            uint16_t bit = (uint16_t)(1u << b);
            if (!(alarmCodeValue & bit)) continue;
            const char* t = lookupAlarmBit(bit);
            if (!t) {
                unknown |= bit;
                continue;
            }
            int n = snprintf(alarmDescBuf + len, sizeof(alarmDescBuf) - len, "%s%s", len ? " + " : "", t);
            if (n < 0 || (size_t)n >= sizeof(alarmDescBuf) - len) return alarmDescBuf;
            len += (size_t)n;
        }
        if (unknown) {
            snprintf(alarmDescBuf + len, sizeof(alarmDescBuf) - len, "%sAlarme 0x%04X (voir le manuel du variateur)",
                     len ? " + " : "", (unsigned)unknown);
        }
        return alarmDescBuf;
    }
    if (hasAlarmRaw()) return "Défaut variateur (code non disponible)";
    if (initialized && commLost) return "Perte de communication Modbus avec le variateur";
    if (driveFaultPending) return driveFaultReason.c_str();
    return "";
}

uint16_t StepperOnlineRS::getSlaveAddress() const {
    return nodes.status ? nodes.status->getModbusAddress() : 0;
}

bool StepperOnlineRS::isPlcSuspended() const {
    BorneUniverselle* borne = BorneUniverselle::getInstance();
    return borne ? borne->getPlcSuspended() : false;
}

void StepperOnlineRS::setError(const char* msg) {
    lastErrorRaw = msg ? msg : "";
    lastError = "[" + servoId + "] " + lastErrorRaw;
    if (msg && msg[0]) logf("Erreur : %s", msg);
}

void StepperOnlineRS::logf(const char* fmt, ...) const {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.printf("[%s] %s\n", servoId.c_str(), buf);
}

void StepperOnlineRS::printStatus() const {
    Serial.printf("===== %s (StepperOnline %s) =====\n", servoId.c_str(), getModelName(model));
    Serial.printf("Initialisé: %d  Prêt: %d  Alarme: %d (0x%04X %s)\n",
                  initialized ? 1 : 0, getIsReady() ? 1 : 0, getHasAlarms() ? 1 : 0,
                  (unsigned)alarmCodeValue, getAlarmDescription());
    Serial.printf("Mot d'état: 0x%04X  Déclenchement: 0x%04X  Com perdue: %d\n",
                  (unsigned)statusWord, (unsigned)triggerStatusValue, commLost ? 1 : 0);
    Serial.printf("Position: %ld  Cible: %ld  En position: %d  En mouvement: %d  Origine: %d\n",
                  (long)getActualPosition(), (long)targetPosition, inPosBit ? 1 : 0,
                  runningBit ? 1 : 0, getHomeDone() ? 1 : 0);
    if (nodes.feedbackPosition) Serial.printf("Position réelle (codeur): %ld\n", (long)getFeedbackPosition());
    Serial.printf("États init/reset/move/homing: %s / %s / %s / %s  Jog: %d\n",
                  getOperationStateText(initState), getOperationStateText(resetState),
                  getOperationStateText(moveState), getOperationStateText(homingState), (int)jogDir);
    Serial.printf("Vitesse: %u tr/min  Jog: %u tr/min  Rampe: %u ms/1000 tr/min\n",
                  (unsigned)moveSpeedRpm, (unsigned)jogSpeedRpm, (unsigned)accelTimeMs);
    if (lastError.length()) Serial.printf("Dernière erreur: %s\n", lastError.c_str());
}

// ============================================================================
// HANDLERS POUR LA CLASSE MÉTIER (mêmes contrats que SureServo / LichuanServo)
// ============================================================================
bool StepperOnlineRS::handleInitializing(OperationStatus& status) {
    if (!driveInitStarted) {
        initFailureReported = false;
        if (!startInitialize()) {
            char msg[128];
            snprintf(msg, sizeof(msg), "[%s] ERREUR: Impossible de démarrer l'initialisation", servoId.c_str());
            BorneUniverselle::prepareMessage(ERROR, msg);
            status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
        driveInitStarted = true;
        status = OperationStatus::IN_PROGRESS;
        return true;
    }

    switch (initState) {
        case OperationState::DRIVE_IDLE:
            driveInitStarted = false;
            initFailureReported = false;
            status = OperationStatus::COMPLETED;
            return true;

        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT: {
            // driveInitStarted reste vrai : remis à false par clearMovementLock() après reset
            if (!initFailureReported) {
                char msg[200];
                snprintf(msg, sizeof(msg), "[%s] Échec init: %s", servoId.c_str(), lastErrorRaw.c_str());
                BorneUniverselle::prepareMessage(ERROR, msg);
                initFailureReported = true;
            }
            status = OperationStatus::SERVO_DRIVE_ERROR;
            return true;
        }

        default:
            status = OperationStatus::IN_PROGRESS;
            return true;
    }
}

bool StepperOnlineRS::handleHoming(OperationStatus& status, uint32_t homingSpeed, uint32_t timeoutMs) {
    (void)homingSpeed;   // vitesses de recherche : paramètres du variateur Pr8.15 / Pr8.16
    if (!driveHomingStarted) {
        if (!startHoming(timeoutMs)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "[%s] ERREUR: Impossible de démarrer le homing (%s)",
                     servoId.c_str(), lastErrorRaw.c_str());
            BorneUniverselle::prepareMessage(ERROR, msg);
            status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
        driveHomingStarted = true;
        status = OperationStatus::IN_PROGRESS;
        return true;
    }

    switch (homingState) {
        case OperationState::DRIVE_IDLE:
            driveHomingStarted = false;
            status = OperationStatus::COMPLETED;
            return true;

        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT: {
            driveHomingStarted = false;
            char msg[200];
            snprintf(msg, sizeof(msg), "[%s] Échec homing: %s", servoId.c_str(), lastErrorRaw.c_str());
            BorneUniverselle::prepareMessage(ERROR, msg);
            status = OperationStatus::SERVO_DRIVE_ERROR;
            return true;
        }

        default:
            status = OperationStatus::IN_PROGRESS;
            return true;
    }
}

bool StepperOnlineRS::handleJogging(OperationStatus& status) {
    if (getHasAlarms()) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[%s] Alarme détectée en JOG (0x%04X)", servoId.c_str(), (unsigned)alarmCodeValue);
        BorneUniverselle::prepareMessage(ERROR, msg);
        status = OperationStatus::SERVO_DRIVE_ERROR;
        return true;
    }
    status = OperationStatus::IN_PROGRESS;
    return true;
}

bool StepperOnlineRS::handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync, uint32_t timeoutMs) {
    // CAS 1 : mouvement en cours
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        status = OperationStatus::IN_PROGRESS;
        return true;
    }

    // CAS 2 : mouvement terminé vers cette cible (synchronisation déjà faite dans processMove)
    if (moveState == OperationState::DRIVE_IDLE && targetPosition == pos) {
        waitForSyncRequested = false;
        status = OperationStatus::COMPLETED;
        return true;
    }

    // CAS 3 : erreur
    if (moveState == OperationState::DRIVE_FAILED || moveState == OperationState::DRIVE_TIMEOUT) {
        char msg[200];
        snprintf(msg, sizeof(msg), "[%s] Mouvement échoué vers %ld: %s", servoId.c_str(), (long)targetPosition,
                 lastErrorRaw.c_str());
        BorneUniverselle::prepareMessage(ERROR, msg);
        waitForSyncRequested = false;
        status = OperationStatus::SERVO_DRIVE_ERROR;
        return true;
    }

    // CAS 4 : nouveau mouvement
    if (!startGoToPosition(pos, waitForSync, timeoutMs)) {
        char msg[200];
        snprintf(msg, sizeof(msg), "[%s] ERREUR: Impossible de démarrer le mouvement vers %ld (%s)",
                 servoId.c_str(), (long)pos, lastErrorRaw.c_str());
        BorneUniverselle::prepareMessage(ERROR, msg);
        status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
        return false;
    }
    status = OperationStatus::IN_PROGRESS;
    return true;
}

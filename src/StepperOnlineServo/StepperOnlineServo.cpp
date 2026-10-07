#include "StepperOnlineServo.h"

#include <stdarg.h>
#include <stdio.h>
#include "BorneUniverselle/borneUniverselle.h"
#include "MyModbus/MyModbus.h"

// Paramètres, adresses et sources : docs/axes/stepperonline-servo.md (section « Vérifié / supposé »).

namespace {

int32_t clampToInt32(int64_t v) {
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

// T6 : Pr8.02 relu. 0x000P = chemin P terminé (prêt pour une nouvelle commande) ;
// 0x01P / 0x020 / 0x040 = commande pas encore prise en compte ; 0x10P = chemin en cours ;
// 0x200 = commande terminée, positionnement en cours (codes du mode PR Leadshine).
constexpr uint16_t PR_BUSY_MASK = 0x03F0;

} // namespace

// ============================================================================
// OPTIONS PAR FAMILLE
// ============================================================================
StepperOnlineOptions StepperOnlineOptions::defaultsFor(StepperOnlineFamily family) {
    StepperOnlineOptions o;
    if (family == StepperOnlineFamily::T6) {
        // Servo ON par la fonction de SI1 (Pr4.00) : 0x83 = SRV-ON normalement fermé (actif
        // sans câblage), 0x03 = SRV-ON normalement ouvert (inactif sans câblage).
        o.servoOnValue  = 0x0083;
        o.servoOffValue = 0x0003;
        // Registre d'état optionnel (0x0B05 supposé) : bit 0 prêt, bit 2 défaut
        o.stReady      = 0;
        o.stAlarm      = 2;
        o.stInPosition = NONE;   // fin de mouvement : Pr8.02 relu (prStatus)
        o.stHomeDone   = NONE;   // origine mémorisée par la classe après la commande 0x20
        o.highWordFirst = true;  // Pr9.01 = position haute, Pr9.02 = position basse
        o.alarmFromCode = true;
        o.maxAccelMs = 32767;
        o.torqueUnitsPerPercent = 1;   // Pr0.13 en % du couple nominal
        o.maxTorqueValue = 300;
    } else {
        // A6-RS : logique des DI (C04.01 / C04.05 / C04.09 / C04.11) : 1 = active sans câblage
        o.inputActiveValue   = 1;
        o.inputInactiveValue = 0;
        o.servoOnValue  = 1;
        o.servoOffValue = 0;
        // U40.05 : bit n = DO(n+1) (fonctions des DO réglées à la mise en service)
        o.stReady      = 0;      // DO1 = 1 servo prêt
        o.stAlarm      = 1;      // DO2 = 4 défaut
        o.stInPosition = 2;      // DO3 = 7 position atteinte
        o.stHomeDone   = 3;      // DO4 = 9 prise d'origine terminée
        o.highWordFirst = false;
        o.alarmFromCode = true;  // sans effet tant que le node alarmCode est absent
        o.maxAccelMs = 65535;
    }
    return o;
}

// ============================================================================
// CONSTRUCTION
// ============================================================================
StepperOnlineServo::StepperOnlineServo(StepperOnlineFamily family_, const StepperOnlineServoNodes& nodes_,
                                       const char* servoId_)
    : ServoDrive(servoId_),
      family(family_),
      opt(StepperOnlineOptions::defaultsFor(family_)),
      nodes(nodes_) {
    construct();
}

StepperOnlineServo::StepperOnlineServo(StepperOnlineFamily family_, const StepperOnlineServoNodes& nodes_,
                                       const StepperOnlineOptions& options_, const char* servoId_)
    : ServoDrive(servoId_),
      family(family_),
      opt(options_),
      nodes(nodes_) {
    construct();
}

void StepperOnlineServo::construct() {
    alarmDescBuf[0] = '\0';
    moveSpeedRpm = opt.defaultMoveSpeedRpm ? opt.defaultMoveSpeedRpm : 1;
    jogSpeedRpm = opt.defaultJogSpeedRpm ? opt.defaultJogSpeedRpm : 1;
    if (moveSpeedRpm > opt.maxSpeedRpm) moveSpeedRpm = opt.maxSpeedRpm;
    if (jogSpeedRpm > opt.maxSpeedRpm) jogSpeedRpm = opt.maxSpeedRpm;

    if (!nodes.validateRequired(family)) {
        String msg = "Nodes obligatoires manquants (";
        msg += getFamilyName(family);
        msg += ") :";
        if (!nodes.targetPosition) msg += " targetPosition";
        if (!nodes.moveSpeed) msg += " moveSpeed";
        if (!nodes.position) msg += " position";
        if (isT6()) {
            if (!nodes.command) msg += " command";
            if (!nodes.prStatus) msg += " prStatus";
        } else {
            if (!nodes.servoOn) msg += " servoOn";
            if (!nodes.startMove) msg += " startMove";
            if (!nodes.status) msg += " status";
        }
        setError(msg.c_str());
        nodesOk = false;
        return;
    }
    nodesOk = true;
    heartbeat = isT6() ? nodes.prStatus : nodes.status;

    initChannel(chServo, nodes.servoOn, opt.servoOnValue, opt.servoOffValue);
    if (!isT6()) {
        initChannel(chStart, nodes.startMove, opt.inputActiveValue, opt.inputInactiveValue);
        initChannel(chHoming, nodes.homingStart, opt.inputActiveValue, opt.inputInactiveValue);
        initChannel(chReset, nodes.alarmReset, opt.inputActiveValue, opt.inputInactiveValue);
    }

    // État sûr au démarrage : servo OFF, aucune commande. Écriture forcée au premier refresh
    // (le variateur peut avoir gardé les valeurs d'un cycle précédent de l'automate).
    uint32_t now = millis();
    setChannel(chServo, false, now, true);
    setChannel(chStart, false, now, true);
    setChannel(chHoming, false, now, true);
    setChannel(chReset, false, now, true);

    logf("StepperOnline %s créé (%u nodes optionnels)", getFamilyName(family), (unsigned)nodes.countOptional());
}

const char* StepperOnlineServo::getFamilyName(StepperOnlineFamily f) {
    return f == StepperOnlineFamily::T6 ? "T6" : "A6-RS";
}

// ============================================================================
// BOUCLE PRINCIPALE
// ============================================================================
void StepperOnlineServo::process() {
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

// Lecture et décodage des registres d'état (aucun accès bus : valeurs du dernier refresh)
void StepperOnlineServo::updateStatus(uint32_t now) {
    (void)now;
    statusWord = nodes.status ? (uint16_t)(nodes.status->getValue() ^ opt.statusInvertMask) : 0;
    alarmCodeValue = nodes.alarmCode ? nodes.alarmCode->getValue() : 0;

    auto bitOf = [this](uint8_t bit, bool whenUnused) -> bool {
        if (!nodes.status || bit >= 16) return whenUnused;
        return (statusWord & (uint16_t)(1u << bit)) != 0;
    };
    readyBit = bitOf(opt.stReady, true);
    alarmBit = bitOf(opt.stAlarm, false);
    inPosBit = bitOf(opt.stInPosition, true);
    homeDoneBit = bitOf(opt.stHomeDone, false);

    if (nodes.prStatus) {
        prStatusValue = nodes.prStatus->getValue();
        prIdle = (prStatusValue & PR_BUSY_MASK) == 0;
    } else {
        prIdle = true;
    }

    // Échantillons de position : vitesse estimée entre deux lectures successives
    uint32_t pr = nodes.position->getLastRefresh();
    if (pr != 0 && pr != posSampleTime) {
        int32_t p = getActualPosition();
        if (posSampleTime != 0) {
            uint32_t dt = pr - posSampleTime;
            if (dt > 0 && dt < 2000) {
                velNum = (int64_t)p - (int64_t)posSampleValue;
                velDen = dt;
            } else {
                velNum = 0;
                velDen = 0;
            }
        }
        posSampleValue = p;
        posSampleTime = pr;
    }

    if (nodes.actualSpeed) {
        int32_t rpm = (int16_t)nodes.actualSpeed->getValue();
        zeroSpeedBit = (rpm <= ZERO_SPEED_RPM && rpm >= -ZERO_SPEED_RPM);
    } else {
        // Sans lecture de vitesse : position immobile entre deux lectures successives
        int64_t moved = velNum < 0 ? -velNum : velNum;
        zeroSpeedBit = velDen == 0 || moved * 1000 <= (int64_t)opt.stillUnitsPerSecond * velDen;
    }

    // Passe de refresh qui a confirmé la dernière écriture du servo ON (relecture)
    if (servoOnConfirmedAt == 0 && chServo.node && fresh(chServo.node, chServo.changeTime)) {
        servoOnConfirmedAt = chServo.node->getLastRefresh();
    }
}

// Surveillance : communication, redémarrage du variateur, front d'alarme
void StepperOnlineServo::supervise(uint32_t now) {
    // ---- Communication ----
    uint16_t addr = getSlaveAddress();
    bool slaveDead = addr != 0 && MyModbus::getInstance().isSlaveConsideredDead(addr);
    uint32_t lastRead = heartbeat->getLastRefresh();
    uint32_t baseInterval = fastPolling ? savedHeartbeatInterval : heartbeat->getRefreshInterval();
    bool stale = lastRead == 0 || (now - lastRead) > COMM_LOSS_MS + 2 * baseInterval;

    if (initialized) {
        if (!commLost && (slaveDead || stale)) {
            commLost = true;
            logf("Perte de communication Modbus (adresse %u)", (unsigned)addr);
            // Redémarrage du variateur détectable seulement par la relecture du servo ON actif
            if (!(nodes.servoOnReadback && chServo.state)) homeLatched = false;
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
        }
    } else {
        commLost = false;
    }

    // ---- Redémarrage du variateur ----
    if (initialized && !commLost) {
        // 1) Relecture du registre servo ON différente de la dernière valeur écrite
        uint16_t expected = chServo.state ? chServo.onValue : chServo.offValue;
        if (nodes.servoOnReadback && chServo.node && servoOnConfirmedAt != 0 &&
            fresh(nodes.servoOnReadback, servoOnConfirmedAt) &&
            nodes.servoOnReadback->getValue() != expected) {
            logf("Relecture du servo ON 0x%04X au lieu de 0x%04X",
                 (unsigned)nodes.servoOnReadback->getValue(), (unsigned)expected);
            homeLatched = false;
            hadHomeDone = false;
            setChannel(chServo, false, now, true);   // pas de remise en service automatique
            notifyDriveFault("Variateur redémarré (servo ON perdu) - refaire l'initialisation");
        }

        // 2) Perte du bit « origine faite » vu stable hors prise d'origine
        if (nodes.status && opt.stHomeDone < 16 && homingState != OperationState::DRIVE_IN_PROGRESS) {
            if (homeDoneBit) {
                if (homeBitHighSince == 0) homeBitHighSince = now ? now : 1;
                if (now - homeBitHighSince >= HOME_BIT_LATCH_MS) hadHomeDone = true;
            } else {
                homeBitHighSince = 0;
                if (hadHomeDone) {
                    hadHomeDone = false;
                    homeLatched = false;
                    notifyDriveFault("Origine perdue (variateur redémarré ?) - refaire la prise d'origine");
                }
            }
        } else {
            homeBitHighSince = 0;
        }
    }

    // ---- Front montant d'alarme ----
    bool has = getHasAlarms();
    if (has && !lastHasAlarms) {
        logf("ALARME 0x%04X : %s", (unsigned)alarmCodeValue, getAlarmDescription());
        if (hasAlarmRaw()) {
            char reason[96];
            snprintf(reason, sizeof(reason), "Alarme variateur : %s", getAlarmDescription());
            failMotion(reason);
            if (onAlarmCallback) onAlarmCallback(alarmCodeValue, getAlarmDescription());
        }
    } else if (!has && lastHasAlarms) {
        logf("Alarme effacée");
    }
    lastHasAlarms = has;
}

bool StepperOnlineServo::motionActive() const {
    return moveState == OperationState::DRIVE_IN_PROGRESS ||
           homingState == OperationState::DRIVE_IN_PROGRESS ||
           jogDir != 0 || stopRetargetActive || settleActive;
}

void StepperOnlineServo::updateFastPolling() {
    if (!opt.fastPollingDuringMotion) return;
    bool want = motionActive();
    if (want && !fastPolling) {
        savedHeartbeatInterval = heartbeat->getRefreshInterval();
        savedPositionInterval = nodes.position->getRefreshInterval();
        heartbeat->setRefreshInterval(0);
        nodes.position->setRefreshInterval(0);
        fastPolling = true;
    } else if (!want && fastPolling) {
        heartbeat->setRefreshInterval((uint16_t)savedHeartbeatInterval);
        nodes.position->setRefreshInterval((uint16_t)savedPositionInterval);
        fastPolling = false;
    }
}

void StepperOnlineServo::updateOutputNodes() {
    if (nodes.servoReady) nodes.servoReady->setValue(getIsReady());
    if (nodes.inPosition) nodes.inPosition->setValue(inPosBit && prIdle);
    if (nodes.zeroSpeed) nodes.zeroSpeed->setValue(zeroSpeedBit);
    if (nodes.homeDone) nodes.homeDone->setValue(getHomeDone());
    if (nodes.servoAlarm) nodes.servoAlarm->setValue(getHasAlarms());
    if (nodes.driveInitialised) nodes.driveInitialised->setValue(initialized);
    if (nodes.modbusError) nodes.modbusError->setValue(commLost);
}

// ============================================================================
// REGISTRES DE COMMANDE
// ============================================================================
void StepperOnlineServo::initChannel(Channel& ch, Uint16OutputNode* node, uint16_t onValue, uint16_t offValue) {
    ch.node = node;
    ch.onValue = onValue;
    ch.offValue = offValue;
    ch.state = false;
    ch.changeTime = 0;
}

// Écriture différée au prochain refresh ; toujours forcée quand l'état change (le registre
// a pu être modifié côté variateur : redémarrage, logiciel de réglage).
void StepperOnlineServo::setChannel(Channel& ch, bool on, uint32_t now, bool force) {
    if (!ch.node) return;
    if (!force && ch.state == on) return;
    ch.state = on;
    ch.changeTime = now;
    ch.node->setValue(on ? ch.onValue : ch.offValue, true);
    if (&ch == &chServo) servoOnConfirmedAt = 0;
}

// Vrai quand le registre est dans l'état demandé ET que l'écriture est passée sur le bus.
// Un registre absent est toujours « confirmé ».
bool StepperOnlineServo::channelOnWire(const Channel& ch, bool state) const {
    if (!ch.node) return true;
    if (ch.state != state) return false;
    return fresh(ch.node, ch.changeTime);
}

void StepperOnlineServo::sendCommand(uint16_t code, uint32_t now) {
    (void)now;
    forceWrite16(nodes.command, code);
}

void StepperOnlineServo::clearCommandChannels(uint32_t now) {
    setChannel(chStart, false, now);
    setChannel(chHoming, false, now);
    setChannel(chReset, false, now);
}

void StepperOnlineServo::forceWrite16(Uint16OutputNode* node, uint16_t value) {
    if (node) node->setValue(value, true);
}

// Uint32OutputNode n'a pas de paramètre « force » : une valeur intermédiaire différente
// garantit l'écriture (seule la valeur finale part sur le bus).
void StepperOnlineServo::forceWrite32(Uint32OutputNode* node, int32_t value) {
    if (!node) return;
    uint32_t v = toWire32(value);
    if (node->getValue() == v) node->setValue(v ^ 1u);
    node->setValue(v);
}

// Les nodes 32 bits transmettent le mot bas à l'adresse de base. T6 : mot haut d'abord,
// on échange donc les deux mots avant l'écriture et après la lecture.
uint32_t StepperOnlineServo::toWire32(int32_t v) const {
    uint32_t u = (uint32_t)v;
    return opt.highWordFirst ? ((u << 16) | (u >> 16)) : u;
}

int32_t StepperOnlineServo::fromWire32(uint32_t raw) const {
    return (int32_t)(opt.highWordFirst ? ((raw << 16) | (raw >> 16)) : raw);
}

// ============================================================================
// SÉQUENCEUR DE CHARGEMENT DE CONSIGNE
// ============================================================================
// Toutes les écritures sont différées au prochain BorneUniverselle::refresh() : le départ
// n'est donné que lorsque cible et vitesse sont confirmées sur le bus.
void StepperOnlineServo::beginLoad(int32_t target, uint16_t rpm, uint32_t now) {
    if (rpm < 1) rpm = 1;
    if (rpm > opt.maxSpeedRpm) rpm = opt.maxSpeedRpm;
    forceWrite16(nodes.moveSpeed, rpm);
    loadAccelWritten = false;
    if (accelTimeMs > 0) {
        if (nodes.accelTime) { forceWrite16(nodes.accelTime, accelTimeMs); loadAccelWritten = true; }
        if (nodes.decelTime) { forceWrite16(nodes.decelTime, accelTimeMs); loadAccelWritten = true; }
    }
    forceWrite32(nodes.targetPosition, target);
    lastLoadSpeedRpm = rpm;
    loadWriteTime = now;
    loadPhaseTime = now;
    loadSetOnWireTime = 0;
    loadState = LoadState::WRITING;
}

StepperOnlineServo::LoadState StepperOnlineServo::serviceLoad(uint32_t now) {
    switch (loadState) {
        case LoadState::WRITING: {
            bool written = fresh(nodes.moveSpeed, loadWriteTime) && fresh(nodes.targetPosition, loadWriteTime);
            if (written && loadAccelWritten) {
                if (nodes.accelTime && !fresh(nodes.accelTime, loadWriteTime)) written = false;
                if (nodes.decelTime && !fresh(nodes.decelTime, loadWriteTime)) written = false;
            }
            if (!written) {
                if (now - loadWriteTime > WRITE_CONFIRM_TIMEOUT_MS) loadState = LoadState::FAILED;
                break;
            }
            loadPhaseTime = now;
            if (isT6()) {
                // T6 : déclenchement du chemin PR0 (écriture unique de Pr8.02)
                sendCommand(opt.cmdStartPath, now);
                loadState = LoadState::PULSE;
                break;
            }
            // A6-RS : front montant garanti, la logique de DI1 doit d'abord être inactive
            if (chStart.state) setChannel(chStart, false, now);
            loadState = LoadState::CLEAR_EDGE;
            [[fallthrough]];
        }
        case LoadState::CLEAR_EDGE:
            if (channelOnWire(chStart, false)) {
                setChannel(chStart, true, now);
                loadPhaseTime = now;
                loadState = LoadState::PULSE;
            } else if (now - loadPhaseTime > WRITE_CONFIRM_TIMEOUT_MS) {
                loadState = LoadState::FAILED;
            }
            break;

        case LoadState::PULSE:
            if (isT6()) {
                if (fresh(nodes.command, loadPhaseTime)) {
                    loadSetOnWireTime = nodes.command->getLastRefresh();
                    loadState = LoadState::DONE;
                } else if (now - loadPhaseTime > WRITE_CONFIRM_TIMEOUT_MS) {
                    loadState = LoadState::FAILED;
                }
                break;
            }
            if (channelOnWire(chStart, true)) {
                // Première passe de refresh qui a transmis le front montant
                if (loadSetOnWireTime == 0) loadSetOnWireTime = chStart.node->getLastRefresh();
                if (now - loadPhaseTime >= START_PULSE_MS) {
                    setChannel(chStart, false, now);   // fin de l'impulsion
                    loadPhaseTime = now;
                    loadState = LoadState::CLEAR_AFTER_PULSE;
                }
            } else if (now - loadPhaseTime > WRITE_CONFIRM_TIMEOUT_MS) {
                loadState = LoadState::FAILED;
            }
            break;

        case LoadState::CLEAR_AFTER_PULSE:
            if (channelOnWire(chStart, false)) {
                loadState = LoadState::DONE;
            } else if (now - loadPhaseTime > WRITE_CONFIRM_TIMEOUT_MS) {
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
bool StepperOnlineServo::startInitialize() {
    if (!nodesOk) {
        setError("Nodes obligatoires manquants pour l'initialisation");
        return false;
    }
    if (initState != OperationState::DRIVE_IDLE) {
        setError("Initialisation déjà en cours ou terminée");
        return false;
    }
    uint32_t now = millis();
    setChannel(chReset, false, now);
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

void StepperOnlineServo::processInitialize(uint32_t now) {
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
            // Communication établie : au moins une lecture de l'état et de la position
            if (heartbeat->getLastRefresh() == 0 || nodes.position->getLastRefresh() == 0) {
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
            clearCommandChannels(now);
            // T6 : mode du chemin PR0 (absolu + interruption), en RAM
            if (nodes.pathMode) forceWrite16(nodes.pathMode, opt.pathModeWord);
            setChannel(chServo, true, now, true);
            initStartTime = now;   // le timeout démarre après les écritures
            initPhaseTime = now;
            initPhase = 1;
            break;

        case 1: {
            bool pathOk = !nodes.pathMode || fresh(nodes.pathMode, initPhaseTime);
            if (pathOk && channelOnWire(chServo, true) && now - initPhaseTime >= SERVO_ON_SETTLE_MS) {
                initPhaseTime = now;
                initPhase = 2;
            }
            break;
        }

        case 2:
            if (fresh(heartbeat, initPhaseTime) && fresh(nodes.position, initPhaseTime) &&
                (!nodes.alarmCode || fresh(nodes.alarmCode, initPhaseTime))) {
                if (!hasAlarmRaw() && readyBit) {
                    initialized = true;
                    initState = OperationState::DRIVE_IDLE;
                    initPhase = 0;
                    logf("Initialisation réussie (position %ld, origine %s)",
                         (long)getActualPosition(), getHomeDone() ? "faite" : "à faire");
                    return;
                }
                if (hasAlarmRaw()) {
                    setError("Alarme variateur pendant l'initialisation");
                    initState = OperationState::DRIVE_FAILED;
                    return;
                }
            }
            if (now - initPhaseTime > 5000 && now - lastInitLog > 2000) {
                logf("Attente servo prêt (état 0x%04X, PR 0x%04X, alarme 0x%04X)",
                     (unsigned)statusWord, (unsigned)prStatusValue, (unsigned)alarmCodeValue);
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
bool StepperOnlineServo::startReset() {
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

void StepperOnlineServo::processReset(uint32_t now) {
    if (now - resetStartTime > RESET_TIMEOUT_MS) {
        setChannel(chReset, false, now);
        setError("Timeout reset après 5 secondes");
        resetState = OperationState::DRIVE_TIMEOUT;
        return;
    }

    switch (resetPhase) {
        case 0: {
            // Le reset acquitte aussi les défauts logiciels (perte de com., redémarrage)
            driveFaultPending = false;
            driveFaultReason = "";
            bool alarmActive = hasAlarmRaw();
            resetServoWasOn = chServo.state;
            cancelLoad();
            stopRetargetActive = false;
            settleActive = false;
            jogDir = 0;
            jogPhase = 0;
            clearCommandChannels(now);
            if (alarmActive && opt.servoOffDuringReset) setChannel(chServo, false, now);
            if (isT6()) {
                if (nodes.alarmReset) forceWrite16(nodes.alarmReset, opt.alarmResetValue);   // 0x1801 <- 0x1111
            } else {
                setChannel(chReset, true, now);
            }
            resetPhaseTime = now;
            resetPhase = 1;
            break;
        }

        case 1: {
            bool sent = isT6() ? (!nodes.alarmReset || fresh(nodes.alarmReset, resetPhaseTime))
                               : channelOnWire(chReset, true);
            if (sent && now - resetPhaseTime >= RESET_PULSE_MS) {
                setChannel(chReset, false, now);
                resetPhaseTime = now;
                resetPhase = 2;
            }
            break;
        }

        case 2:
            if (now - resetPhaseTime >= RESET_STABILIZE_MS) {
                // Ré-écriture forcée : le variateur a pu redémarrer (registre revenu à sa valeur enregistrée)
                setChannel(chServo, resetServoWasOn, now, true);
                if (nodes.pathMode) forceWrite16(nodes.pathMode, opt.pathModeWord);
                resetPhaseTime = now;
                resetSuccessSince = 0;
                resetPhase = 3;
            }
            break;

        case 3: {
            bool freshRead = fresh(heartbeat, resetPhaseTime) &&
                             (!nodes.alarmCode || fresh(nodes.alarmCode, resetPhaseTime));
            bool ok = freshRead && !commLost && !hasAlarmRaw() && !driveFaultPending && readyBit &&
                      channelOnWire(chReset, false) && channelOnWire(chServo, chServo.state);
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
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Reset échoué : prêt=%d, alarme=%d, code=0x%04X%s",
                             readyBit ? 1 : 0, hasAlarmRaw() ? 1 : 0, (unsigned)alarmCodeValue,
                             commLost ? ", communication perdue" : "");
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
bool StepperOnlineServo::startGoToPosition(int32_t positionPuu, bool waitForSync, uint32_t timeout) {
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
        setError("Servo StepperOnline pas prêt pour un mouvement");
        return false;
    }
    if (faultNow()) {
        setError("Servo en alarme - mouvement impossible");
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
    if (loadBusy() || stopRetargetActive || settleActive) {
        setError("Arrêt en cours - mouvement impossible");
        return false;
    }

    uint32_t now = millis();
    // Déjà en position (lecture récente) : succès immédiat si une synchronisation est demandée
    if (waitForSync && zeroSpeedBit && prIdle && isAtPosition(positionPuu) &&
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

void StepperOnlineServo::processMove(uint32_t now) {
    if (now - moveStartTime > currentMoveTimeoutMs) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Timeout mouvement après %lu s (position %ld, cible %ld)",
                 (unsigned long)(currentMoveTimeoutMs / 1000), (long)getActualPosition(), (long)targetPosition);
        failMotion(msg);
        moveState = OperationState::DRIVE_TIMEOUT;
        return;
    }
    if (faultNow()) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Alarme pendant le mouvement (0x%04X)", (unsigned)alarmCodeValue);
        failMotion(msg);
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
                failMotion("Écriture de la consigne non confirmée sur le bus");
            } else if (r == LoadState::DONE) {
                cancelLoad();
                moveCmdOnWireTime = loadSetOnWireTime;
                moveStableCount = 0;
                movePrevPosTime = 0;
                movePhase = 2;
            }
            break;
        }

        case 2: {
            // Fin du mouvement : lectures postérieures au départ, cible atteinte, chemin PR
            // terminé (T6) ou position atteinte (A6-RS), et vitesse nulle.
            if (!fresh(heartbeat, moveCmdOnWireTime) || !fresh(nodes.position, moveCmdOnWireTime)) break;
            if (now - moveCmdOnWireTime < MIN_DONE_MS) break;

            bool done = isAtPosition(targetPosition) && inPosBit && prIdle && zeroSpeedBit;
            if (done && waitForSyncRequested) {
                // Synchronisation demandée : position stable sur plusieurs lectures
                uint32_t pr = nodes.position->getLastRefresh();
                if (pr != movePrevPosTime) {
                    int32_t p = getActualPosition();
                    int64_t d = (int64_t)p - (int64_t)movePrevPos;
                    if (d < 0) d = -d;
                    if (movePrevPosTime != 0 && d <= (int64_t)opt.positionTolerance) {
                        if (moveStableCount < 255) moveStableCount++;
                    } else {
                        moveStableCount = 0;
                    }
                    movePrevPos = p;
                    movePrevPosTime = pr;
                }
                done = moveStableCount >= STABLE_READINGS;
            } else if (!done) {
                moveStableCount = 0;
            }

            if (done) {
                moveState = OperationState::DRIVE_IDLE;
                movePhase = 0;
                logf("Position %ld atteinte (%ld)", (long)targetPosition, (long)getActualPosition());
                return;
            }
            if (now - lastMoveLog > 2000) {
                logf("Déplacement en cours : position %ld / cible %ld (état 0x%04X, PR 0x%04X)",
                     (long)getActualPosition(), (long)targetPosition, (unsigned)statusWord, (unsigned)prStatusValue);
                lastMoveLog = now;
            }
            break;
        }

        default:
            failMotion("Phase de mouvement invalide");
            break;
    }
}

// ============================================================================
// PRISE D'ORIGINE
// ============================================================================
// A6-RS : logique de DI3 (fonction 25) maintenue active jusqu'au bit « origine faite » (DO4).
// T6    : Pr8.02 = 0x20 ; fin = chemin PR terminé, vitesse nulle, position stable.
bool StepperOnlineServo::startHoming(uint32_t timeoutMs) {
    if (!nodesOk) {
        setError("Nodes obligatoires manquants");
        return false;
    }
    if (isPlcSuspended()) {
        setError("Homing refusé - PLC suspendu");
        return false;
    }
    if (!isT6() && !chHoming.node) {
        setError("Commande de prise d'origine non configurée (node homingStart absent)");
        return false;
    }
    if (!getIsReady()) {
        setError("Servo StepperOnline pas prêt pour la prise d'origine");
        return false;
    }
    if (faultNow()) {
        setError("Servo en alarme - prise d'origine impossible");
        return false;
    }
    if (moveState == OperationState::DRIVE_IN_PROGRESS || jogDir != 0 || loadBusy() || stopRetargetActive || settleActive) {
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
    hadHomeDone = false;
    homeBitHighSince = 0;
    if (isT6()) {
        sendCommand(opt.cmdHoming, now);
    } else {
        setChannel(chStart, false, now);
        setChannel(chHoming, true, now);   // maintenu jusqu'à la fin
    }
    homingCmdTime = now;
    homingState = OperationState::DRIVE_IN_PROGRESS;
    homingPhase = 0;
    homingStartTime = now;
    homingSeenHomeLow = false;
    homingSeenMotion = false;
    homingSeenBusy = false;
    homingStableCount = 0;
    homingPrevPosTime = 0;
    lastError = "";
    lastErrorRaw = "";
    logf("Prise d'origine démarrée");
    return true;
}

void StepperOnlineServo::processHoming(uint32_t now) {
    uint32_t elapsed = now - homingStartTime;
    if (elapsed > currentHomingTimeoutMs) {
        failMotion("Timeout prise d'origine");
        homingState = OperationState::DRIVE_TIMEOUT;
        return;
    }
    if (faultNow()) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Alarme pendant la prise d'origine (0x%04X)", (unsigned)alarmCodeValue);
        failMotion(msg);
        return;
    }

    switch (homingPhase) {
        case 0: {
            bool onWire = isT6() ? fresh(nodes.command, homingCmdTime) : channelOnWire(chHoming, true);
            if (onWire) {
                homingOnWireTime = isT6() ? nodes.command->getLastRefresh() : chHoming.node->getLastRefresh();
                homingStartPos = getActualPosition();
                homingPhase = 1;
            } else if (now - homingCmdTime > WRITE_CONFIRM_TIMEOUT_MS) {
                failMotion("Commande de prise d'origine non confirmée sur le bus");
            }
            break;
        }

        case 1: {
            if (!fresh(heartbeat, homingOnWireTime)) break;
            int32_t pos = getActualPosition();
            int64_t moved = (int64_t)pos - (int64_t)homingStartPos;
            if (moved < 0) moved = -moved;
            if (!homeDoneBit) homingSeenHomeLow = true;
            if (!prIdle) homingSeenBusy = true;
            if (!zeroSpeedBit || moved > (int64_t)opt.positionTolerance) homingSeenMotion = true;

            uint32_t sinceStart = now - homingOnWireTime;
            bool useHomeBit = nodes.status != nullptr && opt.stHomeDone < 16;
            bool finished = useHomeBit ? homeDoneBit : prIdle;
            bool seen = useHomeBit ? (homingSeenHomeLow || homingSeenMotion) : (homingSeenBusy || homingSeenMotion);
            bool candidate = finished && zeroSpeedBit && sinceStart >= HOMING_MIN_MS;
            bool complete = false;
            if (candidate && seen) {
                // Position stable sur plusieurs lectures (déplacement final terminé)
                uint32_t pr = nodes.position->getLastRefresh();
                if (pr != homingPrevPosTime && fresh(nodes.position, homingOnWireTime)) {
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
                complete = homingStableCount >= STABLE_READINGS;
            } else if (candidate && sinceStart >= HOMING_NO_MOTION_ACCEPT_MS) {
                // Aucun mouvement ni changement d'état : origine déjà valide, ou mode « position
                // courante = origine » (A6-RS C10.01 = 35 / T6 mode sans déplacement)
                logf("Prise d'origine sans mouvement détecté : origine prise à la position courante");
                complete = true;
            } else {
                homingStableCount = 0;
            }

            if (complete) {
                setChannel(chHoming, false, now);
                homeLatched = true;
                homingState = OperationState::DRIVE_IDLE;
                homingPhase = 0;
                logf("Prise d'origine terminée (position %ld, %lu ms)", (long)pos, (unsigned long)elapsed);
                return;
            }
            if (now - lastHomingLog > 2000) {
                logf("Prise d'origine en cours (%lu s, position %ld, état 0x%04X, PR 0x%04X)",
                     (unsigned long)(elapsed / 1000), (long)pos, (unsigned)statusWord, (unsigned)prStatusValue);
                lastHomingLog = now;
            }
            break;
        }

        default:
            failMotion("Phase de prise d'origine invalide");
            break;
    }
}

// ============================================================================
// JOG
// ============================================================================
// Jog par déplacement (aucune des deux familles n'a d'entrée de jog pilotable connue) :
// consigne absolue vers la butée (ou fenêtre glissante de opt.jogWindow) à la vitesse de
// jog, prolongée avant d'arriver au bout ; arrêt = arrêt du variateur (T6 : Pr8.02 = 0x40,
// A6-RS : re-consigne à la position courante).
bool StepperOnlineServo::jogForward(int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    return jogCommand(1, limitPuu, slowZonePuu, slowSpeed);
}

bool StepperOnlineServo::jogReverse(int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    return jogCommand(-1, limitPuu, slowZonePuu, slowSpeed);
}

bool StepperOnlineServo::jogCommand(int8_t dir, int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    if (!nodesOk) {
        setError("Nodes obligatoires manquants");
        return false;
    }
    if (!initialized) {
        setError("Servo non initialisé - jog impossible");
        return false;
    }
    if (faultNow()) {
        setError("Servo en alarme - jog impossible");
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
    // Arrêt précédent pas terminé (vitesse non nulle, verrou) : on patiente
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

int32_t StepperOnlineServo::computeJogTarget() const {
    int64_t pos = getActualPosition();
    int64_t window = opt.jogWindow > 0 ? (int64_t)opt.jogWindow : 100000;
    int64_t t = jogDir > 0 ? pos + window : pos - window;
    if (jogLimitEnforced) {
        if (jogDir > 0 && t > jogLimit) t = jogLimit;
        if (jogDir < 0 && t < jogLimit) t = jogLimit;
    }
    return clampToInt32(t);
}

void StepperOnlineServo::processJog(uint32_t now) {
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
            // une nouvelle vitesse (zone lente). T6 : le chemin PR0 est interruptible (INS).
            int64_t remaining = (int64_t)jogTarget - (int64_t)getActualPosition();
            if (remaining < 0) remaining = -remaining;
            bool atLimitTarget = jogLimitEnforced && jogTarget == jogLimit;
            bool extend = !atLimitTarget && remaining < (int64_t)(opt.jogWindow / 2);
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
void StepperOnlineServo::stopJogMotion(uint32_t now) {
    if (jogDir == 0) return;
    bool sent = jogPhase >= 1;
    cancelLoad();
    if (sent) {
        beginDriveStop(now, jogTarget);
        beginSettle(now);
    }
    jogDir = 0;
    jogPhase = 0;
}

bool StepperOnlineServo::jogStop() {
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

bool StepperOnlineServo::setJogSpeed(uint32_t speed) {
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
int32_t StepperOnlineServo::estimatedStopPosition(uint32_t now) const {
    int64_t p = getActualPosition();
    if (velDen > 0 && posSampleTime != 0) {
        uint32_t age = now - posSampleTime;
        if (age > EXTRAPOLATION_MAX_MS) age = EXTRAPOLATION_MAX_MS;
        p = (int64_t)posSampleValue + velNum * (int64_t)age / velDen;   // compense l'âge de la lecture
        if (accelTimeMs > 0 && lastLoadSpeedRpm > 0) {
            // Distance de décélération estimée : v * t_dec / 2 (T6 : t_dec = rampe * vitesse / 1000)
            int64_t tdec = isT6() ? (int64_t)accelTimeMs * lastLoadSpeedRpm / 1000 : (int64_t)accelTimeMs;
            p += velNum * tdec / (2 * velDen);
        }
    }
    return clampToInt32(p);
}

// Arrêt physique d'un mouvement chargé :
//  - T6 : Pr8.02 = 0x40 (arrêt du chemin en cours, le servo reste actif) ;
//  - A6-RS : re-consigne à la position courante extrapolée, bornée entre la position lue et
//    la cible du mouvement interrompu, chargée par une nouvelle impulsion de départ.
void StepperOnlineServo::beginDriveStop(uint32_t now, int32_t interruptedTarget) {
    if (isT6()) {
        cancelLoad();
        sendCommand(opt.cmdStop, now);
        stopClearSent = false;
        logf("Arrêt du chemin PR (0x%04X)", (unsigned)opt.cmdStop);
        return;
    }
    int32_t from = getActualPosition();
    int32_t low = from < interruptedTarget ? from : interruptedTarget;
    int32_t high = from < interruptedTarget ? interruptedTarget : from;
    int32_t target = estimatedStopPosition(now);
    if (target < low) target = low;
    if (target > high) target = high;
    uint16_t rpm = lastLoadSpeedRpm ? lastLoadSpeedRpm : moveSpeedRpm;
    beginLoad(target, rpm, now);
    stopRetargetActive = true;
    logf("Arrêt par re-consigne à %ld", (long)target);
}

// Attente de la vitesse nulle après un arrêt (lectures postérieures à l'écriture)
void StepperOnlineServo::beginSettle(uint32_t now) {
    settleActive = true;
    settleStart = now;
    settleWireTime = 0;
}

void StepperOnlineServo::processStop(uint32_t now) {
    // 1. A6-RS : chargement de la re-consigne d'arrêt
    if (stopRetargetActive) {
        LoadState r = serviceLoad(now);
        if (r == LoadState::DONE || r == LoadState::FAILED) {
            if (r == LoadState::FAILED) {
                logf("Re-consigne d'arrêt non confirmée sur le bus");
                if (onWarningCallback) onWarningCallback(true, "Arrêt StepperOnline non confirmé");
            }
            cancelLoad();
            stopRetargetActive = false;
            beginSettle(now);
        }
    }

    // 2. Vitesse nulle constatée (ou délai dépassé)
    if (settleActive && !stopRetargetActive) {
        Node* wire = isT6() ? static_cast<Node*>(nodes.command) : static_cast<Node*>(nodes.startMove);
        if (settleWireTime == 0) {
            if (fresh(wire, settleStart)) settleWireTime = wire->getLastRefresh();
        } else if (fresh(heartbeat, settleWireTime) && fresh(nodes.position, settleWireTime) && zeroSpeedBit) {
            settleActive = false;
        }
        // Délai : 3 s + durée de décélération estimée (rampe connue, sinon 2,5 s / 1000 tr/min)
        uint32_t ramp = accelTimeMs > 0 ? accelTimeMs : 2500;
        uint32_t rpm = lastLoadSpeedRpm > jogSpeedRpm ? lastLoadSpeedRpm : jogSpeedRpm;
        uint32_t limit = STOP_SETTLE_TIMEOUT_MS + ramp * rpm / 1000;
        if (limit > 15000) limit = 15000;
        if (settleActive && now - settleStart > limit) {
            settleActive = false;
            if (fresh(nodes.position, settleStart) && !zeroSpeedBit) {
                // L'ordre d'arrêt n'a pas été exécuté : défaut => la classe métier passe en
                // urgence et coupe le servo (emergencyStop)
                notifyDriveFault("Arrêt non exécuté par le variateur : axe toujours en mouvement");
            } else {
                logf("Arrêt : vitesse nulle non constatée après %lu ms", (unsigned long)limit);
                if (onWarningCallback) onWarningCallback(true, "Arrêt StepperOnline : vitesse nulle non constatée");
            }
        }
    }

    // 3. T6 : arrêt rapide resté verrouillé (Pr8.02 relu = 0x40 une fois l'axe arrêté) :
    //    acquittement 0x1801 = 0x1111 pour que le variateur accepte le prochain déclenchement
    if (isT6() && pendingReleaseImmediateStop && !settleActive && !stopClearSent && settleWireTime != 0 &&
        nodes.alarmReset && !hasAlarmRaw() && fresh(nodes.prStatus, settleWireTime) &&
        prStatusValue == opt.cmdStop) {
        forceWrite16(nodes.alarmReset, opt.alarmResetValue);
        stopClearSent = true;
        stopClearTime = now;
        logf("Arrêt rapide verrouillé (PR 0x%04X) : acquittement 0x%04X", (unsigned)prStatusValue,
             (unsigned)opt.alarmResetValue);
    }
    bool clearPending = stopClearSent && !fresh(nodes.alarmReset, stopClearTime);

    // 4. Fin du verrou : 500 ms minimum et axe arrêté
    if (pendingReleaseImmediateStop && !stopRetargetActive && !settleActive && !clearPending &&
        now - immediateStopTimestamp >= IMMEDIATE_STOP_DURATION_MS) {
        pendingReleaseImmediateStop = false;
        stopClearSent = false;
        movementCancelled = false;
        logf("Arrêt terminé - mouvements autorisés");
    }
}

bool StepperOnlineServo::stopMovement() {
    if (!nodesOk) return false;
    uint32_t now = millis();
    logf("Arrêt demandé");

    bool homingActive = homingState == OperationState::DRIVE_IN_PROGRESS;
    bool moving = moveState == OperationState::DRIVE_IN_PROGRESS || homingActive ||
                  jogDir != 0 || !zeroSpeedBit || !prIdle;
    bool moveLoaded = moveState == OperationState::DRIVE_IN_PROGRESS && movePhase >= 1;
    bool jogLoaded = jogDir != 0 && jogPhase >= 1;
    int32_t interruptedTarget = moveLoaded ? targetPosition : jogTarget;

    // 1. Commandes en cours retirées
    cancelLoad();
    stopRetargetActive = false;
    setChannel(chHoming, false, now);   // A6-RS : départ de prise d'origine relâché
    jogDir = 0;
    jogPhase = 0;

    // 2. Arrêt physique
    if (isT6()) {
        if (moving) beginDriveStop(now, interruptedTarget);   // couvre aussi la prise d'origine
    } else if (moveLoaded || jogLoaded) {
        beginDriveStop(now, interruptedTarget);
    }
    if (moving) beginSettle(now);

    // 3. Annulation des opérations (retour à IDLE, pas FAILED)
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
        targetPosition = 0;
    }
    if (homingActive) {
        homingState = OperationState::DRIVE_IDLE;   // driveHomingStarted conservé (consommé par handleHoming)
        homingPhase = 0;
    }

    // 4. Blocage des nouveaux mouvements : 500 ms minimum et jusqu'à l'arrêt de l'axe
    movementCancelled = true;
    pendingReleaseImmediateStop = true;
    immediateStopTimestamp = now;
    return true;
}

bool StepperOnlineServo::emergencyStop() {
    if (!nodesOk) return false;
    uint32_t now = millis();
    cancelLoad();
    stopRetargetActive = false;
    settleActive = false;
    settleWireTime = 0;
    stopClearSent = false;
    jogDir = 0;
    jogPhase = 0;

    // Arrêt du chemin PR (T6), servo OFF et toutes les commandes inactives, écriture forcée
    if (isT6()) sendCommand(opt.cmdStop, now);
    setChannel(chServo, false, now, true);
    setChannel(chStart, false, now, true);
    setChannel(chHoming, false, now, true);
    setChannel(chReset, false, now, true);

    initState   = OperationState::DRIVE_FAILED;
    resetState  = OperationState::DRIVE_FAILED;
    moveState   = OperationState::DRIVE_FAILED;
    homingState = OperationState::DRIVE_FAILED;
    initialized = false;

    pendingReleaseImmediateStop = true;
    immediateStopTimestamp = now;
    logf("%s", chServo.node ? "Arrêt d'urgence : servo OFF" : "Arrêt d'urgence : arrêt PR (servo ON non pilotable)");
    return true;
}

bool StepperOnlineServo::isImmediateStopActive() const {
    return pendingReleaseImmediateStop || stopRetargetActive || settleActive;
}

void StepperOnlineServo::clearMovementLock() {
    movementCancelled = false;

    if (moveState == OperationState::DRIVE_FAILED || moveState == OperationState::DRIVE_TIMEOUT) {
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
    }

    // Après emergencyStop() : relance d'une vraie initialisation (servo ON)
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
        setChannel(chHoming, false, millis());
    }

    targetPosition = 0;
}

// Interrompt les mouvements en cours (état FAILED) ; T6 : arrêt du chemin PR.
void StepperOnlineServo::failMotion(const char* reason) {
    uint32_t now = millis();
    bool wasMoving = loadBusy() || jogDir != 0 ||
                     moveState == OperationState::DRIVE_IN_PROGRESS ||
                     homingState == OperationState::DRIVE_IN_PROGRESS;
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        moveState = OperationState::DRIVE_FAILED;
        movePhase = 0;
        setError(reason);
    }
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        homingState = OperationState::DRIVE_FAILED;
        homingPhase = 0;
        setError(reason);
    }
    cancelLoad();
    stopRetargetActive = false;
    setChannel(chStart, false, now);
    setChannel(chHoming, false, now);
    jogDir = 0;
    jogPhase = 0;
    if (wasMoving && isT6() && !commLost) sendCommand(opt.cmdStop, now);
}

void StepperOnlineServo::notifyDriveFault(const char* reason) {
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
bool StepperOnlineServo::setServoEnabled(bool enable) {
    if (!nodesOk) return false;
    if (!chServo.node) {
        if (enable) return true;   // T6 sans node servoOn : servo actif par câblage ou Pr4.00 enregistré
        setError("Servo ON non pilotable (node servoOn absent)");
        return false;
    }
    setChannel(chServo, enable, millis());
    logf("Servo %s", enable ? "ON" : "OFF");
    return true;
}

bool StepperOnlineServo::setMoveSpeed(int32_t speed) {
    if (speed < 1) speed = 1;
    if (speed > (int32_t)opt.maxSpeedRpm) speed = opt.maxSpeedRpm;
    moveSpeedRpm = (uint16_t)speed;
    // Transmis dès maintenant au repos ; de toute façon ré-écrit au début de chaque déplacement.
    if (nodesOk && !motionActive() && !loadBusy()) {
        if (nodes.moveSpeed->getValue() != moveSpeedRpm) nodes.moveSpeed->setValue(moveSpeedRpm);
    }
    return true;
}

bool StepperOnlineServo::setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) {
    (void)rampIndex;   // pas de table de rampes : voir setAccelTime()
    return setMoveSpeed(speed);
}

bool StepperOnlineServo::setAccelTime(uint16_t ms) {
    if (!nodes.accelTime && !nodes.decelTime) {
        setError("Temps de rampe non configurable (node accelTime absent)");
        return false;
    }
    if (ms > opt.maxAccelMs) ms = opt.maxAccelMs;
    accelTimeMs = ms;
    return true;
}

bool StepperOnlineServo::setMaxTorque(uint8_t torquePercent) {
    if (!nodes.torqueLimit) {
        setError("Limite de couple non configurée (node torqueLimit absent)");
        return false;
    }
    uint32_t v = (uint32_t)torquePercent * opt.torqueUnitsPerPercent;
    if (v > opt.maxTorqueValue) v = opt.maxTorqueValue;
    nodes.torqueLimit->setValue((uint16_t)v);
    logf("Couple max %u %%", (unsigned)torquePercent);
    return true;
}

bool StepperOnlineServo::saveParameters() {
    // Volontairement non implémenté : la sauvegarde EEPROM (A6-RS C0A.05 = 1, T6 0x1801 =
    // 0x2211) use la mémoire du variateur. Le paramétrage se fait une fois à la mise en service.
    setError("Sauvegarde EEPROM non utilisée (paramétrage unique, voir docs/axes/stepperonline-servo.md)");
    return false;
}

bool StepperOnlineServo::clearError() {
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
int32_t StepperOnlineServo::getActualPosition() const {
    return nodes.position ? fromWire32(nodes.position->getValue()) : 0;
}

bool StepperOnlineServo::hasAlarmRaw() const {
    if (!nodesOk) return false;
    if (alarmBit) return true;
    return opt.alarmFromCode && nodes.alarmCode && alarmCodeValue != 0;
}

bool StepperOnlineServo::faultNow() const {
    return hasAlarmRaw() || driveFaultPending || (initialized && commLost);
}

bool StepperOnlineServo::getHasAlarms() const {
    if (!nodesOk) return true;
    return faultNow();
}

bool StepperOnlineServo::getIsReady() const {
    return nodesOk && initialized && !commLost && readyBit && !hasAlarmRaw() &&
           (!chServo.node || chServo.state);
}

bool StepperOnlineServo::getHomeDone() const {
    if (!nodesOk || homingState == OperationState::DRIVE_IN_PROGRESS) return false;
    return homeDoneBit || homeLatched;
}

bool StepperOnlineServo::isAtPosition(int32_t target) const {
    int64_t d = (int64_t)getActualPosition() - (int64_t)target;
    if (d < 0) d = -d;
    return d <= (int64_t)opt.positionTolerance;
}

const char* StepperOnlineServo::getAlarmDescription() const {
    if (!nodesOk) return "Nodes StepperOnline obligatoires manquants";
    if (nodes.alarmCode && alarmCodeValue != 0) {
        // Codes non traduits : voir le tableau des alarmes du manuel du variateur
        snprintf(alarmDescBuf, sizeof(alarmDescBuf), "Alarme %s 0x%04X (%u)", getFamilyName(family),
                 (unsigned)alarmCodeValue, (unsigned)alarmCodeValue);
        return alarmDescBuf;
    }
    if (hasAlarmRaw()) return "Alarme variateur (code non disponible, voir l'afficheur)";
    if (initialized && commLost) return "Perte de communication Modbus avec le variateur";
    if (driveFaultPending) return driveFaultReason.c_str();
    return "";
}

uint16_t StepperOnlineServo::getSlaveAddress() const {
    if (heartbeat) return heartbeat->getModbusAddress();
    return nodes.position ? nodes.position->getModbusAddress() : 0;
}

bool StepperOnlineServo::isPlcSuspended() const {
    BorneUniverselle* borne = BorneUniverselle::getInstance();
    return borne ? borne->getPlcSuspended() : false;
}

void StepperOnlineServo::setError(const char* msg) {
    lastErrorRaw = msg ? msg : "";
    lastError = "[" + servoId + "] " + lastErrorRaw;
    if (msg && msg[0]) logf("Erreur : %s", msg);
}

void StepperOnlineServo::logf(const char* fmt, ...) const {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.printf("[%s] %s\n", servoId.c_str(), buf);
}

void StepperOnlineServo::printStatus() const {
    Serial.printf("===== %s (StepperOnline %s) =====\n", servoId.c_str(), getFamilyName(family));
    Serial.printf("Initialisé: %d  Prêt: %d  Alarme: %d (0x%04X %s)\n",
                  initialized ? 1 : 0, getIsReady() ? 1 : 0, getHasAlarms() ? 1 : 0,
                  (unsigned)alarmCodeValue, getAlarmDescription());
    Serial.printf("Mot d'état: 0x%04X  PR: 0x%04X  Servo ON: %d  Com perdue: %d\n",
                  (unsigned)statusWord, (unsigned)prStatusValue, chServo.state ? 1 : 0, commLost ? 1 : 0);
    Serial.printf("Position: %ld  Cible: %ld  En position: %d  Vitesse nulle: %d  Origine: %d\n",
                  (long)getActualPosition(), (long)targetPosition, (inPosBit && prIdle) ? 1 : 0,
                  zeroSpeedBit ? 1 : 0, getHomeDone() ? 1 : 0);
    Serial.printf("États init/reset/move/homing: %s / %s / %s / %s  Jog: %d\n",
                  getOperationStateText(initState), getOperationStateText(resetState),
                  getOperationStateText(moveState), getOperationStateText(homingState), (int)jogDir);
    Serial.printf("Vitesse: %u tr/min  Jog: %u tr/min  Rampe: %u ms\n",
                  (unsigned)moveSpeedRpm, (unsigned)jogSpeedRpm, (unsigned)accelTimeMs);
    if (lastError.length()) Serial.printf("Dernière erreur: %s\n", lastError.c_str());
}

// ============================================================================
// HANDLERS POUR LA CLASSE MÉTIER (mêmes contrats que SureServo / LichuanServo)
// ============================================================================
bool StepperOnlineServo::handleInitializing(OperationStatus& status) {
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

bool StepperOnlineServo::handleHoming(OperationStatus& status, uint32_t homingSpeed, uint32_t timeoutMs) {
    (void)homingSpeed;   // vitesses de recherche : paramètres du variateur (A6-RS C10, T6 Pr8.15/Pr8.16)
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

bool StepperOnlineServo::handleJogging(OperationStatus& status) {
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

bool StepperOnlineServo::handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync, uint32_t timeoutMs) {
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

#include "LichuanServo.h"

#include <stdarg.h>
#include <stdio.h>
#include "BorneUniverselle/borneUniverselle.h"
#include "MyModbus/MyModbus.h"

// ============================================================================
// TABLES D'ALARMES
// ============================================================================
namespace {

struct LichuanAlarmText {
    uint16_t code;
    const char* text;
};

// A6 : PA_1C9 contient le numéro d'alarme affiché (décimal), cf. manuel A6 § PA_121 et
// chapitre 8 « Alarm Description ».
const LichuanAlarmText A6_ALARMS[] = {
    {1,  "Erreur système"},
    {2,  "Erreur de configuration DI (fonction affectée deux fois)"},
    {3,  "Erreur de communication Modbus"},
    {4,  "Alimentation de contrôle coupée"},
    {5,  "Erreur interne FPGA"},
    {6,  "Timeout de prise d'origine"},
    {12, "Surtension"},
    {13, "Sous-tension"},
    {14, "Surintensité ou défaut de terre"},
    {15, "Surchauffe"},
    {16, "Surcharge"},
    {18, "Surcharge de la résistance de freinage"},
    {21, "Erreur codeur"},
    {24, "Écart de position excessif"},
    {26, "Survitesse"},
    {27, "Erreur de rapport électronique (division des impulsions)"},
    {29, "Débordement du compteur d'écart"},
    {36, "Erreur des paramètres EEPROM"},
    {38, "Erreur des fins de course"},
    {39, "Surtension de la consigne analogique"},
    {40, "Défaut d'alimentation du codeur absolu (batterie)"},
};

// A5 : P0B-34 contient le code « Er.xyz » (chiffres hexadécimaux), comparé sur 12 bits.
// Source : liste des défauts LCDA630P (Servo_Tool, servo_faults.json).
const LichuanAlarmText A5_ALARMS[] = {
    {0x101, "Paramètres P02 et suivants anormaux"},
    {0x102, "Défaut de configuration de la logique programmable"},
    {0x104, "Défaut d'interruption de la logique programmable"},
    {0x105, "Programme interne anormal"},
    {0x108, "Échec de mémorisation des paramètres"},
    {0x110, "Réglage de la sortie impulsions divisée en défaut"},
    {0x111, "Défaut interne"},
    {0x120, "Moteur et variateur incompatibles"},
    {0x121, "Commande Servo ON invalide (configuration DI/VDI)"},
    {0x122, "Incompatibilité du moteur en mode absolu"},
    {0x130, "Fonction DI affectée deux fois (DI/VDI)"},
    {0x131, "Affectation de fonction DO hors limites"},
    {0x136, "Données ROM du moteur incorrectes"},
    {0x201, "Surintensité 2"},
    {0x207, "Débordement du courant D/Q"},
    {0x208, "Timeout d'échantillonnage système"},
    {0x210, "Court-circuit de sortie à la terre"},
    {0x220, "Erreur d'ordre des phases"},
    {0x234, "Survitesse"},
    {0x400, "Surtension du circuit principal"},
    {0x410, "Sous-tension du circuit principal"},
    {0x420, "Perte de phase de l'alimentation"},
    {0x430, "Sous-tension de l'alimentation de contrôle"},
    {0x500, "Alarme de survitesse"},
    {0x510, "Survitesse de la sortie impulsions"},
    {0x601, "Échec de la prise d'origine"},
    {0x602, "Échec de l'identification d'angle"},
    {0x610, "Surcharge du variateur"},
    {0x620, "Surcharge du moteur"},
    {0x625, "Frein : fermeture anormale"},
    {0x626, "Frein : ouverture anormale"},
    {0x630, "Moteur bloqué"},
    {0x650, "Surchauffe du dissipateur"},
    {0x730, "Avertissement batterie du codeur"},
    {0x731, "Défaut batterie du codeur"},
    {0x733, "Erreur de comptage multitour du codeur"},
    {0x735, "Débordement du comptage multitour du codeur"},
    {0x740, "Interférences codeur"},
    {0x831, "Dérive du zéro AI trop importante"},
    {0x834, "Surtension d'échantillonnage AD"},
    {0x835, "Défaut d'échantillonnage AD"},
    {0x900, "Arrêt d'urgence par DI (avertissement)"},
    {0x909, "Avertissement de surcharge moteur"},
    {0x920, "Surcharge de la résistance de freinage"},
    {0x922, "Résistance de freinage externe trop faible"},
    {0x939, "Câble de puissance moteur débranché"},
    {0x941, "Paramètres modifiés : redémarrage nécessaire"},
    {0x942, "Écritures EEPROM trop fréquentes (P0C-13 doit valoir 0)"},
    {0x950, "Fin de course avant (avertissement)"},
    {0x952, "Fin de course arrière (avertissement)"},
    {0x980, "Défaut interne du codeur"},
    {0xA33, "Données codeur anormales"},
    {0xA34, "Vérification de retour codeur anormale"},
    {0xA35, "Perte du signal Z"},
    {0xA40, "Défaut interne"},
    {0xB00, "Écart de position excessif"},
    {0xB01, "Entrée impulsions anormale"},
    {0xB03, "Rapport électronique hors limites"},
};

template <size_t N>
const char* lookupAlarm(const LichuanAlarmText (&table)[N], uint16_t code) {
    for (size_t i = 0; i < N; i++) {
        if (table[i].code == code) return table[i].text;
    }
    return nullptr;
}

int32_t clampToInt32(int64_t v) {
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

} // namespace

// ============================================================================
// OPTIONS PAR FAMILLE
// ============================================================================
LichuanOptions LichuanOptions::defaultsFor(LichuanFamily family) {
    LichuanOptions o;
    if (family == LichuanFamily::A5) {
        // P31-00 : bit n = VDI(n+1) ; fonctions affectées par P17-xx (voir docs/axes/lichuan.md)
        o.viServoOn     = 0;   // VDI1 = FunIN.1  S-ON
        o.viStartMove   = 1;   // VDI2 = FunIN.28 PosInSen (départ multi-segment)
        o.viHomingStart = 2;   // VDI3 = FunIN.32 HomingStart
        o.viAlarmReset  = 3;   // VDI4 = FunIN.2  ALM-RST
        o.viStop        = 4;   // VDI5 = FunIN.34 arrêt (vitesse nulle puis maintien)
        o.viJogPlus     = 5;   // VDI6 = FunIN.18 JOGCMD+
        o.viJogMinus    = 6;   // VDI7 = FunIN.19 JOGCMD-
        // P17-32 : bit n = VDO(n+1)
        o.stReady      = 0;    // VDO1 = FunOUT.1  S-RDY
        o.stAlarm      = 1;    // VDO2 = FunOUT.11 ALM
        o.stInPosition = 2;    // VDO3 = FunOUT.5  COIN
        o.stZeroSpeed  = 4;    // VDO5 = FunOUT.3  ZERO
        o.stHomeDone   = 5;    // VDO6 = FunOUT.16 HomeAttain
        o.holdStartBit = true;
        o.alarmFromCode = false;
        o.maxAccelMs = 65535;
    } else {
        // PA_1A4 : bit n = DIn ; fonctions PA_080..PA_087 (voir docs/axes/lichuan.md)
        o.viServoOn     = 0;   // DI0 = 0  servo ON
        o.viAlarmReset  = 1;   // DI1 = 1  acquittement alarme
        o.viStartMove   = 5;   // DI5 = 20 chargement de position (front montant, PA_096 = 2)
        o.viHomingStart = 7;   // DI7 = 16 départ retour à zéro
        o.viStop        = NONE;
        o.viJogPlus     = NONE;
        o.viJogMinus    = NONE;
        // PA_1D3 : bit n = DOn ; fonctions PA_088..PA_08D
        o.stReady      = 0;    // DO0 = 0  S-RDY
        o.stAlarm      = 1;    // DO1 = 1  ALM
        o.stInPosition = 2;    // DO2 = 2  COIN
        o.stZeroSpeed  = 4;    // DO4 = 4  ZSP
        o.stHomeDone   = 5;    // DO5 = 11 ORG_FOUND (au lieu de 5 TLC)
        o.holdStartBit = false;
        o.alarmFromCode = true;
        o.maxAccelMs = 2500;
    }
    return o;
}

// ============================================================================
// CONSTRUCTION
// ============================================================================
LichuanServo::LichuanServo(LichuanFamily family_, const LichuanNodes& nodes_, const char* servoId_)
    : ServoDrive(servoId_),
      family(family_),
      opt(LichuanOptions::defaultsFor(family_)),
      nodes(nodes_) {
    construct();
}

LichuanServo::LichuanServo(LichuanFamily family_, const LichuanNodes& nodes_, const LichuanOptions& options_,
                           const char* servoId_)
    : ServoDrive(servoId_),
      family(family_),
      opt(options_),
      nodes(nodes_) {
    construct();
}

void LichuanServo::construct() {
    alarmDescBuf[0] = '\0';
    moveSpeedRpm = opt.defaultMoveSpeedRpm ? opt.defaultMoveSpeedRpm : 1;
    jogSpeedRpm = opt.defaultJogSpeedRpm ? opt.defaultJogSpeedRpm : 1;
    if (moveSpeedRpm > opt.maxSpeedRpm) moveSpeedRpm = opt.maxSpeedRpm;
    if (jogSpeedRpm > opt.maxSpeedRpm) jogSpeedRpm = opt.maxSpeedRpm;

    if (!nodes.validateRequired()) {
        String msg = "Nodes obligatoires manquants :";
        if (!nodes.virtualInputs) msg += " virtualInputs";
        if (!nodes.targetPosition) msg += " targetPosition";
        if (!nodes.moveSpeed) msg += " moveSpeed";
        if (!nodes.position) msg += " position";
        if (!nodes.status) msg += " status";
        if (!nodes.alarmCode) msg += " alarmCode";
        setError(msg.c_str());
        nodesOk = false;
        return;
    }
    nodesOk = true;

    // État sûr au démarrage : servo OFF, aucune commande. Écriture forcée au premier
    // refresh (le variateur peut avoir gardé le mot d'un cycle précédent de l'automate).
    viWord = 0;
    viChangeTime = millis();
    viConfirmed = false;
    nodes.virtualInputs->setValue(0, true);

    logf("Lichuan %s créé (%u nodes optionnels)", getFamilyName(family), (unsigned)nodes.countOptional());
}

const char* LichuanServo::getFamilyName(LichuanFamily f) {
    return f == LichuanFamily::A5 ? "A5" : "A6";
}

// ============================================================================
// BOUCLE PRINCIPALE
// ============================================================================
void LichuanServo::process() {
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

    keepAlive(now);
    updateFastPolling();
    updateOutputNodes();
}

// Lecture et décodage des registres d'état (aucun accès bus : valeurs du dernier refresh)
void LichuanServo::updateStatus(uint32_t now) {
    statusWord = nodes.status->getValue() ^ opt.statusInvertMask;
    alarmCodeValue = nodes.alarmCode->getValue();

    auto bitOf = [this](uint8_t bit, bool whenUnused) -> bool {
        if (bit >= 16) return whenUnused;
        return (statusWord & (uint16_t)(1u << bit)) != 0;
    };
    readyBit = bitOf(opt.stReady, true);
    alarmBit = bitOf(opt.stAlarm, false);
    inPosBit = bitOf(opt.stInPosition, true);
    homeDoneBit = bitOf(opt.stHomeDone, false);

    // Front montant du bit d'alarme : lecture immédiate du code (libellé correct dès le signalement)
    if (alarmBit && !prevAlarmBit) {
        alarmBitSince = now ? now : 1;
        if (!alarmCodeFast) {
            savedAlarmInterval = nodes.alarmCode->getRefreshInterval();
            nodes.alarmCode->setRefreshInterval(0);
            alarmCodeFast = true;
        }
    }
    if (alarmCodeFast && (!alarmBit || fresh(nodes.alarmCode, alarmBitSince))) {
        nodes.alarmCode->setRefreshInterval((uint16_t)savedAlarmInterval);
        alarmCodeFast = false;
    }
    prevAlarmBit = alarmBit;
    if (opt.stZeroSpeed < 16) {
        zeroSpeedBit = bitOf(opt.stZeroSpeed, true);
    } else if (nodes.actualSpeed) {
        int32_t rpm = (int16_t)nodes.actualSpeed->getValue();
        zeroSpeedBit = (rpm <= ZERO_SPEED_RPM && rpm >= -ZERO_SPEED_RPM);
    } else {
        zeroSpeedBit = true;
    }

    // Échantillons de position : vitesse estimée entre deux lectures successives
    uint32_t pr = nodes.position->getLastRefresh();
    if (pr != 0 && pr != posSampleTime) {
        int32_t p = nodes.position->getValueAsInt32();
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

    // Confirmation sur le bus du dernier changement du mot d'entrées virtuelles
    if (!viConfirmed && fresh(nodes.virtualInputs, viChangeTime)) {
        viConfirmed = true;
        viConfirmedAt = nodes.virtualInputs->getLastRefresh();
    }
}

// Surveillance : communication, redémarrage du variateur, front d'alarme
void LichuanServo::supervise(uint32_t now) {
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
            if (!nodes.virtualInputsReadback) homeLatched = false;  // redémarrage du variateur non détectable
            failMotion("Perte de communication Modbus avec le variateur");
            if (onAlarmCallback) onAlarmCallback(0, "Perte de communication Modbus avec le variateur");
        }
        if (commLost && (now - lastCommMessage) > COMM_MESSAGE_INTERVAL_MS) {
            char msg[128];
            snprintf(msg, sizeof(msg), "[%s] Perte de communication avec le variateur Lichuan - vérifiez le bus RS485",
                     servoId.c_str());
            BorneUniverselle::prepareMessage(ERROR, msg);
            lastCommMessage = now;
        }
        if (commLost && !slaveDead && !stale) {
            commLost = false;
            logf("Communication rétablie");
            // Le variateur a pu redémarrer : on ré-impose le mot d'entrées virtuelles
            nodes.virtualInputs->setValue(viWord, true);
            viChangeTime = now;
            viConfirmed = false;
        }
    } else {
        commLost = false;
    }

    // ---- Redémarrage du variateur ----
    if (initialized && !commLost) {
        // 1) Relecture du mot d'entrées virtuelles différente de ce qui a été écrit
        if (nodes.virtualInputsReadback && viConfirmed &&
            fresh(nodes.virtualInputsReadback, viConfirmedAt) &&
            nodes.virtualInputsReadback->getValue() != viWord) {
            logf("Relecture des entrées virtuelles 0x%04X au lieu de 0x%04X",
                 (unsigned)nodes.virtualInputsReadback->getValue(), (unsigned)viWord);
            homeLatched = false;
            hadHomeDone = false;
            notifyDriveFault("Variateur redémarré (entrées virtuelles perdues) - refaire l'initialisation");
            nodes.virtualInputs->setValue(viWord, true);
            viChangeTime = now;
            viConfirmed = false;
        }

        // 2) Perte du bit « origine faite » vu stable hors prise d'origine
        if (homingState != OperationState::DRIVE_IN_PROGRESS) {
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
        if (hasAlarmRaw() && onAlarmCallback) onAlarmCallback(alarmCodeValue, getAlarmDescription());
    } else if (!has && lastHasAlarms) {
        logf("Alarme effacée");
    }
    lastHasAlarms = has;
}

// Ré-écriture périodique du mot d'entrées virtuelles au repos (variateur redémarré)
void LichuanServo::keepAlive(uint32_t now) {
    if (opt.keepAliveMs == 0 || !initialized || commLost) return;
    if (motionActive() || loadBusy() || pendingReleaseImmediateStop || stopResetPulse) return;
    if (initState == OperationState::DRIVE_IN_PROGRESS || resetState == OperationState::DRIVE_IN_PROGRESS) return;
    if (now - viKeepaliveTime < opt.keepAliveMs) return;
    viKeepaliveTime = now;
    nodes.virtualInputs->setValue(viWord, true);
    viChangeTime = now;
    viConfirmed = false;
}

bool LichuanServo::motionActive() const {
    return moveState == OperationState::DRIVE_IN_PROGRESS ||
           homingState == OperationState::DRIVE_IN_PROGRESS ||
           jogDir != 0 || stopRetargetActive || settleActive;
}

void LichuanServo::updateFastPolling() {
    if (!opt.fastPollingDuringMotion) return;
    bool want = motionActive();
    if (want && !fastPolling) {
        savedStatusInterval = nodes.status->getRefreshInterval();
        savedPositionInterval = nodes.position->getRefreshInterval();
        nodes.status->setRefreshInterval(0);
        nodes.position->setRefreshInterval(0);
        fastPolling = true;
    } else if (!want && fastPolling) {
        nodes.status->setRefreshInterval((uint16_t)savedStatusInterval);
        nodes.position->setRefreshInterval((uint16_t)savedPositionInterval);
        fastPolling = false;
    }
}

void LichuanServo::updateOutputNodes() {
    if (nodes.servoReady) nodes.servoReady->setValue(getIsReady());
    if (nodes.inPosition) nodes.inPosition->setValue(inPosBit);
    if (nodes.zeroSpeed) nodes.zeroSpeed->setValue(zeroSpeedBit);
    if (nodes.homeDone) nodes.homeDone->setValue(getHomeDone());
    if (nodes.servoAlarm) nodes.servoAlarm->setValue(getHasAlarms());
    if (nodes.driveInitialised) nodes.driveInitialised->setValue(initialized);
    if (nodes.modbusError) nodes.modbusError->setValue(commLost);
}

// ============================================================================
// ENTRÉES VIRTUELLES
// ============================================================================
void LichuanServo::setVi(uint8_t bit, bool on, uint32_t now) {
    uint16_t mask = bitMask(bit);
    if (!mask) return;
    uint16_t nv = on ? (uint16_t)(viWord | mask) : (uint16_t)(viWord & ~mask);
    if (nv == viWord) return;
    viWord = nv;
    viBitTime[bit] = now;
    viChangeTime = now;
    viConfirmed = false;
    nodes.virtualInputs->setValue(viWord);  // transmis au prochain refresh
}

// Vrai quand le bit est dans l'état demandé ET que l'écriture correspondante est passée sur le bus
bool LichuanServo::viBitOnWire(uint8_t bit, bool state) const {
    uint16_t mask = bitMask(bit);
    if (!mask) return true;
    if (((viWord & mask) != 0) != state) return false;
    return fresh(nodes.virtualInputs, viBitTime[bit]);
}

void LichuanServo::clearCommandBits(uint32_t now) {
    setVi(opt.viStartMove, false, now);
    setVi(opt.viHomingStart, false, now);
    setVi(opt.viJogPlus, false, now);
    setVi(opt.viJogMinus, false, now);
    setVi(opt.viStop, false, now);
    setVi(opt.viAlarmReset, false, now);
}

void LichuanServo::forceWrite16(Uint16OutputNode* node, uint16_t value) {
    if (node) node->setValue(value, true);
}

// Uint32OutputNode n'a pas de paramètre « force » : une valeur intermédiaire différente
// garantit l'écriture (seule la valeur finale part sur le bus).
void LichuanServo::forceWrite32(Uint32OutputNode* node, int32_t value) {
    if (!node) return;
    uint32_t v = (uint32_t)value;
    if (node->getValue() == v) node->setValue(v ^ 1u);
    node->setValue(v);
}

// ============================================================================
// SÉQUENCEUR DE CHARGEMENT DE CONSIGNE
// ============================================================================
// Toutes les écritures sont différées au prochain BorneUniverselle::refresh(), dans
// l'ordre des nodes (pas dans l'ordre des setValue) : le bit de départ n'est levé que
// lorsque cible et vitesse sont confirmées sur le bus (getLastRefresh() postérieur).
void LichuanServo::beginLoad(int32_t target, uint16_t rpm, uint32_t now) {
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
    loadState = LoadState::WRITING;
}

LichuanServo::LoadState LichuanServo::serviceLoad(uint32_t now) {
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
            // Front montant garanti : le bit de départ doit être à 0 côté variateur
            if (viBit(opt.viStartMove)) setVi(opt.viStartMove, false, now);
            loadPhaseTime = now;
            loadState = LoadState::CLEAR_EDGE;
            [[fallthrough]];
        }
        case LoadState::CLEAR_EDGE:
            if (viBitOnWire(opt.viStartMove, false)) {
                setVi(opt.viStartMove, true, now);
                loadPhaseTime = now;
                loadSetOnWireTime = 0;
                loadState = LoadState::PULSE;
            } else if (now - loadPhaseTime > WRITE_CONFIRM_TIMEOUT_MS) {
                loadState = LoadState::FAILED;
            }
            break;

        case LoadState::PULSE:
            if (viBitOnWire(opt.viStartMove, true)) {
                // Première passe de refresh qui a transmis le front montant
                if (loadSetOnWireTime == 0) loadSetOnWireTime = nodes.virtualInputs->getLastRefresh();
                if (opt.holdStartBit) {
                    loadState = LoadState::DONE;          // maintenu jusqu'à la fin du déplacement
                } else if (now - loadPhaseTime >= START_PULSE_MS) {
                    setVi(opt.viStartMove, false, now);   // fin de l'impulsion
                    loadPhaseTime = now;
                    loadState = LoadState::CLEAR_AFTER_PULSE;
                }
            } else if (now - loadPhaseTime > WRITE_CONFIRM_TIMEOUT_MS) {
                loadState = LoadState::FAILED;
            }
            break;

        case LoadState::CLEAR_AFTER_PULSE:
            if (viBitOnWire(opt.viStartMove, false)) {
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
bool LichuanServo::startInitialize() {
    if (!nodesOk) {
        setError("Nodes obligatoires manquants pour l'initialisation");
        return false;
    }
    if (initState != OperationState::DRIVE_IDLE) {
        setError("Initialisation déjà en cours ou terminée");
        return false;
    }
    uint32_t now = millis();
    setVi(opt.viAlarmReset, false, now);
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

void LichuanServo::processInitialize(uint32_t now) {
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
            if (nodes.status->getLastRefresh() == 0 || nodes.position->getLastRefresh() == 0) {
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
            clearCommandBits(now);
            setVi(opt.viServoOn, true, now);
            initStartTime = now;   // le timeout démarre après les écritures
            initPhaseTime = now;
            initPhase = 1;
            break;

        case 1:
            if (viBitOnWire(opt.viServoOn, true) && now - initPhaseTime >= SERVO_ON_SETTLE_MS) {
                initPhaseTime = now;
                initPhase = 2;
            }
            break;

        case 2:
            if (fresh(nodes.status, initPhaseTime) && fresh(nodes.position, initPhaseTime)) {
                if (!hasAlarmRaw() && readyBit) {
                    initialized = true;
                    initState = OperationState::DRIVE_IDLE;
                    initPhase = 0;
                    viKeepaliveTime = now;
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
                logf("Attente servo prêt (état 0x%04X, alarme 0x%04X)", (unsigned)statusWord, (unsigned)alarmCodeValue);
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
bool LichuanServo::startReset() {
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

void LichuanServo::processReset(uint32_t now) {
    if (now - resetStartTime > RESET_TIMEOUT_MS) {
        setVi(opt.viAlarmReset, false, now);
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
            resetServoWasOn = viBit(opt.viServoOn);
            cancelLoad();
            stopRetargetActive = false;
            settleActive = false;
            stopResetPulse = false;
            jogDir = 0;
            jogPhase = 0;
            clearCommandBits(now);
            // A5 : les défauts n°1 et n°2 ne s'acquittent que servo OFF
            if (alarmActive && opt.servoOffDuringReset) setVi(opt.viServoOn, false, now);
            setVi(opt.viAlarmReset, true, now);
            if (nodes.faultReset) nodes.faultReset->setValue(1, true);
            resetPhaseTime = now;
            resetPhase = 1;
            break;
        }

        case 1:
            if (viBitOnWire(opt.viAlarmReset, true) && now - resetPhaseTime >= RESET_PULSE_MS) {
                setVi(opt.viAlarmReset, false, now);
                resetPhaseTime = now;
                resetPhase = 2;
            }
            break;

        case 2:
            if (now - resetPhaseTime >= RESET_STABILIZE_MS) {
                if (resetServoWasOn) setVi(opt.viServoOn, true, now);
                // Ré-écriture forcée : le variateur a pu redémarrer (mot remis à 0 de son côté)
                nodes.virtualInputs->setValue(viWord, true);
                viChangeTime = now;
                viConfirmed = false;
                resetPhaseTime = now;
                resetSuccessSince = 0;
                resetPhase = 3;
            }
            break;

        case 3: {
            bool freshRead = fresh(nodes.status, resetPhaseTime) &&
                             (!opt.alarmFromCode || fresh(nodes.alarmCode, resetPhaseTime));
            bool ok = freshRead && !commLost && !hasAlarmRaw() && !driveFaultPending && readyBit &&
                      viBitOnWire(opt.viAlarmReset, false);
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
bool LichuanServo::startGoToPosition(int32_t positionPuu, bool waitForSync, uint32_t timeout) {
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
        setError("Servo Lichuan pas prêt pour un mouvement");
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
    if (waitForSync && zeroSpeedBit && isAtPosition(positionPuu) &&
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

void LichuanServo::processMove(uint32_t now) {
    if (now - moveStartTime > currentMoveTimeoutMs) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Timeout mouvement après %lu s (position %ld, cible %ld)",
                 (unsigned long)(currentMoveTimeoutMs / 1000), (long)getActualPosition(), (long)targetPosition);
        setError(msg);
        cancelLoad();
        if (opt.holdStartBit) setVi(opt.viStartMove, false, now);
        moveState = OperationState::DRIVE_TIMEOUT;
        movePhase = 0;
        return;
    }
    if (faultNow()) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Alarme pendant le mouvement (0x%04X)", (unsigned)alarmCodeValue);
        setError(msg);
        cancelLoad();
        setVi(opt.viStartMove, false, now);
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
                setVi(opt.viStartMove, false, now);
                setError("Écriture de la consigne non confirmée sur le bus");
                moveState = OperationState::DRIVE_FAILED;
                movePhase = 0;
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
            // Fin du mouvement : lectures postérieures au départ, cible atteinte,
            // positionnement terminé et vitesse nulle.
            if (!fresh(nodes.status, moveCmdOnWireTime) || !fresh(nodes.position, moveCmdOnWireTime)) break;
            if (now - moveCmdOnWireTime < MIN_DONE_MS) break;

            bool done = isAtPosition(targetPosition) && inPosBit && zeroSpeedBit;
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
                if (opt.holdStartBit) setVi(opt.viStartMove, false, now);
                moveState = OperationState::DRIVE_IDLE;
                movePhase = 0;
                logf("Position %ld atteinte (%ld)", (long)targetPosition, (long)getActualPosition());
                return;
            }
            if (now - lastMoveLog > 2000) {
                logf("Déplacement en cours : position %ld / cible %ld (état 0x%04X)",
                     (long)getActualPosition(), (long)targetPosition, (unsigned)statusWord);
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
bool LichuanServo::startHoming(uint32_t timeoutMs) {
    if (!nodesOk) {
        setError("Nodes obligatoires manquants");
        return false;
    }
    if (isPlcSuspended()) {
        setError("Homing refusé - PLC suspendu");
        return false;
    }
    if (opt.viHomingStart >= 16) {
        setError("Bit de départ de prise d'origine non configuré");
        return false;
    }
    if (!getIsReady()) {
        setError("Servo Lichuan pas prêt pour la prise d'origine");
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
    setVi(opt.viStartMove, false, now);
    setVi(opt.viHomingStart, true, now);   // maintenu jusqu'à la fin (A6 : niveau, PA_0A0 = 0)
    homingState = OperationState::DRIVE_IN_PROGRESS;
    homingPhase = 0;
    homingStartTime = now;
    homingSeenHomeLow = false;
    homingSeenMotion = false;
    homingStableCount = 0;
    homingPrevPosTime = 0;
    lastError = "";
    lastErrorRaw = "";
    logf("Prise d'origine démarrée");
    return true;
}

void LichuanServo::processHoming(uint32_t now) {
    uint32_t elapsed = now - homingStartTime;
    if (elapsed > currentHomingTimeoutMs) {
        setVi(opt.viHomingStart, false, now);
        setError("Timeout prise d'origine");
        homingState = OperationState::DRIVE_TIMEOUT;
        homingPhase = 0;
        return;
    }
    if (faultNow()) {
        setVi(opt.viHomingStart, false, now);
        char msg[96];
        snprintf(msg, sizeof(msg), "Alarme pendant la prise d'origine (0x%04X)", (unsigned)alarmCodeValue);
        setError(msg);
        homingState = OperationState::DRIVE_FAILED;
        homingPhase = 0;
        return;
    }

    switch (homingPhase) {
        case 0:
            if (viBitOnWire(opt.viHomingStart, true)) {
                homingOnWireTime = nodes.virtualInputs->getLastRefresh();
                homingStartPos = getActualPosition();
                homingPhase = 1;
            }
            break;

        case 1: {
            if (!fresh(nodes.status, homingOnWireTime)) break;
            int32_t pos = getActualPosition();
            int64_t moved = (int64_t)pos - (int64_t)homingStartPos;
            if (moved < 0) moved = -moved;
            if (!homeDoneBit) homingSeenHomeLow = true;
            if (!zeroSpeedBit || moved > (int64_t)opt.positionTolerance) homingSeenMotion = true;

            uint32_t sinceStart = now - homingOnWireTime;
            bool candidate = homeDoneBit && zeroSpeedBit && sinceStart >= HOMING_MIN_MS;
            bool complete = false;
            if (candidate && (homingSeenHomeLow || homingSeenMotion)) {
                // Position stable sur plusieurs lectures (déplacement final vers l'index terminé)
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
                // Bit « origine faite » resté vrai sans aucun mouvement : origine déjà valide
                logf("Prise d'origine sans mouvement détecté : origine déjà valide côté variateur");
                complete = true;
            } else {
                homingStableCount = 0;
            }

            if (complete) {
                setVi(opt.viHomingStart, false, now);
                homeLatched = true;
                homingState = OperationState::DRIVE_IDLE;
                homingPhase = 0;
                logf("Prise d'origine terminée (position %ld, %lu ms)", (long)pos, (unsigned long)elapsed);
                return;
            }
            if (now - lastHomingLog > 2000) {
                logf("Prise d'origine en cours (%lu s, position %ld, état 0x%04X)",
                     (unsigned long)(elapsed / 1000), (long)pos, (unsigned)statusWord);
                lastHomingLog = now;
            }
            break;
        }

        default:
            setVi(opt.viHomingStart, false, now);
            setError("Phase de prise d'origine invalide");
            homingState = OperationState::DRIVE_FAILED;
            homingPhase = 0;
            break;
    }
}

// ============================================================================
// JOG
// ============================================================================
// Deux stratégies :
//  - jog natif (A5) : bits JOGCMD+ / JOGCMD- et vitesse P06-04 ; arrêt = bit à 0 ;
//  - jog par déplacement (A6, ou A5 sans bits de jog) : consigne absolue vers la butée
//    (ou fenêtre glissante de opt.jogWindow si la butée n'est pas applicable) à la vitesse
//    de jog ; arrêt = nouvelle consigne à la position courante (arrêt par re-consigne).
bool LichuanServo::jogUsesDriveBits() const {
    return opt.viJogPlus < 16 && opt.viJogMinus < 16 && nodes.jogSpeed != nullptr;
}

bool LichuanServo::jogForward(int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    return jogCommand(1, limitPuu, slowZonePuu, slowSpeed);
}

bool LichuanServo::jogReverse(int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    return jogCommand(-1, limitPuu, slowZonePuu, slowSpeed);
}

bool LichuanServo::jogCommand(int8_t dir, int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
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

    uint32_t now = millis();
    int32_t pos = getActualPosition();
    bool homed = getHomeDone();

    // Butées : mêmes règles que SureServo (butée avant seulement si l'origine est faite)
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
    // Arrêt précédent pas terminé (re-consigne, vitesse non nulle, verrou) : on patiente
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
        jogPhaseTime = now;
        jogAppliedSpeed = 0;
        logf("Jog %s à %u tr/min", dir > 0 ? "avant" : "arrière", (unsigned)speed);
    }
    return true;
}

int32_t LichuanServo::computeJogTarget() const {
    int64_t pos = getActualPosition();
    int64_t window = opt.jogWindow > 0 ? (int64_t)opt.jogWindow : 100000;
    int64_t t = jogDir > 0 ? pos + window : pos - window;
    if (jogLimitEnforced) {
        if (jogDir > 0 && t > jogLimit) t = jogLimit;
        if (jogDir < 0 && t < jogLimit) t = jogLimit;
    }
    return clampToInt32(t);
}

void LichuanServo::processJog(uint32_t now) {
    if (jogDir == 0) return;

    if (jogUsesDriveBits()) {
        uint8_t bit = jogDir > 0 ? opt.viJogPlus : opt.viJogMinus;
        switch (jogPhase) {
            case 0:
                forceWrite16(nodes.jogSpeed, jogCmdSpeed);
                jogAppliedSpeed = jogCmdSpeed;
                jogPhaseTime = now;
                jogPhase = 1;
                break;
            case 1:
                if (fresh(nodes.jogSpeed, jogPhaseTime)) {
                    setVi(bit, true, now);
                    jogPhase = 2;
                } else if (now - jogPhaseTime > WRITE_CONFIRM_TIMEOUT_MS) {
                    setError("Vitesse de jog non confirmée sur le bus");
                    jogDir = 0;
                }
                break;
            default:
                if (jogCmdSpeed != jogAppliedSpeed) {      // zone lente : vitesse modifiée en marche
                    nodes.jogSpeed->setValue(jogCmdSpeed);
                    jogAppliedSpeed = jogCmdSpeed;
                }
                break;
        }
        return;
    }

    // Jog par déplacement
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
            // Fenêtre glissante : prolonger la consigne avant d'arriver au bout, ou
            // appliquer une nouvelle vitesse (zone lente)
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
void LichuanServo::stopJogMotion(uint32_t now) {
    setVi(opt.viJogPlus, false, now);
    setVi(opt.viJogMinus, false, now);
    if (jogDir == 0) return;
    bool sent = jogPhase >= 1;
    if (!jogUsesDriveBits()) {
        cancelLoad();
        if (sent) beginRetargetStop(now, jogTarget);
    }
    if (sent) beginSettle(now);
    jogDir = 0;
    jogPhase = 0;
}

bool LichuanServo::jogStop() {
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

bool LichuanServo::setJogSpeed(uint32_t speed) {
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
int32_t LichuanServo::estimatedStopPosition(uint32_t now) const {
    int64_t p = getActualPosition();
    if (velDen > 0 && posSampleTime != 0) {
        uint32_t age = now - posSampleTime;
        if (age > EXTRAPOLATION_MAX_MS) age = EXTRAPOLATION_MAX_MS;
        p = (int64_t)posSampleValue + velNum * (int64_t)age / velDen;   // compense l'âge de la lecture
        if (accelTimeMs > 0 && lastLoadSpeedRpm > 0) {
            // Distance de décélération estimée : v * t_dec / 2, t_dec = rampe * vitesse / 1000
            int64_t tdec = (int64_t)accelTimeMs * lastLoadSpeedRpm / 1000;
            p += velNum * tdec / (2 * velDen);
        }
    }
    return clampToInt32(p);
}

// Arrêt par re-consigne : la cible devient la position courante (extrapolée), chargée par un
// nouveau front sur le bit de départ. Utilisé quand le variateur n'a pas d'entrée d'arrêt (A6).
// La cible est bornée entre la position lue et la cible du mouvement interrompu : l'arrêt
// ne peut jamais emmener l'axe plus loin que le mouvement d'origine.
void LichuanServo::beginRetargetStop(uint32_t now, int32_t interruptedTarget) {
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

// Attente de la vitesse nulle après un arrêt (lecture d'état postérieure à l'écriture)
void LichuanServo::beginSettle(uint32_t now) {
    settleActive = true;
    settleStart = now;
    settleWireTime = 0;
}

void LichuanServo::processStop(uint32_t now) {
    // 1. Chargement de la re-consigne d'arrêt
    if (stopRetargetActive) {
        LoadState r = serviceLoad(now);
        if (r == LoadState::DONE || r == LoadState::FAILED) {
            if (r == LoadState::FAILED) {
                logf("Re-consigne d'arrêt non confirmée sur le bus");
                if (onWarningCallback) onWarningCallback(true, "Arrêt Lichuan non confirmé");
            }
            cancelLoad();
            stopRetargetActive = false;
            beginSettle(now);
        }
    }

    // 2. Vitesse nulle constatée (ou délai dépassé)
    if (settleActive && !stopRetargetActive) {
        if (settleWireTime == 0) {
            if (fresh(nodes.virtualInputs, settleStart)) settleWireTime = nodes.virtualInputs->getLastRefresh();
        } else if (fresh(nodes.status, settleWireTime) && zeroSpeedBit) {
            settleActive = false;
        }
        // Délai : 3 s + durée de décélération estimée (rampe connue, sinon 2,5 s / 1000 tr/min)
        uint32_t ramp = accelTimeMs > 0 ? accelTimeMs : 2500;
        uint32_t rpm = lastLoadSpeedRpm > jogSpeedRpm ? lastLoadSpeedRpm : jogSpeedRpm;
        uint32_t limit = STOP_SETTLE_TIMEOUT_MS + ramp * rpm / 1000;
        if (limit > 15000) limit = 15000;
        if (settleActive && now - settleStart > limit) {
            settleActive = false;
            if (fresh(nodes.status, settleStart) && !zeroSpeedBit) {
                // L'ordre d'arrêt n'a pas été exécuté : défaut => la classe métier passe en
                // urgence et coupe le servo (emergencyStop)
                notifyDriveFault("Arrêt non exécuté par le variateur : axe toujours en mouvement");
            } else {
                logf("Arrêt : vitesse nulle non constatée après %lu ms", (unsigned long)limit);
                if (onWarningCallback) onWarningCallback(true, "Arrêt Lichuan : vitesse nulle non constatée");
            }
        }
    }

    // 3. Fin du verrou : 500 ms minimum et axe arrêté
    if (pendingReleaseImmediateStop && !stopRetargetActive && !settleActive &&
        now - immediateStopTimestamp >= IMMEDIATE_STOP_DURATION_MS) {
        if (viBit(opt.viStop)) {
            setVi(opt.viStop, false, now);
            // Acquitte l'avertissement éventuel d'arrêt par DI (A5 Er.900)
            if (opt.viAlarmReset < 16 && !hasAlarmRaw() && initialized) {
                setVi(opt.viAlarmReset, true, now);
                stopResetPulse = true;
                stopResetPulseTime = now;
            }
        }
        pendingReleaseImmediateStop = false;
        movementCancelled = false;
        logf("Arrêt terminé - mouvements autorisés");
    }

    // 4. Fin de l'impulsion d'acquittement
    if (stopResetPulse && viBitOnWire(opt.viAlarmReset, true) && now - stopResetPulseTime >= RESET_PULSE_MS) {
        setVi(opt.viAlarmReset, false, now);
        stopResetPulse = false;
    }
}

bool LichuanServo::stopMovement() {
    if (!nodesOk) return false;
    uint32_t now = millis();
    logf("Arrêt demandé");

    bool moving = moveState == OperationState::DRIVE_IN_PROGRESS ||
                  homingState == OperationState::DRIVE_IN_PROGRESS ||
                  jogDir != 0 || !zeroSpeedBit;
    bool moveLoaded = moveState == OperationState::DRIVE_IN_PROGRESS && movePhase >= 1;
    bool jogLoaded = jogDir != 0 && jogPhase >= 1 && !jogUsesDriveBits();
    int32_t interruptedTarget = moveLoaded ? targetPosition : jogTarget;

    // 1. Commandes en cours retirées
    cancelLoad();
    stopRetargetActive = false;
    setVi(opt.viHomingStart, false, now);    // A6 : départ homing sur niveau (PA_0A0 = 0) => arrêt
    setVi(opt.viJogPlus, false, now);
    setVi(opt.viJogMinus, false, now);
    if (opt.holdStartBit) setVi(opt.viStartMove, false, now);
    jogDir = 0;
    jogPhase = 0;

    // 2. Arrêt physique
    if (opt.viStop < 16) {
        if (moving) setVi(opt.viStop, true, now);   // A5 FunIN.34 : vitesse nulle puis maintien
    } else if (moveLoaded || jogLoaded) {
        beginRetargetStop(now, interruptedTarget);   // A6 : nouvelle consigne = position courante
    }
    if (moving) beginSettle(now);

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

bool LichuanServo::emergencyStop() {
    if (!nodesOk) return false;
    uint32_t now = millis();
    cancelLoad();
    stopRetargetActive = false;
    settleActive = false;
    stopResetPulse = false;
    jogDir = 0;
    jogPhase = 0;

    // Servo OFF et toutes les commandes à 0, écriture forcée
    for (uint8_t b = 0; b < 16; b++) viBitTime[b] = now;
    viWord = 0;
    viChangeTime = now;
    viConfirmed = false;
    nodes.virtualInputs->setValue(0, true);

    initState   = OperationState::DRIVE_FAILED;
    resetState  = OperationState::DRIVE_FAILED;
    moveState   = OperationState::DRIVE_FAILED;
    homingState = OperationState::DRIVE_FAILED;
    initialized = false;

    pendingReleaseImmediateStop = true;
    immediateStopTimestamp = now;
    logf("Arrêt d'urgence : servo OFF");
    return true;
}

bool LichuanServo::isImmediateStopActive() const {
    return pendingReleaseImmediateStop || stopRetargetActive || settleActive || stopResetPulse ||
           viBit(opt.viStop);
}

void LichuanServo::clearMovementLock() {
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
        setVi(opt.viHomingStart, false, millis());
    }

    targetPosition = 0;
}

void LichuanServo::failMotion(const char* reason) {
    uint32_t now = millis();
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        cancelLoad();
        setVi(opt.viStartMove, false, now);
        moveState = OperationState::DRIVE_FAILED;
        movePhase = 0;
        setError(reason);
    }
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        setVi(opt.viHomingStart, false, now);
        homingState = OperationState::DRIVE_FAILED;
        homingPhase = 0;
        setError(reason);
    }
    if (jogDir != 0) {
        setVi(opt.viJogPlus, false, now);
        setVi(opt.viJogMinus, false, now);
        if (!jogUsesDriveBits()) setVi(opt.viStartMove, false, now);
        cancelLoad();
        jogDir = 0;
        jogPhase = 0;
    }
}

void LichuanServo::notifyDriveFault(const char* reason) {
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
bool LichuanServo::setServoEnabled(bool enable) {
    if (!nodesOk) return false;
    setVi(opt.viServoOn, enable, millis());
    logf("Servo %s", enable ? "ON" : "OFF");
    return true;
}

bool LichuanServo::setMoveSpeed(int32_t speed) {
    if (speed < 1) speed = 1;
    if (speed > (int32_t)opt.maxSpeedRpm) speed = opt.maxSpeedRpm;
    moveSpeedRpm = (uint16_t)speed;
    // Transmis dès maintenant au repos (le registre sert aussi au jog par déplacement) ;
    // de toute façon ré-écrit au début de chaque déplacement.
    if (nodesOk && moveState != OperationState::DRIVE_IN_PROGRESS && !motionActive() && !loadBusy()) {
        if (nodes.moveSpeed->getValue() != moveSpeedRpm) nodes.moveSpeed->setValue(moveSpeedRpm);
    }
    return true;
}

bool LichuanServo::setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) {
    (void)rampIndex;   // pas de table de rampes sur Lichuan : voir setAccelTime()
    return setMoveSpeed(speed);
}

bool LichuanServo::setAccelTime(uint16_t ms) {
    if (!nodes.accelTime && !nodes.decelTime) {
        setError("Temps de rampe non configurable (node accelTime absent)");
        return false;
    }
    if (ms > opt.maxAccelMs) ms = opt.maxAccelMs;
    accelTimeMs = ms;
    return true;
}

bool LichuanServo::setMaxTorque(uint8_t torquePercent) {
    if (!nodes.torqueLimit) {
        setError("Limite de couple non configurée (node torqueLimit absent)");
        return false;
    }
    uint16_t v = (uint16_t)torquePercent * 10;   // 0,1 % du couple nominal
    if (v > 3000) v = 3000;
    nodes.torqueLimit->setValue(v);
    if (nodes.torqueLimitReverse) nodes.torqueLimitReverse->setValue(v);
    logf("Couple max %u %%", (unsigned)torquePercent);
    return true;
}

bool LichuanServo::saveParameters() {
    // Volontairement non implémenté : la sauvegarde EEPROM (A6 PA_1A7 = 0x0801, A5 P0C-13)
    // use la mémoire du variateur. Le paramétrage se fait une fois à la mise en service.
    setError("Sauvegarde EEPROM non utilisée (paramétrage unique, voir docs/axes/lichuan.md)");
    return false;
}

bool LichuanServo::clearError() {
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
int32_t LichuanServo::getActualPosition() const {
    return nodes.position ? nodes.position->getValueAsInt32() : 0;
}

bool LichuanServo::hasAlarmRaw() const {
    if (!nodesOk) return false;
    if (alarmBit) return true;
    return opt.alarmFromCode && alarmCodeValue != 0;
}

bool LichuanServo::faultNow() const {
    return hasAlarmRaw() || driveFaultPending || (initialized && commLost);
}

bool LichuanServo::alarmCodePending() const {
    return alarmBit && alarmBitSince != 0 && !fresh(nodes.alarmCode, alarmBitSince) &&
           (millis() - alarmBitSince) < ALARM_CODE_WAIT_MS;
}

// Signalement public : si le bit d'alarme est arrivé avant le code, on attend la relecture du
// code (500 ms au plus) pour que getAlarmDescription() soit juste dès le premier appel.
// Les contrôles internes (mouvement, homing) utilisent faultNow(), sans délai.
bool LichuanServo::getHasAlarms() const {
    if (!nodesOk) return true;
    if (alarmCodePending() && !(opt.alarmFromCode && alarmCodeValue != 0)) {
        return driveFaultPending || (initialized && commLost);
    }
    return faultNow();
}

bool LichuanServo::getIsReady() const {
    return nodesOk && initialized && !commLost && readyBit && !hasAlarmRaw();
}

bool LichuanServo::getHomeDone() const {
    if (!nodesOk || homingState == OperationState::DRIVE_IN_PROGRESS) return false;
    return homeDoneBit || homeLatched;
}

bool LichuanServo::isAtPosition(int32_t target) const {
    int64_t d = (int64_t)getActualPosition() - (int64_t)target;
    if (d < 0) d = -d;
    return d <= (int64_t)opt.positionTolerance;
}

const char* LichuanServo::findAlarmText(uint16_t code) const {
    if (family == LichuanFamily::A5) return lookupAlarm(A5_ALARMS, (uint16_t)(code & 0x0FFF));
    return lookupAlarm(A6_ALARMS, code);
}

const char* LichuanServo::getAlarmDescription() const {
    if (!nodesOk) return "Nodes Lichuan obligatoires manquants";
    if (alarmCodeValue != 0) {
        const char* t = findAlarmText(alarmCodeValue);
        if (t) return t;
        snprintf(alarmDescBuf, sizeof(alarmDescBuf), "Alarme Lichuan 0x%04X (%u)",
                 (unsigned)alarmCodeValue, (unsigned)alarmCodeValue);
        return alarmDescBuf;
    }
    if (hasAlarmRaw()) return "Alarme variateur (code non disponible)";
    if (initialized && commLost) return "Perte de communication Modbus avec le variateur";
    if (driveFaultPending) return driveFaultReason.c_str();
    return "";
}

uint16_t LichuanServo::getSlaveAddress() const {
    return nodes.status ? nodes.status->getModbusAddress() : 0;
}

bool LichuanServo::isPlcSuspended() const {
    BorneUniverselle* borne = BorneUniverselle::getInstance();
    return borne ? borne->getPlcSuspended() : false;
}

void LichuanServo::setError(const char* msg) {
    lastErrorRaw = msg ? msg : "";
    lastError = "[" + servoId + "] " + lastErrorRaw;
    if (msg && msg[0]) logf("Erreur : %s", msg);
}

void LichuanServo::logf(const char* fmt, ...) const {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.printf("[%s] %s\n", servoId.c_str(), buf);
}

void LichuanServo::printStatus() const {
    Serial.printf("===== %s (Lichuan %s) =====\n", servoId.c_str(), getFamilyName(family));
    Serial.printf("Initialisé: %d  Prêt: %d  Alarme: %d (0x%04X %s)\n",
                  initialized ? 1 : 0, getIsReady() ? 1 : 0, getHasAlarms() ? 1 : 0,
                  (unsigned)alarmCodeValue, getAlarmDescription());
    Serial.printf("Mot d'état: 0x%04X  Entrées virtuelles: 0x%04X  Com perdue: %d\n",
                  (unsigned)statusWord, (unsigned)viWord, commLost ? 1 : 0);
    Serial.printf("Position: %ld  Cible: %ld  En position: %d  Vitesse nulle: %d  Origine: %d\n",
                  (long)getActualPosition(), (long)targetPosition, inPosBit ? 1 : 0,
                  zeroSpeedBit ? 1 : 0, getHomeDone() ? 1 : 0);
    Serial.printf("États init/reset/move/homing: %s / %s / %s / %s  Jog: %d\n",
                  getOperationStateText(initState), getOperationStateText(resetState),
                  getOperationStateText(moveState), getOperationStateText(homingState), (int)jogDir);
    Serial.printf("Vitesse: %u tr/min  Jog: %u tr/min  Rampe: %u ms\n",
                  (unsigned)moveSpeedRpm, (unsigned)jogSpeedRpm, (unsigned)accelTimeMs);
    if (lastError.length()) Serial.printf("Dernière erreur: %s\n", lastError.c_str());
}

// ============================================================================
// HANDLERS POUR LA CLASSE MÉTIER (mêmes contrats que SureServo)
// ============================================================================
bool LichuanServo::handleInitializing(OperationStatus& status) {
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

bool LichuanServo::handleHoming(OperationStatus& status, uint32_t homingSpeed, uint32_t timeoutMs) {
    (void)homingSpeed;   // vitesses de recherche : paramètres du variateur (PA_0A2/PA_0A3, P05-32/P05-33)
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

bool LichuanServo::handleJogging(OperationStatus& status) {
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

bool LichuanServo::handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync, uint32_t timeoutMs) {
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

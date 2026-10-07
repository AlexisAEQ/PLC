#pragma once

// ============================================================================
// LICHUAN SERVO — variateurs Lichuan pilotés en Modbus RTU (familles A6 et A5)
// ============================================================================
//
// Une seule classe pour deux familles de variateurs Lichuan :
//   - A6 : LCDA6 / A6 (paramètres PA_xxx, adresse Modbus = numéro hexadécimal du
//          paramètre, ex. PA_1A4 -> 0x01A4 = 420) ;
//   - A5 : LCDA630P / A5 / LC10E (paramètres Pgg-nn, adresse = (gg << 8) | nn avec nn
//          décimal, ex. P31-00 -> 0x3100, P11-12 -> 0x110C).
//
// Les ADRESSES ne sont pas dans ce code : elles sont dans les fichiers matériels
// data/hardware/Lichuan_A6.json et Lichuan_A5.json. Le firmware crée les nodes Modbus à
// partir de config.json, et la classe métier passe les pointeurs de nodes dans un
// LichuanNodes (comme ServoNodes pour SureServo).
//
// La classe n'écrit que des registres de fonctionnement (entrées virtuelles, consigne,
// vitesse...). Le paramétrage du variateur (mode de commande, DI virtuelles, mode de
// prise d'origine, sorties d'état, vitesse/parité du bus) se fait UNE FOIS à la mise en
// service : voir la liste de contrôle de docs/axes/lichuan.md.
//
// Unités : positions en unités de commande du variateur (impulsions, cf. PA_04A / P05-02),
//          vitesses en tr/min.
//
// Contrats identiques à SureServo (process() à chaque cycle, machines à états non
// bloquantes, OperationState / OperationStatus, verrou de 500 ms après stopMovement()...).
// Aucun état statique : plusieurs instances (plusieurs axes) sont possibles.

#include <Arduino.h>
#include "Node/Node.h"
#include "ServoDrive/ServoDrive.h"

// Famille de variateur (profil de registres et valeurs par défaut des bits)
enum class LichuanFamily : uint8_t {
    A6 = 0,   // LCDA6 / A6  : PA_xxx
    A5 = 1    // LCDA630P / A5 / LC10E : Pgg-nn
};

// ========================================
// OPTIONS (positions des bits, comportement)
// ========================================
// Les valeurs par défaut de chaque famille (LichuanOptions::defaultsFor) correspondent
// au paramétrage décrit dans docs/axes/lichuan.md. Un bit à NONE n'est pas utilisé.
struct LichuanOptions {
    static constexpr uint8_t NONE = 0xFF;

    // --- Mot d'entrées virtuelles (node virtualInputs)
    //     A6 : PA_1A4, bit n = DIn (fonction PA_080+n, bit n de PA_1A0 à 1)
    //     A5 : P31-00, bit n = VDI(n+1) (fonction P17-(2n), P0C-09 = 1)
    uint8_t viServoOn     = 0;     // servo ON (A6 fonction 0 / A5 FunIN.1 S-ON)
    uint8_t viAlarmReset  = 1;     // acquittement alarme (A6 fonction 1 / A5 FunIN.2 ALM-RST)
    uint8_t viStartMove   = 5;     // départ déplacement (A6 fonction 20 POS_LOAD / A5 FunIN.28 PosInSen)
    uint8_t viHomingStart = 7;     // départ prise d'origine (A6 fonction 16 / A5 FunIN.32 HomingStart)
    uint8_t viStop        = NONE;  // arrêt rampe + maintien en position (A5 FunIN.34) ; A6 : NONE
    uint8_t viJogPlus     = NONE;  // jog + natif (A5 FunIN.18 JOGCMD+) ; A6 : NONE
    uint8_t viJogMinus    = NONE;  // jog - natif (A5 FunIN.19 JOGCMD-) ; A6 : NONE

    // --- Mot d'état (node status)
    //     A6 : PA_1D3, bit n = DOn (fonction PA_088+n)
    //     A5 : P17-32, bit n = VDO(n+1) (fonction P17-(33+2n), P0C-11 = 1)
    uint8_t stReady      = 0;      // servo prêt (A6 DO fonction 0 S-RDY / A5 FunOUT.1)
    uint8_t stAlarm      = 1;      // alarme (A6 DO fonction 1 ALM / A5 FunOUT.11)
    uint8_t stInPosition = 2;      // positionnement terminé (A6 DO fonction 2 COIN / A5 FunOUT.5)
    uint8_t stZeroSpeed  = 4;      // vitesse nulle (A6 DO fonction 4 ZSP / A5 FunOUT.3)
    uint8_t stHomeDone   = 5;      // origine faite (A6 DO fonction 11 ORG_FOUND / A5 FunOUT.16)
    uint16_t statusInvertMask = 0; // XOR appliqué au mot d'état (sorties à logique inversée)

    // --- Comportement
    bool holdStartBit = false;        // true : bit de départ maintenu jusqu'à la fin du déplacement (A5)
                                      // false : impulsion (front montant, A6 PA_096 = 2)
    bool alarmFromCode = true;        // true : code d'alarme != 0 => alarme (A6)
                                      // false : seul le bit d'alarme compte (A5 : les avertissements 9xx ne sont pas des alarmes)
    bool servoOffDuringReset = true;  // coupe le servo pendant l'acquittement d'une alarme active (requis A5)
    bool fastPollingDuringMotion = true; // lecture rapide de l'état et de la position pendant un mouvement

    uint16_t maxSpeedRpm = 3000;          // borne haute des vitesses (registre : A6 3000, A5 6000)
    uint16_t maxAccelMs = 2500;           // borne haute du temps de rampe (A6 PA_058 : 2500 ; A5 P11-15 : 65535)
    uint16_t defaultMoveSpeedRpm = 100;   // vitesse de déplacement avant le premier setMoveSpeed()
    uint16_t defaultJogSpeedRpm = 60;     // vitesse de jog avant le premier setJogSpeed()
    uint32_t positionTolerance = 100;     // écart admis pour « position atteinte » (unités de commande)
    uint32_t jogWindow = 100000;          // jog par déplacement : fenêtre glissante (unités de commande)
    uint32_t keepAliveMs = 2000;          // ré-écriture périodique du mot d'entrées virtuelles au repos (0 = jamais)

    static LichuanOptions defaultsFor(LichuanFamily family);
};

// ========================================
// NODES
// ========================================
// Ids des fichiers matériels (data/hardware/Lichuan_A6.json / Lichuan_A5.json) entre crochets.
struct LichuanNodes {
    // === OBLIGATOIRES ===
    Uint16OutputNode* virtualInputs  = nullptr;  // [W1]  A6 PA_1A4 (420)        / A5 P31-00 (0x3100)
    Uint32OutputNode* targetPosition = nullptr;  // [DW1] A6 PA_168/169 (360)    / A5 P11-12 (0x110C), 32 bits signés, mot bas d'abord
    Uint16OutputNode* moveSpeed      = nullptr;  // [W2]  A6 PA_190 (400)        / A5 P11-14 (0x110E), tr/min
    Uint32InputNode*  position       = nullptr;  // [DR1] A6 PA_1B8/1B9 (440) position commande / A5 P0B-07 (0x0B07) position, unités de commande
    Uint16InputNode*  status         = nullptr;  // [R1]  A6 PA_1D3 (467) état des DO / A5 P17-32 (0x1720) état des VDO
    Uint16InputNode*  alarmCode      = nullptr;  // [R2]  A6 PA_1C9 (457)        / A5 P0B-34 (0x0B22)

    // === OPTIONNELS (nullptr si absents) ===
    Uint16OutputNode* accelTime          = nullptr; // [W3] A6 PA_058 (88) ms/1000 tr/min / A5 P11-15 (0x110F) ms
    Uint16OutputNode* decelTime          = nullptr; // [W4] A6 PA_059 (89)
    Uint16OutputNode* jogSpeed           = nullptr; // [W5] A5 P06-04 (0x0604) vitesse de jog natif, tr/min
    Uint16OutputNode* torqueLimit        = nullptr; // [W6] A6 PA_05E (94) / A5 P07-09 (0x0709), 0,1 %
    Uint16OutputNode* torqueLimitReverse = nullptr; // [W7] A5 P07-10 (0x070A), 0,1 %
    Uint16OutputNode* faultReset         = nullptr; // [W8] A5 P0D-01 (0x0D01) acquittement défaut
    Uint16InputNode*  actualSpeed        = nullptr; // [R3] A6 PA_1C1 (449) / A5 P0B-00 (0x0B00), tr/min signés
    Uint16InputNode*  virtualInputsReadback = nullptr; // [R4] relecture PA_1A4 / P31-00 (détection redémarrage variateur)
    Uint32InputNode*  feedbackPosition   = nullptr; // [DR2] A6 PA_1BC/1BD (444) / A5 P0B-17 (0x0B11), unités codeur (diagnostic)

    // États décodés (sorties virtuelles pour l'IHM)
    BooleanOutputNode* servoReady       = nullptr;
    BooleanOutputNode* inPosition       = nullptr;
    BooleanOutputNode* zeroSpeed        = nullptr;
    BooleanOutputNode* homeDone         = nullptr;
    BooleanOutputNode* servoAlarm       = nullptr;
    BooleanOutputNode* driveInitialised = nullptr;
    BooleanOutputNode* modbusError      = nullptr;

    bool validateRequired() const {
        return virtualInputs != nullptr &&
               targetPosition != nullptr &&
               moveSpeed != nullptr &&
               position != nullptr &&
               status != nullptr &&
               alarmCode != nullptr;
    }

    size_t countOptional() const {
        size_t n = 0;
        if (accelTime) n++;
        if (decelTime) n++;
        if (jogSpeed) n++;
        if (torqueLimit) n++;
        if (torqueLimitReverse) n++;
        if (faultReset) n++;
        if (actualSpeed) n++;
        if (virtualInputsReadback) n++;
        if (feedbackPosition) n++;
        if (servoReady) n++;
        if (inPosition) n++;
        if (zeroSpeed) n++;
        if (homeDone) n++;
        if (servoAlarm) n++;
        if (driveInitialised) n++;
        if (modbusError) n++;
        return n;
    }
};

// ========================================
// CLASSE LICHUANSERVO
// ========================================
class LichuanServo : public ServoDrive {
public:
    LichuanServo(LichuanFamily family, const LichuanNodes& nodes, const char* servoId = "LICHUAN");
    LichuanServo(LichuanFamily family, const LichuanNodes& nodes, const LichuanOptions& options,
                 const char* servoId = "LICHUAN");

    // ========================================
    // INTERFACE SERVODRIVE
    // ========================================
    void process() override;

    bool startInitialize() override;
    bool startReset() override;
    using ServoDrive::startGoToPosition;  // surcharge float de l'interface
    bool startGoToPosition(int32_t positionPuu, bool waitForSync = false, uint32_t timeout = 60000) override;
    bool startHoming(uint32_t timeoutMs = 0) override;
    bool stopMovement() override;

    OperationState getInitializeState() const override { return initState; }
    OperationState getResetState() const override { return resetState; }
    OperationState getMoveState() const override { return moveState; }
    OperationState getHomingState() const override { return homingState; }

    bool jogForward(int32_t limitPuu = INT32_MAX, uint32_t slowZonePuu = 0, uint16_t slowSpeed = 10) override;
    bool jogReverse(int32_t limitPuu = INT32_MIN, uint32_t slowZonePuu = 0, uint16_t slowSpeed = 10) override;
    bool jogStop() override;
    bool setJogSpeed(uint32_t speed) override;     // tr/min
    uint32_t getJogSpeed() const override { return jogSpeedRpm; }
    bool setServoEnabled(bool enable) override;
    bool setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) override; // speed en tr/min, rampIndex ignoré
    bool setMaxTorque(uint8_t torquePercent) override;               // % du couple nominal (node torqueLimit requis)
    bool emergencyStop() override;
    bool saveParameters() override;                                  // non utilisé (pas d'écriture EEPROM)
    void clearMovementLock() override;

    int32_t getPosition() const override { return getActualPosition(); }
    bool getIsReady() const override;
    bool getHasAlarms() const override;
    uint16_t getAlarmCode() const override { return alarmCodeValue; }
    bool getIsInitialized() const override { return initialized; }
    const char* getLastError() const override { return lastError.c_str(); }
    bool getHomeDone() const override;

    int32_t getActualPosition() const override;
    bool setMoveSpeed(int32_t speed) override;     // tr/min, borné à 1..maxSpeedRpm
    bool isImmediateStopActive() const override;
    const char* getAlarmDescription() const override;

    bool handleInitializing(OperationStatus& status) override;
    bool handleHoming(OperationStatus& status, uint32_t homingSpeed = 0, uint32_t timeoutMs = 0) override;
    bool handleJogging(OperationStatus& status) override;
    bool handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync = true, uint32_t timeoutMs = 60000) override;

    // ========================================
    // SPÉCIFIQUE LICHUAN
    // ========================================
    LichuanFamily getFamily() const { return family; }
    const LichuanOptions& getOptions() const { return opt; }
    static const char* getFamilyName(LichuanFamily f);

    int32_t getMoveSpeed() const { return moveSpeedRpm; }
    // Temps de rampe écrit au début de chaque déplacement (0 = réglage du variateur conservé).
    // A6 : ms pour 0 -> 1000 tr/min (PA_058/PA_059) ; A5 : ms (P11-15).
    bool setAccelTime(uint16_t ms);
    uint16_t getAccelTime() const { return accelTimeMs; }

    bool getServoReadyBit() const { return readyBit; }
    bool getInPosition() const { return inPosBit; }
    bool getZeroSpeed() const { return zeroSpeedBit; }
    bool isCommunicationLost() const { return commLost; }
    bool isHomingInProgress() const { return homingState == OperationState::DRIVE_IN_PROGRESS; }
    bool isJogging() const { return jogDir != 0; }
    uint16_t getStatusWord() const { return statusWord; }
    uint16_t getVirtualInputsWord() const { return viWord; }

    // Défaut logiciel détecté (perte de communication, redémarrage du variateur) : reste
    // signalé par getHasAlarms() jusqu'au prochain startReset().
    bool hasPendingDriveFault() const { return driveFaultPending; }
    void acknowledgeDriveFault() { driveFaultPending = false; }

    bool clearError();
    void printStatus() const;

private:
    // ========================================
    // CONSTANTES DE TEMPS
    // ========================================
    static constexpr uint32_t INIT_TIMEOUT_MS            = 15000;
    static constexpr uint32_t SERVO_ON_SETTLE_MS         = 300;
    static constexpr uint32_t RESET_TIMEOUT_MS           = 5000;
    static constexpr uint32_t RESET_PULSE_MS             = 200;
    static constexpr uint32_t RESET_STABILIZE_MS         = 500;
    static constexpr uint32_t RESET_VERIFY_MS            = 2000;
    static constexpr uint32_t RESET_STABLE_MS            = 200;
    static constexpr uint32_t MOVE_TIMEOUT_MS            = 60000;
    static constexpr uint32_t HOMING_TIMEOUT_MS          = 100000;
    static constexpr uint32_t HOMING_MIN_MS              = 300;
    static constexpr uint32_t HOMING_NO_MOTION_ACCEPT_MS = 2000;
    static constexpr uint32_t START_PULSE_MS             = 100;
    static constexpr uint32_t WRITE_CONFIRM_TIMEOUT_MS   = 2000;
    static constexpr uint32_t MIN_DONE_MS                = 100;
    static constexpr uint32_t IMMEDIATE_STOP_DURATION_MS = 500;
    static constexpr uint32_t STOP_SETTLE_TIMEOUT_MS     = 3000;
    static constexpr uint32_t COMM_LOSS_MS               = 5000;
    static constexpr uint32_t COMM_MESSAGE_INTERVAL_MS   = 10000;
    static constexpr uint32_t HOME_BIT_LATCH_MS          = 2000;
    static constexpr uint32_t EXTRAPOLATION_MAX_MS       = 500;
    static constexpr uint32_t ALARM_CODE_WAIT_MS         = 500;
    static constexpr uint8_t  STABLE_READINGS            = 3;
    static constexpr int32_t  ZERO_SPEED_RPM             = 10;

    // Séquenceur de chargement de consigne : cible + vitesse écrites et confirmées sur le
    // bus, puis front montant sur le bit de départ (impulsion ou maintien selon la famille).
    enum class LoadState : uint8_t { IDLE, WRITING, CLEAR_EDGE, PULSE, CLEAR_AFTER_PULSE, DONE, FAILED };

    // ========================================
    // CONFIGURATION
    // ========================================
    const LichuanFamily family;
    const LichuanOptions opt;
    const LichuanNodes nodes;
    bool nodesOk = false;

    // ========================================
    // MACHINES À ÉTATS
    // ========================================
    OperationState initState   = OperationState::DRIVE_IDLE;
    OperationState resetState  = OperationState::DRIVE_IDLE;
    OperationState moveState   = OperationState::DRIVE_IDLE;
    OperationState homingState = OperationState::DRIVE_IDLE;
    uint8_t initPhase = 0, resetPhase = 0, movePhase = 0, homingPhase = 0;
    uint32_t initStartTime = 0, initPhaseTime = 0;
    uint32_t resetStartTime = 0, resetPhaseTime = 0, resetSuccessSince = 0;
    uint32_t moveStartTime = 0;
    uint32_t homingStartTime = 0;
    uint32_t currentMoveTimeoutMs = MOVE_TIMEOUT_MS;
    uint32_t currentHomingTimeoutMs = HOMING_TIMEOUT_MS;

    bool initialized = false;
    bool driveInitStarted = false;
    bool initFailureReported = false;
    bool driveHomingStarted = false;
    bool waitForSyncRequested = false;
    bool resetServoWasOn = false;

    int32_t targetPosition = 0;
    uint16_t moveSpeedRpm = 100;
    uint16_t jogSpeedRpm = 60;
    uint16_t accelTimeMs = 0;
    uint16_t lastLoadSpeedRpm = 0;

    String lastError;
    String lastErrorRaw;

    // ========================================
    // MOT D'ENTRÉES VIRTUELLES
    // ========================================
    uint16_t viWord = 0;
    uint32_t viChangeTime = 0;           // dernier changement du mot (millis)
    uint32_t viBitTime[16] = {};         // dernier changement de chaque bit
    uint32_t viConfirmedAt = 0;          // passe de refresh qui a confirmé le dernier changement
    bool viConfirmed = false;
    uint32_t viKeepaliveTime = 0;

    // ========================================
    // ÉTAT DÉCODÉ
    // ========================================
    uint16_t statusWord = 0;
    uint16_t alarmCodeValue = 0;
    bool readyBit = false;
    bool alarmBit = false;
    bool inPosBit = false;
    bool zeroSpeedBit = true;
    bool homeDoneBit = false;
    bool lastHasAlarms = false;
    bool prevAlarmBit = false;
    uint32_t alarmBitSince = 0;          // front montant du bit d'alarme (lecture du code accélérée)
    bool alarmCodeFast = false;
    uint32_t savedAlarmInterval = 0;

    // Échantillons de position (vitesse estimée pour l'arrêt par re-consigne)
    int32_t posSampleValue = 0;
    uint32_t posSampleTime = 0;
    int64_t velNum = 0;    // impulsions
    int64_t velDen = 0;    // ms

    // ========================================
    // SÉQUENCEUR DE CHARGEMENT
    // ========================================
    LoadState loadState = LoadState::IDLE;
    uint32_t loadWriteTime = 0;
    uint32_t loadPhaseTime = 0;
    uint32_t loadSetOnWireTime = 0;
    bool loadAccelWritten = false;

    // ========================================
    // DÉPLACEMENT
    // ========================================
    uint32_t moveCmdOnWireTime = 0;
    uint8_t moveStableCount = 0;
    int32_t movePrevPos = 0;
    uint32_t movePrevPosTime = 0;
    uint32_t lastMoveLog = 0;

    // ========================================
    // PRISE D'ORIGINE
    // ========================================
    uint32_t homingOnWireTime = 0;
    int32_t homingStartPos = 0;
    bool homingSeenHomeLow = false;
    bool homingSeenMotion = false;
    uint8_t homingStableCount = 0;
    int32_t homingPrevPos = 0;
    uint32_t homingPrevPosTime = 0;
    uint32_t lastHomingLog = 0;
    bool homeLatched = false;            // origine validée par une prise d'origine de cette session
    bool hadHomeDone = false;            // bit « origine faite » vu stable (détection de redémarrage)
    uint32_t homeBitHighSince = 0;

    // ========================================
    // JOG
    // ========================================
    int8_t jogDir = 0;                   // 0 = arrêt, +1 = avant, -1 = arrière
    uint8_t jogPhase = 0;
    uint32_t jogPhaseTime = 0;
    uint16_t jogCmdSpeed = 0;            // vitesse demandée (zone lente comprise)
    uint16_t jogAppliedSpeed = 0;        // vitesse transmise au variateur
    int32_t jogLimit = 0;
    bool jogLimitEnforced = false;
    int32_t jogTarget = 0;

    // ========================================
    // ARRÊT
    // ========================================
    bool pendingReleaseImmediateStop = false;  // verrou de 500 ms (stopMovement / emergencyStop)
    uint32_t immediateStopTimestamp = 0;
    bool movementCancelled = false;
    bool stopRetargetActive = false;           // re-consigne d'arrêt en cours de chargement
    bool settleActive = false;                 // attente de la vitesse nulle après un arrêt
    uint32_t settleStart = 0;
    uint32_t settleWireTime = 0;
    bool stopResetPulse = false;
    uint32_t stopResetPulseTime = 0;

    // ========================================
    // COMMUNICATION / DÉFAUTS
    // ========================================
    bool commLost = false;
    uint32_t lastCommMessage = 0;
    uint32_t waitModbusStartTime = 0;
    uint32_t lastWaitLog = 0;
    bool driveFaultPending = false;
    String driveFaultReason;
    bool fastPolling = false;
    uint32_t savedStatusInterval = 0;
    uint32_t savedPositionInterval = 0;
    uint32_t lastInitLog = 0;

    mutable char alarmDescBuf[48];

    // ========================================
    // MÉTHODES PRIVÉES
    // ========================================
    void construct();
    void processInitialize(uint32_t now);
    void processReset(uint32_t now);
    void processMove(uint32_t now);
    void processHoming(uint32_t now);
    void processJog(uint32_t now);
    void processStop(uint32_t now);

    void updateStatus(uint32_t now);
    void supervise(uint32_t now);
    void keepAlive(uint32_t now);
    void updateFastPolling();
    void updateOutputNodes();

    // Entrées virtuelles
    static uint16_t bitMask(uint8_t bit) { return bit < 16 ? (uint16_t)(1u << bit) : 0; }
    bool viBit(uint8_t bit) const { return (viWord & bitMask(bit)) != 0; }
    void setVi(uint8_t bit, bool on, uint32_t now);
    bool viBitOnWire(uint8_t bit, bool state) const;
    void clearCommandBits(uint32_t now);

    // Séquenceur
    void beginLoad(int32_t target, uint16_t rpm, uint32_t now);
    LoadState serviceLoad(uint32_t now);
    void cancelLoad() { loadState = LoadState::IDLE; }
    bool loadBusy() const { return loadState != LoadState::IDLE; }

    // Jog
    bool jogCommand(int8_t dir, int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed);
    bool jogUsesDriveBits() const;
    int32_t computeJogTarget() const;
    void stopJogMotion(uint32_t now);

    // Arrêt par re-consigne (variateur sans entrée d'arrêt : A6). La cible d'arrêt est
    // bornée entre la position courante et la cible du mouvement interrompu.
    void beginRetargetStop(uint32_t now, int32_t interruptedTarget);
    int32_t estimatedStopPosition(uint32_t now) const;
    void beginSettle(uint32_t now);

    // Utilitaires
    bool hasAlarmRaw() const;
    bool faultNow() const;               // alarme variateur ou défaut logiciel, sans délai
    bool alarmCodePending() const;       // bit d'alarme vu, code pas encore relu
    bool isAtPosition(int32_t target) const;
    static bool after(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }
    static bool fresh(Node* node, uint32_t t) { return node && after(node->getLastRefresh(), t); }
    uint16_t getSlaveAddress() const;
    bool isPlcSuspended() const;
    bool motionActive() const;
    void notifyDriveFault(const char* reason);
    void failMotion(const char* reason);
    void setError(const char* msg);
    void logf(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void forceWrite16(Uint16OutputNode* node, uint16_t value);
    void forceWrite32(Uint32OutputNode* node, int32_t value);
    const char* findAlarmText(uint16_t code) const;
};

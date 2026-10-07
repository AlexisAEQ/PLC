#pragma once

// ============================================================================
// STEPPERONLINE SERVO — variateurs StepperOnline pilotés en Modbus RTU (RS485)
// ============================================================================
//
// Une seule classe pour deux familles StepperOnline qui n'ont PAS le même jeu de registres :
//   - A6-RS : paramètres Cgg.nn, adresse Modbus = (gg << 8) | nn, gg et nn en hexadécimal
//             (C04.11 -> 0x0411, C11.06 -> 0x1106) ; surveillance U40.nn -> 0x40nn.
//             Pilotage recommandé par StepperOnline : la LOGIQUE des entrées DI (C04.01,
//             C04.05...) est écrite par Modbus (en RAM) pour activer leur fonction sans
//             câblage ; positionnement par la planification de positions (groupe 1 de C11).
//   - T6 (version RS485, ex. T6-1000RS) : paramètres Prx.yy de type Leadshine
//             (Prx.yy -> x * 0x100 + 2 * yy + 1, ex. Pr3.04 -> 0x0309) et mode PR
//             (Pr8.xx -> 0x6000 + xx, Pr9.xx -> 0x6200 + xx) : chemin PR0 écrit puis
//             déclenché par Pr8.02 (0x6002) = 0x0010.
// Ni l'une ni l'autre n'est compatible avec les registres Lichuan (PA_xxx ou Pgg-nn) :
// voir docs/axes/stepperonline-servo.md.
//
// Les ADRESSES ne sont pas dans ce code : elles sont dans les fichiers matériels
// data/hardware/StepperOnline_A6RS.json et StepperOnline_T6.json. La classe métier (ou le
// code généré par PLC Studio) passe les pointeurs de nodes dans un StepperOnlineServoNodes.
//
// La classe n'écrit que des registres de fonctionnement en RAM (logique des DI, chemin
// PR0, commandes). Le paramétrage du variateur (mode de commande, fonctions des DI/DO,
// mode de prise d'origine, bus) se fait UNE FOIS à la mise en service : voir la liste de
// contrôle de docs/axes/stepperonline-servo.md. Aucune écriture EEPROM.
//
// Unités : positions en unités de commande du variateur (impulsions par tour :
//          A6-RS C00.02, T6 Pr0.08 ; 10000 par défaut), vitesses en tr/min.
//
// Contrats identiques à LichuanServo / SureServo (process() à chaque cycle, machines à
// états non bloquantes, OperationState / OperationStatus, verrou de 500 ms après
// stopMovement()...). Aucun état statique : plusieurs instances (plusieurs axes) possibles.

#include <Arduino.h>
#include "Node/Node.h"
#include "ServoDrive/ServoDrive.h"

// Famille de variateur (profil de commande et valeurs par défaut)
enum class StepperOnlineFamily : uint8_t {
    A6RS = 0,   // A6-RS  : Cgg.nn, commandes par logique des DI, planification de positions C11
    T6   = 1    // T6 RS485 : Prx.yy (type Leadshine), mode PR (0x6000 / 0x6200)
};

// ========================================
// OPTIONS
// ========================================
// StepperOnlineOptions::defaultsFor(famille) correspond au paramétrage décrit dans
// docs/axes/stepperonline-servo.md. Un bit d'état à NONE n'est pas utilisé.
struct StepperOnlineOptions {
    static constexpr uint8_t NONE = 0xFF;

    // --- Registres de commande « entrée » (A6-RS : logique des DI C04.(4n-3))
    uint16_t inputActiveValue   = 1;      // logique qui rend la fonction de la DI active sans câblage
    uint16_t inputInactiveValue = 0;
    // Servo ON (node servoOn) : A6-RS logique de DI5 (1 / 0) ;
    // T6 fonction de SI1 Pr4.00 : 0x83 (SRV-ON normalement fermé = actif sans câblage) / 0x03
    uint16_t servoOnValue  = 1;
    uint16_t servoOffValue = 0;

    // --- T6 : codes du registre de commande PR Pr8.02 (0x6002) et du mot de commande 0x1801
    uint16_t cmdStartPath    = 0x0010;  // déclenche le chemin PR0
    uint16_t cmdHoming       = 0x0020;  // prise d'origine (mode Pr8.10)
    uint16_t cmdStop         = 0x0040;  // arrêt rapide du chemin en cours (le servo reste actif)
    uint16_t alarmResetValue = 0x1111;  // 0x1801 <- 0x1111 : acquittement de l'alarme courante
    uint16_t pathModeWord    = 0x0011;  // Pr9.00 : positionnement absolu (1) + interruption INS (bit 4)

    // --- Mot d'état (node status)
    //     A6-RS : U40.05 état des DO, bit n = DO(n+1) ; T6 (optionnel) : registre d'état du variateur
    uint8_t stReady      = 0;           // A6-RS DO1 = 1 servo prêt
    uint8_t stAlarm      = 1;           // A6-RS DO2 = 4 défaut
    uint8_t stInPosition = 2;           // A6-RS DO3 = 7 position atteinte
    uint8_t stHomeDone   = 3;           // A6-RS DO4 = 9 prise d'origine terminée
    uint16_t statusInvertMask = 0;      // XOR appliqué au mot d'état

    // --- Comportement
    bool highWordFirst = false;         // mots 32 bits : T6 mot haut d'abord (Pr9.01 H, Pr9.02 L)
    bool alarmFromCode = true;          // code d'alarme != 0 => alarme (si le node alarmCode existe)
    bool servoOffDuringReset = true;    // coupe le servo pendant l'acquittement d'une alarme active
    bool fastPollingDuringMotion = true;// lecture rapide de l'état et de la position pendant un mouvement

    uint16_t maxSpeedRpm = 3000;        // borne haute des vitesses (moteurs A6 / T6 : 3000 tr/min nominal)
    uint16_t maxAccelMs = 65535;        // borne haute du temps de rampe écrit
    uint16_t defaultMoveSpeedRpm = 100; // vitesse de déplacement avant le premier setMoveSpeed()
    uint16_t defaultJogSpeedRpm = 60;   // vitesse de jog avant le premier setJogSpeed()
    uint16_t torqueUnitsPerPercent = 1; // T6 Pr0.13 : 1 = 1 % du couple nominal
    uint16_t maxTorqueValue = 300;      // borne haute de la valeur écrite dans torqueLimit
    uint32_t positionTolerance = 100;   // écart admis pour « position atteinte » (unités de commande)
    uint32_t stillUnitsPerSecond = 1000;// sans node actualSpeed : vitesse en dessous de laquelle l'axe
                                        // est « arrêté » (1000 = 6 tr/min à 10000 unités/tour)
    uint32_t jogWindow = 100000;        // jog par déplacement : fenêtre glissante (unités de commande)

    static StepperOnlineOptions defaultsFor(StepperOnlineFamily family);
};

// ========================================
// NODES
// ========================================
// Ids des fichiers matériels entre crochets : R = ModbusReadHoldingRegister,
// W = ModbusWriteHoldingRegister, DR / DW = sections 32 bits (Dobble).
struct StepperOnlineServoNodes {
    // === OBLIGATOIRES (deux familles) ===
    Uint32OutputNode* targetPosition = nullptr;  // [DW1] A6 C11.06 (0x1106) déplacement du groupe 1 / T6 Pr9.01-9.02 (0x6201) position PR0
    Uint16OutputNode* moveSpeed      = nullptr;  // [W2]  A6 C11.08 (0x1108) / T6 Pr9.03 (0x6203), tr/min
    Uint32InputNode*  position       = nullptr;  // [DR1] A6 U40.16 (0x4016) position absolue / T6 Pr8.42-8.43 (0x602A) position commande

    // === OBLIGATOIRES A6-RS ===
    Uint16OutputNode* servoOn        = nullptr;  // [W1]  A6 C04.11 (0x0411) logique DI5 / T6 Pr4.00 (0x0401) fonction SI1 (optionnel T6)
    Uint16OutputNode* startMove      = nullptr;  // [W5]  A6 C04.01 (0x0401) logique DI1 (fonction 19, départ planification)
    Uint16InputNode*  status         = nullptr;  // [R1]  A6 U40.05 (0x4005) état des DO / T6 optionnel

    // === OBLIGATOIRES T6 ===
    Uint16OutputNode* command        = nullptr;  // [W8]  T6 Pr8.02 (0x6002) commande PR (0x10 chemin 0, 0x20 origine, 0x40 arrêt)
    Uint16InputNode*  prStatus       = nullptr;  // [R4]  T6 Pr8.02 (0x6002) relu : état du chemin PR

    // === OPTIONNELS (nullptr si absents) ===
    Uint16OutputNode* accelTime      = nullptr;  // [W3]  A6 C11.0A (0x110A) ms / T6 Pr9.04 (0x6204) ms pour 1000 tr/min
    Uint16OutputNode* decelTime      = nullptr;  // [W4]  A6 C11.0C (0x110C) ms / T6 Pr9.05 (0x6205)
    Uint16OutputNode* homingStart    = nullptr;  // [W6]  A6 C04.09 (0x0409) logique DI3 (fonction 25, départ prise d'origine)
    Uint16OutputNode* alarmReset     = nullptr;  // [W7]  A6 C04.05 (0x0405) logique DI2 (fonction 2) / T6 0x1801 mot de commande
    Uint16OutputNode* pathMode       = nullptr;  // [W9]  T6 Pr9.00 (0x6200) mode du chemin PR0 (écrit à l'initialisation)
    Uint16OutputNode* torqueLimit    = nullptr;  // [W10] T6 Pr0.13 (0x001B) 1re limite de couple, %
    Uint16InputNode*  alarmCode      = nullptr;  // [R2]  T6 0x0B03 alarme courante (supposé) ; A6 : registre non identifié
    Uint16InputNode*  actualSpeed    = nullptr;  // [R3]  vitesse moteur, tr/min signés (adresses à vérifier, cf. doc)
    Uint16InputNode*  servoOnReadback = nullptr; // [R5]  relecture du registre servoOn (détection d'un redémarrage du variateur)
    Uint32InputNode*  feedbackPosition = nullptr;// [DR2] T6 0x602C position moteur (diagnostic)

    // États décodés (sorties virtuelles pour l'IHM)
    BooleanOutputNode* servoReady       = nullptr;
    BooleanOutputNode* inPosition       = nullptr;
    BooleanOutputNode* zeroSpeed        = nullptr;
    BooleanOutputNode* homeDone         = nullptr;
    BooleanOutputNode* servoAlarm       = nullptr;
    BooleanOutputNode* driveInitialised = nullptr;
    BooleanOutputNode* modbusError      = nullptr;

    // Nodes obligatoires d'une famille donnée
    bool validateRequired(StepperOnlineFamily family) const {
        bool common = targetPosition != nullptr && moveSpeed != nullptr && position != nullptr;
        if (family == StepperOnlineFamily::T6) {
            return common && command != nullptr && prStatus != nullptr;
        }
        return common && servoOn != nullptr && startMove != nullptr && status != nullptr;
    }

    // Sans famille : vrai si le jeu de nodes est complet pour l'une des deux familles
    // (le constructeur vérifie ensuite la famille réelle).
    bool validateRequired() const {
        return validateRequired(StepperOnlineFamily::A6RS) || validateRequired(StepperOnlineFamily::T6);
    }

    size_t countOptional() const {
        const void* opts[] = { accelTime, decelTime, homingStart, alarmReset, pathMode, torqueLimit,
                               alarmCode, actualSpeed, servoOnReadback, feedbackPosition,
                               servoReady, inPosition, zeroSpeed, homeDone, servoAlarm,
                               driveInitialised, modbusError };
        size_t n = 0;
        for (const void* p : opts) if (p) n++;
        return n;
    }
};

// ========================================
// CLASSE STEPPERONLINESERVO
// ========================================
class StepperOnlineServo : public ServoDrive {
public:
    StepperOnlineServo(StepperOnlineFamily family, const StepperOnlineServoNodes& nodes,
                       const char* servoId = "STEPPERONLINE");
    StepperOnlineServo(StepperOnlineFamily family, const StepperOnlineServoNodes& nodes,
                       const StepperOnlineOptions& options, const char* servoId = "STEPPERONLINE");

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
    // SPÉCIFIQUE STEPPERONLINE
    // ========================================
    StepperOnlineFamily getFamily() const { return family; }
    const StepperOnlineOptions& getOptions() const { return opt; }
    static const char* getFamilyName(StepperOnlineFamily f);

    int32_t getMoveSpeed() const { return moveSpeedRpm; }
    // Temps de rampe écrit au début de chaque déplacement (0 = réglage du variateur conservé).
    // A6-RS : ms (C11.0A / C11.0C) ; T6 : ms pour 0 -> 1000 tr/min (Pr9.04 / Pr9.05).
    bool setAccelTime(uint16_t ms);
    uint16_t getAccelTime() const { return accelTimeMs; }

    bool getServoReadyBit() const { return readyBit; }
    bool getInPosition() const { return inPosBit; }
    bool getZeroSpeed() const { return zeroSpeedBit; }
    bool isCommunicationLost() const { return commLost; }
    bool isHomingInProgress() const { return homingState == OperationState::DRIVE_IN_PROGRESS; }
    bool isJogging() const { return jogDir != 0; }
    uint16_t getStatusWord() const { return statusWord; }
    uint16_t getPrStatus() const { return prStatusValue; }

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
    static constexpr uint8_t  STABLE_READINGS            = 3;
    static constexpr int32_t  ZERO_SPEED_RPM             = 10;

    // Séquenceur de chargement de consigne : cible + vitesse (+ rampe) écrites et confirmées
    // sur le bus, puis départ (A6-RS : impulsion sur la logique de DI1 ; T6 : 0x6002 = 0x10).
    enum class LoadState : uint8_t { IDLE, WRITING, CLEAR_EDGE, PULSE, CLEAR_AFTER_PULSE, DONE, FAILED };

    // Registre de commande à deux états (servo ON, départ, origine, acquittement A6-RS) :
    // valeur demandée et instant du dernier changement (confirmation sur le bus).
    struct Channel {
        Uint16OutputNode* node = nullptr;
        uint16_t onValue = 1;
        uint16_t offValue = 0;
        bool state = false;
        uint32_t changeTime = 0;
    };

    // ========================================
    // CONFIGURATION
    // ========================================
    const StepperOnlineFamily family;
    const StepperOnlineOptions opt;
    const StepperOnlineServoNodes nodes;
    bool nodesOk = false;
    Uint16InputNode* heartbeat = nullptr;     // lecture qui sert à surveiller la communication

    Channel chServo, chStart, chHoming, chReset;

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
    // ÉTAT DÉCODÉ
    // ========================================
    uint16_t statusWord = 0;
    uint16_t prStatusValue = 0;
    uint16_t alarmCodeValue = 0;
    bool readyBit = false;
    bool alarmBit = false;
    bool inPosBit = false;
    bool zeroSpeedBit = true;
    bool homeDoneBit = false;
    bool prIdle = true;                  // T6 : chemin PR terminé (Pr8.02 relu = 0x000P)
    bool lastHasAlarms = false;

    // Échantillons de position (vitesse estimée : arrêt par re-consigne, vitesse nulle)
    int32_t posSampleValue = 0;
    uint32_t posSampleTime = 0;
    int64_t velNum = 0;    // unités de commande
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
    uint32_t homingCmdTime = 0;          // écriture de la commande (T6) / du niveau (A6-RS)
    uint32_t homingOnWireTime = 0;
    int32_t homingStartPos = 0;
    bool homingSeenHomeLow = false;
    bool homingSeenMotion = false;
    bool homingSeenBusy = false;         // T6 : chemin PR vu en cours
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
    bool stopRetargetActive = false;           // A6-RS : re-consigne d'arrêt en cours de chargement
    bool settleActive = false;                 // attente de la vitesse nulle après un arrêt
    uint32_t settleStart = 0;
    uint32_t settleWireTime = 0;
    bool stopClearSent = false;                // T6 : 0x1111 écrit après un arrêt rapide resté verrouillé
    uint32_t stopClearTime = 0;

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
    uint32_t savedHeartbeatInterval = 0;
    uint32_t savedPositionInterval = 0;
    uint32_t lastInitLog = 0;
    uint32_t servoOnConfirmedAt = 0;           // passe de refresh qui a confirmé la dernière écriture servoOn

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
    void updateFastPolling();
    void updateOutputNodes();

    // Registres de commande
    void initChannel(Channel& ch, Uint16OutputNode* node, uint16_t onValue, uint16_t offValue);
    void setChannel(Channel& ch, bool on, uint32_t now, bool force = false);
    bool channelOnWire(const Channel& ch, bool state) const;
    void sendCommand(uint16_t code, uint32_t now);     // T6 : écriture forcée de Pr8.02
    void clearCommandChannels(uint32_t now);
    bool isT6() const { return family == StepperOnlineFamily::T6; }

    // Séquenceur
    void beginLoad(int32_t target, uint16_t rpm, uint32_t now);
    LoadState serviceLoad(uint32_t now);
    void cancelLoad() { loadState = LoadState::IDLE; }
    bool loadBusy() const { return loadState != LoadState::IDLE; }

    // Jog
    bool jogCommand(int8_t dir, int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed);
    int32_t computeJogTarget() const;
    void stopJogMotion(uint32_t now);

    // Arrêts
    void beginDriveStop(uint32_t now, int32_t interruptedTarget);
    int32_t estimatedStopPosition(uint32_t now) const;
    void beginSettle(uint32_t now);

    // Utilitaires
    uint32_t toWire32(int32_t v) const;
    int32_t fromWire32(uint32_t raw) const;
    bool motionActive() const;
    bool hasAlarmRaw() const;
    bool faultNow() const;               // alarme variateur ou défaut logiciel, sans délai
    bool isAtPosition(int32_t target) const;
    static bool after(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }
    static bool fresh(Node* node, uint32_t t) { return node && after(node->getLastRefresh(), t); }
    uint16_t getSlaveAddress() const;
    bool isPlcSuspended() const;
    void notifyDriveFault(const char* reason);
    void failMotion(const char* reason);
    void setError(const char* msg);
    void logf(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void forceWrite16(Uint16OutputNode* node, uint16_t value);
    void forceWrite32(Uint32OutputNode* node, int32_t value);
};

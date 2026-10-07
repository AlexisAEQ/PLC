#pragma once

// ============================================================================
// STEPPERONLINE RS — variateurs pas-à-pas Modbus RTU (RS485) à mode PR
// ============================================================================
//
// Une seule classe pour les quatre variateurs « RS » de StepperOnline, qui reprennent le
// protocole Modbus des variateurs Leadshine RS (paramètres Prx.yy, table de trajets « PR »
// Pr8.xx / Pr9.xx) :
//   - boucle ouverte : DM556RS (Nema 23/24), DM882RS (Nema 34) ;
//   - boucle fermée  : CL57RS  (Nema 23/24), CL86RS  (Nema 34).
//
// Principe : le variateur contient son propre contrôleur de position. L'automate écrit le
// trajet PR0 (mode, position cible 32 bits, vitesse, rampes : 0x6200..0x6205) puis
// déclenche le trajet en écrivant 0x10 dans le registre de déclenchement Pr8.02 (0x6002).
// Prise d'origine : 0x20 (ou 0x21 = position actuelle prise comme origine) ; arrêt
// rapide : 0x40 (décélération Pr8.23). Le mot d'état 0x1003, le registre de déclenchement
// relu, le code d'alarme 0x2203 et la position commande Pr8.42/43 donnent l'état.
//
// Les ADRESSES ne sont pas dans ce code : elles sont dans les fichiers matériels
// data/hardware/StepperOnline_DM_RS.json et StepperOnline_CL_RS.json. La classe métier passe
// les pointeurs de nodes dans un StepperOnlineRSNodes (comme LichuanNodes).
//
// Mots 32 bits : le variateur range le mot de POIDS FORT à l'adresse basse (Pr9.01 = position
// haute, Pr9.02 = position basse) alors que les nodes « Dobble » du firmware envoient et
// lisent le mot de POIDS FAIBLE à l'adresse basse. La classe échange donc les deux mots
// (option highWordFirst) : la valeur du node n'est pas la position, utilisez
// getActualPosition().
//
// Unités : positions en PAS (impulsions de commande, Pr0.00 « impulsions par tour »),
//          vitesses (déplacement et jog) en tr/min, rampes en ms pour 0 -> 1000 tr/min.
//
// Le paramétrage du variateur (adresse, vitesse et format du bus, impulsions par tour,
// courant, mode de prise d'origine, entrées) se fait UNE FOIS à la mise en service : voir
// docs/axes/stepperonline-rs.md. Aucune écriture EEPROM à l'exécution.
//
// Contrats identiques à LichuanServo / SureServo (process() à chaque cycle, machines à états
// non bloquantes, verrou de 500 ms après stopMovement()...). Aucun état statique : plusieurs
// instances (plusieurs axes) sont possibles.

#include <Arduino.h>
#include "Node/Node.h"
#include "ServoDrive/ServoDrive.h"

// Modèle de variateur (valeurs par défaut des options)
enum class StepperOnlineRSModel : uint8_t {
    DM556RS = 0,   // boucle ouverte, 24-48 VDC, 0,1-5,6 A
    DM882RS = 1,   // boucle ouverte, 30-110 VDC / 18-80 VAC, 2,1-8,2 A
    CL57RS  = 2,   // boucle fermée, 24-48 VDC, 0,5-7,0 A
    CL86RS  = 3    // boucle fermée, 30-110 VDC / 18-80 VAC, 2,1-8,0 A
};

// ========================================
// OPTIONS
// ========================================
// StepperOnlineRSOptions::defaultsFor(modèle) donne le réglage correspondant à la mise en
// service décrite dans docs/axes/stepperonline-rs.md. Un bit à NONE n'est pas utilisé.
struct StepperOnlineRSOptions {
    static constexpr uint8_t NONE = 0xFF;

    // --- Protocole
    bool highWordFirst = true;      // mots 32 bits : poids fort à l'adresse basse (protocole Leadshine RS)
    uint16_t prMode = 0x0001;       // Pr9.00 du trajet PR0 : positionnement (1), absolu (bit 6 = 0), sans saut

    // --- Mot d'état 0x1003 (bit n = 1 : vrai)
    uint8_t stFault    = 0;         // défaut
    uint8_t stEnabled  = 1;         // moteur alimenté (enable)
    uint8_t stRunning  = 2;         // en mouvement
    uint8_t stCmdDone  = 4;         // commande terminée
    uint8_t stPathDone = 5;         // trajet terminé
    uint8_t stHomeDone = 6;         // prise d'origine terminée
    bool requireEnableBit = true;   // « prêt » exige le bit enable (false : DM-RS sans entrée ENA câblée...)

    // --- Comportement
    bool homingSetsCurrentPosition = false; // true : prise d'origine = 0x21 (position actuelle = 0, sans mouvement)
    bool disableOnEmergency = false;        // emergencyStop() coupe aussi le courant (Pr0.07 = 0, node softwareEnable)
    bool resetAfterQuickStop = true;        // après un arrêt rapide, 0x1111 si le registre de déclenchement reste à 0x40
    bool fastPollingDuringMotion = true;    // lecture rapide de l'état et de la position pendant un mouvement

    // --- Limites et valeurs par défaut
    uint16_t maxSpeedRpm = 3000;         // borne haute des vitesses (déplacement et jog)
    uint16_t defaultMoveSpeedRpm = 120;  // vitesse de déplacement avant le premier setMoveSpeed()
    uint16_t defaultJogSpeedRpm = 60;    // vitesse de jog avant le premier setJogSpeed()
    uint16_t defaultAccelMs = 300;       // rampe écrite à chaque déplacement, ms pour 0 -> 1000 tr/min (0 = réglage du variateur)
    uint16_t maxAccelMs = 10000;         // borne haute de la rampe
    uint16_t quickStopMs = 0;            // Pr8.23 écrit à l'initialisation, ms (0 = réglage du variateur conservé)
    uint16_t maxPeakCurrent = 56;        // borne du courant crête écrit par setMaxTorque(), 0,1 A (modèle)
    uint16_t torqueRefCurrent = 0;       // courant crête du moteur = 100 % de setMaxTorque(), 0,1 A (0 = couple non géré)
    uint8_t minTorquePercent = 20;       // plancher de setMaxTorque() (le moteur garde un couple de maintien)
    uint32_t positionTolerance = 10;     // écart admis pour « position atteinte », pas
    uint32_t stepsPerRev = 10000;        // Pr0.00 (impulsions par tour) : sert à dimensionner la fenêtre de jog
    uint32_t jogWindowMinSteps = 20000;  // jog : fenêtre glissante minimale, pas
    uint16_t jogWindowMs = 2000;         // jog : fenêtre glissante = distance parcourue en ce temps à la vitesse de jog

    static StepperOnlineRSOptions defaultsFor(StepperOnlineRSModel model);
};

// ========================================
// NODES
// ========================================
// Ids des fichiers matériels (data/hardware/StepperOnline_DM_RS.json / _CL_RS.json) entre crochets.
struct StepperOnlineRSNodes {
    // === OBLIGATOIRES ===
    Uint16OutputNode* prMode         = nullptr;  // [W1]  Pr9.00 0x6200 mode du trajet PR0
    Uint16OutputNode* moveSpeed      = nullptr;  // [W2]  Pr9.03 0x6203 vitesse du trajet PR0, tr/min
    Uint16OutputNode* trigger        = nullptr;  // [W5]  Pr8.02 0x6002 déclenchement (0x10 PR0, 0x20 origine, 0x21 zéro, 0x40 arrêt)
    Uint16OutputNode* controlWord    = nullptr;  // [W6]  0x1801 mot de commande (0x1111 acquittement alarme)
    Uint32OutputNode* targetPosition = nullptr;  // [DW1] Pr9.01/9.02 0x6201 position du trajet PR0, pas (poids fort d'abord)
    Uint16InputNode*  status         = nullptr;  // [R1]  0x1003 état du mouvement
    Uint16InputNode*  alarmCode      = nullptr;  // [R2]  0x2203 alarme courante (0 = aucune)
    Uint16InputNode*  triggerStatus  = nullptr;  // [R3]  Pr8.02 0x6002 relu (0x000P terminé, 0x010P en cours...)
    Uint32InputNode*  position       = nullptr;  // [DR1] Pr8.42/8.43 0x602A position commande, pas (poids fort d'abord)

    // === OPTIONNELS (nullptr si absents) ===
    Uint16OutputNode* accelTime      = nullptr;  // [W3]  Pr9.04 0x6204 accélération PR0, ms/1000 tr/min
    Uint16OutputNode* decelTime      = nullptr;  // [W4]  Pr9.05 0x6205 décélération PR0, ms/1000 tr/min
    Uint16OutputNode* softwareEnable = nullptr;  // [W7]  Pr0.07 0x000F alimentation forcée par logiciel (CL-RS)
    Uint16OutputNode* quickStopTime  = nullptr;  // [W8]  Pr8.23 0x6017 décélération de l'arrêt rapide, ms
    Uint16OutputNode* peakCurrent    = nullptr;  // [W9]  Pr5.00 0x0191 courant crête, 0,1 A (setMaxTorque)
    Uint16InputNode*  prWarning      = nullptr;  // [R4]  Pr8.29 0x601D avertissement PR (0x100 / 0x102 : défaut d'origine)
    Uint16InputNode*  softwareEnableReadback = nullptr; // [R5] Pr0.07 relu (détection d'un redémarrage du variateur)
    Uint16InputNode*  actualSpeed    = nullptr;  // [R6]  0x1047 vitesse de retour (mot bas), tr/min signés (diagnostic)
    Uint32InputNode*  feedbackPosition = nullptr; // [DR2] Pr8.44/8.45 0x602C position réelle (CL-RS, diagnostic)

    // États décodés (sorties virtuelles pour l'IHM)
    BooleanOutputNode* servoReady       = nullptr;
    BooleanOutputNode* inPosition       = nullptr;
    BooleanOutputNode* zeroSpeed        = nullptr;
    BooleanOutputNode* homeDone         = nullptr;
    BooleanOutputNode* servoAlarm       = nullptr;
    BooleanOutputNode* driveInitialised = nullptr;
    BooleanOutputNode* modbusError      = nullptr;

    bool validateRequired() const {
        return prMode != nullptr &&
               moveSpeed != nullptr &&
               trigger != nullptr &&
               controlWord != nullptr &&
               targetPosition != nullptr &&
               status != nullptr &&
               alarmCode != nullptr &&
               triggerStatus != nullptr &&
               position != nullptr;
    }

    size_t countOptional() const {
        size_t n = 0;
        if (accelTime) n++;
        if (decelTime) n++;
        if (softwareEnable) n++;
        if (quickStopTime) n++;
        if (peakCurrent) n++;
        if (prWarning) n++;
        if (softwareEnableReadback) n++;
        if (actualSpeed) n++;
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
// CLASSE STEPPERONLINERS
// ========================================
class StepperOnlineRS : public ServoDrive {
public:
    StepperOnlineRS(StepperOnlineRSModel model, const StepperOnlineRSNodes& nodes, const char* servoId = "STEPPERONLINE");
    StepperOnlineRS(StepperOnlineRSModel model, const StepperOnlineRSNodes& nodes, const StepperOnlineRSOptions& options,
                    const char* servoId = "STEPPERONLINE");

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
    bool setServoEnabled(bool enable) override;    // node softwareEnable requis (CL-RS)
    bool setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) override; // speed en tr/min, rampIndex ignoré
    bool setMaxTorque(uint8_t torquePercent) override;               // réduit le courant crête (voir torqueRefCurrent)
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

    int32_t getActualPosition() const override;    // pas (position commande Pr8.42/43)
    bool setMoveSpeed(int32_t speed) override;     // tr/min, borné à 1..maxSpeedRpm
    bool isImmediateStopActive() const override;
    const char* getAlarmDescription() const override;

    bool handleInitializing(OperationStatus& status) override;
    bool handleHoming(OperationStatus& status, uint32_t homingSpeed = 0, uint32_t timeoutMs = 0) override;
    bool handleJogging(OperationStatus& status) override;
    bool handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync = true, uint32_t timeoutMs = 60000) override;

    // ========================================
    // SPÉCIFIQUE STEPPERONLINE RS
    // ========================================
    StepperOnlineRSModel getModel() const { return model; }
    const StepperOnlineRSOptions& getOptions() const { return opt; }
    static const char* getModelName(StepperOnlineRSModel m);
    static bool isClosedLoop(StepperOnlineRSModel m) {
        return m == StepperOnlineRSModel::CL57RS || m == StepperOnlineRSModel::CL86RS;
    }

    int32_t getMoveSpeed() const { return moveSpeedRpm; }
    // Rampe du trajet PR0, ms pour 0 -> 1000 tr/min, écrite à chaque déplacement et jog
    // (nodes accelTime/decelTime requis ; 0 = rampe réglée dans le variateur).
    bool setAccelTime(uint16_t ms);
    uint16_t getAccelTime() const { return accelTimeMs; }
    // Courant crête du moteur (0,1 A) correspondant à 100 % de setMaxTorque() ; 0 = couple non géré.
    bool setTorqueReferenceCurrent(uint16_t deciAmps);
    // Position réelle (codeur, CL-RS) si le node feedbackPosition est présent, sinon position commande.
    int32_t getFeedbackPosition() const;

    bool getInPosition() const { return inPosBit; }
    bool getZeroSpeed() const { return !runningBit; }
    bool isCommunicationLost() const { return commLost; }
    bool isHomingInProgress() const { return homingState == OperationState::DRIVE_IN_PROGRESS; }
    bool isJogging() const { return jogDir != 0; }
    uint16_t getStatusWord() const { return statusWord; }
    uint16_t getTriggerStatus() const { return triggerStatusValue; }

    // Défaut logiciel détecté (perte de communication, redémarrage du variateur, arrêt non
    // exécuté) : reste signalé par getHasAlarms() jusqu'au prochain startReset().
    bool hasPendingDriveFault() const { return driveFaultPending; }
    void acknowledgeDriveFault() { driveFaultPending = false; }

    bool clearError();
    void printStatus() const;

private:
    // ========================================
    // CONSTANTES
    // ========================================
    static constexpr uint32_t INIT_TIMEOUT_MS            = 15000;
    static constexpr uint32_t ENABLE_SETTLE_MS           = 300;
    static constexpr uint32_t RESET_TIMEOUT_MS           = 5000;
    static constexpr uint32_t RESET_STABILIZE_MS         = 500;
    static constexpr uint32_t RESET_VERIFY_MS            = 2000;
    static constexpr uint32_t RESET_STABLE_MS            = 200;
    static constexpr uint32_t MOVE_TIMEOUT_MS            = 60000;
    static constexpr uint32_t HOMING_TIMEOUT_MS          = 100000;
    static constexpr uint32_t HOMING_MIN_MS              = 300;
    static constexpr uint32_t HOMING_NO_MOTION_ACCEPT_MS = 2000;
    static constexpr uint32_t WRITE_CONFIRM_TIMEOUT_MS   = 2000;
    static constexpr uint32_t MIN_DONE_MS                = 100;
    static constexpr uint32_t IDLE_OFF_TARGET_MS         = 1000;
    static constexpr uint32_t IMMEDIATE_STOP_DURATION_MS = 500;
    static constexpr uint32_t STOP_SETTLE_TIMEOUT_MS     = 3000;
    static constexpr uint32_t COMM_LOSS_MS               = 5000;
    static constexpr uint32_t COMM_MESSAGE_INTERVAL_MS   = 10000;
    static constexpr uint32_t ALARM_CODE_WAIT_MS         = 500;
    static constexpr uint8_t  STABLE_READINGS            = 3;

    // Valeurs du registre de déclenchement Pr8.02 (0x6002)
    static constexpr uint16_t TRIG_PR0        = 0x0010;  // départ du trajet PR0
    static constexpr uint16_t TRIG_HOMING     = 0x0020;  // prise d'origine
    static constexpr uint16_t TRIG_SET_ZERO   = 0x0021;  // position actuelle = origine
    static constexpr uint16_t TRIG_QUICK_STOP = 0x0040;  // arrêt rapide (décélération Pr8.23)
    // Mot de commande 0x1801
    static constexpr uint16_t CW_ALARM_RESET  = 0x1111;

    // Séquenceur de chargement du trajet PR0 : registres écrits et confirmés sur le bus,
    // puis écriture du déclenchement.
    enum class LoadState : uint8_t { IDLE, WRITING, TRIGGER, DONE, FAILED };

    // ========================================
    // CONFIGURATION
    // ========================================
    const StepperOnlineRSModel model;
    const StepperOnlineRSOptions opt;
    const StepperOnlineRSNodes nodes;
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
    bool enableWritten = false;          // Pr0.07 = 1 écrit par l'initialisation

    int32_t targetPosition = 0;
    uint16_t moveSpeedRpm = 120;
    uint16_t jogSpeedRpm = 60;
    uint16_t accelTimeMs = 0;
    uint16_t torqueRefCurrent = 0;
    uint16_t lastLoadSpeedRpm = 0;

    String lastError;
    String lastErrorRaw;

    // ========================================
    // ÉTAT DÉCODÉ
    // ========================================
    uint16_t statusWord = 0;
    uint16_t alarmCodeValue = 0;
    uint16_t triggerStatusValue = 0;
    bool faultBit = false;
    bool enabledBit = false;
    bool runningBit = false;
    bool inPosBit = false;
    bool homeDoneBit = false;
    bool lastHasAlarms = false;
    bool prevFaultBit = false;
    uint32_t faultBitSince = 0;          // front montant du bit de défaut (lecture du code accélérée)
    bool alarmCodeFast = false;
    uint32_t savedAlarmInterval = 0;

    // ========================================
    // SÉQUENCEUR DE CHARGEMENT
    // ========================================
    LoadState loadState = LoadState::IDLE;
    uint32_t loadWriteTime = 0;
    uint32_t loadTriggerTime = 0;
    uint32_t loadOnWireTime = 0;
    bool loadAccelWritten = false;

    // ========================================
    // DÉPLACEMENT
    // ========================================
    uint32_t moveCmdOnWireTime = 0;
    uint8_t moveStableCount = 0;
    int32_t movePrevPos = 0;
    uint32_t movePrevPosTime = 0;
    uint32_t moveIdleOffTargetSince = 0;
    uint32_t lastMoveLog = 0;

    // ========================================
    // PRISE D'ORIGINE
    // ========================================
    uint32_t homingWriteTime = 0;
    uint32_t homingOnWireTime = 0;
    int32_t homingStartPos = 0;
    bool homingSeenHomeLow = false;
    bool homingSeenMotion = false;
    uint8_t homingStableCount = 0;
    int32_t homingPrevPos = 0;
    uint32_t homingPrevPosTime = 0;
    uint32_t lastHomingLog = 0;
    bool homeLatched = false;            // origine validée par une prise d'origine de cette session

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
    bool settleActive = false;                 // attente de l'arrêt de l'axe après un arrêt rapide
    uint32_t settleStart = 0;
    uint32_t settleWireTime = 0;
    bool stopResetPending = false;             // acquittement 0x1111 après arrêt rapide
    uint32_t stopResetTime = 0;

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
    uint32_t savedTriggerInterval = 0;
    uint32_t lastInitLog = 0;
    uint32_t settingsWriteTime = 0;      // dernière écriture de Pr0.07 / Pr8.23
    bool quickStopWritten = false;       // Pr8.23 écrit par writeRuntimeSettings()
    uint32_t enableConfirmedAt = 0;      // passe de refresh qui a confirmé Pr0.07 = 1

    mutable char alarmDescBuf[96];

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
    void writeRuntimeSettings(uint32_t now);   // Pr0.07 et Pr8.23 (initialisation, reprise de communication)
    bool runtimeSettingsOnWire() const;        // écritures de writeRuntimeSettings() confirmées sur le bus

    // Séquenceur
    void beginLoad(int32_t target, uint16_t rpm, uint32_t now);
    LoadState serviceLoad(uint32_t now);
    void cancelLoad() { loadState = LoadState::IDLE; }
    bool loadBusy() const { return loadState != LoadState::IDLE; }

    // Jog
    bool jogCommand(int8_t dir, int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed);
    int64_t jogWindow() const;
    int32_t computeJogTarget() const;
    void stopJogMotion(uint32_t now);

    // Arrêt rapide (0x40) puis attente de l'arrêt de l'axe
    void beginQuickStop(uint32_t now);

    // Utilitaires
    uint32_t toWire(int32_t value) const;
    int32_t fromWire(uint32_t raw) const;
    bool statusBit(uint8_t bit, bool whenUnused) const;
    bool hasAlarmRaw() const;
    bool faultNow() const;               // alarme variateur ou défaut logiciel, sans délai
    bool alarmCodePending() const;       // bit de défaut vu, code pas encore relu
    bool isAtPosition(int32_t target) const;
    bool motionActive() const;
    static bool after(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }
    static bool fresh(Node* node, uint32_t t) { return node && after(node->getLastRefresh(), t); }
    uint16_t getSlaveAddress() const;
    bool isPlcSuspended() const;
    void notifyDriveFault(const char* reason);
    void failMotion(const char* reason);
    void setError(const char* msg);
    void logf(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void forceWrite16(Uint16OutputNode* node, uint16_t value);
    void forceWrite32(Uint32OutputNode* node, uint32_t value);
};

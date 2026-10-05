#include "RessortRoyal2.h"

// ========================================
// CONSTRUCTEUR
// ========================================

RessortRoyal2::RessortRoyal2() : BusinessLogic() {
    
    #ifdef UNIT_TEST_MODE
        currentState = (int)State::UNDEFINED;
        previousState = (int)State::UNDEFINED;
        Serial.println("Mode TEST");
        return;
    #endif

    Serial.println("RessortRoyal2() VERSION 2.0 étape 1: constructeur BusinessLogic");


    currentState = static_cast<int>(State::UNDEFINED);
    Serial.println("RessortRoyal2() étape 1.1: constructeur BusinessLogic");
   
    previousState = static_cast<int>(State::UNDEFINED);
    Serial.println("RessortRoyal2() étape 1.2: constructeur BusinessLogic");
  
    stateStartTime = millis();

    
    Serial.println("RessortRoyal2() étape 2: callback MemoryMonitor");
    static bool callbackRegistered = false;
    if (!callbackRegistered) {
        MemoryMonitor::setCleanupCallback([]() {
            PLC_Tools::cleanupThrottleMaps();
        });
        callbackRegistered = true;
    }
    
    Serial.println("RessortRoyal2() étape 3: get BorneUniverselle");
    BorneUniverselle* instance = BorneUniverselle::getInstance();
    
    Serial.println("RessortRoyal2() étape 4: register callback");
    instance->setInitialStateLoadedCallback([this]() {
        this->initialStateLoadedHandler();
    });

    Serial.println("RessortRoyal2() etape 5: register deferred save");
    PLC_Persistence::getInstance().registerDeferredSave(CONFIG_FILE_NAME, [this]() {
        return this->saveMachineParameters();
    });

    // Lecture du fichier de configuration
    if (!readDedicacedConfigFile()){
        BorneUniverselle::setPlcBroken("Erreur lecture fichier de configuration dédié au projet");
    } else {
        Serial.println("Fichier de configuration dédié au projet lu avec succès");
    }
    Serial.println("Fin du constructeur RessortRoyal2");
}

// ========================================
// DESTRUCTEUR
// ========================================

RessortRoyal2::~RessortRoyal2() {
    Serial.println("RessortRoyal2::~RessortRoyal2()");
}

// ========================================
// MÉTHODES X-MACRO POUR LA TABLE DES ÉTATS
// ========================================

const char* RessortRoyal2::getStateNameFromTable(State state) {
    #define X(name, category, description) \
        if (state == State::name) return #name;
    
    RESSORT_ROYAL2_STATE_TABLE
    
    #undef X
    
    return "UNKNOWN_STATE";
}

const char* RessortRoyal2::getStateCategoryFromTable(State state) {
    #define X(name, category, description) \
        if (state == State::name) return #category;
    
    RESSORT_ROYAL2_STATE_TABLE
    
    #undef X
    
    return "UNKNOWN";
}

const char* RessortRoyal2::getStateDescriptionFromTable(State state) {
    #define X(name, category, description) \
        if (state == State::name) return description;
    
    RESSORT_ROYAL2_STATE_TABLE
    
    #undef X
    
    return "État inconnu";
}

BusinessLogic::StateType RessortRoyal2::getStateTypeFromTable(State state) {
    #define X(name, category, description) \
        if (state == State::name) { \
            if (strcmp(#category, "STABLE") == 0) return StateType::STABLE; \
            if (strcmp(#category, "TRANSIENT") == 0) return StateType::TRANSIENT; \
            if (strcmp(#category, "UTILITY") == 0) return StateType::UTILITY; \
            if (strcmp(#category, "EMERGENCY") == 0) return StateType::EMERGENCY; \
        }
    
    RESSORT_ROYAL2_STATE_TABLE
    
    #undef X
    
    return StateType::TRANSIENT;
}

// ========================================
// OVERRIDE BUSINESSLOGIC
// ========================================

const char* RessortRoyal2::stateToString(int state) const {
    return getStateNameFromTable(static_cast<State>(state));
}

BusinessLogic::StateType RessortRoyal2::classifyState(int state) const {
    return getStateTypeFromTable(static_cast<State>(state));
}

// ========================================
// LECTURE DU FICHIER DE CONFIGURATION
// ========================================

bool RessortRoyal2::readDedicacedConfigFile() { 
    Serial.println("========================================");
    Serial.println("RessortRoyal2::readDedicacedConfigFile - Début");
    Serial.println("========================================");
    
    BorneUniverselle* instance = BorneUniverselle::getInstance();
    if (!instance) {
        Serial.println("ERREUR: Instance BorneUniverselle non disponible");
        return false;
    }
 
    // ========================================
    // ÉTAPE 1 : LIRE LE FICHIER RessortRoyal2.json
    // ========================================
    SafeJsonDocument doc(8192); 
    PLC_Persistence& persistence = PLC_Persistence::getInstance();

    if (!persistence.readJsonFromFile(CONFIG_FILE_NAME, doc)) {
        char errorMsg[256];
        snprintf(errorMsg, sizeof(errorMsg), "Erreur lecture fichier %s: %s", 
                CONFIG_FILE_NAME, persistence.getLastError());
        Serial.println(errorMsg);
        instance->setPlcBroken(errorMsg);
        return false;
    }
    
    Serial.println("✅ Fichier RessortRoyal2.json lu avec succès");

    // ========================================
    // ÉTAPE 2 : EXTRAIRE LES PARAMÈTRES MACHINE
    // ========================================
    if (!extractMachineParameters(doc)) {
        Serial.println("❌ Échec extraction paramètres machine");
        return false;
    }
    
    Serial.println("========================================");
    Serial.println("RessortRoyal2::readDedicacedConfigFile - Terminé");
    Serial.println("========================================");
    return true;
}

// ========================================
// EXTRACTION DES PARAMÈTRES MACHINE
// ========================================

bool RessortRoyal2::extractMachineParameters(const JsonDocument& doc) {
    Serial.println("\n========================================");
    Serial.println("RessortRoyal2::extractMachineParameters");
    Serial.println("========================================");

    bool allOk = true;
    bool ok    = false;

    // ========================================
    // PARAMETRES TOLERANTS  (defaut acceptable)
    // ========================================

    // machineName : informatif, pas critique
    strcpy(parameters.machineName,
           PLC_Tools::safeGetString(doc, MACHINE_NAME_KEY, DEFAULT_MACHINE_NAME, 80).c_str());
    Serial.printf("  machineName      : %s\n", parameters.machineName);

    // Compteurs : peuvent legitimement etre a 0 sur un fichier neuf
    parameters.numberOfCyclesToDo  = PLC_Tools::safeGetUint32(doc, NUMBER_OF_CYCLES_KEY,       0U);
    Serial.printf("  numberOfCyclesToDo  : %lu\n", parameters.numberOfCyclesToDo);

    parameters.numberOfCyclesMade  = PLC_Tools::safeGetUint32(doc, NUMBER_OF_CYCLES_MADE_KEY,  0U);
    Serial.printf("  numberOfCyclesMade  : %lu\n", parameters.numberOfCyclesMade);

    parameters.nbTotalCycles       = PLC_Tools::safeGetUint32(doc, NB_TOTAL_CYCLES_KEY,        0U);
    Serial.printf("  nbTotalCycles       : %lu\n", parameters.nbTotalCycles);

    // ========================================
    // PARAMETRES OBLIGATOIRES
    // La machine ne peut pas fonctionner sans eux
    // ========================================

    ok = false;
    parameters.userTarget = PLC_Tools::safeGetUint32(doc, USER_TARGET_KEY, 0U, &ok);
    Serial.printf("  userTarget       : %lu [pulses] %s\n", parameters.userTarget, ok ? "" : "<-- MANQUANT");
    if (!ok) allOk = false;

    ok = false;
    parameters.returnPos = PLC_Tools::safeGetUint32(doc, RETURN_POS_KEY, 0U, &ok);
    Serial.printf("  returnPos        : %lu [pulses] %s\n", parameters.returnPos, ok ? "" : "<-- MANQUANT");
    if (!ok) allOk = false;

    ok = false;
    parameters.goAndBack = PLC_Tools::safeGetBool(doc, GO_AND_BACK_KEY, false, &ok);
    Serial.printf("  goAndBack        : %s %s\n", parameters.goAndBack ? "true" : "false", ok ? "" : "<-- MANQUANT");
    if (!ok) allOk = false;

    ok = false;
    parameters.jogSpeed = static_cast<uint16_t>(PLC_Tools::safeGetUint32(doc, JOG_SPEED_KEY, 500U, &ok));
    Serial.printf("  jogSpeed         : %u %s\n", parameters.jogSpeed, ok ? "" : "<-- MANQUANT");
    if (!ok) allOk = false;

    ok = false;
    parameters.cycleSpeed = static_cast<uint16_t>(PLC_Tools::safeGetUint32(doc, CYCLE_SPEED_KEY, 10U, &ok));
    Serial.printf("  cycleSpeed       : %u %s\n", parameters.cycleSpeed, ok ? "" : "<-- MANQUANT");
    if (parameters.cycleSpeed > 15){
        parameters.cycleSpeed = 15;
    }
    if (!ok) allOk = false;

    ok = false;
    parameters.fullTorque = PLC_Tools::safeGetBool(doc, FULL_TORQUE_KEY, false, &ok);
    Serial.printf("  fullTorque       : %s %s\n", parameters.fullTorque ? "true" : "false", ok ? "" : "<-- MANQUANT");
    if (!ok) allOk = false;

    ok = false;
    parameters.initialTorque = static_cast<uint16_t>(PLC_Tools::safeGetUint32(doc, INITIAL_TORQUE_KEY, 50U, &ok));
    Serial.printf("  initialTorque    : %u%% %s\n", parameters.initialTorque, ok ? "" : "<-- MANQUANT");
    if (!ok) allOk = false;

    // ========================================
    // VALIDATION FINALE
    // ========================================

    if (!allOk) {
        Serial.println("========================================");
        Serial.println("ERREUR: Parametres obligatoires manquants");
        Serial.println("        Fichier de configuration refuse");
        Serial.println("========================================");
        return false;
    }

    Serial.println("========================================");
    Serial.println("OK: Extraction des parametres terminee");
    Serial.println("========================================");
    return true;
}

// ========================================
// INITIALISATION DES POINTEURS DE NŒUDS
// ========================================

bool RessortRoyal2::initializePointers(const char* contexte) {
    Serial.println("========================================");
    Serial.printf("RessortRoyal2::initializePointers - Début (%s)\n", contexte);
    Serial.println("========================================");
    
    // === NŒUDS DE CONTRÔLE PRINCIPAL ===
    initNodePtr(buzzer, BUZZER, contexte);
    initNodePtr(eStopA, ESTOPA, contexte);
    initNodePtr(startCycle, START_CYCLE, contexte);
    
    // === NŒUDS DE SORTIE KINCONY ===
    initNodePtr(triggerPR, TRIGGER_PR, contexte);
    initNodePtr(immediateStop, IMMEDIATE_STOP, contexte);
    initNodePtr(alarmsReset, ALARMS_RESET, contexte);
    initNodePtr(freeRun, FREE_RUN, contexte);
    initNodePtr(goAndBack, GO_AND_BACK, contexte);
    
    // === NŒUDS JOG ===
    initNodePtr(jog, JOG, contexte);
    initNodePtr(fwd, FWD, contexte);
    initNodePtr(rwd, RWD, contexte);
    initNodePtr(fullTorque, FULL_TORQUE, contexte);
    
    // === NŒUDS DE STATUS SERVO ===
    initNodePtr(servoReady, SERVOREADY, contexte);
    initNodePtr(servoActivated, SERVOACTIVATED, contexte);
    initNodePtr(zeroSpeed, ZEROSPEED, contexte);
    initNodePtr(targetSpeedRated, TARGETSPEEDRATED, contexte);
    initNodePtr(targetPositionReached, ATTARGET, contexte);
    initNodePtr(servoAlarm, SERVOALARM, contexte);
    initNodePtr(absolutePositionLost, ABSOLUTE_POS_LOST, contexte);
    initNodePtr(batteryAlarm, BATTERYALARM, contexte);
    initNodePtr(multipleTurnsOverflow, MULTIPLETURNSOVERFLOW, contexte);
    initNodePtr(puuOverflow, PUUOVERFLOW, contexte);
    initNodePtr(absoluteCoordonateNotSet, ABSOLUTECOORDONATENOTSET, contexte);
    initNodePtr(homeDone, HOMEDONE, contexte);
    initNodePtr(modbusError, MODBUSERROR, contexte);
    initNodePtr(driveInitialised, DRIVEINITIALISED, contexte);
    
    // === NŒUDS D'ENTRÉE UTILISATEUR ===
    initNodePtr(userTarget, USER_TARGET, contexte);
    initNodePtr(returnPos, RETURN_POS, contexte);
    initNodePtr(v_jogSpeed, VJOG_SPEED, contexte);
    initNodePtr(v_cycleSpeed, VCYCLE_SPEED, contexte);
    initNodePtr(v_position, V_POSITION, contexte);
    initNodePtr(numberOfCyclesToDo, NUMBER_OF_CYCLES_TO_DO, contexte);
    initNodePtr(numberOfCyclesMade, NUMBER_OF_CYCLES_MADE, contexte);
    initNodePtr(nbCyclesClear, NB_CYCLES_CLEAR, contexte);
    
    // === NŒUDS MODBUS READ HOLDING REGISTER ===
    initNodePtr(status, STATUS_DRIVE, contexte);
    initNodePtr(absoluteCoordonateSystemStatus, ABSOLUTE_COORDONATE_SYSTEM_STATUS, contexte);
    initNodePtr(alarms, ALARMS, contexte);
    
    // === NŒUDS MODBUS WRITE HOLDING REGISTER ===
    initNodePtr(auxFunctions, AUX_FUNCTION, contexte);
    initNodePtr(trigger, TRIGGER, contexte);
    initNodePtr(jogSpeed, JOG_SPEED, contexte);
    initNodePtr(maxTorque, MAX_TORQUE, contexte);
    
    // === NŒUDS MODBUS READ DOUBLE HOLDING REGISTER ===
    initNodePtr(position, POSITION, contexte);
    
    // === NŒUDS MODBUS WRITE DOUBLE HOLDING REGISTER ===
    initNodePtr(pr1Speed, PR1_SPEED, contexte);
    initNodePtr(pr2Speed, PR2_SPEED, contexte);
    initNodePtr(pr3Speed, PR3_SPEED, contexte);

    initNodePtr(statesMachine, STATES_MACHINE, contexte);

    initNodePtr(target_A, TARGET_A, contexte);
    initNodePtr(target_B, TARGET_B, contexte);
    initNodePtr(target_C, TARGET_C, contexte);
    initNodePtr(servoOn, SERVOON, contexte);

    initNodePtr(v_immediateStop, V_IMMEDIAT_STOP, contexte);
    initNodePtr(v_alarmsReset, V_ALARMS_RESET, contexte);
    initNodePtr(v_servoOn, V_SERVO_ON, contexte);
    initNodePtr(v_setPLC_Clock, V_SET_PLC_CLOCK, contexte);
    v_setPLC_Clock_enhenced = new EnhancedButton(v_setPLC_Clock);
    initNodePtr(v_clock, V_CLOCK, contexte);
    ClockDisplay::attach(v_clock);  // Masque le champ horloge si aucun RTC détecté
    initNodePtr(v_torque, V_TORQUE, contexte);
    
    Serial.println("=== CRÉATION SURESERVO ===");
    ServoNodes servoNodes;
   // === NODES OBLIGATOIRES ===
    servoNodes.servoOn = servoOn;
    servoNodes.immediateStop = immediateStop;
    servoNodes.alarmsReset = alarmsReset;
    servoNodes.position = position;
    servoNodes.targetA = target_A;
    servoNodes.targetB = target_B;
    servoNodes.targetC = target_C;
    servoNodes.trigger = trigger;
    servoNodes.jogSpeed = jogSpeed;
    servoNodes.maxTorque = maxTorque;
    servoNodes.pr1Speed = pr1Speed;
    servoNodes.pr2Speed = pr2Speed;
    servoNodes.pr3Speed = pr3Speed;
    servoNodes.auxFunctions = auxFunctions;
    servoNodes.status = status;
    servoNodes.alarms = alarms;
    servoNodes.absoluteCoordonateSystemStatus = absoluteCoordonateSystemStatus;
    servoNodes.targetPositionReached = targetPositionReached;

    // === NODES OPTIONNELS ===
    servoNodes.servoReady = servoReady;
    servoNodes.servoActivated = servoActivated;
    servoNodes.zeroSpeed = zeroSpeed;
    servoNodes.targetSpeedRated = targetSpeedRated;
    servoNodes.servoAlarm = servoAlarm;
    servoNodes.homeDone = homeDone;
    servoNodes.modbusError = modbusError;
    servoNodes.driveInitialised = driveInitialised;
    servoNodes.absolutePositionLost = absolutePositionLost;
    servoNodes.batteryAlarm = batteryAlarm;
    servoNodes.multipleTurnsOverflow = multipleTurnsOverflow;
    servoNodes.puuOverflow = puuOverflow;
    servoNodes.absoluteCoordonateNotSet = absoluteCoordonateNotSet;
    
    // Créer le SureServo (pas de conversion PUU pour RessortRoyal2, on utilise 1:1)
    const uint32_t PULSES_PER_UNIT = 1;  // Pas de conversion, travail direct en pulses
    const uint32_t MAX_RANGE = 1000000;  // 1 million de pulses max
    
    sureServo = std::make_unique<SureServo>(PULSES_PER_UNIT, servoNodes, HomePositionPolicy::NEGATIVE_INVALIDATES_HOME, MAX_RANGE);

    sureServo->setOnAlarmCallback([this](uint16_t code, const char* desc) {
        transitionToState(State::EMERGENCY, ContextCode::SERVO_ALARM);
    });
 
    if (!sureServo) {
        BorneUniverselle::getInstance()->setPlcBroken("Échec création SureServo - pointeur null");
        return false;
    }

    // Vérification que le constructeur s'est bien passé
    const char* lastError = sureServo->getLastError();
    if (lastError && strlen(lastError) > 0 && strcmp(lastError, "Pas d'erreur") != 0) {
        String errorMsg = "Erreur SureServo: ";
        errorMsg += lastError;
        BorneUniverselle::getInstance()->setPlcBroken(errorMsg.c_str());
        Serial.println(errorMsg);
        return false;
    }
    
    Serial.println("✅ SureServo créé avec succès");
    sureServo->printConfiguration();

    // Vérifier si des erreurs se sont produites pendant l'initialisation
    if (BorneUniverselle::getInstance()->isPlcBroken()) {
        Serial.println("Erreur détectée pendant l'initialisation des pointeurs");
        return false;
    }

    visu = new VisualIndicator(numberOfCyclesMade);

    applyParametersToNodes();
    
    Serial.println("RessortRoyal2::initializePointers - Fin avec succès");
    Serial.println("========================================");
    return true;
}

// ========================================
// INITIAL STATE LOADED HANDLER
// ========================================

void RessortRoyal2::initialStateLoadedHandler() {
    uint32_t start = millis();
    Serial.println("🌐 NEW CLIENT CONNECTED AND LOADED!");
    Serial.println("========================================");
    Serial.println("RessortRoyal2::initialStateLoadedHandler - Début");
    Serial.println("========================================");
    
    // ====================================================================
    // SYNCHRONISATION DE L'ÉTAT AVEC LE NOUVEAU CLIENT
    // ====================================================================
    
    // État du servo
    if (servoOn) {
        Serial.printf("ServoOn state synchronized: %s\n", 
                     servoOn->getValue() ? "ON" : "OFF");
    }
    
    // État initial du jog (toujours faux au démarrage)
    if (jog) {
        jog->setValue(false);
        Serial.println("Jog initialized to false");
    }
    
    // ====================================================================
    // MESSAGES D'ÉTAT POUR LE NOUVEAU CLIENT
    // ====================================================================
    
    if (getCurrentState() == State::EMERGENCY) {
        BorneUniverselle::prepareMessage(ERROR, 
            "Attention ! Machine en arrêt d'urgence");
        Serial.println("Client notified: EMERGENCY state");
    }
    
    if (immediateStop && !immediateStop->getValue()) {
        BorneUniverselle::prepareMessage(ERROR, 
            "Arrêt immédiat actif - E_STOP ou problème servo drive");
        Serial.println("Client notified: Immediate stop active");
    }
    
    // ====================================================================
    // APPLICATION DES PARAMÈTRES PERSISTANTS
    // ====================================================================
    
    Serial.println("Applying persistent parameters to nodes...");
    applyParametersToNodes();
    Serial.println("✅ Parameters applied successfully");


    bool hasClock = RTC_Service::getInstance()->hasHardwareClock();

    if (!hasClock) {
        Serial.println("⚠️ No hardware clock detected, PLC clock features will be unavailable");
        v_setPLC_Clock_enhenced->setDisabled(true);
    }

    
    // ====================================================================
    // ACTIVATION DE L'INTERFACE
    // ====================================================================
    
    if (BorneUniverselle::enableInterface(true)){
        Serial.println("✅ Interface enabled successfully");
    } else {
        Serial.println("❌ Failed to enable interface");
    }

    // ====================================================================
    // MARQUAGE COMME INITIALISÉ
    // ====================================================================
    
    initialised = true;
    
    uint32_t duration = millis() - start;

    BorneUniverselle::getInstance()->clearAllClientNotifications();
    Serial.println("========================================");
    Serial.printf("RessortRoyal2::initialStateLoadedHandler - Terminé en %lu ms\n", duration);
    Serial.println("========================================");
}

// ========================================
// ON FILE SAVED
// ========================================

void RessortRoyal2::onFileSaved(const char* path, bool success) {
    // Validation des paramètres
    if (!path || !success) {
        Serial.printf("onFileSaved: Chemin invalide: %s, échec de sauvegarde: %s\r\n", path, success ? "true" : "false");
        return;
    }

    // Filtrer les modifications internes (faites par la machine elle-même)
    if (!shouldProcessFileCallback(path)) {
        Serial.printf("Fichier %s modifié en interne, ignoré\n", path);
        return;
    }

    Serial.printf("========================================\n");
    Serial.printf("RessortRoyal2::onFileSaved: %s\n", path);
    Serial.printf("========================================\n");

    // Fichier de configuration RessortRoyal2.json
    if (strcmp(path, CONFIG_FILE_NAME) == 0) {
        Serial.println("Fichier RessortRoyal2.json modifié → handleRessortRoyal2ConfigFileChanged()");
        handleRessortRoyal2ConfigFileChanged();
        return;
    }

     // 3. Fichier JSON uploadé qui ne correspond à aucun fichier configuré
    // Vérifier si c'est un fichier JSON
    size_t pathLen = strlen(path);
    bool isJsonFile = (pathLen > 5 && 
                       strcmp(path + pathLen - 5, ".json") == 0);
    
    if (isJsonFile) {
        Serial.printf("⚠️ Fichier JSON uploadé non reconnu: %s\n", path);
        Serial.println("Suggérez à l'utilisateur de vérifier la configuration");
        
        char msg[256];
        snprintf(msg, sizeof(msg), 
                "Fichier %s uploadé mais pas configuré. "
                "Vérifiez 'recetteFileName' dans PMB.json", 
                path);
        BorneUniverselle::prepareMessage(WARNING, msg);
    }
}

// ========================================
// VALIDATE CONFIG FILE
// ========================================

bool RessortRoyal2::validateConfigFile(const char* path, const JsonDocument& doc, String& errorMsg) {
    // Validation basique pour RessortRoyal2
    // Pour l'instant, on accepte tout
    return true;
}

// ========================================
// CAN DELETE FILE
// ========================================

bool RessortRoyal2::canDeleteFile(const char* path) {
    // Protéger le fichier de configuration principal
    if (strcmp(path, CONFIG_FILE_NAME) == 0) {
        Serial.printf("❌ Cannot delete protected file: %s\n", path);
        return false;
    }
    
    return true;
}

// ========================================
// START STATE MACHINE
// ========================================

bool RessortRoyal2::startStateMachine() {
    Serial.println("RessortRoyal2::startStateMachine()");
    uint32_t startupTime = millis();

    Serial.println("Initialisation des paramètres");
    Serial.println("------------------------------");
    
    Serial.printf("immediateStop hideValue au démarrage: %s\n", 
    immediateStop ? (immediateStop->getValue() ? "true" : "false") : "nullptr");

    // Transition vers l'état INITIALIZING
    transitionToState(State::INITIALIZING);
    
    Serial.printf("RessortRoyal2::startStateMachine() - Durée démarrage: %lu ms\n", 
                  (unsigned long)(millis() - startupTime));
    return true;
}

// ========================================
// DO BUSINESS LOGIC - BOUCLE PRINCIPALE
// ========================================

bool RessortRoyal2::doBusinessLogic() {
    uint32_t startTotal = millis();
    uint32_t now = startTotal;
    
    // =====================================
    // 1. TRAITEMENT SURESERVO
    // =====================================
     if (sureServo) {
        sureServo->process();
    }
    
    // =====================================
    // 2. VÉRIFICATION PLC BROKEN
    // =====================================
    if (BorneUniverselle::getInstance()->isPlcBroken()) {
        Serial.printf("%lu:: PLC broken, exiting\r\n", (unsigned long)now);
        return false;
    }
    
    // =====================================
    // 3. HANDLE INTERRUPTS
    // =====================================
    handleInterrupts();
    
    // =====================================
    // 4. UPDATE OUTPUTS
    // =====================================
    updateOutputs();
    
    // =====================================
    // 5. MACHINE À ÉTATS
    // =====================================
    State state = getCurrentState();
    bool handlerResult = false;
    
    switch(state) {
        case State::UNDEFINED:
            handlerResult = handleUndefinedState(now);
            break;
            
        case State::INITIALIZING:
            handlerResult = handleInitializingState(now);
            break;
            
        case State::STOP:
            handlerResult = handleStopState(now);
            break;
            
        case State::IDLE:
            handlerResult = handleIdleState(now);
            break;

        case State::GOING: // un start pour le go et un start pour le back
            handlerResult = handleGoingState(now);
            break;

        case State::CYCLING:
            handlerResult = handleCyclingState(now);
            break;
            
        case State::EMERGENCY:
            handlerResult = handleEmergencyState(now);
            break;
            
        case State::HOMING:
            handlerResult = handleHomingState(now);
            break;
            
        case State::JOGGING:
            handlerResult = handleJoggingState(now);
            break;
            
        default:
            Serial.printf("ERREUR: État inconnu: %d\n", static_cast<int>(state));
            transitionToState(State::EMERGENCY);
            break;
    }
    
    if (!handlerResult) {
        return false;
    }
    
    return true;
}

// ========================================
// APPLY PARAMETERS TO NODES
// ========================================

void RessortRoyal2::applyParametersToNodes() {
    Serial.println("RessortRoyal2::applyParametersToNodes - Début");
    
    // Appliquer les paramètres de mouvement
    if (userTarget) {
        userTarget->setValue(parameters.userTarget);
        Serial.printf("  User target: %lu\n", parameters.userTarget);
    }
    
    if (returnPos) {
        returnPos->setValue(parameters.returnPos);
        Serial.printf("  Return position: %lu\n", parameters.returnPos);
    }

    if (goAndBack) {
        goAndBack->setValue(parameters.goAndBack);
        Serial.printf("  Go and back mode: %s\n", parameters.goAndBack ? "true" : "false");
    } 
    
    if (v_jogSpeed) {
        v_jogSpeed->setValue(parameters.jogSpeed);
        Serial.printf("  Jog speed: %u\n", parameters.jogSpeed);
    }
    
    if (v_cycleSpeed) {
        v_cycleSpeed->setValue(parameters.cycleSpeed);
        Serial.printf("  Cycle speed: %u\n", parameters.cycleSpeed);
    }
    
    // Appliquer les paramètres de cycle
    if (numberOfCyclesToDo) {
        numberOfCyclesToDo->setValue(parameters.numberOfCyclesToDo);
        Serial.printf("  Cycles to do: %lu\n", parameters.numberOfCyclesToDo);
    }
    
    if (numberOfCyclesMade) {
        numberOfCyclesMade->setValue(parameters.numberOfCyclesMade);
        Serial.printf("  Cycles made: %lu\n", parameters.numberOfCyclesMade);
    }
    
    // ⭐ AJOUTÉ - Synchronisation de nbTotalCycles (si un node existe)
    // Note: Vérifier si le node existe dans interface.json avant d'activer
    // if (nbTotalCycles) {
    //     nbTotalCycles->setValue(parameters.nbTotalCycles);
    //     Serial.printf("  Total cycles: %lu\n", parameters.nbTotalCycles);
    // }
    
    // Appliquer les paramètres de contrôle
    if (fullTorque) {
        fullTorque->setValue(parameters.fullTorque);
        Serial.printf("  Full torque: %s\n", parameters.fullTorque ? "true" : "false");
    }
    
    if (!fullTorque->getValue()) {
        uint16_t torqueValue = parameters.initialTorque;
        maxTorque->setValue(torqueValue); // c'est le servo drive
        v_torque->setValue(torqueValue);
        Serial.printf("  Max torque: %u%%\n", torqueValue);
    }
    
    Serial.println("RessortRoyal2::applyParametersToNodes - Terminé");
}

// ========================================
// HANDLE CONFIG FILE CHANGED
// ========================================

void RessortRoyal2::handleRessortRoyal2ConfigFileChanged() {
    Serial.println("========================================");
    Serial.println("RessortRoyal2::handleRessortRoyal2ConfigFileChanged");
    Serial.println("========================================");

    // Phase 0 : Vérifier que la machine est dans un état sûr (IDLE, STOP ou EMERGENCY)
    // Sinon applyParametersToNodes() reconfigurerait maxTorque/PR1/PR2 sur le drive
    // pendant un mouvement en cours (GOING/CYCLING).
    if (!isIdle()) {
        State currentState = getCurrentState();
        Serial.printf("handleRessortRoyal2ConfigFileChanged:: BLOQUÉ - Machine en état %s, rechargement interdit\r\n",
                      stateToString(static_cast<int>(currentState)));
        BorneUniverselle::prepareMessage(WARNING, "Rechargement config bloqué: machine en cours d'opération. Attendez l'état IDLE, STOP, EMERGENCY, INITIALIZE");
        return;
    }

    BorneUniverselle* bu = BorneUniverselle::getInstance();
    bu->suspendForInternalOperation("RessortRoyal2: fichier RessortRoyal2.json modifié");

    // Relire le fichier de configuration
    if (readDedicacedConfigFile()) {
        Serial.println("✅ Configuration rechargée avec succès");

        // Réappliquer les paramètres aux nœuds
        applyParametersToNodes();

        // Ré écrire le fichier pour s'assurer que les paramètres sont à jour (utile si des paramètres manquants ont été ajoutés avec des valeurs par défaut)
        PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);

        BorneUniverselle::prepareMessage(SUCCESS,
            "Configuration RessortRoyal2 rechargée avec succès");
    } else {
        Serial.println("❌ Erreur lors du rechargement de la configuration");
        BorneUniverselle::prepareMessage(ERROR,
            "Erreur lors du rechargement de la configuration");
    }

    bu->resumeFromInternalOperation();
}

// ========================================
// HANDLERS D'ÉTATS - IMPLÉMENTATIONS
// ========================================

bool RessortRoyal2::handleUndefinedState(uint32_t now) {
    // Transition automatique vers INITIALIZING
    transitionToState(State::INITIALIZING);
    return true;
}

bool RessortRoyal2::handleInitializingState(uint32_t now) {
    printCurrentState();
    
    ServoDrive::OperationStatus status;
    
    if (!sureServo->handleInitializing(status)) {
        BorneUniverselle::getInstance()->setPlcBroken("Drive init failed");
        return false;
    }
    
    switch (status) {
        case ServoDrive::OperationStatus::COMPLETED:
            if (driveInitialised) driveInitialised->setValue(true);
            BorneUniverselle::prepareMessage(SUCCESS, "Servo drive initialisé avec succès");

            // Appliquer la config PR selon le mode actif au moment de l'init
            if (parameters.goAndBack) {
                sureServo->configurePR(1, parameters.userTarget, parameters.cycleSpeed, true);
                sureServo->configurePR(2, parameters.returnPos,  parameters.cycleSpeed, false);
                Serial.println("✅ Post-init: PR1+PR2 configurés (mode go-and-back)");
            } else {
                sureServo->setSpeedAndRamp(parameters.cycleSpeed, 0);
                Serial.println("✅ Post-init: vitesse cycle appliquée (mode simple)");
            }

            transitionToState(State::STOP);
            break;
            
        case ServoDrive::OperationStatus::SERVO_DRIVE_FATAL_ERROR:
            transitionToState(State::EMERGENCY, ContextCode::SERVO_ALARM);
            if (driveInitialised) driveInitialised->setValue(false);
            BorneUniverselle::prepareMessage(ERROR, "Erreur fatale servo drive");
            break;
            
        case ServoDrive::OperationStatus::SERVO_DRIVE_ERROR:
            transitionToState(State::EMERGENCY, ContextCode::INIT_FAILED);
            if (driveInitialised) driveInitialised->setValue(false);
            BorneUniverselle::prepareMessage(ERROR, "Erreur initialisation servo drive");
            break;
            
        case ServoDrive::OperationStatus::IN_PROGRESS:
        default:
            // Initialisation en cours
            break;
    }
    
    return true;
}

bool RessortRoyal2::handleCyclingState(uint32_t now) {
    return handleMovementState(now);
}

bool RessortRoyal2::handleGoingState(uint32_t now) {
    return handleMovementState(now);
}


// ========================================
// HANDLER COMMUN - Utilisé par CYCLING et GOING
// Optimisé pour délai ALLER → RETOUR constant (~33 ms)
// ========================================

bool RessortRoyal2::handleMovementState(uint32_t now) {
    printCurrentState();

    if (checkEmergencyConditions(now)){
        return true;    
    }

    // ========================================
    // VÉRIFICATION PRÉALABLE: HOME REQUIS
    // ========================================

    if (homeDone && !homeDone->getValue()) {
        State currentState = static_cast<State>(getCurrentState());
        Serial.printf("⚠️ %s: Home non fait - Redirection vers HOMING\n", 
                     getStateNameFromTable(currentState));
        BorneUniverselle::prepareMessage(WARNING, "Home requis avant mouvement");
        goingMovementStarted = false;
        goingPhase = true;
        transitionToState(State::HOMING, ContextCode::NORMAL);
        return true;
    }

    // ========================================
    // PHASE 1: DÉMARRAGE DU MOUVEMENT
    // ========================================

    if (!goingMovementStarted) {
        Serial.printf("%lu:: 🔄 Démarrage phase %s\n", 
                      millis(), goingPhase ? "ALLER" : "RETOUR");

        // Vérifier que le servo est disponible
        ServoDrive::OperationState moveState = sureServo->getMoveState();
        if (moveState != ServoDrive::OperationState::DRIVE_IDLE) {
            char msg[100];
            snprintf(msg, sizeof(msg), "Servo occupé (%s) - Impossible de démarrer",
                     ServoDrive::getOperationStateText(moveState));
            BorneUniverselle::prepareMessage(WARNING, msg);
            goingMovementStarted = false;
            transitionToState(State::IDLE, ContextCode::INIT_FAILED);
            return true;
        }

        // Déterminer la position cible selon la phase
        // En mode chaîné : le drive gère PR1→PR2 seul, on surveille la position finale (returnPos)
        uint32_t targetPosition;
        if (goingPhase && parameters.goAndBack) {
            targetPosition = returnPos ? returnPos->getValue() : 0;
        } else {
            targetPosition = goingPhase
                ? (userTarget ? userTarget->getValue() : 0)
                : (returnPos  ? returnPos->getValue()  : 0);
        }


        // Démarrer le mouvement sans attendre la re-sync
        bool success = sureServo->startGoToPosition(targetPosition, false);
        if (!success) {
            Serial.printf("❌ Échec démarrage mouvement: %s\n", sureServo->getLastError());
            BorneUniverselle::prepareMessage(ERROR, "Échec démarrage mouvement");
            goingMovementStarted = false;
            goingPhase = true;
            transitionToState(State::EMERGENCY, ContextCode::EMERGENCY_SERVO_FAULT);
            return true;
        }

#ifdef PERF_MONITOR
        if (goingPhase && goAndBack && goAndBack->getValue()) sureServo->pmStartCycle();
#endif
        Serial.printf("✅ Mouvement démarré vers %s\n", goingPhase ? "TARGET" : "BACK");
        currentTargetPosition = targetPosition;
        goingMovementStarted = true;
        return true;
    }

    // ========================================
    // PHASE 2: SURVEILLANCE PENDANT LE MOUVEMENT
    // ========================================

    // ✅ Lire l'état du mouvement EN PREMIER — si terminé, traiter le succès avant de vérifier le bouton.
    // Évite un EMERGENCY parasite quand le bouton est relâché dans le même cycle que la fin du mouvement.
    ServoDrive::OperationState moveState = sureServo->getMoveState();
    bool isGoAndBackMode = (goAndBack && goAndBack->getValue());

    // ========================================
    // MOUVEMENT TERMINÉ AVEC SUCCÈS
    // ========================================
    if (moveState == ServoDrive::OperationState::DRIVE_IDLE) {

        goingMovementStarted = false;

        if (goingPhase) {
            if (isGoAndBackMode) {
                // Mode chaîné : PR1→PR2 complet en un seul DRIVE_IDLE.
                Serial.println("✅ Cycle chaîné PR1→PR2 terminé");
                if (numberOfCyclesMade)
                    numberOfCyclesMade->setValue(numberOfCyclesMade->getValue() + 1);
                parameters.numberOfCyclesMade = numberOfCyclesMade->getValue();
                parameters.nbTotalCycles++;
                PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
                char msg[64];
                snprintf(msg, sizeof(msg), "Cycle %lu terminé (aller-retour)",
                        numberOfCyclesMade->getValue());
                BorneUniverselle::prepareMessage(SUCCESS, msg);
#ifdef PERF_MONITOR
                uint32_t cycleMs = millis() - sureServo->pmGetCycleStartTime();
                sureServo->pmNotifyCompleted();
                sureServo->pmPrintReport(cycleMs);
#endif
                goingPhase = true;
                transitionToState(State::IDLE, ContextCode::CYCLE_COMPLETE);
            } else {
                // Mode normal : ALLER terminé → attendre prochain start
                Serial.println("✅ ALLER terminé → retour IDLE");
                if (numberOfCyclesMade)
                    numberOfCyclesMade->setValue(numberOfCyclesMade->getValue() + 1);
                parameters.numberOfCyclesMade = numberOfCyclesMade->getValue();
                PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
                goingPhase = false;  // Prochain start fera le RETOUR
                transitionToState(State::IDLE, ContextCode::NORMAL);
            }
        } else {
            // RETOUR terminé (mode normal seulement)
            Serial.println("✅ RETOUR terminé → cycle complet");
            parameters.nbTotalCycles++;
            parameters.numberOfCyclesMade = numberOfCyclesMade->getValue();
            PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
            char msg[64];
            snprintf(msg, sizeof(msg), "Mouvement %lu terminé (vers BACK)",
                    numberOfCyclesMade->getValue());
            BorneUniverselle::prepareMessage(SUCCESS, msg);
#ifdef PERF_MONITOR
            uint32_t cycleMs = millis() - sureServo->pmGetCycleStartTime();
            sureServo->pmNotifyCompleted();
            sureServo->pmPrintReport(cycleMs);
#endif
            goingPhase = true;
            // Reconfigurer PR1+PR2 après retour de recovery
            if (parameters.goAndBack) {
                sureServo->setChainedPRMode(true);
                sureServo->configurePR(1, parameters.userTarget, parameters.cycleSpeed, true);
                sureServo->configurePR(2, parameters.returnPos,  parameters.cycleSpeed, false);
                Serial.println("✅ Recovery: PR1+PR2 reconfigurés après retour");
            }
            transitionToState(State::IDLE, ContextCode::CYCLE_COMPLETE);
        }
        return true;
    }

    // Vérifier si le bouton startCycle est relâché pendant le mouvement
    // En mode go-and-back, on ne surveille que pendant la phase ALLER
    bool pr1Done = isGoAndBackMode && sureServo->isChainedPR1Complete();
    bool shouldCheckStartButton = !isGoAndBackMode || (goingPhase && !pr1Done);

    if (!startCycle->getValue() && shouldCheckStartButton) {
        Serial.printf("%lu:: ⚠️ Bouton startCycle relâché pendant mouvement!\n", millis());
        BorneUniverselle::prepareMessage(ERROR, "Bouton start relâché pendant cycle");

        goingMovementStarted = false;
        goingPhase = true;
        transitionToState(State::EMERGENCY, ContextCode::EMERGENCY_STOP);
        return true;
    }

    // Mouvement en cours
    if (moveState == ServoDrive::OperationState::DRIVE_IN_PROGRESS) {
        return true;
    }

    // Mouvement échoué
    if (moveState == ServoDrive::OperationState::DRIVE_FAILED ||
        moveState == ServoDrive::OperationState::DRIVE_TIMEOUT) {

        char detailedError[256];
        snprintf(detailedError, sizeof(detailedError),
                 "ÉCHEC MOUVEMENT: %s - %s",
                 ServoDrive::getOperationStateText(moveState),
                 sureServo->getLastError());
        Serial.println(detailedError);
        BorneUniverselle::prepareMessage(ERROR, detailedError);

        goingMovementStarted = false;
        goingPhase = true;
        transitionToState(State::EMERGENCY, ContextCode::EMERGENCY_SERVO_FAULT);
        return true;
    }

    // État inattendu
    Serial.printf("⚠️ État servo inattendu: %d\n", static_cast<int>(moveState));
    return true;
}

bool RessortRoyal2::handleStopState(uint32_t now) {
    printCurrentState();
    interfaceTreatment();
    if (checkEmergencyConditions(now)){
        return true;
    }

    // PRIORITÉ 1: JOG
    if (jog->getIsChanged() && jog->getValue()) {
        // ✅ GUARD: ne pas entrer en JOGGING si non initialisé
        if (!sureServo->getIsInitialized()) {
            BorneUniverselle::prepareMessage(WARNING, "Servo non initialisé - Réinitialisation requise");
            transitionToState(State::INITIALIZING, ContextCode::INIT_FAILED);
            return true;
        }
        transitionToState(State::JOGGING);
        return true;
    }
    
    // PRIORITÉ 2: HOMING (si START appuyé et pas de home)
    if (startCycle->getValue() && !homeDone->getValue()) {
        BorneUniverselle::prepareMessage(INFO, "Appui sur démarrage cycle - Homing requis");
        transitionToState(State::HOMING);
        return true;
    }

    // ===================================================
    // TRANSITION VERS IDLE
    // ===================================================
    
    if (homeDone->getValue()) {
        transitionToState(State::IDLE);
        return true;
    }

    return true;
}

bool RessortRoyal2::handleIdleState(uint32_t now) {
    printCurrentState();
    interfaceTreatment();
    if (checkEmergencyConditions(now)){
        return true;
    }

    if (startCycle->getIsChanged() && startCycle->getValue()) {
        // Récupération après emergency pendant phase aller → forcer retour
        if (goingPhase && getPreviousState() == static_cast<int>(State::EMERGENCY)) {
            Serial.println("⚠️ Recovery après emergency pendant aller → forcer retour");
            BorneUniverselle::prepareMessage(WARNING, "Retour forcé après urgence - appuyez à nouveau sur Start");
            goingPhase = false;
            if (parameters.goAndBack) {
                sureServo->setChainedPRMode(false);
            }
        }

        ServoDrive::OperationState moveState = sureServo->getMoveState();
        if (moveState != ServoDrive::OperationState::DRIVE_IDLE) {
            char msg[100];
            snprintf(msg, sizeof(msg), "Servo occupé (%s) - Réessayez", ServoDrive::getOperationStateText(moveState));
            PLC_Tools::sendPeriodicMessage(true, WARNING, msg);
            return true;
        }

        if (!homeDone->getValue()) {
            BorneUniverselle::prepareMessage(INFO, "Homing requis avant de démarrer le cycle");
            transitionToState(State::HOMING);
            return true;
        } else {
            if (goAndBack && goAndBack->getValue()) {
                Serial.println("Mode go-and-back: Démarrage du cycle complet (aller-retour)");
                transitionToState(State::CYCLING, ContextCode::USER_REQUEST);
            } else {
                Serial.println("Mode normal: Démarrage du cycle (aller ou retour selon la phase");
                transitionToState(State::GOING, ContextCode::USER_REQUEST);
                return true;
            }
        }
       
    }
    
    // Mode JOG
    if (jog && jog->getIsChanged() && jog->getValue()) {
        // ✅ GUARD: ne pas entrer en JOGGING si non initialisé
        if (!sureServo->getIsInitialized()) {
            BorneUniverselle::prepareMessage(WARNING, "Servo non initialisé - Réinitialisation requise");
            transitionToState(State::INITIALIZING, ContextCode::INIT_FAILED);
            return true;
        }
        transitionToState(State::JOGGING, ContextCode::USER_REQUEST);
        return true;
    }
    
    return true;
}

bool RessortRoyal2::handleEmergencyState(uint32_t now) {
    // Logique standard
    printCurrentState();
    interfaceTreatment(); 
    
    // ⚠️ NE PAS appeler checkEmergencyConditions() ici !
    
    // ========================================
    // GESTION DU CHANGEMENT DE PHASE (logging)
    // ========================================
    if (emergencyPhaseCache != emergencyPhase) {
        Serial.printf("RessortRoyal2::handleEmergencyState: Emergency phase changed from %u to %u\r\n", 
            emergencyPhaseCache, emergencyPhase);
        emergencyPhaseCache = emergencyPhase;
    }

    // ========================================
    // MACHINE À ÉTATS DES PHASES D'URGENCE
    // ========================================
    switch (emergencyPhase) {
        case 0: {
                // ========================================
                // PHASE 0: Vérification des conditions de sortie
                // ========================================
                
                if (now - lastEmergencyCheck < 1000) {
                    return true;  // Trop tôt
                }

                lastEmergencyCheck = now;
                
                // Informer l'utilisateur si une alarme drive est active
                // (ne bloque pas la sortie - sera effacée via le reset en phase 1)
                if (sureServo && sureServo->getHasAlarms()) {
                    char alarmMsg[128];
                    snprintf(alarmMsg, sizeof(alarmMsg),
                            "DRIVE EN ALARME AL%03u: %s - Appuyez sur RESET",
                            sureServo->getAlarmCode(),
                            sureServo->getAlarmDescription());
                    PLC_Tools::sendPeriodicMessage(true, ERROR, alarmMsg);
                }
                
                if (!canExitEmergency()) {
                    return true; // Reste en phase 0 (E-Stop physique encore actif)
                }
                
                Serial.println("✅ Phase 0: Conditions OK, passage en phase 1");
                emergencyPhase = 1;
                BorneUniverselle::prepareMessage(INFO, 
                    "Conditions OK - Appuyez sur RESET pour continuer");
                return true;
            }

        case 1: {
            // ========================================
            // PHASE 1: Attente reset utilisateur + vérification état reset
            // ========================================
            
            if (!v_alarmsReset || !v_alarmsReset->getValue()) {
                return true; // Attendre le reset utilisateur
            }
            
            // 🔧 VÉRIFIER l'état du reset servo
            ServoDrive::OperationState resetState = sureServo->getResetState();
            
            switch (resetState) {
                case ServoDrive::OperationState::DRIVE_IN_PROGRESS:
                    Serial.println("🔄 Phase 1: Reset déjà en cours, passage en phase 2");
                    emergencyPhase = 2;
                    BorneUniverselle::prepareMessage(INFO, "Reset servo en cours...");
                    break;
                    
                case ServoDrive::OperationState::DRIVE_IDLE: {
                    Serial.println("🔧 Phase 1: Démarrage reset servo");
                    bool success = sureServo->startReset();
                    if (success) {
                        Serial.println("✅ Phase 1: Reset Emergency démarré");
                        emergencyPhase = 2;
                        BorneUniverselle::prepareMessage(INFO, "Reset servo en cours...");
                    } else {
                        Serial.printf("❌ Phase 1: Échec reset: %s\n", sureServo->getLastError());
                        PLC_Tools::sendPeriodicMessage(true, ERROR, 
                            "Échec reset servo - Réessayez RESET");
                    }
                    break;
                }
                    
                case ServoDrive::OperationState::DRIVE_FAILED:
                case ServoDrive::OperationState::DRIVE_TIMEOUT: {
                    Serial.println("❌ Phase 1: Reset précédent échoué, nouveau démarrage");
                    bool retrySuccess = sureServo->startReset();
                    if (retrySuccess) {
                        Serial.println("✅ Phase 1: Reset redémarré");
                        emergencyPhase = 2;
                        BorneUniverselle::prepareMessage(INFO, "Reset servo redémarré...");
                    } else {
                        Serial.printf("❌ Phase 1: Échec retry reset: %s\n", sureServo->getLastError());
                        PLC_Tools::sendPeriodicMessage(true, ERROR, 
                            "Impossible de redémarrer le reset - Contactez le support");
                    }
                    break;
                }
                    
                default:
                    Serial.printf("⚠️ Phase 1: État reset inattendu: %s\n", 
                        ServoDrive::getOperationStateText(resetState));
                    break;
            }
            return true;
        }

        case 2: {
            // ========================================
            // PHASE 2: Surveillance du reset et sortie
            // ========================================
            
            ServoDrive::OperationState resetState = sureServo->getResetState();
            
            if (resetState == ServoDrive::OperationState::DRIVE_IN_PROGRESS) {
                return true; // Continuer à attendre
            }
            
            if (resetState != ServoDrive::OperationState::DRIVE_IDLE) {
                // Reset échoué - retour phase 0
                emergencyPhase = 0;
                BorneUniverselle::prepareMessage(ERROR, "Échec reset servo");
                return true;
            }
            
            // Reset terminé avec succès
            if (!canExitEmergency()) {
                emergencyPhase = 0; // Retour phase 0
                return true;
            }

            // ✅ CAS SPÉCIAL JOGGING : Retour automatique si JOG actif
            if (jog && jog->getValue() && !sureServo->getHasAlarms()) {
                // ✅ GUARD: ne retourner en JOGGING que si le servo est initialisé
                // Cas couvert : urgence pendant INITIALIZING (avant que initialized=true)
                if (!sureServo->getIsInitialized()) {
                    BorneUniverselle::prepareMessage(INFO, "Reset terminé - Réinitialisation requise avant JOG");
                    emergencyPhase = 0;
                    transitionToState(State::INITIALIZING, ContextCode::INIT_FAILED);
                    return true;
                }
                BorneUniverselle::prepareMessage(SUCCESS, "Reset terminé - Passage en mode JOG");
                emergencyPhase = 0;
                transitionToState(State::JOGGING, ContextCode::NORMAL);
                return true;
            }   
            
            // Sortie normale
            char buffer[100];
            snprintf(buffer, sizeof(buffer), 
                "Reset terminé - Transition vers: %s", 
                getStateNameFromTable(emergencyTargetState));
            PLC_Tools::logDiagnostic(buffer);
            BorneUniverselle::prepareMessage(SUCCESS, buffer);
            emergencyPhase = 0;

            transitionToState(emergencyTargetState, ContextCode::NORMAL);
            return true;
        }
        
        default:
            Serial.printf("❌ Phase emergency inconnue: %u - reset à phase 0\n", emergencyPhase);
            emergencyPhase = 0;
            return true;
    }
} // handleEmergencyState

bool RessortRoyal2::canExitEmergency() const {
    // ========================================
    // VÉRIFICATION DES CONDITIONS D'URGENCE
    // ========================================
    // Seules les conditions physiques non-effaçables par software bloquent ici.
    // Les alarmes drive sont traitées dans handleEmergencyState (phases 1-2 : reset).
    // ========================================
    
    bool hasEmergency = false;
    
    // 🔧 1. VÉRIFICATION E-Stop A (bouton physique)
    if (eStopA != nullptr && !eStopA->getValue()) {
        PLC_Tools::sendPeriodicMessage(
            true,
            ERROR, 
            "URGENCE: Relâchez le bouton d'arrêt d'urgence"
        );
        hasEmergency = true;
    }
    
    // ✅ Retourner false si au moins une condition physique bloque
    return !hasEmergency;
}

bool RessortRoyal2::handleHomingState(uint32_t now) {
    printCurrentState();
    if (checkEmergencyConditions(now)){
        return true;
    }

    // Reset défauts pendant le homing → interruption propre (l'utilisateur doit
    // pouvoir stopper un homing qui part dans le mauvais sens sans attendre EMERGENCY)
    if (v_alarmsReset->getIsChanged() && v_alarmsReset->getValue()) {
        Serial.println("⚠️ Reset défauts demandé pendant HOMING - interruption");
        sureServo->stopMovement(); // homingState -> IDLE proprement (pas FAILED)
        BorneUniverselle::prepareMessage(WARNING, "Homing interrompu par reset défauts");
        if (resetDrive("HOMING_INTERRUPT")) {
            resetInProgress = true;
        }
        transitionToState(State::STOP, ContextCode::STOP_MODE);
        return true;
    }

    ServoDrive::OperationStatus status;
    
    if (!sureServo->handleHoming(status)) {
        BorneUniverselle::getInstance()->setPlcBroken("Homing failed");
        return false;
    }
    
    switch (status) {
        case ServoDrive::OperationStatus::COMPLETED:
            BorneUniverselle::prepareMessage(SUCCESS, "Homing terminé avec succès");
            if (homeDone) homeDone->setValue(true);
            transitionToState(State::IDLE, ContextCode::MOVEMENT_SUCCESS);
            break;
            
        case ServoDrive::OperationStatus::SERVO_DRIVE_FATAL_ERROR:
            BorneUniverselle::prepareMessage(ERROR, "Erreur fatale pendant homing");
            transitionToState(State::EMERGENCY, ContextCode::SERVO_ALARM);
            break;
            
        case ServoDrive::OperationStatus::SERVO_DRIVE_ERROR:
            BorneUniverselle::prepareMessage(ERROR, "Erreur pendant homing");
            transitionToState(State::EMERGENCY, ContextCode::SERVO_ALARM);
            break;
            
        case ServoDrive::OperationStatus::IN_PROGRESS:
        default:
            // Homing en cours
            break;
    }
    
    return true;
}

bool RessortRoyal2::handleJoggingState(uint32_t now) {
    printCurrentState();

    if (checkEmergencyConditions(now)){
        return true;    
    }

    // ✅ GUARD: servo doit être initialisé avant tout mouvement jog
    if (!sureServo->getIsInitialized()) {
        BorneUniverselle::prepareMessage(WARNING, "Servo non initialisé - Réinitialisation requise");
        transitionToState(State::INITIALIZING, ContextCode::INIT_FAILED);
        return true;
    }

    if (!jog->getValue()) {
        Serial.println("🔄 Sortie du mode JOG demandée");
        
        // ✅ Attendre re-sync si nécessaire
        if (sureServo->getHomeDone() && !sureServo->getHasReliablePosition()) {
            Serial.println("Attente re-synchronisation position après JOG...");
            return true; // Rester en JOGGING
        }

        if (homeDone && homeDone->getValue()) {
            Serial.println("Retour à IDLE après JOG");
            transitionToState(State::IDLE, ContextCode::JOG_MODE);
        } else {
            Serial.println("Retour à STOP après JOG (home non fait)");
             transitionToState(State::STOP, ContextCode::STOP_MODE);
        }
    }
    
    ServoDrive::OperationStatus status;
    
    // ✅ Délégation pour vérification alarmes
    if (!sureServo->handleJogging(status)) {
        BorneUniverselle::getInstance()->setPlcBroken("Jogging check failed");
        return false;
    }
    
    if (status == ServoDrive::OperationStatus::SERVO_DRIVE_ERROR) {
        transitionToState(State::EMERGENCY);
        return true;
    }
 
    // Traitement normal du JOG 
    jogTreatment();
    interfaceTreatment();

    return true;
}

void RessortRoyal2::saveDriveParameters() {
    auxFunctions->setValue(8);
    Serial.printf("%lu:: Paramètres de persistance sauvegardés\r\n", millis());
}

void RessortRoyal2::jogTreatment() {
    if (fwd->getIsChanged()) {
        if (fwd->getValue()) {
            Serial.println("Jog forward");
            bool success = sureServo->jogForward();
            if (success) {
                Serial.println("✅ Jog forward via SureServo");
            } else {
                Serial.printf("❌ Échec jog forward: %s\n", sureServo->getLastError());
                BorneUniverselle::prepareMessage(ERROR, "Échec jog forward");
                transitionToState(State::EMERGENCY, ContextCode::JOG_MODE);
            } 
        } else {
            sureServo->jogStop();
        }
    }

    if (rwd->getIsChanged()) {
        if (rwd->getValue()) {  
            Serial.println("Jog reverse");
            
            bool success = sureServo->jogReverse();
            if (success) {
                Serial.println("✅ Jog reverse via SureServo");
            } else {
                Serial.printf("❌ Échec jog reverse: %s\n", sureServo->getLastError());
                BorneUniverselle::prepareMessage(ERROR, "Échec jog reverse");
                transitionToState(State::EMERGENCY, ContextCode::JOG_MODE);
            }
        } else {
            sureServo->jogStop();
        }
    }  
}

// ========================================
// MÉTHODES UTILITAIRES - IMPLÉMENTATIONS
// ========================================
bool RessortRoyal2::checkEmergencyConditions(uint32_t now) {
    State currentState = getCurrentState();
    
    // Guard: Si déjà en EMERGENCY, ne pas re-déclencher
    if (currentState == State::EMERGENCY) {
        return false;
    }
    
    // 1. VÉRIFICATION E-Stop A (NC : circuit ouvert = danger)
    if (eStopA && !eStopA->getValue()) {
        BorneUniverselle::prepareMessage(ERROR, "Arrêt d'urgence A activé !");
        transitionToState(State::EMERGENCY, ContextCode::EMERGENCY_STOP);
        return true;
    }
    
    // 2. VÉRIFICATION alarme servo drive (délégation à SureServo)
    if (sureServo && sureServo->getHasAlarms()) {
        char alarmMsg[128];
        snprintf(alarmMsg, sizeof(alarmMsg),
                "🚨 DRIVE EN ALARME AL%03u: %s - %s",
                sureServo->getAlarmCode(),
                sureServo->getAlarmDescription(),
                sureServo->getAlarmAction());
        BorneUniverselle::prepareMessage(ERROR, alarmMsg);
        transitionToState(State::EMERGENCY, ContextCode::SERVO_ALARM);
        return true;
    }
    
    // 3. VÉRIFICATION immediate stop VIRTUEL (bouton UI)
    if (v_immediateStop && v_immediateStop->getIsChanged() && v_immediateStop->getValue()) {
        BorneUniverselle::prepareMessage(ERROR, "Bouton d'arrêt immédiat activé");
        transitionToState(State::EMERGENCY, ContextCode::EMERGENCY_STOP);
        return true;
    }
    
    return false;
}

void RessortRoyal2::transitionToState(State newState, ContextCode contextCode) {
    // Utiliser transitionWithContext pour alimenter baseStateHistory
    BusinessLogic::transitionWithContext(static_cast<int>(newState), 
                                        static_cast<uint32_t>(contextCode));
}

void RessortRoyal2::handleInterrupts() {
    // Gérer les demandes d'arrêt immédiat
    if (PF8574BooleanInputNode::isInterrupt()) {
        BorneUniverselle::refresHardwareInputs();
        Serial.printf("Latency: %lu [ms]\r\n", millis() - PF8574BooleanInputNode::getTimeOfInterupt());
    }
}

void RessortRoyal2::updateOutputs() {
    // Mettre à jour le buzzer si nécessaire
    State currentState = getCurrentState();
    if (buzzer) {
        bool shouldBuzz = (currentState == State::EMERGENCY);
        if (buzzer->getValue() != shouldBuzz) {
            //buzzer->setValue(shouldBuzz);  // trop de bruit !
        }
    }
    
    // Mettre à jour servoOn
    if (servoOn) {
        bool shouldBeOn = (currentState != State::UNDEFINED && 
                          currentState != State::EMERGENCY);
        if (servoOn->getValue() != shouldBeOn) {
            servoOn->setValue(shouldBeOn);
        }
        if (servoOn->getIsChanged()) {
            char text[128];
            snprintf(text, sizeof(text), "%lu:: servoOn:: %s\r\n", millis(), servoOn->getValue() ? "true" : "false");
            BorneUniverselle::prepareMessage(INFO, text);
        }
    }

    // Synchroniser le torque
    if (fullTorque && maxTorque) {
        uint16_t targetTorque = fullTorque->getValue() ? 100 : parameters.initialTorque;
        if (maxTorque->getValue() != targetTorque) {
            maxTorque->setValue(targetTorque);
        }
    }

     static uint32_t latestPos;
    uint32_t pos = position->getValue();
    uint32_t diff = pos > latestPos ? pos - latestPos : latestPos - pos;
    if (diff > 10) {
        v_position->setValue(position->getValue());
        latestPos = pos;
    }
    displayAlarmsAndStatus();
}

void RessortRoyal2::interfaceTreatment() {
    // ========================================
    // GESTION IMMEDIATE STOP
    // ========================================
    if (v_immediateStop->getIsChanged() && v_immediateStop->getValue()) {
        char text[128];
        snprintf(text, sizeof(text), "immediateStop:: %s\r\n", v_immediateStop->getValue() ? "true" : "false");
        BorneUniverselle::prepareMessage(ERROR, text);
        transitionToState(State::EMERGENCY);
    }

    if (v_servoOn->getIsChanged()) {
        char text[128];
        snprintf(text, sizeof(text), "servoOn:: %s\r\n", v_servoOn->getValue() ? "true" : "false");
        BorneUniverselle::prepareMessage(INFO, text);
        servoOn->setValue(v_servoOn->getValue());
    }

    // ========================================
    // GESTION VITESSE JOG
    // ========================================
    if (v_jogSpeed->getIsChanged()) {
        uint32_t newSpeed = v_jogSpeed->getValue();
        if (!setJogSpeed(newSpeed)){
            v_jogSpeed->setValue(parameters.jogSpeed);
            BorneUniverselle::prepareMessage(ERROR, "Inacapable de mettre la nouvelle vitesse de jog");
        }
    }

    if (v_torque->getIsChanged()){
        parameters.initialTorque = v_torque->getValue();
        sureServo->setMaxTorque(parameters.initialTorque);
        char msg[64];
        snprintf(msg, sizeof(msg), "New max torque value via interface: %u\r\n", parameters.initialTorque);
        PLC_Tools::logDiagnostic(msg);
        PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
    }

    // ========================================
    // GESTION FREE RUN
    // ========================================
    if (freeRun && freeRun->getIsChanged()) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Mode Free Run %s via interface", freeRun->getValue() ? "activé" : "désactivé");
        PLC_Tools::logDiagnostic(msg);
        BorneUniverselle::prepareMessage(INFO, freeRun->getIsChanged() ? "Mode Free Run activé" : "Mode Free Run désactivé");
    }

    // ========================================
    // GESTION RESET ALARMES
    // ========================================
    if (v_alarmsReset->getIsChanged() && v_alarmsReset->getValue()) {
        PLC_Tools::logDiagnostic("Reset alarmes demandé via interface", true);

        bool resetResult = resetDrive("INTERFACE");
        if (resetResult) {
            resetInProgress = true;
            Serial.println("✅ Reset alarmes démarré - surveillance activée");
            BorneUniverselle::prepareMessage(INFO, "Reset alarmes en cours...");
        } else {
            Serial.printf("❌ Échec reset interface: %s\n", sureServo->getLastError());
            BorneUniverselle::prepareMessage(ERROR, "Échec démarrage reset alarmes");
        }
    }

    // ✅ SURVEILLANCE CONTINUE reset
    if (resetInProgress) {
        ServoDrive::OperationState resetState = sureServo->getResetState();
        switch (resetState) {
            case ServoDrive::OperationState::DRIVE_IDLE:
                resetInProgress = false;
                Serial.println("✅ Reset interface terminé avec succès");
                BorneUniverselle::prepareMessage(SUCCESS, "Reset alarmes terminé avec succès !");
                break;
            case ServoDrive::OperationState::DRIVE_FAILED:
            case ServoDrive::OperationState::DRIVE_TIMEOUT:
                resetInProgress = false;
                Serial.printf("❌ Reset interface échoué: %s\n", sureServo->getLastError());
                BorneUniverselle::prepareMessage(ERROR, "Échec reset alarmes");
                break;
            case ServoDrive::OperationState::DRIVE_IN_PROGRESS:
                break;
        }
    }

    // ========================================
    // GESTION VITESSE CYCLE
    // ========================================
    if (v_cycleSpeed && v_cycleSpeed->getIsChanged()) {
        cycleSpeedChange();
        PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
    }

    // ========================================
    // GESTION TARGET UTILISATEUR
    // ========================================
    if (userTarget && userTarget->getIsChanged()) {
        parameters.userTarget = userTarget->getValue();
        PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
    }

   // ========================================
    // GESTION POSITION DE RETOUR
    // ========================================
    if (returnPos && returnPos->getIsChanged()) {
        parameters.returnPos = returnPos->getValue();
        PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
        if (parameters.goAndBack) {
            sureServo->configurePR(2, parameters.returnPos, parameters.cycleSpeed, false);
            Serial.println("✅ Go-and-back: PR2 reconfiguré (returnPos changé)");
        }
    }

    if (userTarget && userTarget->getIsChanged()) {
        parameters.userTarget = userTarget->getValue();
        PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
        if (parameters.goAndBack) {
            sureServo->configurePR(1, parameters.userTarget, parameters.cycleSpeed, true);
            Serial.println("✅ Go-and-back: PR1 reconfiguré (userTarget changé)");
        }
    }

    if (goAndBack->getIsChanged()) {
        parameters.goAndBack = goAndBack->getValue();
        PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);

        if (parameters.goAndBack) {
            sureServo->configurePR(1, parameters.userTarget, parameters.cycleSpeed, true);
            sureServo->configurePR(2, parameters.returnPos,  parameters.cycleSpeed, false);
            Serial.println("✅ Mode go-and-back: PR1+PR2 configurés");
        } else {
            sureServo->clearPRConfig();
#ifdef PERF_MONITOR
            sureServo->pmDeactivate();
#endif
            Serial.println("✅ Mode simple: PR restaurés");
        }
    }

    // ========================================
    // GESTION NOMBRE DE CYCLES À FAIRE
    // ========================================
    if (numberOfCyclesToDo && numberOfCyclesToDo->getIsChanged()) {
        parameters.numberOfCyclesToDo = numberOfCyclesToDo->getValue();
        visu->setMaxValue(parameters.numberOfCyclesToDo);
        PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
    }

    // ========================================
    // GESTION RESET COMPTEUR CYCLES
    // ========================================
    if (nbCyclesClear && nbCyclesClear->getIsChanged() && nbCyclesClear->getValue()) {
        Serial.println("🔄 Reset compteur de cycles demandé");
        parameters.numberOfCyclesMade = 0;
        if (numberOfCyclesMade) {
            numberOfCyclesMade->setValue(0);
        }
        PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
        BorneUniverselle::prepareMessage(SUCCESS, "Compteur de cycles remis à zéro");
    }

    // ========================================
    // GESTION HORLOGE
    // ========================================
    if (v_setPLC_Clock->getIsChanged() && v_setPLC_Clock->getValue()) {
        BorneUniverselle::getInstance()->clockUpdateRequest();
    }

    if (shouldShowClock()) {
        updateClockNode(v_clock);
    }
}

bool RessortRoyal2::saveMachineParameters() {
    Serial.println("\n========================================");
    Serial.println("Enregistrement du fichier de paramètres machine");
    Serial.println("========================================");
    
    // Créer le document JSON avec tous les paramètres
    SafeJsonDocument doc(4096);
    
    // Paramètres machine de base
    doc[MACHINE_NAME_KEY] = parameters.machineName;
    
    // Paramètres de mouvement
    doc[USER_TARGET_KEY] = parameters.userTarget;
    doc[RETURN_POS_KEY] = parameters.returnPos;
    doc[GO_AND_BACK_KEY] = parameters.goAndBack;
    doc[JOG_SPEED_KEY] = parameters.jogSpeed;
    doc[CYCLE_SPEED_KEY] = parameters.cycleSpeed;
    
    // Paramètres de cycle
    doc[NUMBER_OF_CYCLES_KEY] = parameters.numberOfCyclesToDo;
    doc[NUMBER_OF_CYCLES_MADE_KEY] = parameters.numberOfCyclesMade;
    doc[NB_TOTAL_CYCLES_KEY] = parameters.nbTotalCycles;
    
    // Paramètres de contrôle
    doc[FULL_TORQUE_KEY] = parameters.fullTorque;
    doc[INITIAL_TORQUE_KEY] = parameters.initialTorque;
    
    // Sauvegarder via la méthode interne
    return saveRessortRoyalConfigInternal(doc);
}

// ========================================
// SAVE RESSORT ROYAL CONFIG INTERNAL
// ========================================

bool RessortRoyal2::saveRessortRoyalConfigInternal(const JsonDocument& doc) {
    Serial.println("RessortRoyal2::saveRessortRoyalConfigInternal - Début");
    
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    
    // Marquer la sauvegarde comme interne pour éviter le callback récursif
    markInternalSave(CONFIG_FILE_NAME);
    
    // Sauvegarder dans le fichier
    if (!persistence.saveJsonToFile(CONFIG_FILE_NAME, doc)) {
        Serial.printf("❌ Erreur écriture %s: %s\n", CONFIG_FILE_NAME, persistence.getLastError());
        BorneUniverselle::prepareMessage(ERROR, "Échec sauvegarde paramètres machine");
        return false;
    }
    
    Serial.println("✅ Paramètres machine sauvegardés dans RessortRoyal2.json");
    BorneUniverselle::prepareMessage(SUCCESS, "Paramètres sauvegardés");
    
    return true;
}

void RessortRoyal2::displayAlarmsAndStatus() {
    // Gestion des erreurs Modbus avec throttling intelligent
    MyModbus& myModbus = MyModbus::getInstance();
    
    if (modbusError) {
        bool hasError = (myModbus.getLastEvent() != Modbus::EX_SUCCESS);
        
        // ✅ Utiliser sendPeriodicMessage pour gérer le throttling
        PLC_Tools::sendPeriodicMessage(
            hasError,
            ERROR,
            "⚠️ Erreur Modbus détectée - Vérifiez la connexion du servo drive"
        );
        
        // Mettre à jour la variable d'état
        modbusError->setValue(hasError);
    }
} // displayAlarmsAndStatus // displayAlarmsAndStatus

bool RessortRoyal2::setJogSpeed(uint32_t speed) {
    Serial.println("🎛️ Définition vitesse jog via SureServo...");
    
    if (speed == 0) {
        speed = 100;
        BorneUniverselle::prepareMessage(WARNING, "Vitesse jog nulle - corrigée a 100 tr/min");
        // Permettre la valeur 0 pour arrêter le jog
    }
    
    // Utiliser SureServo pour définir la vitesse
    bool success = sureServo->setJogSpeed(speed);
    
    if (success) {
        // Mettre à jour la variable locale pour cohérence
        
        char successMsg[150];
        snprintf(successMsg, sizeof(successMsg), "✅ Vitesse jog définie via SureServo: %lu t/min", (unsigned long)speed);
        BorneUniverselle::prepareMessage(SUCCESS, successMsg);
        parameters.jogSpeed = speed;

        PLC_Persistence::getInstance().markDirty(CONFIG_FILE_NAME);
        BorneUniverselle::prepareMessage(SUCCESS, "Vitesse jog mise à jour et sauvegardée"); 
    } else {
        Serial.printf("❌ Échec configuration vitesse jog: %s\n", sureServo->getLastError());
        BorneUniverselle::prepareMessage(ERROR, "Échec configuration vitesse");
    }
    
    return success;
}

void RessortRoyal2::cycleSpeedChange() {
    if (!v_cycleSpeed) {
        return;
    }
    
    uint32_t speed = v_cycleSpeed->getValue();
    parameters.cycleSpeed = speed;
    
    if (sureServo) {
        if (!sureServo->setSpeedAndRamp((uint8_t)speed, 0)) {
            Serial.printf("❌ Échec mise à jour vitesse cycle: %s\n", sureServo->getLastError());
            return;
        }

        if (parameters.goAndBack) {
            sureServo->configurePR(1, parameters.userTarget, parameters.cycleSpeed, true);
            sureServo->configurePR(2, parameters.returnPos,  parameters.cycleSpeed, false);
            Serial.println("✅ Go-and-back: PR1+PR2 reconfigurés (vitesse changée)");
        }
    }
    
    Serial.printf("✅ Vitesse cycle configurée: %lu\n", speed);
}

void RessortRoyal2::onStateEnter(int newState, int oldState) {
    // Attention pas de changement d'état dans cette fonction !!!
    State ressortRoyalNewState = static_cast<State>(newState);
    //State ressortRoyalOldState = static_cast<State>(oldState);

    // Mettre à jour l'affichage de l'état dans l'interface
    statesMachine->setValue(stateToString(newState));
    
    switch (ressortRoyalNewState) {
       case State::EMERGENCY:
        Serial.println("🚨 Entrée en état EMERGENCY");
        emergencyPhase = 0;
        emergencyPhaseCache = 99;   // Force le log dès le 1er appel
        lastEmergencyCheck = 0;     // Force la vérification immédiate en phase 0
        emergencyTargetState = (homeDone && homeDone->getValue()) ? State::IDLE : State::STOP;
        Serial.printf("🎯 Emergency target: %s\n", getStateNameFromTable(emergencyTargetState));
        
        // ✅ Arrêt physique du moteur (aligné sur Formaca2)
        setEmergencyMode(true);
        break;
            
        case State::INITIALIZING:
            Serial.println("⚙️ Entrée en état INITIALIZING");
            break;
            
        case State::STOP:
            Serial.println("🛑 Entrée en état STOP");
            break;
            
        case State::IDLE:
            Serial.println("✅ Entrée en état IDLE");
            break;
            
        case State::HOMING:
            Serial.println("🏠 Entrée en état HOMING");
            break;

        case State::GOING:
            Serial.println("➡️ Entrée en état GOING");
            goingMovementStarted = false;
            currentTargetPosition = 0;
            // goingPhase contrôle la cible, pas besoin de setChainedPRMode ici
            break;

        case State::CYCLING:
            Serial.println("🔄 Entrée en état CYCLING");
            // Ne pas écraser goingPhase si un retour forcé est en cours
            if (goingPhase) {
                sureServo->setChainedPRMode(true);
            }
            // Si goingPhase == false : retour de recovery, setChainedPRMode(false) déjà fait
            goingMovementStarted = false;
            currentTargetPosition = 0;
            break;
            
        case State::JOGGING:
            Serial.println("🎛️ Entrée en état JOGGING");
            setJogSpeed(parameters.jogSpeed);
            break;
            
        default:
            Serial.printf("ℹ️ Entrée en état inconnu: %d\n", newState);
            break;
    }
}

void RessortRoyal2::onStateExit(int oldState, int newState) {
    State ressortRoyalOldState = static_cast<State>(oldState);
    //State ressortRoyalNewState = static_cast<State>(newState);
    
    Serial.printf("Sortie de l'état: %s\n", stateToString(oldState));
    
    // Désactiver emergency stop lors de la sortie de EMERGENCY
    if (ressortRoyalOldState == State::EMERGENCY) {
        // Désactiver l'arrêt d'urgence seulement à la sortie de EMERGENCY
        setEmergencyMode(false);
    }
}

void RessortRoyal2::onTransitionComplete(int newState, int oldState) {
    Serial.printf("Transition complète: %s -> %s\n", 
                 stateToString(oldState), 
                 stateToString(newState));
}

bool RessortRoyal2::resetDrive(const char* context) {
    Serial.printf("🔧 Reset drive demandé [%s]\n", context ? context : "Unknown");
    
    // ✅ PROTECTION : Vérifier l'état AVANT de tenter
    ServoDrive::OperationState currentResetState = sureServo->getResetState();
    
    if (currentResetState == ServoDrive::OperationState::DRIVE_IN_PROGRESS) {
        Serial.printf("⚠️ Reset déjà en cours - pas de nouveau démarrage\n");
        return true; // Considérer comme succès car reset en cours
    }
    
    // ✅ DÉLÉGATION COMPLÈTE À SURESERVO
    bool success = sureServo->startReset();
    
    if (success) {
        Serial.printf("✅ Reset [%s] démarré via SureServo\n", context);
    } else {
        Serial.printf("❌ Échec reset [%s]: %s\n", context, sureServo->getLastError());
    }
    
    return success;
}

void RessortRoyal2::setEmergencyMode(bool status) {
    if (status) {
        // ✅ DÉLÉGUER l'arrêt physique à SureServo (Design Pattern Strategy)
        if (sureServo) {
            sureServo->emergencyStop();
        }
        
        // ✅ Mettre à jour le node virtuel UI (responsabilité RessortRoyal2)
        if (v_immediateStop) {
            v_immediateStop->setValue(true);
        }
        
        BorneUniverselle::prepareMessage(WARNING, "EMERGENCY STOP ACTIVÉ");
    } else {
        // ✅ Désactivation de l'emergency
        if (v_immediateStop) {
            v_immediateStop->setValue(false);
        }
        BorneUniverselle::prepareMessage(WARNING, "EMERGENCY STOP DESACTIVÉ");
    }
}

const char* RessortRoyal2::getContextNameFromTable(ContextCode code) {
    switch(code) {
        #define X(name, category, desc) case ContextCode::name: return #name;
        RESSORT_ROYAL2_CONTEXT_TABLE
        #undef X
        default: return "UNKNOWN";
    }
}

// Description (ex: "Arrêt d'urgence")
const char* RessortRoyal2::getContextDescriptionFromTable(ContextCode code) {
    switch(code) {
        #define X(name, category, desc) case ContextCode::name: return desc;
        RESSORT_ROYAL2_CONTEXT_TABLE
        #undef X
        default: return "Contexte inconnu";
    }
}

// Catégorie (ex: ContextCategory::EMERGENCY)
BusinessLogic::ContextCategory RessortRoyal2::getContextCategoryFromTable(ContextCode code) {
    switch(code) {
        #define X(name, category, desc) case ContextCode::name: return BusinessLogic::ContextCategory::category;
        RESSORT_ROYAL2_CONTEXT_TABLE
        #undef X
        default: return BusinessLogic::ContextCategory::UNKNOWN;
    }
}

// 4. Override contextToString
const char* RessortRoyal2::contextToString(uint32_t context) const {
    if (context <= static_cast<uint32_t>(ContextCode::INVALID_PARAMETER)) {
        return getContextDescriptionFromTable(static_cast<ContextCode>(context));
    }
    return BusinessLogic::contextToString(context);
}

bool RessortRoyal2::shouldShowClock() const {
    State currentState = getCurrentState();
    return (currentState == State::IDLE || 
            currentState == State::EMERGENCY || 
            currentState == State::STOP);
}

void RessortRoyal2::printPersistance() {
    File file = LittleFS.open(CONFIG_FILE_NAME, FILE_READ);
    if (!file) {
        Serial.println("Error: Failed to open config file for reading");
        return;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        char buff[1000];
        sprintf(buff, "%lu:: Formaca.json: deserializeJson() failed: ", millis());
        strcpy_P(buff + strlen(buff), (const prog_char*)error.f_str());
        Serial.println(buff);
        return;
    }

    Serial.println("Paramètres de persistance enregistrés");
    Serial.println("-------------------------------------");
    serializeJsonPretty(doc, Serial);
}
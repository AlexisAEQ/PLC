#include "SureServo.h"

//#define SURESERVO_DEBUG 1

// variables statiques
BusinessLogic* SureServo::businessLogicInstance = nullptr;

// ========================================
// CONSTRUCTEUR SURESERVO - IMPLÉMENTATION COMPLÈTE
// ========================================

SureServo::SureServo(double pulsesPerUnit, const ServoNodes& nodes, HomePositionPolicy homePositionPolicy, float maxRangeInches, const char* servoId)
    : ServoDrive(servoId),
    pulsesPerUnit(pulsesPerUnit),                    // 1
      _homePositionPolicy(homePositionPolicy),
      reliablePosition(0),                             // 2
      hasReliablePosition(false),                      // 3
      nodes(nodes),                                    // 4
      previousHomingPosition(0),                       // 5
      lastHomingPositionReadTime(0),                   // 6
      homingStableReadingsCount(0),                    // 7
      previousIdlePosition(0),                         // 8
      lastIdlePositionReadTime(0),                     // 9
      idleStableReadingsCount(0),                      // 10
      resyncInProgress(false),                         // 11
      resyncStartTime(0),                              // 12
      initState(OperationState::DRIVE_IDLE),           // 13
      resetState(OperationState::DRIVE_IDLE),          // 14
      moveState(OperationState::DRIVE_IDLE),           // 15
      homingState(OperationState::DRIVE_IDLE),         // 16
      initPhase(0),                                    // 17
      resetPhase(0),                                   // 18
      movePhase(0),                                    // 19
      homingPhase(0),                                  // 20 
      lastTargetPositionReached(false),                // 21
      signalValidated(false),                          // 22
      operationStartTime(0),                           // 23
      phaseStartTime(0),                               // 24
      initialized(false),                              // 25
      currentPr(1),                                    // 26
      targetPosition(0),                               // 27
      currentJogSpeed(1000),                           // 28
      currentPrSpeed(8),                               // 29
      lastError(""),                                   // 30
      currentEEPROMMode(EEPROMMode::AUTO_SAVE_DISABLED), // 31
      lastEEPROMSaveTime(0),                           // 32
      eepromSaveInProgress(false),                     // 33
      maxRangePuu(0) {                                 // 34
    
    // Calculer maxRangePuu si spécifié (calcul en double pour matcher la précision
    // de Formaca2::convToPUU() et éviter un écart d'arrondi entre les deux conversions)
    if (maxRangeInches > 0) {
        maxRangePuu = (uint32_t)((double)maxRangeInches * pulsesPerUnit);
        Serial.printf("=== INITIALISATION SURESERVO ===\n");
        Serial.printf("Limite physique: %.2f\" = %lu PUU\n", maxRangeInches, maxRangePuu);
    }
    
    Serial.println("=== INITIALISATION SURESERVO ===");
    Serial.println("Position interne: pas encore fiable - utilisation nodes");
   
    
    // ========================================
    // VALIDATION DES NODES OBLIGATOIRES
    // ========================================
    
    if (!nodes.validateRequired()) {
        String errorMsg = "SureServo: Nodes obligatoires manquants - ";
        
        // Diagnostic détaillé des nodes manquants
        if (!nodes.servoOn) errorMsg += "servoOn ";
        if (!nodes.immediateStop) errorMsg += "immediateStop ";
        if (!nodes.alarmsReset) errorMsg += "alarmsReset ";
        if (!nodes.position) errorMsg += "position ";
        if (!nodes.targetA) errorMsg += "targetA ";
        if (!nodes.targetB) errorMsg += "targetB ";
        if (!nodes.targetC) errorMsg += "targetC ";
        if (!nodes.trigger) errorMsg += "trigger ";
        if (!nodes.jogSpeed) errorMsg += "jogSpeed ";
        if (!nodes.maxTorque) errorMsg += "maxTorque ";
        if (!nodes.pr1Speed) errorMsg += "pr1Speed ";
        if (!nodes.pr2Speed) errorMsg += "pr2Speed ";
        if (!nodes.pr3Speed) errorMsg += "pr3Speed ";
        if (!nodes.auxFunctions) errorMsg += "auxFunctions ";
        if (!nodes.status) errorMsg += "status ";
        if (!nodes.alarms) errorMsg += "alarms ";
        if (!nodes.absoluteCoordonateSystemStatus) errorMsg += "absoluteCoordonateSystemStatus ";
        
        Serial.println(errorMsg);
        setError(errorMsg);
        return; // Construction échouée, mais objet créé quand même
    }
    
    // ========================================
    // VALIDATION DES PARAMÈTRES
    // ========================================
    
    if (pulsesPerUnit <= 0.0) {
        setError("SureServo: pulsesPerUnit ne peut pas être zéro");
        return;
    }

    Serial.printf("Pulses per unit: %.4f\n", pulsesPerUnit);
    
    // ========================================
    // INITIALISATION DES VARIABLES D'ÉTAT
    // ========================================
    
    // États des machines à états
    initState = OperationState::DRIVE_IDLE;
    resetState = OperationState::DRIVE_IDLE;
    moveState = OperationState::DRIVE_IDLE;
    homingState = OperationState::DRIVE_IDLE;
    
    // Phases internes
    initPhase = 0;
    resetPhase = 0;
    movePhase = 0;
    homingPhase = 0;
    
    // Timers
    operationStartTime = 0;
    phaseStartTime = 0;
    
    // État général
    initialized = false;
    currentPr = 1;  // Commencer par PR1
    targetPosition = 0;
    currentJogSpeed = nodes.jogSpeed->getValue();  // Vitesse de jog par défaut
    currentPrSpeed = 8;      // Vitesse PR par défaut (0-15, 8 = vitesse moyenne)
    if (nodes.maxTorque == 0){
        Serial.println("ATTENTION !!! Le couple max est a 0, on le met a 100 %");
        nodes.maxTorque->setValue(100);  // Couple max par défaut (100%)
    }
    lastError = "";
    
    // ========================================
    // INITIALISATION DE L'ÉTAT DÉCODÉ
    // ========================================
    
    status.reset();
    
    // ========================================
    // CONFIGURATION INITIALE DES NODES DE SORTIE
    // ========================================
    
    // Initialisation des vitesses PR avec la valeur par défaut (nodes garantis non-null par validateRequired)
    nodes.pr1Speed->setValue(currentPrSpeed);
    nodes.pr2Speed->setValue(currentPrSpeed);
    nodes.pr3Speed->setValue(currentPrSpeed);
    
    // Vitesse de jog par défaut
    nodes.jogSpeed->setValue(STOP_VALUE);
    
    // Fonctions auxiliaires (désactiver écriture EEPROM automatique)
    nodes.auxFunctions->setValue(5);
    
    // S'assurer que les contrôles sont désactivés au démarrage
    nodes.servoOn->setValue(false);
    nodes.immediateStop->setValue(false);
    nodes.alarmsReset->setValue(false);
    nodes.trigger->setValue(0);
    
    // Initialiser les positions cibles à zéro
    nodes.targetA->setValue(0);
    nodes.targetB->setValue(0);
    nodes.targetC->setValue(0);
    
    // ========================================
    // DIAGNOSTIC DE CONFIGURATION
    // ========================================
    
    Serial.printf("Nodes obligatoires: OK (%zu)\n", 17); // Nombre de nodes obligatoires
    Serial.printf("Nodes optionnels: %zu configurés\n", nodes.countOptional());
    
    // Liste des nodes optionnels configurés
    if (nodes.countOptional() > 0) {
        Serial.println("Nodes optionnels disponibles:");
        if (nodes.servoReady) Serial.println("  - servoReady");
        if (nodes.servoActivated) Serial.println("  - servoActivated");
        if (nodes.zeroSpeed) Serial.println("  - zeroSpeed");
        if (nodes.targetSpeedRated) Serial.println("  - targetSpeedRated");
        if (nodes.targetPositionReached) Serial.println("  - targetPositionReached");
        if (nodes.servoAlarm) Serial.println("  - servoAlarm");
        if (nodes.homeDone) Serial.println("  - homeDone");
        if (nodes.modbusError) Serial.println("  - modbusError");
        if (nodes.driveInitialised) Serial.println("  - driveInitialised");
        if (nodes.absolutePositionLost) Serial.println("  - absolutePositionLost");
        if (nodes.batteryAlarm) Serial.println("  - batteryAlarm");
        if (nodes.multipleTurnsOverflow) Serial.println("  - multipleTurnsOverflow");
        if (nodes.puuOverflow) Serial.println("  - puuOverflow");
        if (nodes.absoluteCoordonateNotSet) Serial.println("  - absoluteCoordonateNotSet");
    }
    
    // ========================================
    // LECTURE INITIALE DES REGISTRES
    // ========================================
    
    // Lecture de l'état initial (nodes garantis non-null par validateRequired)
    status.statusRegister = nodes.status->getValue();
    status.decodeStatusRegister();
    Serial.printf("Status initial: 0x%04X\n", status.statusRegister);
    
    status.alarmsRegister = nodes.alarms->getValue();
    Serial.printf("Alarms initial: 0x%04X\n", status.alarmsRegister);
    
    status.coordSystemRegister = nodes.absoluteCoordonateSystemStatus->getValue();
    status.decodeCoordSystemRegister();
    Serial.printf("Coord system initial: 0x%04X\n", status.coordSystemRegister);
    
    Serial.printf("Position initiale: %lu PUU\n", nodes.position->getValue());
    
    // ========================================
    // MISE À JOUR INITIALE DES NODES DE SORTIE
    // ========================================
    
    updateOptionalOutputNodes();
    
    // ========================================
    // VÉRIFICATION DE SANTÉ INITIALE
    // ========================================
    
    if (status.servoAlarm) {
        Serial.printf("ATTENTION: Servo en alarme au démarrage (0x%04X)\n", status.alarmsRegister);
        setError("Servo en alarme au démarrage");
    } else if (!status.servoReady) {
        Serial.println("INFO: Servo pas encore prêt - initialisation requise");
    } else {
        Serial.println("INFO: Servo prêt");
    }

    initializeEEPROMConfiguration();

    Serial.println("Configuration EEPROM initialisée (auto-save désactivé)");
    
    Serial.println("=== SURESERVO INITIALISÉ ===");
} // constructeur SureServo

void SureServo::process() {
#ifdef PERF_MONITOR
    uint32_t _pm_t0 = micros();
#endif
    uint32_t now = millis();

    // ========================================
    // RELÂCHEMENT DIFFÉRÉ DE immediateStop (après stopMovement)
    // ========================================
    
    if (pendingReleaseImmediateStop) {
        uint32_t elapsed = now - immediateStopTimestamp;
        
        if (elapsed >= IMMEDIATE_STOP_DURATION_MS) {
            nodes.immediateStop->setValue(false);
            pendingReleaseImmediateStop = false;
            
            // ✅ RÉINITIALISATION DU FLAG DE BLOCAGE
            // Le délai de sécurité est écoulé, autoriser les nouveaux mouvements
            if (movementCancelled) {
                movementCancelled = false;
                Serial.println("   → movementCancelled réinitialisé - mouvements autorisés");
            }
            
            Serial.printf("✅ immediateStop relâché après %lu ms (arrêt propre terminé)\n", elapsed);
        }
    }

    // ========================================
    // MISE À JOUR DE LA STRUCTURE STATUS
    // ========================================
    
    if (status.updateFromNodes(nodes)) {
        // Des changements ont été détectés
        // Log optionnel si nécessaire pour debug
        // logStatusChange("process");
    }

    uint16_t currentStatusValue = nodes.status->getValue();
    if (currentStatusValue != lastLoggedStatusValue || 
        (now - lastStatusLogTime) > MIN_LOG_INTERVAL_MS) {
        
        // Log seulement si changement ou après délai
        if (moveState == OperationState::DRIVE_IN_PROGRESS) {
           // Serial.printf("🎯 MOVING: État SureServo = %d\n", currentStatusValue);
        }
        
        lastLoggedStatusValue = currentStatusValue;
        lastStatusLogTime = now;
    }

    // ========================================
    // RELÂCHEMENT DIFFÉRÉ DU GUARD homeAtCurrentPosition
    // ========================================
    if (homeAtCurrentPositionInProgress && homeAtCurrentPositionEndTime > 0) {
        if (millis() - homeAtCurrentPositionEndTime >= HOME_AT_POS_GUARD_MS) {
            homeAtCurrentPositionInProgress = false;
            homeAtCurrentPositionEndTime = 0;
            Serial.println("✅ homeAtCurrentPosition guard expiré - détection redémarrage réactivée");
        }
    }

    // ========================================
    // DÉTECTION DE REDÉMARRAGE DU DRIVE
    // ========================================
    detectDriveRestart(now);

    static uint32_t lastValidCommunication = now;
    static uint32_t consecutiveTimeouts = 0;

    // ✅ SURVEILLER LA COMMUNICATION MODBUS
    bool hasValidData = (nodes.status && nodes.status->getValue() > 0) ||
                       (nodes.position && nodes.position->getValue() != 0xFFFFFFFF);

    if (hasValidData) {
        lastValidCommunication = now;
        consecutiveTimeouts = 0;
    } else {
        consecutiveTimeouts++;

        // Détecter une perte de communication le drive
        if (now - lastValidCommunication > MODBUS_TIMEOUT_MS) {
            Serial.printf("🚨 SureServo: Perte communication Modbus depuis %lu ms\n",
                         now - lastValidCommunication);

            // ✅ MESSAGE UTILISATEUR POUR PERTE COMMUNICATION
            static uint32_t lastCommErrorMessage = 0;
            if (now - lastCommErrorMessage > 10000) { // Une fois toutes les 10 secondes max
                BorneUniverselle::prepareMessage(ERROR,
                    "Perte de communication avec le drive servo - Vérifiez les connexions");
                lastCommErrorMessage = now;
            }

            // Notifier la classe métier quel que soit l'état courant (idle inclus)
            notifyBusinessLogicFault("Perte communication Modbus avec le drive servo");

            // Interrompre les opérations en cours
            if (homingState == OperationState::DRIVE_IN_PROGRESS) {
                Serial.println("⚠️ Interruption du homing - perte communication");
                setError("Perte communication pendant homing");
                homingState = OperationState::DRIVE_FAILED;
            }

            if (moveState == OperationState::DRIVE_IN_PROGRESS) {
                Serial.println("⚠️ Interruption du mouvement - perte communication");
                setError("Perte communication pendant mouvement");
                moveState = OperationState::DRIVE_FAILED;
                releaseFastStatusPolling();
            }
        }
    }

    static bool wasReady = false;
    bool currentlyReady = getIsReady();
    
    if (!wasReady && currentlyReady) {
        Serial.println("🔄 Servo redevenu ready - Reset conditions resync");
        endPositionResync(false);
        previousIdlePosition = 0;
        lastIdlePositionReadTime = 0;
    }
    wasReady = currentlyReady;

    // ========================================
    // TRAITEMENT DES MACHINES À ÉTATS
    // ========================================
    
    // Traitement de l'initialisation
    if (initState == OperationState::DRIVE_IN_PROGRESS) {
        processInitialize(now);
    }
    
    // Traitement du reset
    if (resetState == OperationState::DRIVE_IN_PROGRESS) {
        processReset(now);
    }
    
    // Traitement des mouvements
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        processMove(now);
    }
    
    // Traitement du homing
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        processHoming(now);
    }
    
    // ========================================
    // MISE À JOUR DES NODES DE SORTIE
    // ========================================
    
    updateOptionalOutputNodes();
    // ========================================
    // GESTION EEPROM SAVE EN COURS
    // ========================================
    
    if (eepromSaveInProgress && (now - lastEEPROMSaveTime) > EEPROM_SAVE_DELAY_MS) {
        setEEPROMMode(currentEEPROMMode);
        eepromSaveInProgress = false;
        Serial.println("EEPROM: Sauvegarde terminée, mode restauré");
    }

    // ========================================
    // HOME AT CURRENT POSITION - machine à états
    // ========================================
    // ⚠️ DOIT s'exécuter AVANT le bloc cohérence position.
    // La phase 4 écrit reliablePosition=0 / hasReliablePosition=true.
    // Si ce bloc venait après, le bloc cohérence verrait encore l'ancienne
    // valeur Modbus (22M PUU) et invaliderait hasReliablePosition dans le
    // même cycle, déclenchant un faux positif de mouvement dans la re-sync.
    processHomeAtCurrentPosition();

    bool homingCorrectionGraceActive = homingCorrectionTime > 0 &&
                                        (now - homingCorrectionTime) < HOMING_CORRECTION_GRACE_MS;

    if (hasReliablePosition) {
        if (homeAtCurrentPositionInProgress || homingCorrectionGraceActive) {
            // Guard: drive pas encore à jour après home virtuel, ou courte fenêtre après
            // une correction homing "position aberrante forcée à 0" — laisser une chance
            // à la lecture suivante avant de réinvalider sur la base du même relevé.
        } else {
            // ✅ int32_t : plus besoin du garde-fou overflow, la comparaison signée
            // gère nativement les positions négatives (axe qui a dérivé sous 0)
            int32_t nodePos = nodes.position->getValueAsInt32();
            int32_t difference = (nodePos > reliablePosition) ?
                                 (nodePos - reliablePosition) :
                                 (reliablePosition - nodePos);
            if (difference > (int32_t)TOLERANCE_PUU) {
                hasReliablePosition = false;
                // Serial.printf("❌ Position interne invalidée - divergence: %ld PUU\n", (long)difference);
            }
        }
    }
    
    static bool wasMoving = false;
    bool currentlyMoving = !status.zeroSpeed;

    if (!wasMoving && currentlyMoving) {
        if (hasReliablePosition) {
            hasReliablePosition = false;
            Serial.printf("🔄 Re-sync status change: %s → %s\n", "FIABLE", "NON-FIABLE");
        }
    }
    wasMoving = currentlyMoving;

    // TENTATIVE DE RE-SYNCHRONISATION
    if (!currentlyMoving && !hasReliablePosition) {
        attemptPositionResync(now);
    }
    
    // ========================================
    // DIAGNOSTIC PÉRIODIQUE (OPTIONNEL)
    // ========================================
    
    static uint32_t lastDiagnostic = 0;
    if (now - lastDiagnostic > 30000) { // Toutes les 30 secondes
        // Log de santé périodique
        if (consecutiveTimeouts > 10) {
            Serial.printf("⚠️ SureServo: %lu timeouts consécutifs détectés\n", consecutiveTimeouts);
        }
        
        // Vérification de cohérence d'état
        if (initialized && !getIsReady() && !getHasAlarms()) {
            Serial.println("⚠️ SureServo: État incohérent - initialisé mais pas ready sans alarme");
        }
        
        lastDiagnostic = now;
    }

    /*
    // PAs fini de développer... mais l'idée est de pouvoir déclencher un clearPuu depuis l'UI (via un node) pour forcer le servo à remettre à zéro sa position interne (reliée à l'encodeur) avec la position du node. Utile en cas de dérive ou de perte de synchronisation, pour éviter d'avoir à refaire un homing complet.
    if (clearUserPulsesPending){
        nodes.clearPuu->setValue(clearPuu);
        if (clearPuu){
             clearPuu = false;
             return;
        }
       
        if (!clearPuu){
            clearUserPulsesPending = false;
        }
    }
    */
#ifdef PERF_MONITOR
    if (pm_active) {
        uint32_t _d = micros() - _pm_t0;
        pm_processTimeCumul += _d;
        pm_processCallCount++;
        if (_d > pm_processTimeMax) pm_processTimeMax = _d;
    }
#endif
}// process

bool SureServo::startInitialize() {
    Serial.println("🔧 SureServo::startInitialize() - DÉBUT");
    // Forcer alarmsReset à false au démarrage.
    // hideValue=false par défaut donc setValue(false) dans le constructeur
    // n'a pas déclenché d'écriture hardware — le relais peut avoir retenu
    // l'état true du cycle précédent.
    if (nodes.alarmsReset) {
        nodes.alarmsReset->setValue(true);   // force un changement de hideValue
        nodes.alarmsReset->setValue(false);  // écrit false au hardware
        Serial.println("✅ SureServo: alarmsReset forcé à false (démarrage)");
    }

    // Note: immediateStop sera forcé à true dans handleInitialize() Phase 0,
    // une fois que le Modbus est opérationnel, pour éviter AL019 au démarrage.

    // ✅ VALIDATION : Vérifier que les nodes obligatoires sont disponibles
    if (!nodes.validateRequired()) {
        setError("Nodes obligatoires manquants pour l'initialisation");
        return false;
    }
    
    // ✅ VALIDATION : Vérifier l'état actuel
    if (initState != OperationState::DRIVE_IDLE) {
        setError("Initialisation déjà en cours ou terminée");
        return false;
    }
    
    // ✅ NOUVEAU : AUTO-RECOVERY SI SERVO EN ALARME
    if (status.servoAlarm) {
        Serial.println("⚠️ SureServo: Servo en alarme détecté - Tentative de reset automatique");
        
        // Vérifier si un reset est déjà en cours
        if (resetState != OperationState::DRIVE_IDLE) {
            setError("Servo en alarme et reset déjà en cours - impossible d'initialiser");
            return false;
        }
        
        // Démarrer un reset automatique
        if (!startReset()) {
            setError("Échec du démarrage du reset automatique");
            return false;
        }
        
        Serial.println("🔄 SureServo: Reset automatique démarré avant initialisation");
        
        // Passer en mode d'initialisation avec auto-recovery
        initState = OperationState::DRIVE_IN_PROGRESS;
        initPhase = 100;  // Phase spéciale pour auto-recovery
        operationStartTime = millis();
        phaseStartTime = millis();
        lastError = "";
        
        Serial.println("✅ SureServo: Initialisation avec auto-recovery démarrée");
        return true;
    }
    
    // ✅ DÉMARRAGE NORMAL : Initialiser les variables d'état
    initState = OperationState::DRIVE_IN_PROGRESS;
    initPhase = 0;
    operationStartTime = millis();   // sera fixé en phase 0, après les writes hardware
    phaseStartTime = millis();
    
    // ✅ Clear l'erreur précédente
    lastError = "";
    
    Serial.printf("✅ SureServo: Initialisation normale démarrée (phase=%d)\n", initPhase);
    return true;
}

bool SureServo::startReset() {
    if (isPlcSuspended()) {
        setError("Reset refusé - PLC suspendu");
        Serial.println("❌ SureServo: startReset refusé - PLC suspendu");
        return false;
    }

    Serial.println("🔧 SureServo::startReset() - DÉBUT");
    
    // ✅ GESTION SIMPLE DES ÉTATS
    switch (resetState) {
        case OperationState::DRIVE_IDLE:
            // ✅ Prêt - continuer
            Serial.println("✅ SureServo: Reset - état IDLE");
            break;
            
        case OperationState::DRIVE_IN_PROGRESS:
            // ❌ Vraiment en cours - refuser
            setError("Reset déjà en cours");
            return false;
            
        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT:
            // ✅ Auto-clear des erreurs - retry autorisé
            Serial.printf("⚠️ SureServo: Reset après erreur - retry autorisé\n");
            resetState = OperationState::DRIVE_IDLE;
            resetPhase = 0;
            lastError = "";
            break;
    }
    
    // ✅ DÉMARRAGE (inchangé)
    resetState = OperationState::DRIVE_IN_PROGRESS;
    resetPhase = 0;
    operationStartTime = millis();
    phaseStartTime = millis();
    lastError = "";
    
    Serial.printf("✅ SureServo: Reset démarré (phase=%d)\n", resetPhase);
    return true;
}
    
bool SureServo::startGoToPosition(int32_t positionPuu, bool waitForSync, uint32_t timeout) {
    if (isPlcSuspended()) {
        setError("Mouvement refusé - PLC suspendu");
        Serial.printf("❌ SureServo: startGoToPosition(%lu) refusé - PLC suspendu\n", 
                     (unsigned long)positionPuu);
        return false;
    }

    Serial.printf("🔧 SureServo::startGoToPosition(%lu) - DÉBUT\n", (unsigned long)positionPuu);
    chainedPR1Complete = false;
    chainedMovingAway   = 0;
    chainedPrevPosition = nodes.position->getValueAsInt32();
    Serial.printf("🔍 DEBUG chainedPR1Target=%ld, prevPos=%ld\n", (long)chainedPR1Target, (long)chainedPrevPosition);


    // ========================================
    // 0. PROTECTION: BLOCAGE POST-ANNULATION
    // ========================================
    
    if (movementCancelled) {
        setError("Mouvement bloqué - annulation récente (attendez cycle complet)");
        Serial.println("⛔ SureServo: startGoToPosition bloqué - movementCancelled actif");
        return false;
    }
    
    // ========================================
    // 1. GESTION INTELLIGENTE DES ÉTATS
    // ========================================
    
   switch (moveState) {
        case OperationState::DRIVE_IDLE:
            Serial.println("✅ SureServo: État IDLE - démarrage autorisé");
            break;
            
        case OperationState::DRIVE_IN_PROGRESS:
            setError("Mouvement déjà en cours");
            return false;
            
        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT:
            setError("Erreur précédente - utilisez RESET d'abord");
            return false;
    }
    
    // ========================================
    // 2. VÉRIFICATIONS DE SANTÉ DU SERVO
    // ========================================
    
    if (!getIsReady()) {
        setError("SureServo pas prêt pour mouvement");
        Serial.printf("❌ SureServo: Refus - servo pas prêt (ready=%s, initialized=%s)\n",
                     getServoReady() ? "true" : "false",
                     initialized ? "true" : "false");
        return false;
    }
    
    if (getHasAlarms()) {
        setError("Servo en alarme - mouvement impossible");
        Serial.printf("❌ SureServo: Refus - alarme active (0x%04X)\n", getAlarmCode());
        return false;
    }
    
    // ========================================
    // 2.5 VÉRIFICATION LIMITE PHYSIQUE (butée droite)
    // ========================================

    if (maxRangePuu > 0 && positionPuu > (int32_t)(maxRangePuu + PHYSICAL_LIMIT_MARGIN_PUU)) {
        setError("Position cible au-delà de la butée physique");
        Serial.printf("⛔ SureServo: Refus - cible %ld PUU > limite %lu PUU (marge: %lu)\n",
                     (long)positionPuu, maxRangePuu, (unsigned long)PHYSICAL_LIMIT_MARGIN_PUU);
        return false;
    }

    // ========================================
    // 3. VÉRIFICATION POSITION (OPTIMISATION)
    // ========================================

    int32_t currentPos = getPosition();

    // Si déjà à la position ET position fiable ET synchronisation demandée
    // → succès immédiat sans bouger
    // Sans waitForSync : on fait confiance au drive, on envoie toujours le trigger
    if (waitForSync && hasReliablePosition && isAtPosition(positionPuu)) {
        int32_t difference = (currentPos > positionPuu) ?
                             (currentPos - positionPuu) :
                             (positionPuu - currentPos);
        Serial.printf("ℹ️ SureServo: Déjà à la position cible (écart: %ld PUU <= tolérance: %lu)\n",
                     (long)difference, (unsigned long)TOLERANCE_PUU);
        
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
        targetPosition = positionPuu;
        advancePR();
        
        return true;
    }
    
    // ========================================
    // 4. INITIALISATION DU MOUVEMENT
    // ========================================
    
    moveState = OperationState::DRIVE_IN_PROGRESS;
    hasReliablePosition = false;
    waitForSyncRequested = waitForSync;
    movePhase = 0;
    targetPosition = positionPuu;
    currentMoveTimeoutMs = (timeout > 0) ? timeout : MOVE_TIMEOUT_MS;
    operationStartTime = millis();
    phaseStartTime = millis();

    lastError = "";
    
    lastTargetPositionReached = nodes.targetPositionReached->getValue();
    signalValidated = false;
    
    return true;
} // startGoToPosition // startGoToPosition

bool SureServo::startHoming(uint32_t timeoutMs) {
    if (isPlcSuspended()) {
        setError("Homing refusé - PLC suspendu");
        Serial.println("❌ SureServo: startHoming refusé - PLC suspendu");
        return false;
    }

    currentHomingTimeoutMs = (timeoutMs > 0) ? timeoutMs : HOMING_TIMEOUT_MS;

    Serial.println("🔧 SureServo::startHoming() - DÉBUT");

    // Vérifications...
    if (!getIsReady()) return false;

    homeDoneInvalidated = false;  // un nouveau homing revalide le home

    // ✅ ENVOI IMMÉDIAT DE LA COMMANDE
    nodes.trigger->setValue(120);  // Reset
    nodes.trigger->setValue(0);    // Commande homing
    Serial.println("🏠 Commande homing envoyée");
    
    // Démarrer la surveillance
    homingState = OperationState::DRIVE_IN_PROGRESS;
    homingPhase = 0;  // phase 0: attendre HomeDone avant de passer à la stabilisation (phase 1)
    operationStartTime = millis();
    
    return true;
}

ServoDrive::OperationState SureServo::getInitializeState() const {
    return initState;
}

ServoDrive::OperationState SureServo::getResetState() const {
    return resetState;
}

ServoDrive::OperationState SureServo::getMoveState() const {
    return moveState;  // ✅ Simple, pas d'auto-reset
}

ServoDrive::OperationState SureServo::getHomingState() const {
    return homingState;
}

bool SureServo::jogForward(int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    if (!initialized) {
        setError("Servo non initialisé - jog impossible");
        return false;
    }
    
    if (status.servoAlarm) {
        setError("Servo en alarme - jog impossible");
        return false;
    }

    if (isPlcSuspended()) {
        setError("Commande refusée - PLC suspendu");
        Serial.println("❌ SureServo: jogForward refusé - PLC suspendu");
        return false;
    }

    int32_t pos = (int32_t)nodes.position->getValue();
    int32_t limit = (int32_t)limitPuu;
    if (maxRangePuu > 0 && (int32_t)maxRangePuu < limit) {
        limit = (int32_t)maxRangePuu;  // Butée physique (rightStop) prioritaire sur la limite appelant
    }
    int32_t slowLimit = limit - (int32_t)slowZonePuu;

    // ✅ LIMITE DURE (limit = limitPuu, éventuellement resserré par maxRangePuu/butée droite)
    if (limit != INT32_MAX && nodes.homeDone && nodes.homeDone->getValue() && pos >= limit) {
        jogStop();
        jogForwardActive = false;
        Serial.printf("⛔ SureServo: Limite forward atteinte (%ld >= %ld)\n", pos, limit);
        return false;
    }

    // ✅ PREMIER APPEL — Séquence 2 cycles : DIRECTION puis VITESSE
    if (!jogForwardActive) {
        if (!_jogSpeedRestored) {
            // **CYCLE 1 — Envoyer DIRECTION d'abord**
            _savedJogSpeed = currentJogSpeed;
            acquireFastPositionPolling();
            nodes.jogSpeed->setValue(FWD_VALUE);  // ← CHANGEMENT : direction d'abord
            _jogSpeedRestored = true;
            Serial.println("JOG FWD Cycle 1: DIRECTION (FWD_VALUE)");
            return true;  // ← attendre prochain cycle
        }
        
        // **CYCLE 2 — Envoyer VITESSE ensuite**
        // Calculer vitesse selon zone AU MOMENT DU DÉMARRAGE
        uint16_t speed = currentJogSpeed;
        if (slowZonePuu > 0 && nodes.homeDone && nodes.homeDone->getValue() && pos >= slowLimit) {
            speed = slowSpeed;  // Démarrage dans slow zone
            Serial.printf("JOG FWD Cycle 2: Démarrage dans SLOW ZONE (%u RPM)\n", speed);
        } else {
            Serial.printf("JOG FWD Cycle 2: Démarrage vitesse normale (%u RPM)\n", speed);
        }
        
        setJogSpeed(speed);  // ← CHANGEMENT : vitesse ensuite
        jogForwardActive = true;
        _jogSpeedRestored = false;
    }

    invalidateReliablePosition();

    // ✅ ZONE DE RALENTISSEMENT (ajustement dynamique pendant mouvement)
    if (slowZonePuu > 0 && nodes.homeDone && nodes.homeDone->getValue() && pos >= slowLimit) {
        uint16_t currentSpeed = nodes.jogSpeed->getValue();
        if (currentSpeed != slowSpeed) {
            nodes.jogSpeed->setValue(slowSpeed);
            Serial.printf("JOG FWD: Entrée slow zone → %u RPM\n", slowSpeed);
        }
        return true;
    }

    return true;
}

bool SureServo::jogReverse(int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    if (!initialized) {
        setError("Servo non initialisé - jog impossible");
        return false;
    }
    
    if (status.servoAlarm) {
        setError("Servo en alarme - jog impossible");
        return false;
    }

    if (isPlcSuspended()) {
        setError("Commande refusée - PLC suspendu");
        Serial.println("❌ SureServo: jogReverse refusé - PLC suspendu");
        return false;
    }

    int32_t pos = (int32_t)nodes.position->getValue();
    int32_t limit = (int32_t)limitPuu;
    int32_t slowLimit = limit + (int32_t)slowZonePuu;

    // ✅ LIMITE DURE (contrairement à jogForward, s'applique même si le home n'est pas fait :
    // 0 est le plancher absolu, indépendant de la référence de butée droite)
    if (limitPuu != INT32_MIN && pos <= limit) {
        jogStop();
        jogReverseActive = false;
        Serial.printf("⛔ SureServo: Limite reverse atteinte (%ld <= %ld)\n", pos, limit);
        return false;
    }

    // ✅ PREMIER APPEL — Séquence 2 cycles : DIRECTION puis VITESSE
    if (!jogReverseActive) {
        if (!_jogSpeedRestored) {
            // **CYCLE 1 — Envoyer DIRECTION d'abord**
            _savedJogSpeed = currentJogSpeed;
            acquireFastPositionPolling();
            nodes.jogSpeed->setValue(RWD_VALUE);  // ← CHANGEMENT : direction d'abord
            _jogSpeedRestored = true;
            Serial.println("JOG REV Cycle 1: DIRECTION (RWD_VALUE)");
            return true;  // ← attendre prochain cycle
        }
        
        // **CYCLE 2 — Envoyer VITESSE ensuite**
        // Calculer vitesse selon zone AU MOMENT DU DÉMARRAGE
        uint16_t speed = currentJogSpeed;
        if (slowZonePuu > 0 && nodes.homeDone && nodes.homeDone->getValue() &&
            pos <= slowLimit) {
            speed = slowSpeed;  // Démarrage dans slow zone
            Serial.printf("JOG REV Cycle 2: Démarrage dans SLOW ZONE (%u RPM)\n", speed);
        } else {
            Serial.printf("JOG REV Cycle 2: Démarrage vitesse normale (%u RPM)\n", speed);
        }
        
        setJogSpeed(speed);  // ← CHANGEMENT : vitesse ensuite
        jogReverseActive = true;
        _jogSpeedRestored = false;
    }

    invalidateReliablePosition();

    // ✅ ZONE DE RALENTISSEMENT (ajustement dynamique pendant mouvement)
    if (slowZonePuu > 0 && nodes.homeDone && nodes.homeDone->getValue() &&
        pos <= slowLimit) {
        uint16_t currentSpeed = nodes.jogSpeed->getValue();
        if (currentSpeed != slowSpeed) {
            nodes.jogSpeed->setValue(slowSpeed);
            Serial.printf("JOG REV: Entrée slow zone → %u RPM\n", slowSpeed);
        }
        return true;
    }

    return true;
}

bool SureServo::jogStop() {
    nodes.jogSpeed->setValue(STOP_VALUE);

    releaseFastPositionPolling();
    if (jogForwardActive || jogReverseActive) {
        currentJogSpeed = _savedJogSpeed;
    }

    jogForwardActive = false;
    jogReverseActive = false;
    _jogSpeedRestored = false;

    pendingResyncAfterJogStop = true;
    jogToStopDetected = true;
    jogStopTimestamp = millis();
    
    Serial.println("✅ SureServo: Jog arrêté - Re-sync programmée (non-bloquant)");
    
    if (moveState != OperationState::DRIVE_IDLE &&
        moveState != OperationState::DRIVE_FAILED) {
        Serial.printf("Correction moveState après JOG: %d → IDLE\n",
                        static_cast<int>(moveState));
        moveState = OperationState::DRIVE_IDLE;
        releaseFastStatusPolling();
    }

    return true;
}

bool SureServo::setJogSpeed(uint32_t speed) {
    if (speed > 3000) {  // Limite raisonnable pour jog
        setError("Vitesse de jog trop élevée");
        return false;
    }
    
    currentJogSpeed = speed;
    if (nodes.jogSpeed) {
        nodes.jogSpeed->setValue(speed);
    }
    
    Serial.printf("Vitesse jog définie: %lu\n", speed);
    return true;
}

uint32_t SureServo::getJogSpeed() const {
    return currentJogSpeed;
}

bool SureServo::setServoEnabled(bool enable) {
    nodes.servoOn->setValue(enable);
    Serial.printf("✅ SureServo: Servo %s\n", enable ? "activé" : "désactivé");
    return true;
}

bool SureServo::setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) {
    if (isPlcSuspended()) {
        setError("Commande refusée - PLC suspendu");
        Serial.println("❌ SureServo: setSpeedAndRamp refusé - PLC suspendu");
        return false;
    }

    if (speed > 15) {
        setError("Vitesse PR invalide (0-15)");
        return false;
    }

    if (rampIndex > 15) {
        setError("Index rampe invalide (0-15)");
        return false;
    }

    // Rampe accel/décel symétrique — excursion utile du drive limitée à 0-13
    uint8_t ramp = (rampIndex > 13) ? 13 : rampIndex;
    if (ramp != rampIndex) {
        Serial.printf("⚠️ SureServo: rampIndex %u hors excursion, clampé à %u\r\n", rampIndex, ramp);
    }

    uint32_t driveValue = ((uint32_t)speed << 16)   // vitesse PR (0-15)
                         | ((uint32_t)ramp << 12)    // décélération
                         | ((uint32_t)ramp << 8)     // accélération
                         | PR_TYPE_SINGLE;

    Serial.printf("New user speed: %u, ramp: %u. Will set register with value 0x%08x\r\n",
                  speed, ramp, driveValue);
    nodes.pr1Speed->setValue(driveValue);
    nodes.pr2Speed->setValue(driveValue);
    nodes.pr3Speed->setValue(driveValue);

    currentPrSpeed = speed;
    currentRampIndex = ramp;

    Serial.printf("Vitesse PR définie: %u, rampe: %u (table servo drive)\n", speed, ramp);
    return true;
}

bool SureServo::setMaxTorque(uint8_t torquePercent) {
    if (!nodes.validateRequired()) {
        setError("Nodes manquants pour réglage couple");
        return false;
    }
    
    if (torquePercent > 100) {
        setError("Couple maximum invalide (0-100%)");
        return false;
    }
    
    nodes.maxTorque->setValue(torquePercent);
    Serial.printf("✅ SureServo: Couple max défini à %u%%\n", torquePercent);
    return true;
}

uint8_t SureServo::getMaxTorque() const {
    return nodes.maxTorque->getValue();
}

void SureServo::setMaxRangeInches(float maxRangeInches) {
    if (maxRangeInches > 0) {
        maxRangePuu = (uint32_t)((double)maxRangeInches * pulsesPerUnit);
        Serial.printf("Limite physique mise à jour à chaud: %.2f\" = %lu PUU\n", maxRangeInches, maxRangePuu);
    } else {
        maxRangePuu = 0;
        Serial.println("Limite physique désactivée (maxRangeInches <= 0)");
    }
}

bool SureServo::emergencyStop() {
    immediateStopTimestamp = millis();       
    pendingReleaseImmediateStop = true;     
    nodes.immediateStop->setValue(true);
    nodes.jogSpeed->setValue(STOP_VALUE);
    if (nodes.servoOn) nodes.servoOn->setValue(false);
    
    initState  = OperationState::DRIVE_FAILED;
    resetState = OperationState::DRIVE_FAILED;
    moveState  = OperationState::DRIVE_FAILED;
    homingState= OperationState::DRIVE_FAILED;
    releaseFastStatusPolling(); // moveState quitte DRIVE_IN_PROGRESS sans repasser par processMove()

    Serial.println("🚨 SureServo: Arrêt d'urgence activé");
    return true;
}

bool SureServo::saveParameters() {
    return triggerManualEEPROMSave();
}

int32_t SureServo::getPosition() const {
    if (hasReliablePosition) {
        Serial.println("Retour de la position effective (resychro terminé)");
        return reliablePosition;   // position effective (re-sync terminée)
    } else {
         Serial.println("Retour de la position théorique (re synchro en cours)");
        return targetPosition;     // position théorique (confiance au drive)
    }
}

bool SureServo::getIsReady() const {
    return status.servoReady;
}

bool SureServo::getHasAlarms() const {
    if (status.servoAlarm && status.alarmsRegister == 0) {
        // Code pas encore arrivé — tolérer 200ms avant de signaler "Alarme inconnue"
        if (alarmPendingTime == 0) return false;  // premier cycle, pas encore en attente
        return (millis() - alarmPendingTime) > 200;
    }
    return status.servoAlarm || (status.alarmsRegister != 0);
}

uint16_t SureServo::getAlarmCode() const {
    return status.alarmsRegister;
}

bool SureServo::getIsInitialized() const {
    return initialized;
}

const char* SureServo::getLastError() const {
    return lastError.c_str();
}

// ========================================
// MÉTHODES SPÉCIFIQUES SURESERVO
// ========================================

bool SureServo::getServoReady() const {
    return status.servoReady;
}

bool SureServo::getServoActivated() const {
    return status.servoActivated;
}

bool SureServo::getTargetSpeedRated() const {
    return status.targetSpeedRated;
}

bool SureServo::getTargetPositionReached() const {
    return nodes.targetPositionReached;
}

bool SureServo::getHomeDone() const {
    return status.homeDone && !homeDoneInvalidated;
}

bool SureServo::getAbsolutePositionLost() const {
    return status.absolutePositionLost;
}

bool SureServo::getBatteryAlarm() const {
    return status.batteryAlarm;
}

bool SureServo::getMultipleTurnsOverflow() const {
    return status.multipleTurnsOverflow;
}

bool SureServo::getPuuOverflow() const {
    return status.puuOverflow;
}

bool SureServo::getAbsoluteCoordonateNotSet() const {
    return status.absoluteCoordonateNotSet;
}

const char* SureServo::getAlarmDescription() const {
    uint16_t alarmCode = getAlarmCode();
    
    // Recherche dans la table
    for (size_t i = 0; i < SURESERVO_ALARMS_COUNT; i++) {
        if (SURESERVO_ALARMS[i].code == alarmCode) {
            return SURESERVO_ALARMS[i].description;
        }
    }
    
    return "Alarme inconnue";
}

const char* SureServo::getAlarmAction() const {
    uint16_t alarmCode = getAlarmCode();
    
    // Recherche dans la table
    for (size_t i = 0; i < SURESERVO_ALARMS_COUNT; i++) {
        if (SURESERVO_ALARMS[i].code == alarmCode) {
            return SURESERVO_ALARMS[i].action;
        }
    }
    
    return "Consulter le manuel";
}

void SureServo::printStatus() const {
    Serial.println("==============================");
    Serial.println("     SURESERVO STATUS");
    Serial.println("==============================");
    
    // ÉTAT GÉNÉRAL
    Serial.printf("Ready: %s\n", getIsReady() ? "true" : "false");
    Serial.printf("Initialized: %s\n", getIsInitialized() ? "true" : "false");
    Serial.printf("Servo Activated: %s\n", getServoActivated() ? "true" : "false");
    Serial.printf("Zero Speed: %s\n", status.zeroSpeed ? "true" : "false");
    Serial.printf("Home Done: %s\n", getHomeDone() ? "true" : "false");
    
    // ALARMES
    if (getHasAlarms()) {
        Serial.printf("ALARMES: OUI (0x%04X)\n", getAlarmCode());
        Serial.printf("Description: %s\n", getAlarmDescription());
        Serial.printf("Action: %s\n", getAlarmAction());
    } else {
        Serial.println("ALARMES: Aucune");
    }
    
    // POSITION (CŒUR DU SYSTÈME)
    if (hasReliablePosition) {
        Serial.printf("Position Interne: %ld PUU (FIABLE)\n", (long)reliablePosition);
        Serial.printf("Position Nodes: %ld PUU\n", (long)nodes.position->getValueAsInt32());

        int32_t difference = (reliablePosition > nodes.position->getValueAsInt32()) ?
                             (reliablePosition - nodes.position->getValueAsInt32()) :
                             (nodes.position->getValueAsInt32() - reliablePosition);
        Serial.printf("Écart: %ld PUU\n", (long)difference);
        
        // Conversion en unités physiques
        Serial.printf("Position (inch): %.3f\"\n", (float)reliablePosition / pulsesPerUnit);
    } else {
        Serial.printf("Position: %lu PUU (depuis nodes - PAS FIABLE)\n", getPosition());
        Serial.printf("Position (inch): %.3f\"\n", (float)getPosition() / pulsesPerUnit);
        
        // Info sur re-sync en cours
        if (resyncInProgress) {
            Serial.printf("Re-sync: EN COURS (%d/5 lectures stables)\n", idleStableReadingsCount);
            Serial.printf("Re-sync interval: %lu ms\n", getHomingPositionReadInterval());
        } else {
            Serial.println("Re-sync: En attente d'immobilité");
        }
    }
    
    // OPÉRATIONS EN COURS
    Serial.println("--- OPÉRATIONS ---");
    Serial.printf("Init State: %s\n", 
                 initState == OperationState::DRIVE_IDLE ? "IDLE" :
                 initState == OperationState::DRIVE_IN_PROGRESS ? "IN_PROGRESS" :
                 initState == OperationState::DRIVE_FAILED ? "FAILED" : "OTHER");
    
    Serial.printf("Move State: %s\n", 
                 moveState == OperationState::DRIVE_IDLE ? "IDLE" :
                 moveState == OperationState::DRIVE_IN_PROGRESS ? "IN_PROGRESS" :
                 moveState == OperationState::DRIVE_FAILED ? "FAILED" : "OTHER");
    
    Serial.printf("Homing State: %s\n", 
                 homingState == OperationState::DRIVE_IDLE ? "IDLE" :
                 homingState == OperationState::DRIVE_IN_PROGRESS ? "IN_PROGRESS" :
                 homingState == OperationState::DRIVE_FAILED ? "FAILED" : "OTHER");
    
    // DÉTAILS HOMING SI EN COURS
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        Serial.printf("Homing Phase: %d\n", homingPhase);
        if (homingPhase == 1) {
            Serial.printf("Homing Lectures stables: %d/3\n", homingStableReadingsCount);
            Serial.printf("Homing Interval: %lu ms\n", getHomingPositionReadInterval());
        }
    }
    
    // JOG ET VITESSES
    Serial.println("--- JOG & VITESSES ---");
    uint16_t jogValue = nodes.jogSpeed->getValue();
    if (jogValue != STOP_VALUE) {
        Serial.printf("JOG: %s (%u)\n", 
                     jogValue == FWD_VALUE ? "FORWARD" :
                     jogValue == RWD_VALUE ? "REVERSE" : "UNKNOWN",
                     jogValue);
    } else {
        Serial.println("JOG: STOPPED");
    }
    
    Serial.printf("Jog Speed: %lu\n", currentJogSpeed);
    Serial.printf("PR Speed: %u\n", currentPrSpeed);
    Serial.printf("Current PR: %u\n", currentPr);
    Serial.printf("Max Torque: %u%%\n", nodes.maxTorque->getValue());
    
    // CONFIGURATION
    Serial.println("--- CONFIGURATION ---");
    Serial.printf("Pulses per unit: %.4f\n", pulsesPerUnit);
    Serial.printf("Tolerance: %lu PUU\n", (unsigned long)TOLERANCE_PUU);
    Serial.printf("Nodes Required: %s\n", nodes.validateRequired() ? "OK" : "MANQUANTS");
    Serial.printf("Nodes Optional: %zu configurés\n", nodes.countOptional());
    
    // ERREURS
    if (!lastError.isEmpty()) {
        Serial.println("--- ERREUR ---");
        Serial.printf("Last Error: %s\n", lastError.c_str());
    }
    
    Serial.println("==============================");
}

void SureServo::printAlarms() const {
    if (getHasAlarms()) {
        Serial.println("=== ALARMES ACTIVES ===");
        Serial.printf("Code: 0x%04X\n", getAlarmCode());
        Serial.printf("Description: %s\n", getAlarmDescription());
        Serial.printf("Action: %s\n", getAlarmAction());
        Serial.println("=======================");
    } else {
        Serial.println("Aucune alarme active");
    }
}

void SureServo::printConfiguration() const {
    Serial.println("=== CONFIGURATION SERVO MOTOR ===");
    Serial.printf("Pulses per unit: %.4f\n", pulsesPerUnit);
    Serial.printf("Nodes required: %s\n", nodes.validateRequired() ? "OK" : "MANQUANTS");
    Serial.printf("Nodes optional: %zu configurés\n", nodes.countOptional());
    Serial.println("================================");
}

// ========================================
// MÉTHODES PRIVÉES - MACHINES À ÉTATS
// ========================================

void SureServo::processInitialize(uint32_t now) {

    // ========================================
    // ATTENTE MODBUS VIVANT SUR ADRESSE DU DRIVE
    // ========================================
    uint8_t slaveAddr = getSlaveAddress();
    if (slaveAddr != 0 && MyModbus::getInstance().isSlaveConsideredDead(slaveAddr)) {
        // Geler le timer - ne pas consommer le budget de 15s pendant que le drive ne repond pas
        operationStartTime = now;
        if (waitModbusStartTime == 0) {
            waitModbusStartTime = now;
            Serial.printf("[%s] Attente drive sur adresse Modbus %u...\n", servoId.c_str(), slaveAddr);
        }

        // Message repetitif apres 5s d'attente
        if (now - waitModbusStartTime > 5000) {
            static uint32_t lastWaitLog = 0;
            if (now - lastWaitLog > 5000) {
                Serial.printf("[%s] Attente drive addr %u - toujours injoignable...\n", servoId.c_str(), slaveAddr);
                lastWaitLog = now;
            }
        }
        return;
    }

    // Adresse vivante - eteindre le message periodique si actif et reset du chrono d'attente
    if (waitModbusStartTime != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Drive servo addr %u: pas de reponse - verifier alimentation", slaveAddr);
        PLC_Tools::sendPeriodicMessage(false, WARNING, msg);
        waitModbusStartTime = 0;
        Serial.printf("[%s] Drive addr %u repond - demarrage initialisation\n", servoId.c_str(), slaveAddr);
    }

    // Position marquee non fiable au demarrage
    if (initPhase == 0 && !initialized) {
        hasReliablePosition = false;
        Serial.println("Initialisation : Position marquee NON fiable (demarrage)");
    }

    // VERIFIER TIMEOUT GLOBAL
    if (now - operationStartTime > INIT_TIMEOUT_MS) {
        Serial.printf("Timeout initialisation apres %lu ms\n", now - operationStartTime);
        setError("Timeout initialisation");
        initState = OperationState::DRIVE_TIMEOUT;
        return;
    }

    // GESTION PHASE AUTO-RECOVERY
    if (initPhase == 100) {
        switch (resetState) {
            case OperationState::DRIVE_IN_PROGRESS:
                static uint32_t lastProgressMsg = 0;
                if (now - lastProgressMsg > 2000) {
                    Serial.println("SureServo: Reset automatique en cours...");
                    lastProgressMsg = now;
                }
                return;

            case OperationState::DRIVE_IDLE:
                Serial.println("SureServo: Reset automatique reussi - Passage a l'initialisation normale");
                initPhase = 0;
                phaseStartTime = now;
                resetState = OperationState::DRIVE_IDLE;
                resetPhase = 0;
                break;

            case OperationState::DRIVE_FAILED:
            case OperationState::DRIVE_TIMEOUT:
                Serial.println("SureServo: Reset automatique echoue");
                setError("Reset automatique echoue - Servo toujours en alarme");
                initState = OperationState::DRIVE_FAILED;
                return;

            default:
                Serial.println("SureServo: Etat reset inattendu pendant auto-recovery");
                setError("Etat reset inattendu pendant auto-recovery");
                initState = OperationState::DRIVE_FAILED;
                return;
        }
    }  // initPhase == 100

    // PHASES NORMALES D'INITIALISATION
    switch (initPhase) {
        case 0:
            Serial.println("SureServo Init Phase 0: Configuration");
            if (status.servoAlarm) {
                setError("Servo encore en alarme apres auto-recovery - Verifier manuellement");
                initState = OperationState::DRIVE_FAILED;
                return;
            }
            nodes.immediateStop->setValue(false);
            nodes.auxFunctions->setValue(5);
            nodes.servoOn->setValue(true);
            operationStartTime = millis();  // timeout démarre après les writes hardware
            initPhase = 1;
            phaseStartTime = now;
            break;

        case 1:
            if (now - phaseStartTime > 100) {
                Serial.println("SureServo Init Phase 1: Verification etat");
                initPhase = 2;
                phaseStartTime = now;
            }
            break;

        case 2:
            // servoActivated non requis au repos
            if (status.servoReady && !status.servoAlarm) {
                Serial.println("SureServo: Initialisation reussie");

                if (getHomeDone()) {
                    uint32_t lastRefresh = nodes.position->getLastRefresh();
                    if (lastRefresh < operationStartTime) {
                        static uint32_t lastWaitLog = 0;
                        if (now - lastWaitLog > 500) {
                            Serial.printf("Attente lecture position (last refresh: %lu, start: %lu)...\n",
                                        lastRefresh, operationStartTime);
                            lastWaitLog = now;
                        }
                        return;
                    }

                    int32_t currentPos = nodes.position->getValueAsInt32();
                    bool positionError = false;

                    if (currentPos < 0 && currentPos > -(int32_t)TOLERANCE_PUU) {
                        Serial.printf("Position legerement negative: %ld PUU -> corrigee a 0\n", (long)currentPos);
                        currentPos = 0;
                        BorneUniverselle::prepareMessage(WARNING, "Position negative corrigee a 0");
                    } else if (_homePositionPolicy == HomePositionPolicy::NEGATIVE_INVALIDATES_HOME && currentPos < 0) {
                        // Position négative au-delà de la tolérance : homeDone n'est plus fiable,
                        // mais ce n'est pas une vraie sortie de course (le drive a juste dérivé sous 0)
                        Serial.printf("Position negative detectee: %ld PUU -> home invalide, re-homing requis\n",
                                    (long)currentPos);
                        homeDoneInvalidated = true;
                        BorneUniverselle::prepareMessage(WARNING, "Position negative - re-homing requis");
                    } else if (maxRangePuu > 0 && (uint32_t)currentPos > maxRangePuu) {
                        Serial.printf("ERREUR: Position hors limites: %ld PUU (max: %lu)\n", (long)currentPos, maxRangePuu);
                        setError("Position hors limites machine, veuillez reseter le drive");
                        positionError = true;
                    }

                    if (positionError) {
                        nodes.immediateStop->setValue(true);
                        nodes.servoOn->setValue(false);
                        initialized = false;
                        hasReliablePosition = false;
                        initState = OperationState::DRIVE_FAILED;
                        PLC_Tools::sendPeriodicMessage(true, ERROR,
                            "Position incoherente - Eteignez et rallumez le drive");
                        return;
                    }

                    if (homeDoneInvalidated) {
                        Serial.println("Home invalide - Homing necessaire");
                        hasReliablePosition = false;
                    } else {
                        reliablePosition = currentPos;
                        hasReliablePosition = true;
                        Serial.printf("HomeDone - Position: %ld PUU (%.2f\")\n",
                                    (long)reliablePosition, (float)reliablePosition / pulsesPerUnit);
                    }
                } else {
                    Serial.println("HomeDone=false - Homing necessaire");
                    hasReliablePosition = false;
                }

                initialized = true;
                initState = OperationState::DRIVE_IDLE;
                bool success = setMaxTorque(nodes.maxTorque->getValue());
                if (!success){
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Unable to set torque %u, probably not in range\r\n", nodes.maxTorque->getValue());
                    setError(String(msg));
                }
                success = setSpeedAndRamp(currentPrSpeed, 0);
                if (success){
                    Serial.println("SureServo: Initialisation complete avec succes");
                } else {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Unable to set speed: %u, probably not in range\r\n", currentPrSpeed);
                    setError(String(msg));
                }

            } else if (now - phaseStartTime > 5000) {
                // Servo pas encore ready apres 5s - log periodique en attendant le timeout global
                static uint32_t lastPhase2Log = 0;
                if (now - lastPhase2Log > 2000) {
                    Serial.printf("[%s] Phase 2: attente servoReady (status=0x%04X alarm=0x%04X)\n",
                        servoId.c_str(), nodes.status->getValue(), nodes.alarms->getValue());
                    lastPhase2Log = now;
                }
            }
            break;

        default:
            setError("Phase d'initialisation inconnue: " + String(initPhase));
            initState = OperationState::DRIVE_FAILED;
            break;
    }
}

void SureServo::processReset(uint32_t now) {
    // Vérifier timeout
    if (now - operationStartTime > RESET_TIMEOUT_MS) {
        setError("Timeout reset SureServo après 5 secondes");
        resetState = OperationState::DRIVE_TIMEOUT;
        return;
    }
    
    switch (resetPhase) {
        case 0:
            Serial.println("🔧 SureServo Reset Phase 0: Activation relais");
            
            // S'assurer que le relais est d'abord OFF
            if (nodes.alarmsReset->getValue()) {
                nodes.alarmsReset->setValue(false);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            
            // Activer le relais
            nodes.alarmsReset->setValue(true);
            resetPhase = 1;
            phaseStartTime = now;
            Serial.println("✅ SureServo Reset: Relais activé");
            break;
            
       case 1: // Maintenir le pulse
            if (now - phaseStartTime >= RESET_PULSE_MS) {
                Serial.println("🔧 SureServo Reset Phase 1: Désactivation relais");
                nodes.alarmsReset->setValue(false);   
                resetPhase = 2;
                phaseStartTime = now;
                Serial.println("✅ SureServo Reset: Relais désactivé");
            }
            break;
            
        case 2: // Stabilisation
            if (now - phaseStartTime >= RESET_STABILIZE_MS) {
                Serial.println("🔧 SureServo Reset Phase 2: Vérification continue");
                resetPhase = 3;
                phaseStartTime = now;
            }
            break;
            
        case 3: // ✅ VÉRIFICATION CONTINUE (pas juste 1 fois)
            {
                bool currentServoReady = getServoReady();
                bool currentServoActivated = getServoActivated(); 
                uint16_t currentAlarms = getAlarmCode();
                bool currentServoAlarm = getHasAlarms();

                Serial.printf("🔍 Reset check: servoOn=%d, immediateStop=%d, alarmRaw=%d\n", 
                    nodes.servoOn->getValue(), 
                    nodes.immediateStop->getValue(),
                    nodes.alarms->getValue());
                
                bool success = currentServoReady && !currentServoAlarm && (currentAlarms == 0);

                if (success && !currentServoActivated) {
                    Serial.println("🔧 Servo ready mais pas activated - activation...");
                    nodes.servoOn->setValue(true);
                }
                
                if (success) {
                    // ✅ VÉRIFICATION STABILITÉ (succès pendant 200ms minimum)
                    static uint32_t successStartTime = 0;
                    
                    if (successStartTime == 0) {
                        successStartTime = now;
                        Serial.println("🔍 SureServo Reset: Début période de stabilité");
                    }
                    
                    if (now - successStartTime >= 200) { // 200ms de stabilité
                        Serial.println("✅ SureServo Reset: Succès stable confirmé");
                        resetState = OperationState::DRIVE_IDLE;
                        successStartTime = 0;
                        // Un reset réussi réinitialise tous les états bloqués
                        // (moveState, initState, driveInitStarted, initFailureReported, homingState)
                        clearMovementLock();
                    } else {
                        // Log périodique pendant stabilité
                        static uint32_t lastStabilityLog = 0;
                        if (now - lastStabilityLog > 50) {
                            Serial.printf("🔍 Stabilité: %lu ms / 200 ms\n", now - successStartTime);
                            lastStabilityLog = now;
                        }
                    }
                    
                } else {
                    if (now - phaseStartTime > 2000) { // 2s pour la vérification
                        String error = "Reset SureServo échoué: ";
                        error += "ready=" + String(currentServoReady ? "1" : "0");
                        error += ", activated=" + String(currentServoActivated ? "1" : "0");
                        error += ", alarm=" + String(currentServoAlarm ? "1" : "0");
                        error += ", alarmCode=0x" + String(currentAlarms, HEX);
                        
                        Serial.printf("❌ SureServo Reset: Échec définitif - %s\n", error.c_str());
                        setError(error);
                        resetState = OperationState::DRIVE_FAILED;
                    } else {
                        // Log périodique d'attente
                        static uint32_t lastDebugLog = 0;
                        if (now - lastDebugLog > 500) {
                            Serial.printf("🔄 SureServo Reset: Attente... ready=%s, activated=%s, alarm=%s, code=0x%04X\n",
                                        currentServoReady ? "✅" : "❌",
                                        currentServoActivated ? "✅" : "❌", 
                                        currentServoAlarm ? "🚨" : "✅",
                                        currentAlarms);
                            lastDebugLog = now;
                        }
                    }
                }
            }
            break;
            
        default:
            setError("Phase reset SureServo inconnue: " + String(resetPhase));
            resetState = OperationState::DRIVE_FAILED;
            break;
    }
}

// ✅ NOUVELLE MÉTHODE : Vérification état en temps réel
bool SureServo::isCurrentlyHealthy() const {
    return getServoReady() && 
           getServoActivated() && 
           !getHasAlarms() &&
           (getAlarmCode() == 0);
}

void SureServo::processMove(uint32_t now) {
    // 🔍 VÉRIFICATION : Pourquoi processMove() est appelé ?
    if (moveState != OperationState::DRIVE_IN_PROGRESS) {
        Serial.printf("🚨 BUG: processMove() appelé avec moveState=%d (pas IN_PROGRESS)\n", 
                     static_cast<int>(moveState));
        return;
    }
    
    // ✅ VÉRIFIER TIMEOUT GLOBAL 
    uint32_t elapsed = now - operationStartTime;
    if (elapsed > currentMoveTimeoutMs) {
        Serial.printf("⏰ SureServo: TIMEOUT mouvement après %lu ms (limite: %lu ms)\n",
                     elapsed, currentMoveTimeoutMs);
        
        // ✅ LOG DÉTAILLÉ DU TIMEOUT
        Serial.printf("📊 TIMEOUT - État au moment du timeout:\n");
        Serial.printf("   Position actuelle: %lu PUU (%.2f\")\n", 
                     (unsigned long)nodes.position->getValue(),
                     (float)nodes.position->getValue() / pulsesPerUnit);
        Serial.printf("   Target: %ld PUU (%.2f\")\n",
                     (long)targetPosition,
                     (float)targetPosition / pulsesPerUnit);
        Serial.printf("   Phase: %d, PR: %d\n", movePhase, currentPr);
        Serial.printf("   Signal validé: %s\n", signalValidated ? "OUI" : "NON");
        Serial.printf("   TargetReached: %s\n", 
                     nodes.targetPositionReached->getValue() ? "TRUE" : "FALSE");
        
        setError("Timeout mouvement après " + String(currentMoveTimeoutMs / 1000) + " secondes");
        moveState = OperationState::DRIVE_TIMEOUT;
        movePhase = 0; // Reset pour cohérence
        releaseFastStatusPolling();
        return;
    }

    // ✅ VÉRIFICATION ALARMES CONTINUES
    if (getHasAlarms()) {
        Serial.printf("🚨 Alarme pendant mouvement (0x%04X) - Arrêt immédiat\n", getAlarmCode());
        setError("Alarme détectée pendant le mouvement");
        moveState = OperationState::DRIVE_FAILED;
        movePhase = 0;
        releaseFastStatusPolling();
        return;
    }
    
    switch (movePhase) {
        case 0: {
            // ✅ PHASE 0: ENVOI DU TARGET AU PR ACTUEL
            // En mode chaîné, les targets PR sont déjà configurés via configurePR() — ne pas écraser
            if (chainedPRMode) {
                Serial.printf("🔧 SureServo Move Phase 0: Mode chaîné - target PR%d déjà configuré\n", currentPr);
            } else {
                Serial.printf("🔧 SureServo Move Phase 0: Envoi target %ld PUU au PR%d\n",
                             (long)targetPosition, currentPr);
                sendTargetToPR(targetPosition);
            }
            
            movePhase = 1;
            phaseStartTime = now;
            
            Serial.printf("✅ SureServo: Target envoyé au PR%d - Attente 1 cycle PLC\n", currentPr);
            break;
        }
        
        case 1: {
            // ✅ PHASE 1: ATTENTE 1 CYCLE PLC AVANT TRIGGER
#ifdef TRIGGER_SMART_DELAY
            Node* targetNode = (currentPr == 1) ?
                (Node*)nodes.targetA : (Node*)nodes.targetB;
            bool targetWritten = (targetNode->getLastRefresh() > phaseStartTime);
            bool safetyTimeout = (now - phaseStartTime >= TRIGGER_DELAY_MS);
            #ifdef PERF_MONITOR
            if (pm_active) {
                if (targetWritten && !safetyTimeout) pm_smartDelayCount++;
                if (safetyTimeout)                  pm_safetyTimeoutCount++;
            }
            #endif

            if (targetWritten || safetyTimeout) {
            //if (now - phaseStartTime >= TRIGGER_DELAY_MS) {
                if (safetyTimeout && !targetWritten)
                    Serial.printf("⚠️ TRIGGER safety timeout %lu ms\n", now - phaseStartTime);
#else
            if (now - phaseStartTime >= TRIGGER_DELAY_MS) {
#endif
                Serial.printf("🚀 SureServo Move Phase 1: Déclenchement trigger PR%d\n", currentPr);
                
                nodes.trigger->setValue(currentPr, true);
#ifdef PERF_MONITOR
                if (pm_active) {
                    pm_writeTriggerCount++;
                    pm_triggerSentTime = now;
                    if (pm_targetSentTime > 0) {
                        uint32_t ttDur = now - pm_targetSentTime;
                        pm_targetToTrigCumul += ttDur;
                        if (ttDur > pm_targetToTrigMax) pm_targetToTrigMax = ttDur;
                        pm_targetToTrigCount++;
                        pm_targetSentTime = 0;
                    }
                }
#endif
                Serial.printf("✅ SureServo: Trigger déclenché PR%d vers %ld PUU\n",
                             currentPr, (long)targetPosition);

                movePhase = 2;
                phaseStartTime = now; // Reset timer pour phase 2
                acquireFastStatusPolling(); // Détection targetReached/zeroSpeed sans attendre le refresh 200ms
            }
            break;
        }

        case 2: {
            uint32_t timeSinceTrigger = now - phaseStartTime;

            // Sous-phase A : confirmer que le mouvement a démarré
            if (!signalValidated) {
                if (!status.targetPositionReached) {
                    // Mouvement long : bit passé à false
                    signalValidated = true;
#ifdef PERF_MONITOR
                    if (pm_active && pm_triggerSentTime > 0) {
                        uint32_t lat = now - pm_triggerSentTime;
                        pm_driveLatencyCumul += lat;
                        if (lat > pm_driveLatencyMax) pm_driveLatencyMax = lat;
                        pm_signalValidatedTime = now;
                    }
#endif
                    Serial.println("✅ SureServo: Mouvement long - transition false détectée");
                } else if (!status.zeroSpeed) {
                    // Mouvement confirmé par vitesse non nulle
                    signalValidated = true;
#ifdef PERF_MONITOR
                    if (pm_active && pm_triggerSentTime > 0) {
                        uint32_t lat = now - pm_triggerSentTime;
                        pm_driveLatencyCumul += lat;
                        if (lat > pm_driveLatencyMax) pm_driveLatencyMax = lat;
                        pm_signalValidatedTime = now;
                    }
#endif
                    Serial.println("✅ SureServo: Mouvement détecté - vitesse non nulle");
                } else if (timeSinceTrigger > 150 && 
                    nodes.position->getLastRefresh() > phaseStartTime &&
                    nodes.status->getLastRefresh() > phaseStartTime &&
                    !chainedPRMode) {
                    signalValidated = true;
#ifdef PERF_MONITOR
                    if (pm_active && pm_triggerSentTime > 0) {
                        uint32_t lat = now - pm_triggerSentTime;
                        pm_driveLatencyCumul += lat;
                        if (lat > pm_driveLatencyMax) pm_driveLatencyMax = lat;
                        pm_signalValidatedTime = now;
                    }
#endif
                    Serial.println("⚠️ SureServo: Mouvement court - status confirmé après trigger");
                }
                // Log périodique
                static uint32_t lastLogA = 0;
                if (now - lastLogA > 2000) {
                    Serial.printf("⏳ Attente début mouvement... (%lu ms)\n", timeSinceTrigger);
                    lastLogA = now;
                }
                break;
            }

            // Détection PR1→PR2 en mode chaîné
            if (chainedPRMode && !chainedPR1Complete) {
                int32_t currentPos = nodes.position->getValueAsInt32();

                if (currentPos > chainedPrevPosition) {
                    if (chainedMovingAway == 2) {
                        chainedPR1Complete = true;  // était en descente, repart en montée → PR2
                    }
                    chainedMovingAway = 1;
                } else if (currentPos < chainedPrevPosition) {
                    if (chainedMovingAway == 1) {
                        chainedPR1Complete = true;  // était en montée, repart en descente → PR2
                    }
                    chainedMovingAway = 2;
                }
                chainedPrevPosition = currentPos;
            }

            bool movementDone;
            if (chainedPRMode) {
                movementDone = signalValidated && isAtPosition(targetPosition)
                            && status.zeroSpeed && timeSinceTrigger > 100;
            } else {
                movementDone = status.targetPositionReached && status.zeroSpeed
                            && timeSinceTrigger > 100;
            }

            if (movementDone) {
                reliablePosition = targetPosition;
                hasReliablePosition = !waitForSyncRequested;
            #ifdef PERF_MONITOR
                if (pm_active && pm_signalValidatedTime > 0) {
                    uint32_t mv = now - pm_signalValidatedTime;
                    pm_moveTimeCumul += mv;
                    if (mv > pm_moveTimeMax) pm_moveTimeMax = mv;
                    pm_moveCount++;
                    pm_triggerSentTime = 0;
                    pm_signalValidatedTime = 0;
                }
            #endif
                if (!chainedPRMode) {
                    advancePR();
                } else {
                    currentPr = 1;  // Mode chaîné : toujours recommencer sur PR1
                }
                moveState = OperationState::DRIVE_IDLE;
                movePhase = 0;
                releaseFastStatusPolling();
                Serial.printf("✅ Mouvement terminé vers %ld PUU%s\n",
                            (long)targetPosition,
                            waitForSyncRequested ? " - resync en attente" : "");
                return;
            }

            // Log périodique
            static uint32_t lastLogB = 0;
            if (now - lastLogB > 2000) {
                Serial.printf("⏳ Attente target reached... (%lu ms)\n", timeSinceTrigger);
                lastLogB = now;
            }
            break;
        }
        
        default: {
            Serial.printf("❌ SureServo: Phase mouvement inconnue: %d\n", movePhase);
            setError("Phase mouvement invalide: " + String(movePhase));
            moveState = OperationState::DRIVE_FAILED;
            releaseFastStatusPolling();
            break;
        }
    }
} // processMove

void SureServo::processHoming(uint32_t now) {
    if (homingState != OperationState::DRIVE_IN_PROGRESS) {
        return;
    }
    
    uint32_t totalElapsed = now - operationStartTime;
    
    // ========================================
    // TIMEOUT GLOBAL DE SÉCURITÉ
    // ========================================
    
    if (totalElapsed > currentHomingTimeoutMs) {
        Serial.printf("⏰ SureServo: Timeout homing après %lu ms (limite: %lu ms)\n", totalElapsed, currentHomingTimeoutMs);
        setError("Timeout homing global");
        homingState = OperationState::DRIVE_TIMEOUT;
        
        // ❌ Invalider position en cas de timeout
        hasReliablePosition = false;
        return;
    }
    
    // ========================================
    // VÉRIFICATION CONTINUE DES ALARMES
    // ========================================
    
    if (getHasAlarms()) {
        Serial.printf("🚨 Alarme pendant homing (0x%04X)\n", getAlarmCode());
        setError("Alarme servo pendant homing");
        homingState = OperationState::DRIVE_FAILED;
        
        // ❌ Invalider position en cas d'alarme
        hasReliablePosition = false;
        return;
    }
    
    // ========================================
    // MACHINE À ÉTATS HOMING AVEC STABILISATION
    // ========================================
    
    switch (homingPhase) {
        case 0: {
            // PHASE 0: Attendre que HomeDone devienne true
            if (getHomeDone()) {
                Serial.println("✅ HomeDone détecté - début détection stabilisation position");
                
                // Initialiser la détection de stabilisation
                uint32_t currentPos = nodes.position->getValue();
                previousHomingPosition = currentPos;
                lastHomingPositionReadTime = now;
                homingStableReadingsCount = 0;
                homingPhase = 1;
                
                uint32_t readInterval = getHomingPositionReadInterval();
                Serial.printf("🎯 Position initiale: %lu PUU (intervalle lecture: %lu ms)\n", 
                             currentPos, readInterval);
                Serial.println("🔄 Attente stabilisation (déplacement vers index si nécessaire)...");
            } else {
                // Attendre HomeDone
                static uint32_t lastLog = 0;
                if (now - lastLog > 2000) {
                    Serial.printf("⏳ Attente HomeDone... (%lu ms écoulées)\n", totalElapsed);
                    lastLog = now;
                }
            }
            break;
        }
        
        case 1: {
            // PHASE 1: Attendre stabilisation de la position (déplacement vers index)
            uint32_t currentPos = nodes.position->getValue();
            
            // ✅ SIMPLIFICATION: Utiliser int32_t pour gérer naturellement les valeurs négatives
            int32_t currentPosition_signed = (int32_t)currentPos;
            
            if (isHomingPositionStabilized(currentPos, now)) {
                // ✅ POSITION STABILISÉE
                
                // Position finale = position actuelle (déjà gérée comme int32 dans stabilisation)
                // Si négative proche de zéro, on accepte et on corrige
                uint32_t finalHomingPosition = currentPos;

                // ⚠️ GARDE-FOU: Si valeur vraiment aberrante (> 1 million PUU dans un sens
                // ou l'autre), corriger. Comparaison signée des deux côtés : avant, le
                // second membre comparait le uint32_t brut (finalHomingPosition) à 1000000,
                // ce qui est systématiquement vrai pour N'IMPORTE QUELLE valeur négative
                // (ex: -248 devient 4294967048 une fois réinterprété en uint32_t) — donc
                // tout résidu négatif de homing, même sous TOLERANCE_PUU, déclenchait ce
                // message "Position aberrante" au lieu de la branche ci-dessous, bien que
                // le résultat final (forcé à 0) soit resté correct pour ces résidus.
                if (currentPosition_signed < -1000000 || currentPosition_signed > 1000000) {
                    Serial.printf("🚨 HOMING: Position aberrante: %ld PUU (int32)\n",
                                 (long)currentPosition_signed);
                    Serial.println("🔧 CORRECTION: Position forcée à 0 pour sécurité");
                    finalHomingPosition = 0;
                    // Grace period : laisse une chance à la prochaine lecture (potentiellement
                    // non polluée) avant que le contrôle de cohérence (process()) ne réinvalide
                    // hasReliablePosition sur la base de cette même lecture aberrante.
                    homingCorrectionTime = now;

                    BorneUniverselle::prepareMessage(WARNING,
                        "Position homing corrigée - valeur aberrante détectée");
                } else if (currentPosition_signed < 0 && currentPosition_signed > -TOLERANCE_PUU) {
                    // Valeur légèrement négative (-50 à -1) → acceptable, corriger à 0
                    Serial.printf("ℹ️ Position légèrement négative: %ld PUU → 0 PUU\n", 
                                 (long)currentPosition_signed);
                    finalHomingPosition = 0;
                }
                
                // ✅ Utiliser la position validée
                reliablePosition = finalHomingPosition;
                hasReliablePosition = true;
                homeDoneInvalidated = false; // home re-validé après homing réussi

                Serial.printf("✅ HOMING TERMINÉ - Position finale stabilisée: %lu PUU\n", reliablePosition);
                Serial.printf("📊 Temps total homing: %lu ms\n", totalElapsed);
                Serial.println("🎯 Position fiable établie après index");
                Serial.println("🏠 Homing avec détection d'index terminé avec succès");
                
                homingState = OperationState::DRIVE_IDLE;
                homingPhase = 0;
                
                setError(""); // Effacer toute erreur précédente
#ifdef DEBUG_SURESERVO                        
                BorneUniverselle::prepareMessage(SUCCESS, "Homing terminé avec succès !!!");
#endif
                Serial.printf("[32m%lu:: SUCCESS: Homing terminé avec succès !!! Position: %lu PUU[0m\n", 
                            now, reliablePosition);
                return;
            }
            
            // Continuer à attendre la stabilisation...
            static uint32_t lastStabilizationLog = 0;
            if (now - lastStabilizationLog > 1000) {
                Serial.printf("🔄 Attente stabilisation... Position: %ld PUU (int32)\n", 
                             (long)currentPosition_signed);
                lastStabilizationLog = now;
            }
            break;
        }
        
        default: {
            Serial.printf("❌ Phase homing invalide: %d\n", homingPhase);
            setError("Phase homing invalide");
            homingState = OperationState::DRIVE_FAILED;
            hasReliablePosition = false;
            break;
        }
    }
}



// ========================================
// MÉTHODES UTILITAIRES SIMPLIFIÉES
// ========================================

bool SureServo::isHomingInProgress() const {
    return homingState == OperationState::DRIVE_IN_PROGRESS;
}

bool SureServo::isHomingCompleted() const {
    return (homingState == OperationState::DRIVE_IDLE) && getHomeDone();
}

bool SureServo::isHomingFailed() const {
    return (homingState == OperationState::DRIVE_FAILED) || 
           (homingState == OperationState::DRIVE_TIMEOUT);
}

void SureServo::printHomingStatus() const {
    Serial.println("=== SURESERVO HOMING STATUS ===");
    Serial.printf("Homing State: %d (%s)\n", 
                 static_cast<int>(homingState),
                 homingState == OperationState::DRIVE_IDLE ? "IDLE" :
                 homingState == OperationState::DRIVE_IN_PROGRESS ? "IN_PROGRESS" :
                 homingState == OperationState::DRIVE_FAILED ? "FAILED" :
                 homingState == OperationState::DRIVE_TIMEOUT ? "TIMEOUT" : "UNKNOWN");
    Serial.printf("Homing Phase: %d\n", homingPhase);
    Serial.printf("Home Done: %s\n", getHomeDone() ? "true" : "false");
    Serial.printf("Is Ready: %s\n", getIsReady() ? "true" : "false");
    Serial.printf("Has Alarms: %s\n", getHasAlarms() ? "true" : "false");
    if (getHasAlarms()) {
        Serial.printf("Alarm Code: 0x%04X (%s)\n", getAlarmCode(), getAlarmDescription());
    }
    Serial.printf("Position: %lu PUU (%.2f\")\n", 
                 getPosition(), (float)getPosition() / pulsesPerUnit);
    
    if (!lastError.isEmpty()) {
        Serial.printf("Last Error: %s\n", lastError.c_str());
    }
    
    Serial.println("==============================");
}

bool SureServo::isAtPosition(int32_t targetPuu) const {
    // targetPuu est déjà signé (contrat ServoDrive) — plus besoin de cast local
    int32_t current_signed = nodes.position->getValueAsInt32();
    int32_t diff = current_signed - targetPuu;
    uint32_t difference = (diff >= 0) ? diff : -diff;  // Valeur absolue

    bool atPosition = difference <= TOLERANCE_PUU;
    
    if (atPosition) {
        Serial.printf("✅ SureServo: Position atteinte - Écart: %lu PUU (tolérance: %lu)\n",
                     (unsigned long)difference, (unsigned long)TOLERANCE_PUU);
    }
    
    return atPosition;
}

void SureServo::sendTargetToPR(int32_t targetPuu) {
    Serial.printf("📤 SureServo: Envoi target %ld PUU au PR%d\n",
                 (long)targetPuu, currentPr);

    switch (currentPr) {
        case 1:
            // ✅ targetA est un node OBLIGATOIRE - pas de validation nécessaire
            nodes.targetA->setValueFromInt32(targetPuu);
            Serial.printf("✅ Target A défini: %ld PUU\n", (long)targetPuu);
            break;

        case 2:
            // ✅ targetB est un node OBLIGATOIRE - pas de validation nécessaire
            nodes.targetB->setValueFromInt32(targetPuu);
            Serial.printf("✅ Target B défini: %ld PUU\n", (long)targetPuu);
            break;

        case 3:
            // ✅ targetC est un node OBLIGATOIRE - pas de validation nécessaire
            nodes.targetC->setValueFromInt32(targetPuu);
            Serial.printf("✅ Target C défini: %ld PUU\n", (long)targetPuu);
            break;
            
        default:
            Serial.printf("❌ PR invalide: %d\n", currentPr);
            setError("PR invalide: " + String(currentPr));
            break;
    }
#ifdef PERF_MONITOR
    if (pm_active) {
        pm_writeTargetCount++;
        if (pm_interMoveStart > 0) {
            uint32_t interDur = millis() - pm_interMoveStart;
            pm_interMoveCumul += interDur;
            if (interDur > pm_interMoveMax) pm_interMoveMax = interDur;
            pm_interMoveCount++;
            pm_interMoveStart = 0;
        }
        pm_targetSentTime = millis();
    }
#endif
}

void SureServo::advancePR() {
    currentPr++;
    if (currentPr > 2) {
        currentPr = 1;
    }
}

bool SureServo::checkDriveHealth() const {
    return !getHasAlarms() && getIsReady();
}

bool SureServo::areNodesValid() const {
    return nodes.validateRequired();
}

void SureServo::setError(const String& error) {
    // Enrichir le message d'erreur avec contexte
    String enhancedError = error;
    
    // Ajouter informations de contexte
    if (error.indexOf("communication") >= 0 || error.indexOf("Modbus") >= 0) {
        enhancedError += " [Pos:" + String(getPosition()) + " PUU]";
        enhancedError += " [Ready:" + String(getIsReady() ? "Y" : "N") + "]";
    }
    
    if (error.indexOf("redémarr") >= 0) {
        enhancedError += " [États: R:" + String(getServoReady() ? "1" : "0");
        enhancedError += " A:" + String(getServoActivated() ? "1" : "0");
        enhancedError += " H:" + String(getHomeDone() ? "1" : "0") + "]";
    }
    
    lastError = enhancedError;
    Serial.printf("❌ SureServo Error: %s\n", enhancedError.c_str());
}

void SureServo::updateOptionalOutputNodes() {
    if (nodes.targetPositionReached){
        nodes.targetPositionReached->setValue(status.targetPositionReached);
    }
    
    // === NODES OPTIONNELS - STATUS PRINCIPAL ===
    if (nodes.servoReady) {
        nodes.servoReady->setValue(status.servoReady);
    }
    
    if (nodes.servoActivated) {
        nodes.servoActivated->setValue(status.servoActivated);
    }
    
    if (nodes.zeroSpeed) {
        nodes.zeroSpeed->setValue(status.zeroSpeed);
    }
    
    if (nodes.targetSpeedRated) {
        nodes.targetSpeedRated->setValue(status.targetSpeedRated);
    }
    
    if (nodes.servoAlarm) {
        bool wasAlarm = nodes.servoAlarm->getValue();
        nodes.servoAlarm->setValue(status.servoAlarm);
        if (status.servoAlarm && !wasAlarm) {
            logAlarmChange("updateOptionalOutputNodes");  // Serial uniquement
            // Notification projet → transition EMERGENCY
            if (onAlarmCallback) {
                onAlarmCallback(getAlarmCode(), getAlarmDescription());
            }
        }
    }
    
    if (nodes.homeDone) {
        nodes.homeDone->setValue(getHomeDone());
    }
    
    // === NODES OPTIONNELS - SYSTÈME COORDONNÉES ABSOLUES ===
    if (nodes.absolutePositionLost) {
        nodes.absolutePositionLost->setValue(status.absolutePositionLost);
    }
    
    if (nodes.batteryAlarm) {
        nodes.batteryAlarm->setValue(status.batteryAlarm);
    }
    
    if (nodes.multipleTurnsOverflow) {
        nodes.multipleTurnsOverflow->setValue(status.multipleTurnsOverflow);
    }
    
    if (nodes.puuOverflow) {
        nodes.puuOverflow->setValue(status.puuOverflow);
    }
    
    if (nodes.absoluteCoordonateNotSet) {
        nodes.absoluteCoordonateNotSet->setValue(status.absoluteCoordonateNotSet);
    }
    
    // === NODES CALCULÉS ===
    if (nodes.driveInitialised) {
        nodes.driveInitialised->setValue(initialized);
    }
} // updateOptionalOutputNodes()

const SureServoAlarm* SureServo::findAlarmInfo(uint16_t alarmCode) const {
    for (size_t i = 0; i < SURESERVO_ALARMS_COUNT; i++) {
        if (SURESERVO_ALARMS[i].code == alarmCode) {
            return &SURESERVO_ALARMS[i];
        }
    }
    return nullptr;
}

void SureServo::logStatusChange(const char* context) const {
    if (context) {
        Serial.printf("SureServo Status Change [%s]: Ready=%s, Alarm=%s\n", 
                     context,
                     getIsReady() ? "true" : "false",
                     getHasAlarms() ? "true" : "false");
    }
}

void SureServo::logAlarmChange(const char* context) const {
    if (getHasAlarms()) {
        Serial.printf("\n");
        Serial.printf("========================================\n");
        Serial.printf("🚨 ALARME SERVO DÉTECTÉE [%s]\n", context ? context : "Unknown");
        Serial.printf("========================================\n");
        Serial.printf("Code:        0x%04X (%u)\n", getAlarmCode(), getAlarmCode());
        Serial.printf("Description: %s\n", getAlarmDescription());
        Serial.printf("Action:      %s\n", getAlarmAction());
        Serial.printf("========================================\n");
        
        // Message utilisateur aussi
        char txt[512];
        snprintf(txt, sizeof(txt), "DRIVE: %s - %s", getAlarmDescription(),  getAlarmAction()),
        Serial.println(txt);      
    } else {
        // Alarme effacée
        Serial.printf("✅ Alarme servo effacée [%s]\n", context ? context : "Unknown");
    }
}

void SureServo::logOperationStateChange(const char* operation, 
                                       ServoDrive::OperationState oldState, 
                                       ServoDrive::OperationState newState) const {
    Serial.printf("SureServo %s: %s -> %s\n", operation, ServoDrive::getOperationStateText(oldState), ServoDrive::getOperationStateText(newState));
}

// ========================================
// MÉTHODES DE LA STRUCTURE DecodedStatus
// ========================================

bool SureServo::DecodedStatus::updateFromNodes(const ServoNodes& nodes) {
    bool hasChanges = false;
    
    // Lecture des registres
    if (nodes.status) {
        uint16_t newStatus = nodes.status->getValue();
        if (newStatus != statusRegister) {
            statusRegister = newStatus;
            decodeStatusRegister();
            hasChanges = true;
        }
    }
    
    if (nodes.alarms) {
        uint16_t newAlarms = nodes.alarms->getValue();
        if (newAlarms != alarmsRegister) {
            alarmsRegister = newAlarms;
            decodeAlarmsRegister();
            hasChanges = true;
        }
    }
    
    if (nodes.absoluteCoordonateSystemStatus) {
        uint16_t newCoordSystem = nodes.absoluteCoordonateSystemStatus->getValue();
        if (newCoordSystem != coordSystemRegister) {
            coordSystemRegister = newCoordSystem;
            decodeCoordSystemRegister();
            hasChanges = true;
        }
    }
    
    return hasChanges;
}

void SureServo::DecodedStatus::decodeStatusRegister() {
    servoReady = (statusRegister & STATUS_SERVO_READY) != 0;
    servoActivated = (statusRegister & STATUS_SERVO_ACTIVATED) != 0;
    zeroSpeed = (statusRegister & STATUS_ZERO_SPEED) != 0;
    targetSpeedRated = (statusRegister & STATUS_TARGET_SPEED_RATED) != 0;
    targetPositionReached = (statusRegister & STATUS_TARGET_REACHED) != 0;
    servoAlarm = (statusRegister & STATUS_SERVO_ALARM) != 0;
    homeDone = (statusRegister & STATUS_HOME_DONE) != 0;
}

void SureServo::DecodedStatus::decodeAlarmsRegister() {
    // ========================================
    // REGISTRE ALARMS - PAS DE DÉCODAGE NÉCESSAIRE
    // ========================================
    //
    // DIFFÉRENCE IMPORTANTE AVEC LES AUTRES REGISTRES:
    //
    // - STATUS Register (0x9C):
    //   Contient des BITS individuels à extraire
    //   Exemple: bit 0 = Ready, bit 1 = Activated, bit 6 = Alarm
    //   → Nécessite: servoReady = (statusRegister & 0x0001) != 0
    //
    // - ALARMS Register (0x9E):
    //   Contient DIRECTEMENT le code d'alarme complet
    //   Exemple: 0x0013 = "Arrêt du drive actif"
    //            0x0030 = "Blocage mécanique"
    //   → Pas de décodage: alarmsRegister EST déjà le code final
    //
    // - COORD_SYSTEM Register (0xA0):
    //   Contient des BITS individuels à extraire
    //   Exemple: bit 0 = Position Lost, bit 1 = Battery Alarm
    //   → Nécessite: batteryAlarm = (coordSystemRegister & 0x0002) != 0
    //
    // UTILISATION:
    //   getAlarmCode() retourne directement alarmsRegister
    //   getAlarmDescription() cherche alarmsRegister dans SURESERVO_ALARMS[]
    //
    // CONCLUSION: Cette fonction reste vide par design - c'est CORRECT.
    //
    // Note: Le bit STATUS_SERVO_ALARM (bit 6 du STATUS) indique simplement
    //       qu'une alarme est active. Le code spécifique est dans alarmsRegister.
}

void SureServo::DecodedStatus::decodeCoordSystemRegister() {
    absolutePositionLost = (coordSystemRegister & COORD_ABS_POSITION_LOST) != 0;
    batteryAlarm = (coordSystemRegister & COORD_BATTERY_ALARM) != 0;
    multipleTurnsOverflow = (coordSystemRegister & COORD_MULTI_TURNS_OVERFLOW) != 0;
    puuOverflow = (coordSystemRegister & COORD_PUU_OVERFLOW) != 0;
    absoluteCoordonateNotSet = (coordSystemRegister & COORD_ABS_NOT_SET) != 0;
}

void SureServo::DecodedStatus::reset() {
    statusRegister = 0;
    alarmsRegister = 0;
    coordSystemRegister = 0;
    
    servoReady = false;
    servoActivated = false;
    zeroSpeed = false;
    targetSpeedRated = false;
    servoAlarm = false;
    homeDone = false;
    
    absolutePositionLost = false;
    batteryAlarm = false;
    multipleTurnsOverflow = false;
    puuOverflow = false;
    absoluteCoordonateNotSet = false;
}

void SureServo::initializeEEPROMConfiguration() {
    // Désactiver la sauvegarde automatique par défaut
    currentEEPROMMode = EEPROMMode::AUTO_SAVE_DISABLED;
    
    if (nodes.auxFunctions) {
        nodes.auxFunctions->setValue(EEPROM_AUTO_SAVE_DISABLED);
        Serial.printf("EEPROM: Auto-save désactivé (valeur: %u)\n", EEPROM_AUTO_SAVE_DISABLED);
    } else {
        Serial.println("ERREUR: Node auxFunctions non disponible pour config EEPROM");
    }
}

bool SureServo::setEEPROMMode(EEPROMMode mode) {
    if (!nodes.auxFunctions) {
        setError("Node auxFunctions non disponible");
        return false;
    }
    
    uint16_t registerValue = 0;
    
    switch (mode) {
        case EEPROMMode::AUTO_SAVE_ENABLED:
            registerValue = 0;  // Valeur par défaut du drive
            break;
            
        case EEPROMMode::AUTO_SAVE_DISABLED:
            registerValue = EEPROM_AUTO_SAVE_DISABLED;
            break;
            
        case EEPROMMode::MANUAL_SAVE_ONLY:
            registerValue = EEPROM_AUTO_SAVE_DISABLED;
            break;
            
        default:
            setError("Mode EEPROM invalide");
            return false;
    }
    
    nodes.auxFunctions->setValue(registerValue);
    currentEEPROMMode = mode;
    
    Serial.printf("EEPROM mode configuré: %d (registre: %u)\n", 
                 static_cast<int>(mode), registerValue);
    
    return true;
}

bool SureServo::triggerManualEEPROMSave() {
    uint32_t now = millis();
    
    // Éviter les sauvegardes trop fréquentes
    if (eepromSaveInProgress || (now - lastEEPROMSaveTime) < EEPROM_SAVE_DELAY_MS) {
        Serial.println("EEPROM: Sauvegarde déjà en cours ou trop récente");
        return false;
    }
    
    // Déclencher la sauvegarde
    nodes.auxFunctions->setValue(EEPROM_MANUAL_SAVE_TRIGGER);
    
    eepromSaveInProgress = true;
    lastEEPROMSaveTime = now;
    
    Serial.println("EEPROM: Sauvegarde manuelle déclenchée");
    
    // Programmer la restauration du mode après délai
    // (sera gérée dans process())
    
    return true;
}

EEPROMMode SureServo::getEEPROMMode() const {
    return currentEEPROMMode;
}

bool SureServo::setAuxiliaryFunctions(uint16_t value) {
    nodes.auxFunctions->setValue(value);
    return true;
}

bool SureServo::clearError() {
    Serial.printf("🔄 SureServo::clearError() - États avant: move=%d, init=%d, reset=%d\n",
                 (int)moveState, (int)initState, (int)resetState);
    
    // Reset tous les états d'erreur
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
    
    // Clear le message d'erreur
    lastError = "";
    
    Serial.println("✅ SureServo: Erreurs effacées");
    return true;
}

void SureServo::notifyBusinessLogicFault(const char* reason) {
    setError(reason);
    driveFaultPending = true;
    if (onAlarmCallback) {
        // code=0 : pas un vrai code d'alarme drive, signale un défaut logiciel détecté
        // (redémarrage ou perte communication) — cohérent avec les consommateurs actuels
        // (Woodzco/RotaryIndexingTable) qui ignorent le code et transitionnent en EMERGENCY.
        onAlarmCallback(0, reason);
    }
}

void SureServo::detectDriveRestart(uint32_t now) {
    (void)now;

    // ✅ DÉTECTER PERTE INATTENDUE DE L'ÉTAT
    if (restartDetect_wasInitialized && initialized && restartDetect_hadHomeDone && !getHomeDone()) {
        Serial.println("🚨 SureServo: REDÉMARRAGE DRIVE DÉTECTÉ !");
        Serial.printf("   Position actuelle: %lu PUU\n", getPosition());
        Serial.println("   - Perte de l'état HomeDone");
        Serial.println("   - Drive probablement redémarré");

        // ✅ MESSAGE UTILISATEUR
        BorneUniverselle::prepareMessage(ERROR,
            "Drive servo redémarré - Position home perdue - Refaire le homing");

        notifyBusinessLogicFault("Drive redémarré - État home perdu");

        // Si homing en cours, le marquer comme échoué
        if (homingState == OperationState::DRIVE_IN_PROGRESS) {
            Serial.println("   - Interruption du homing en cours");
            homingState = OperationState::DRIVE_FAILED;
        }

        // Si mouvement en cours, le marquer comme échoué aussi
        if (moveState == OperationState::DRIVE_IN_PROGRESS) {
            Serial.println("   - Interruption du mouvement en cours");
            moveState = OperationState::DRIVE_FAILED;
            releaseFastStatusPolling();
        }
    }

    // Mémoriser les états pour la prochaine fois
    restartDetect_wasInitialized = initialized;
    restartDetect_hadHomeDone = getHomeDone();
}

void SureServo::acquireFastPositionPolling() {
    if (_fastPollRefCount == 0) {
        _positionBaselineRefreshInterval = nodes.position->getRefreshInterval();
        nodes.position->setRefreshInterval(0);
    }
    _fastPollRefCount++;
}

void SureServo::releaseFastPositionPolling() {
    if (_fastPollRefCount == 0) return;
    _fastPollRefCount--;
    if (_fastPollRefCount == 0) {
        nodes.position->setRefreshInterval(_positionBaselineRefreshInterval);
    }
}

void SureServo::acquireFastStatusPolling() {
    if (_statusFastPollActive) return;
    _statusBaselineRefreshInterval = nodes.status->getRefreshInterval();
    nodes.status->setRefreshInterval(0);
    _statusFastPollActive = true;
}

void SureServo::releaseFastStatusPolling() {
    if (!_statusFastPollActive) return;
    nodes.status->setRefreshInterval(_statusBaselineRefreshInterval);
    _statusFastPollActive = false;
}

void SureServo::setReliablePosition(int32_t newPosition) {
    reliablePosition = newPosition;
    hasReliablePosition = true;

    Serial.printf("✅ Position interne fiable: %ld PUU\n", (long)reliablePosition);
}

uint32_t SureServo::getHomingPositionReadInterval() const {
    // Valeur par défaut pour PLC à 10 Hz = 100ms
    uint32_t defaultInterval = 100; 
    
    // getRefreshRate() retourne l'intervalle en ms entre deux mesures
    if (nodes.position && nodes.position->getRefreshInterval() > 0) {
        uint32_t refreshIntervalMs = nodes.position->getRefreshInterval(); // Ex: 100ms ou 1000ms
        
        // Prendre la moitié de l'intervalle pour être sûr de détecter les changements
        uint32_t readInterval = refreshIntervalMs / 2;
        
        // Limites de sécurité: minimum 25ms, maximum 500ms
        if (readInterval < 25) readInterval = 25;
        if (readInterval > 500) readInterval = 500;
        
        return readInterval;
    }
    
    return defaultInterval / 2; // 50ms par défaut
}

bool SureServo::isHomingPositionStabilized(uint32_t currentPosition, uint32_t now) {
    // Calculer l'intervalle de lecture adaptatif (moitié du refreshRate)
    uint32_t readInterval = getHomingPositionReadInterval();
    
    // Vérifier s'il est temps de lire la position
    if (now - lastHomingPositionReadTime < readInterval) {
        return false; // Pas encore temps de lire
    }
    
    // ✅ SIMPLIFICATION: Convertir en int32_t pour gérer naturellement les oscillations
    // autour de zéro (y compris valeurs négatives)
    int32_t current_signed = (int32_t)currentPosition;
    int32_t previous_signed = (int32_t)previousHomingPosition;
    
    // Calculer la différence (fonctionne parfaitement avec valeurs négatives)
    int32_t diff = current_signed - previous_signed;
    uint32_t positionDifference = (diff >= 0) ? diff : -diff;  // Valeur absolue
    
    // Log pour debug avec valeurs signées pour clarté
    Serial.printf("🏠 Homing - Current: %ld PUU, Previous: %ld PUU, Écart: %lu PUU, Lectures stables: %d/%d\n",
                 (long)current_signed, (long)previous_signed, 
                 (unsigned long)positionDifference, homingStableReadingsCount, 3);
    
    if (positionDifference <= TOLERANCE_PUU) {
        // Position stable - incrémenter le compteur
        homingStableReadingsCount++;
        Serial.printf("✅ Position homing stable (%d/%d lectures)\n", homingStableReadingsCount, 3);
    } else {
        // Position bouge encore (déplacement vers index) - remettre le compteur à zéro
        homingStableReadingsCount = 0;
        Serial.printf("🔄 Déplacement vers index en cours (écart: %lu PUU > tolérance: %lu PUU)\n", 
                     (unsigned long)positionDifference, (unsigned long)TOLERANCE_PUU);
    }
    
    // Mettre à jour pour la prochaine lecture
    previousHomingPosition = currentPosition;
    lastHomingPositionReadTime = now;
    
    // Position considérée comme stabilisée après 3 lectures consécutives stables
    return homingStableReadingsCount >= 3;
}

void SureServo::invalidateReliablePosition() {
    if (hasReliablePosition) {
        hasReliablePosition = false;
        Serial.println("Position interne invalidée (JOG détecté)");
        
        // Arrêter toute re-synchronisation en cours
        resyncInProgress = false;
        idleStableReadingsCount = 0;
    }
    
    // NOUVEAU: Marquer qu'une re-sync sera nécessaire après JOG
    // mais seulement si le servo est en bon état
    if (getIsReady() && !getHasAlarms()) {
        //Serial.println("Re-sync post-JOG programmée");
        // On utilisera cette info dans attemptPositionResync
    }
}

bool SureServo::isIdlePositionStable(uint32_t currentPosition, uint32_t now) {
    // Utiliser le même intervalle que pour le homing
    uint32_t readInterval = getHomingPositionReadInterval();
    
    // Vérifier s'il est temps de lire la position
    if (now - lastIdlePositionReadTime < readInterval) {
        return false; // Pas encore temps de lire
    }
    
    // ✅ SIMPLIFICATION: Convertir en int32_t pour gérer naturellement les oscillations
    // autour de zéro (y compris valeurs négatives) — même pattern que isHomingPositionStabilized()
    int32_t current_signed = (int32_t)currentPosition;
    int32_t previous_signed = (int32_t)previousIdlePosition;
    int32_t diff = current_signed - previous_signed;
    uint32_t positionDifference = (diff >= 0) ? diff : -diff;  // Valeur absolue

    // Log discret pour debug (pas trop verbeux)
    static uint32_t lastDebugLog = 0;
    if (now - lastDebugLog > 5000) { // Log seulement toutes les 5 secondes
        Serial.printf("🔄 Re-sync: Position: %ld PUU, Écart: %lu, Stables: %d/5\n",
                     (long)current_signed, positionDifference, idleStableReadingsCount);
        lastDebugLog = now;
    }
    
    if (positionDifference <= TOLERANCE_PUU) {
        // Position stable - incrémenter le compteur
        idleStableReadingsCount++;
    } else {
        // Position bouge - moteur en mouvement, arrêter la re-sync
        endPositionResync(false);
        Serial.println("🔄 Re-sync interrompue (mouvement détecté)");
    }
    
    // Mettre à jour pour la prochaine lecture
    previousIdlePosition = currentPosition;
    lastIdlePositionReadTime = now;
    
    // Position considérée comme stable après 5 lectures (plus strict que homing)
    return idleStableReadingsCount >= 3;
}

void SureServo::beginPositionResync(uint32_t now) {
    // Polling rapide temporaire pour raccourcir le temps de validation
    // (même compteur partagé que le JOG, cf. acquireFastPositionPolling()
    // : évite qu'un resync démarré pendant un JOG actif ne sauvegarde 0).
    acquireFastPositionPolling();

    resyncInProgress = true;
    resyncStartTime = now;
    previousIdlePosition = nodes.position->getValue();
    lastIdlePositionReadTime = now;
    idleStableReadingsCount = 0;

    Serial.printf("Début re-sync position: %lu PUU (refresh rapide activé)\n",
                 previousIdlePosition);
}

void SureServo::endPositionResync(bool success) {
    if (resyncInProgress) {
        releaseFastPositionPolling();
    }
    resyncInProgress = false;
    idleStableReadingsCount = 0;
    (void)success; // réservé pour usage futur (log différencié succès/abandon)
}

void SureServo::attemptPositionResync(uint32_t now) {
    if (!getHomeDone()) {
        return;  // Pas de home = pas de référence = pas de resync possible
    }

    static uint32_t lastDebugLog = 0;
    static bool lastHadReliablePosition = true;
    
    bool statusChanged = (hasReliablePosition != lastHadReliablePosition);
    bool hasProblem = !hasReliablePosition;
    
    if (statusChanged || (hasProblem && now - lastDebugLog > 10000)) {
        if (statusChanged) {
            Serial.printf("🔄 Re-sync status change: %s → %s\n",
                         lastHadReliablePosition ? "FIABLE" : "NON-FIABLE",
                         hasReliablePosition ? "FIABLE" : "NON-FIABLE");
        }
        
        if (hasProblem) {
            Serial.printf("🔍 Re-sync conditions - zeroSpeed:%s, homeDone:%s, ready:%s, resyncInProgress:%s\n", 
                        status.zeroSpeed ? "OUI" : "NON", 
                        getHomeDone() ? "OUI" : "NON", 
                        getIsReady() ? "OUI" : "NON",
                        resyncInProgress ? "OUI" : "NON");
        }
        
        lastDebugLog = now;
        lastHadReliablePosition = hasReliablePosition;
    }
    
    // CONDITIONS POUR DÉMARRER (existant - CONDITIONS STRICTES)
    if (hasReliablePosition) {
        if (resyncInProgress) {
            endPositionResync(true);
        }
        return;
    }
    
    // Ne pas re-sync pendant les opérations (existant)
    if (moveState == OperationState::DRIVE_IN_PROGRESS ||
        homingState == OperationState::DRIVE_IN_PROGRESS ||
        initState == OperationState::DRIVE_IN_PROGRESS ||
        resetState == OperationState::DRIVE_IN_PROGRESS) {
        
        if (resyncInProgress) {
            endPositionResync(false);
            Serial.println("🔄 Re-sync interrompue (opération en cours)");
        }
        return;
    }
    
    // Ne pas re-sync si servo pas prêt ou en alarme (existant)
    if (!getIsReady() || getHasAlarms()) {
        if (resyncInProgress) {
            endPositionResync(false);
            Serial.println("🔄 Re-sync interrompue (servo pas prêt ou alarme)");
        }
        return;
    }
    
    uint32_t currentPosition = nodes.position->getValue();
    
    // DÉMARRAGE RE-SYNC NORMAL - CONDITIONS STRICTES (existant)
    if (!resyncInProgress) {
        bool shouldStartResync = false;
        
        // ✅ GARDE LES CONDITIONS STRICTES pour la re-sync normale
        if (status.zeroSpeed) {
            shouldStartResync = true;
        }
        
        if (shouldStartResync) {
            // 🔒 Corriger immédiatement si position négative dans la tolérance
            int32_t currentPositionSigned = (int32_t)currentPosition;
            if (currentPositionSigned < 0 && currentPositionSigned > -(int32_t)TOLERANCE_PUU) {
                Serial.printf("🚨 Position négative détectée au début re-sync: %ld PUU\n", (long)currentPositionSigned);
                currentPosition = 0;
                Serial.println("🔧 CORRECTION: Utilisation de 0 pour la re-sync");
                
                // Valider immédiatement avec 0
                reliablePosition = 0;
                hasReliablePosition = true;
                
                Serial.println("✅ Position corrigée et validée immédiatement à 0");
                BorneUniverselle::prepareMessage(WARNING, "Position négative corrigée à 0");
                return;  // Pas besoin de re-sync, on a déjà corrigé
            }
            
            // Démarrer la re-sync normale si pas de correction nécessaire
            beginPositionResync(now);
        }
        return;
    }
    
    // VÉRIFICATION DE STABILITÉ (existant)
    if (isIdlePositionStable(currentPosition, now)) {
        // 🔒 GARDE-FOU : Vérifier si position légèrement négative avant de valider
        int32_t finalPosition = (int32_t)currentPosition;

        if (finalPosition < 0 && finalPosition > -(int32_t)TOLERANCE_PUU) {
            Serial.printf("🚨 RE-SYNC: Position légèrement négative détectée: %ld PUU\n", (long)finalPosition);
            Serial.println("🔧 CORRECTION: Position forcée à 0 pour sécurité");
            finalPosition = 0;

            BorneUniverselle::prepareMessage(WARNING,
                "Re-sync: position corrigée - valeur négative détectée");
        }

        // ✅ Position stable - RE-SYNCHRONISATION RÉUSSIE avec valeur validée !
        reliablePosition = finalPosition;  // Utiliser la position corrigée
        hasReliablePosition = true;
        endPositionResync(true);

        Serial.printf("✅ Re-sync réussie ! Position interne: %ld PUU\n", (long)reliablePosition);
    }
}

bool SureServo::getHasReliablePosition() const {
    return hasReliablePosition;
}

bool SureServo::isMovePositionStabilized(uint32_t currentPosition, uint32_t now) {
    // Utiliser le même intervalle que pour le homing
    uint32_t readInterval = getHomingPositionReadInterval();
    
    // Vérifier s'il est temps de lire la position
    if (now - lastMovePositionReadTime < readInterval) {
        return false;
    }
    
    // ✅ SIMPLIFICATION: Convertir en int32_t pour gérer naturellement les oscillations
    // autour de zéro (y compris valeurs négatives) — même pattern que isHomingPositionStabilized()
    int32_t current_signed = (int32_t)currentPosition;
    int32_t previous_signed = (int32_t)previousMovePosition;
    int32_t diff = current_signed - previous_signed;
    uint32_t positionDifference = (diff >= 0) ? diff : -diff;  // Valeur absolue

    // Log pour debug
    Serial.printf("🎯 Move - Position: %ld PUU, Écart: %lu PUU, Lectures stables: %d/3\n",
                 (long)current_signed, positionDifference, moveStableReadingsCount);
    
    if (positionDifference <= TOLERANCE_PUU) {
        moveStableReadingsCount++;
        Serial.printf("✅ Position stable (%d/3 lectures)\n", moveStableReadingsCount);
    } else {
        moveStableReadingsCount = 0;
        Serial.printf("🔄 Mouvement en cours (écart: %lu PUU)\n", positionDifference);
    }
    
    // Mettre à jour pour la prochaine lecture
    previousMovePosition = currentPosition;
    lastMovePositionReadTime = now;
    
    // Position stabilisée après 3 lectures stables
    return moveStableReadingsCount >= 3;
} // isMovePositionStabilized()

bool SureServo::handleInitializing(ServoDrive::OperationStatus& status) {
    if (!driveInitStarted) {
        initFailureReported = false;  // Nouveau cycle d'init, reset du flag
        if (!startInitialize()) {
            char msg[128];
            snprintf(msg, sizeof(msg), "[%s] ERREUR: Impossible de démarrer l'initialisation", servoId.c_str());
            BorneUniverselle::prepareMessage(ERROR, msg);
            
            lastError = "Échec démarrage initialisation";
            status = ServoDrive::OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
        driveInitStarted = true;
        status = ServoDrive::OperationStatus::IN_PROGRESS;
        return true;
    }
    
    OperationState initState = getInitializeState();
    
    switch (initState) {
        case OperationState::DRIVE_IDLE:
            Serial.printf("[%s] ✅ Initialisation terminée avec succès\n", servoId.c_str());
            driveInitStarted = false;
            initFailureReported = false;
            status = ServoDrive::OperationStatus::COMPLETED;
            return true;
            
        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT: {
            // NE PAS remettre driveInitStarted à false ici.
            // RessortRoyal2 transite vers EMERGENCY sur réception de l'erreur.
            // driveInitStarted sera remis à false par clearMovementLock() après reset.
            const char* errorMsg = getLastError();
            
            char userMessage[200];
            ServoDrive::OperationStatus errorStatus;
            if (strstr(errorMsg, "Position") != nullptr) {
                snprintf(userMessage, sizeof(userMessage), 
                    "[%s] ERREUR CRITIQUE: %s - Éteignez et rallumez le drive", 
                    servoId.c_str(), errorMsg);
                errorStatus = ServoDrive::OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            } else if (strstr(errorMsg, "timeout") != nullptr) {
                snprintf(userMessage, sizeof(userMessage), 
                    "[%s] Timeout initialisation: %s", servoId.c_str(), errorMsg);
                errorStatus = ServoDrive::OperationStatus::SERVO_DRIVE_ERROR;
            } else {
                snprintf(userMessage, sizeof(userMessage), 
                    "[%s] Échec init: %s", servoId.c_str(), errorMsg);
                errorStatus = ServoDrive::OperationStatus::SERVO_DRIVE_ERROR;
            }
            
            // Signaler l'erreur une seule fois - évite le spam à chaque cycle
            if (!initFailureReported) {
                BorneUniverselle::prepareMessage(ERROR, userMessage);
                Serial.printf("[%s] ❌ %s\n", servoId.c_str(), errorMsg);
                initFailureReported = true;
            }
            
            status = errorStatus;
            return true;
        }
            
        case OperationState::DRIVE_IN_PROGRESS:
        default:
            status = ServoDrive::OperationStatus::IN_PROGRESS;
            return true;
    }
}

bool SureServo::handleHoming(ServoDrive::OperationStatus& status, uint32_t speed, uint32_t timeoutMs) {
    if (!driveHomingStarted) {
        if (!startHoming(timeoutMs)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "[%s] ERREUR: Impossible de démarrer le homing", servoId.c_str());
            BorneUniverselle::prepareMessage(ERROR, msg);
            lastError = lastError.isEmpty() ? "Échec démarrage homing" : lastError;
            status = ServoDrive::OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
        driveHomingStarted = true;
        status = ServoDrive::OperationStatus::IN_PROGRESS;
        return true;
    }
    
    // Surveiller l'état du homing
    OperationState homingState = getHomingState();
    
    switch (homingState) {
        case OperationState::DRIVE_IDLE: {
            char successMsg[128];
            snprintf(successMsg, sizeof(successMsg), 
                "[%s] HOMING terminé avec succès! Position: %lu PUU", 
                servoId.c_str(), (unsigned long)getPosition());
#ifdef DEBUG_SURESERVO
            BorneUniverselle::prepareMessage(SUCCESS, successMsg);
#endif
            Serial.printf("[%s] ✅ Homing terminé\n", servoId.c_str());
            driveHomingStarted = false;
            status = ServoDrive::OperationStatus::COMPLETED;
            return true;
        }
            
        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT: {
            driveHomingStarted = false;
            const char* errorMsg = getLastError();
            char userMessage[200];
            snprintf(userMessage, sizeof(userMessage), 
                "[%s] Échec homing: %s", servoId.c_str(), errorMsg);
            BorneUniverselle::prepareMessage(ERROR, userMessage);
            Serial.printf("[%s] ❌ %s\n", servoId.c_str(), errorMsg);
            status = ServoDrive::OperationStatus::SERVO_DRIVE_ERROR;
            return true;
        }
            
        case OperationState::DRIVE_IN_PROGRESS: {
            static uint32_t lastProgressLog = 0;
            uint32_t now = millis();
            if (now - lastProgressLog > 5000) {
                uint32_t elapsed = (now - operationStartTime) / 1000;
                char progressMsg[200];
                snprintf(progressMsg, sizeof(progressMsg), 
                    "[%s] HOMING en cours... (%lu sec) [Position: %lu PUU]",
                    servoId.c_str(), (unsigned long)elapsed, (unsigned long)getPosition());
#ifdef DEBUG_SURESERVO
                BorneUniverselle::prepareMessage(INFO, progressMsg);
#endif
                lastProgressLog = now;
            }
            status = ServoDrive::OperationStatus::IN_PROGRESS;
            return true;
        }
            
        default:
            status = ServoDrive::OperationStatus::IN_PROGRESS;
            return true;
    }
}

bool SureServo::handleJogging(ServoDrive::OperationStatus& status) {
    // ✅ Vérifier alarmes critiques
    if (getHasAlarms()) {
        uint16_t alarmCode = getAlarmCode();
        char msg[128];
        snprintf(msg, sizeof(msg), "[%s] Alarme détectée en JOG (0x%04X)", servoId.c_str(), alarmCode);
        BorneUniverselle::prepareMessage(ERROR, msg);
        Serial.printf("[%s] 🚨 %s\n", servoId.c_str(), msg);
        
        status = ServoDrive::OperationStatus::SERVO_DRIVE_ERROR;
        return true;
    }
    
    // ✅ Tout va bien, continuer le JOG
    status = ServoDrive::OperationStatus::IN_PROGRESS;
    return true;
}

bool SureServo::handleMovingToPosition(int32_t pos, ServoDrive::OperationStatus& status, bool waitForSync, uint32_t timeoutMs) {
    // CAS 1 : Mouvement en cours - continuer la surveillance
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        // Messages de progression...
        static uint32_t lastProgressLog = 0;
        uint32_t now = millis();
        
        if (now - lastProgressLog > 1000) {
            Serial.printf("[%s] 🔄 Mouvement en cours vers %ld PUU...\n",
                         servoId.c_str(), (long)targetPosition);
            lastProgressLog = now;
        }
        
        status = ServoDrive::OperationStatus::IN_PROGRESS;
        return true;
    }
    
    // CAS 2 : Mouvement terminé (DRIVE_IDLE) - vérifier si on doit attendre sync
    if (moveState == OperationState::DRIVE_IDLE && targetPosition == pos) {
        // ✅ On a déjà fait ce mouvement
        
        // Si waitForSync demandé ET pas encore synchronisé
        if (waitForSyncRequested && !getHasReliablePosition()) {  // ✅ CORRIGÉ: waitForSyncRequested au lieu de waitForSync
            Serial.printf("[%s] ⏳ Attente re-synchronisation position...\n", servoId.c_str());
            status = ServoDrive::OperationStatus::IN_PROGRESS;
            return true;
        }
        
        // ✅ MOUVEMENT COMPLETÉ (position atteinte ET sync OK si demandée)
        char successMsg[128];
        snprintf(successMsg, sizeof(successMsg), 
            "[%s] ✅ Position %ld PUU atteinte",
            servoId.c_str(), (long)targetPosition);
#ifdef DEBUG_SURESERVO
        BorneUniverselle::prepareMessage(SUCCESS, successMsg);
#endif
        
        waitForSyncRequested = false;
        
        status = ServoDrive::OperationStatus::COMPLETED;
        return true;
    }
    
    // CAS 3 : Erreurs
    if (moveState == OperationState::DRIVE_FAILED || moveState == OperationState::DRIVE_TIMEOUT) {
        char errorMsg[200];
        snprintf(errorMsg, sizeof(errorMsg), "[%s] ❌ Mouvement échoué vers %ld PUU: %s",
                 servoId.c_str(), (long)targetPosition, getLastError());
        BorneUniverselle::prepareMessage(ERROR, errorMsg);
        
        waitForSyncRequested = false;
        
        status = ServoDrive::OperationStatus::SERVO_DRIVE_ERROR;
        return true;
    }
    
    // CAS 4 : Nouveau mouvement à démarrer (IDLE mais targetPosition différente)
    if (moveState == OperationState::DRIVE_IDLE) {
        // ✅ Utiliser startGoToPosition qui remplit targetPosition
        if (!startGoToPosition(pos, waitForSync, timeoutMs)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "[%s] ERREUR: Impossible de démarrer le mouvement vers %lu PUU", 
                     servoId.c_str(), (unsigned long)pos);
            BorneUniverselle::prepareMessage(ERROR, msg);
            
            status = ServoDrive::OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
        
        char msg[128];
        snprintf(msg, sizeof(msg), "[%s] ✅ Mouvement démarré vers %lu PUU%s", 
                 servoId.c_str(), (unsigned long)pos, 
                 waitForSync ? " (avec sync)" : "");
#ifdef DEBUG_SURESERVO
        BorneUniverselle::prepareMessage(INFO, msg);
#endif
        
        status = ServoDrive::OperationStatus::IN_PROGRESS;
        return true;
    }
    
    // Par défaut
    status = ServoDrive::OperationStatus::IN_PROGRESS;
    return true;
}

bool SureServo::isPlcSuspended() const {
    BorneUniverselle* borne = BorneUniverselle::getInstance();
    if (!borne) {
        return false;  // Pas de borne = pas de suspension (cas très improbable)
    }
    
    return borne->getPlcSuspended();
}

uint8_t SureServo::getSlaveAddress() const {
    ModbusNode* modbusNode = dynamic_cast<ModbusNode*>(nodes.status);
    if (modbusNode != nullptr) {
        return modbusNode->getModbusAddress();
    }
    // Pas un node Modbus (ex: mode debug avec VirtualNode) - pas de filtrage
    Serial.printf("[%s] getSlaveAddress: nodes.status n'est pas un ModbusNode\n", servoId.c_str());
    return 0;
}

bool SureServo::stopMovement() {
    Serial.println("🛑 SureServo::stopMovement() - Arrêt propre demandé");
    
    // ========================================
    // 1. ARRÊT PHYSIQUE DU MOTEUR
    // ========================================
    nodes.immediateStop->setValue(true);
    nodes.jogSpeed->setValue(STOP_VALUE);
    immediateStopTimestamp = millis();
    
    // ========================================
    // 2. ANNULATION DES OPÉRATIONS EN COURS (SANS FAILED)
    // ========================================
    
    // Annuler le mouvement en cours
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        Serial.printf("   → Mouvement annulé (phase %d)\n", movePhase);
        moveState = OperationState::DRIVE_IDLE;  // ✅ Retour à IDLE (pas FAILED)
        movePhase = 0;
        targetPosition = 0;
        releaseFastStatusPolling();
    }
    
    // Annuler le homing en cours
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        Serial.println("   → Homing annulé");
        homingState = OperationState::DRIVE_IDLE;  // ✅ Retour à IDLE (pas FAILED)
    }
    
    // ========================================
    // 3. INVALIDATION DE LA POSITION
    // ========================================
    
    // La position n'est plus fiable après un arrêt forcé en plein mouvement
    hasReliablePosition = false;
    
    // ========================================
    // 4. BLOQUER REDÉMARRAGE AUTOMATIQUE
    // ========================================
    
    // CRITIQUE: Empêcher handleMovingToPosition() de redémarrer le mouvement
    // pendant la transition Cancel → IDLE (délai de ~1 seconde)
    // Ce flag sera réinitialisé lors du prochain startGoToPosition()
    movementCancelled = true;
    
    Serial.println("   → Flag movementCancelled activé (blocage redémarrage)");
    
    // ========================================
    // 5. PROGRAMMATION DU RELÂCHEMENT DIFFÉRÉ
    // ========================================
    
    // Le relais a besoin de 500ms pour coller/décoller de façon fiable
    pendingReleaseImmediateStop = true;
    
    Serial.printf("✅ SureServo: Arrêt propre demandé - immediateStop actif pendant %lu ms\n", 
                  IMMEDIATE_STOP_DURATION_MS);
    
    return true;
}

void SureServo::clearUserPulses(){
    // Pas fini de développé....
    clearPuu = true;
}

void SureServo::clearMovementLock() {
    if (movementCancelled) {
        movementCancelled = false;
    }

    // immediateStop reste à false (état normal, contact NO)

     // ✅ AJOUT : Remettre moveState à IDLE si bloqué en FAILED/TIMEOUT
    if (moveState == OperationState::DRIVE_FAILED || moveState == OperationState::DRIVE_TIMEOUT) {
        Serial.println("   → moveState réinitialisé (clearMovementLock)");
        moveState = OperationState::DRIVE_IDLE;
        movePhase = 0;
    }

    if (nodes.servoOn && !nodes.servoOn->getValue()) {
        nodes.servoOn->setValue(true);
    }

    // Remettre initState à IDLE si bloqué en FAILED/TIMEOUT par emergencyStop,
    // puis relancer une vraie initialisation : sans ça, initState==DRIVE_IDLE
    // mais initialized reste false pour toujours (jog refusé en permanence,
    // même si home est fait), car rien ne revalide plus jamais la position.
    if (initState == OperationState::DRIVE_FAILED || initState == OperationState::DRIVE_TIMEOUT) {
        Serial.println("   → Init réinitialisée (clearMovementLock) - relance de l'initialisation");
        initState = OperationState::DRIVE_IDLE;
        initPhase = 0;
        driveInitStarted = false;
        initFailureReported = false;
        if (!startInitialize()) {
            Serial.printf("   ⚠️ Échec relance initialisation: %s\n", lastError.c_str());
        }
    }

    // Remettre homingState à IDLE si bloqué en FAILED/TIMEOUT
    if (homingState != OperationState::DRIVE_IDLE) {
        Serial.println("   → Homing réinitialisé (clearMovementLock)");
        homingState = OperationState::DRIVE_IDLE;
        driveHomingStarted = false;
    }

    targetPosition = 0;
}

// ========================================
// PERF MONITOR
// ========================================

#ifdef PERF_MONITOR

void SureServo::pmNotifyCompleted() {
    if (pm_active) pm_interMoveStart = millis();
}

void SureServo::pmStartCycle() {
    pm_active             = true;
    pm_cycleStartTime     = millis();
    pm_processCallCount   = 0;
    pm_processTimeMax     = 0;
    pm_processTimeCumul   = 0;
    pm_writeTargetCount   = 0;
    pm_writeTriggerCount  = 0;
    pm_driveLatencyMax    = 0;
    pm_driveLatencyCumul  = 0;
    pm_moveTimeMax        = 0;
    pm_moveTimeCumul      = 0;
    pm_moveCount          = 0;
    pm_triggerSentTime    = 0;
    pm_signalValidatedTime= 0;
    pm_refreshCumul       = 0;
    pm_interMoveStart     = 0;
    pm_interMoveCount     = 0;
    pm_interMoveCumul     = 0;
    pm_interMoveMax       = 0;
    pm_targetSentTime     = 0;
    pm_targetToTrigCount   = 0;
    pm_targetToTrigCumul   = 0;
    pm_targetToTrigMax     = 0;
    pm_smartDelayCount     = 0;
    pm_safetyTimeoutCount  = 0;
}

void SureServo::pmPrintReport(uint32_t cycleTotalMs) const {
    uint32_t procMs  = pm_processTimeCumul / 1000;
    uint32_t pctRef  = (cycleTotalMs > 0) ? (pm_refreshCumul      * 100 / cycleTotalMs) : 0;
    uint32_t pctProc = (cycleTotalMs > 0) ? (procMs               * 100 / cycleTotalMs) : 0;
    uint32_t pctT1       = (cycleTotalMs > 0) ? (pm_driveLatencyCumul * 100 / cycleTotalMs) : 0;
    uint32_t pctT2       = (cycleTotalMs > 0) ? (pm_moveTimeCumul     * 100 / cycleTotalMs) : 0;
    uint32_t pctInterMvt = (cycleTotalMs > 0) ? (pm_interMoveCumul    * 100 / cycleTotalMs) : 0;
    uint32_t pctTTrig    = (cycleTotalMs > 0) ? (pm_targetToTrigCumul * 100 / cycleTotalMs) : 0;

    Serial.println(F("\r\n╔══════════════════════════════════════════════════╗"));
    Serial.println(F(    "║   PERF MONITOR — Cycle complet                   ║"));
#ifdef TRIGGER_SMART_DELAY
    Serial.println(F(    "║   Mode: SMART DELAY                              ║"));
#else
    Serial.printf(       "║   Mode: FIXED DELAY (%3lums)                      ║\r\n", (uint32_t)TRIGGER_DELAY_MS);
#endif
    Serial.println(F(    "╠══════════════════════════════════════════════════╣"));
    Serial.printf(       "║ Durée totale cycle   : %5lu ms  (100%%)          ║\r\n", cycleTotalMs);
    Serial.printf(       "║ refresh() cumulé     : %5lu ms  (%3lu%%)          ║\r\n", pm_refreshCumul, pctRef);
    Serial.printf(       "║ process() cumulé     : %5lu ms  (%3lu%%)          ║\r\n", procMs, pctProc);
    Serial.printf(       "║ process() pic        : %5lu µs                   ║\r\n", pm_processTimeMax);
    Serial.printf(       "║ process() appels     : %5lu                      ║\r\n", pm_processCallCount);
    Serial.println(F(    "╠══════════════════════════════════════════════════╣"));
    Serial.printf(       "║ Mouvements complétés : %5lu                      ║\r\n", pm_moveCount);
    Serial.printf(       "║ Inter-mvt cumulé     : %5lu ms  (%3lu%%)          ║\r\n", pm_interMoveCumul, pctInterMvt);
    Serial.printf(       "║ Inter-mvt pic        : %5lu ms                   ║\r\n", pm_interMoveMax);
    Serial.println(F(    "╠══════════════════════════════════════════════════╣"));
    Serial.printf(       "║ Targets envoyés      : %5lu                      ║\r\n", pm_writeTargetCount);
    Serial.printf(       "║ Target→Trigger cumulé: %5lu ms  (%3lu%%)          ║\r\n", pm_targetToTrigCumul, pctTTrig);
    Serial.printf(       "║ Target→Trigger pic   : %5lu ms                   ║\r\n", pm_targetToTrigMax);
    Serial.printf(       "║ Triggers envoyés     : %5lu                      ║\r\n", pm_writeTriggerCount);
    Serial.println(F(    "╠══════════════════════════════════════════════════╣"));
    Serial.printf(       "║ T1 (latence) cumulé  : %5lu ms  (%3lu%%)          ║\r\n", pm_driveLatencyCumul, pctT1);
    Serial.printf(       "║ T1 (latence) pic     : %5lu ms                   ║\r\n", pm_driveLatencyMax);
    Serial.printf(       "║ T2 (mvt) cumulé      : %5lu ms  (%3lu%%)          ║\r\n", pm_moveTimeCumul, pctT2);
    Serial.printf(       "║ T2 (mvt) pic         : %5lu ms                   ║\r\n", pm_moveTimeMax);
#ifdef TRIGGER_SMART_DELAY
    Serial.println(F(    "╠══════════════════════════════════════════════════╣"));
    Serial.printf(       "║ Smart delay confirmé : %5lu fois               ║\r\n", pm_smartDelayCount);
    Serial.printf(       "║ Safety timeout       : %5lu fois               ║\r\n", pm_safetyTimeoutCount);
#endif
    Serial.println(F(    "╚══════════════════════════════════════════════════╝"));
    Serial.println(F(    "T1=trigger→signalValidated  T2=signalValidated→reached&&zeroSpeed"));
    pm_active = false;
}

void SureServo::pmAccumulateRefresh(uint32_t ms) {
    pm_refreshCumul += ms;
}

#endif // PERF_MONITOR

bool SureServo::configurePR(uint8_t prNumber, int32_t targetPuu, uint8_t speedIndex, bool chainToNext) {
    if (isPlcSuspended()) {
        setError("configurePR refusé - PLC suspendu");
        return false;
    }

    if (prNumber < 1 || prNumber > 3) {
        setError("configurePR: prNumber invalide (1-3)");
        return false;
    }
    if (speedIndex > 15) {
        setError("configurePR: speedIndex invalide (0-15)");
        return false;
    }

    if (prNumber == 1) {
        chainedPR1Target = targetPuu;  // int32_t = int32_t, homogène depuis 2b
    }

    uint32_t type   = chainToNext ? PR_TYPE_AUTO : PR_TYPE_SINGLE;
    uint32_t config = ((uint32_t)speedIndex << 16)
                     | ((uint32_t)currentRampIndex << 12)   // décélération
                     | ((uint32_t)currentRampIndex << 8)    // accélération
                     | type;

    switch (prNumber) {
        case 1:
            nodes.pr1Speed->setValue(config);
            nodes.targetA->setValueFromInt32(targetPuu);
            break;
        case 2:
            nodes.pr2Speed->setValue(config);
            nodes.targetB->setValueFromInt32(targetPuu);
            break;
        case 3:
            nodes.pr3Speed->setValue(config);
            nodes.targetC->setValueFromInt32(targetPuu);
            break;
    }

    if (chainToNext){
        chainedPRMode = true;
    }

    Serial.printf("✅ configurePR: PR%d target=%ld SPD_IDX=%u chain=%s config=0x%08lX\n",
                  prNumber, (long)targetPuu, speedIndex,
                  chainToNext ? "AUTO" : "SINGLE", config);

    return true;
}

bool SureServo::clearPRConfig() {
    bool ok = true;
    for (uint8_t pr = 1; pr <= 3; pr++) {
        if (!clearPRConfig(pr)) ok = false;
    }

    chainedPRMode = false;

    Serial.println("✅ clearPRConfig: tous les PR restaurés");
    return ok;
}

bool SureServo::clearPRConfig(uint8_t prNumber) {
    if (prNumber < 1 || prNumber > 3) {
        setError("clearPRConfig: prNumber invalide (1-3)");
        return false;
    }

    // Restaure exactement le pattern de setSpeedAndRamp : TYPE=SINGLE, vitesse/rampe courantes
    uint32_t config = ((uint32_t)currentPrSpeed << 16)
                     | ((uint32_t)currentRampIndex << 12)   // décélération
                     | ((uint32_t)currentRampIndex << 8)    // accélération
                     | PR_TYPE_SINGLE;

    switch (prNumber) {
        case 1: nodes.pr1Speed->setValue(config); break;
        case 2: nodes.pr2Speed->setValue(config); break;
        case 3: nodes.pr3Speed->setValue(config); break;
    }

    Serial.printf("✅ clearPRConfig: PR%d restauré config=0x%08lX (SPD_IDX=%u)\n", prNumber, config, currentPrSpeed);
    return true;
}

bool SureServo::isPostResetStabilizing() const {
    return postResetStabilizationTime > 0 &&
           millis() - postResetStabilizationTime < POST_RESET_STABILIZATION_MS;
}

bool SureServo::setHomeAtCurrentPosition() {
    homeAtCurrentPositionInProgress = true;

    // Guard 1 : moteur en mouvement
    if (!status.zeroSpeed) {
        Serial.println("❌ setHomeAtCurrentPosition: refusé - moteur en mouvement");
        BorneUniverselle::prepareMessage(WARNING, "setHomeAtCurrentPosition: moteur en mouvement");
        homeAtCurrentPositionInProgress = false;   // refus avant écriture → reset immédiat
        return false;
    }

    // Guard 2 : drive occupé
    if (initState   != OperationState::DRIVE_IDLE ||
        resetState  != OperationState::DRIVE_IDLE ||
        moveState   != OperationState::DRIVE_IDLE ||
        homingState != OperationState::DRIVE_IDLE) {
        Serial.println("❌ setHomeAtCurrentPosition: refusé - drive occupé");
        BorneUniverselle::prepareMessage(WARNING, "setHomeAtCurrentPosition: drive occupé");
        homeAtCurrentPositionInProgress = false;   // refus avant écriture → reset immédiat
        return false;
    }

    // Guard 3 : nodes disponibles
    if (!nodes.homingModeRead || !nodes.homingModeWrite || !nodes.trigger) {
        Serial.println("❌ setHomeAtCurrentPosition: refusé - nodes manquants");
        BorneUniverselle::prepareMessage(ERROR, "setHomeAtCurrentPosition: nodes manquants");
        homeAtCurrentPositionInProgress = false;   // refus avant écriture → reset immédiat
        return false;
    }

    // Sauvegarder le mode homing actuel
    homeAtCurrentPositionSavedMode = nodes.homingModeRead->getValue();
    homeAtCurrentPositionPhase = 1;
    homeAtCurrentPositionInProgress = true;

    return true;
}

void SureServo::processHomeAtCurrentPosition() {
    if (homeAtCurrentPositionPhase == 0) return;

    switch (homeAtCurrentPositionPhase) {

        case 1:
            // Écrire mode 0x08 — sera physiquement envoyé au drive dans ce cycle
            nodes.homingModeWrite->setValue(0x08);
            Serial.println("✍️  homeAtCurrentPosition phase 1: mode 0x08 écrit");
            homeAtCurrentPositionPhase = 2;
            break;

        case 2:
            // Mode 0x08 est maintenant sur le drive — envoyer trigger PR0
            nodes.trigger->setValue(0, true);   // force write
            Serial.println("🏠 homeAtCurrentPosition phase 2: trigger PR0 envoyé");
            homeAtCurrentPositionPhase = 3;
            break;

        case 3:
            // Trigger exécuté — restaurer le mode homing original
            nodes.homingModeWrite->setValue(homeAtCurrentPositionSavedMode);
            Serial.printf("↩️  homeAtCurrentPosition phase 3: mode restauré = 0x%04X\n",
                         homeAtCurrentPositionSavedMode);
            homeAtCurrentPositionPhase = 4;
            break;

        case 4:
            // Mode restauré — finaliser
            reliablePosition = 0;
            hasReliablePosition = true;
            targetPosition = 0;
            softwareHomeDone = true;
            homeAtCurrentPositionDone = true;
            homeAtCurrentPositionPhase = 0;
            homeAtCurrentPositionEndTime = millis();
            Serial.println("✅ homeAtCurrentPosition: position remise à 0, home virtuel confirmé");
            break;
    }
}

 void SureServo::setChainedPRMode(bool enabled) {
        chainedPRMode = enabled;
        if (enabled){
         currentPr = 1; 
        }
    }
#include "SureServo.h"

#include <cstdarg>
#include <cstdlib>
#include <cstring>

//#define SURESERVO_DEBUG 1

// Constantes du drive (TOLERANCE_PUU, STATUS_*, ALARMS...) : namespace sureservo (voir SureServo.h)
using namespace sureservo;

// ========================================
// JOURNALISATION IDENTIFIÉE PAR DRIVE
// ========================================

void SureServo::logPrintf(const char* format, ...) const {
    // Préfixe + message formatés dans un seul tampon puis écrits en une fois,
    // pour ne pas entrelacer le préfixe d'un drive avec la sortie d'un autre.
    char buffer[160];
    int prefixLen = snprintf(buffer, sizeof(buffer), "[%s] ", servoId.c_str());
    if (prefixLen < 0) return;
    if ((size_t)prefixLen >= sizeof(buffer)) prefixLen = sizeof(buffer) - 1;

    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer + prefixLen, sizeof(buffer) - prefixLen, format, args);
    va_end(args);
    if (len < 0) return;

    if ((size_t)(prefixLen + len) < sizeof(buffer)) {
        Serial.print(buffer);
        return;
    }

    // Message long : allocation dynamique (même stratégie que Print::printf)
    size_t total = (size_t)prefixLen + (size_t)len + 1;
    char* big = static_cast<char*>(malloc(total));
    if (!big) {
        Serial.print(buffer);   // à défaut, message tronqué
        return;
    }
    memcpy(big, buffer, prefixLen);
    va_start(args, format);
    vsnprintf(big + prefixLen, total - prefixLen, format, args);
    va_end(args);
    Serial.print(big);
    free(big);
}

void SureServo::logPrintln(const char* text) const {
    logPrintf("%s\r\n", text ? text : "");
}

void SureServo::notifyUser(uint8_t type, const char* text) const {
    char msg[200];
    snprintf(msg, sizeof(msg), "[%s] %s", servoId.c_str(), text ? text : "");
    BorneUniverselle::prepareMessage(type, msg);
}

void SureServo::notifyUserPeriodic(bool condition, uint8_t type, const char* text) const {
    char msg[200];
    snprintf(msg, sizeof(msg), "[%s] %s", servoId.c_str(), text ? text : "");
    PLC_Tools::sendPeriodicMessage(condition, type, msg);
}

// ========================================
// CONSTRUCTEUR SURESERVO - IMPLÉMENTATION COMPLÈTE
// ========================================

SureServo::SureServo(double pulsesPerUnit, const ServoNodes& nodes, HomePositionPolicy homePositionPolicy, float maxRangeInches, const char* servoId, bool invertJogDirection)
    : ServoDrive(servoId),
    pulsesPerUnit(pulsesPerUnit),                    // 1
      _homePositionPolicy(homePositionPolicy),
      // Sens JOG : true = câblage inversé (Formaca, valeurs historiques FWD 4999 / RWD 4998)
      fwdJogValue(invertJogDirection ? JOG_RWD_VALUE_STANDARD : JOG_FWD_VALUE_STANDARD),
      rwdJogValue(invertJogDirection ? JOG_FWD_VALUE_STANDARD : JOG_RWD_VALUE_STANDARD),
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
        logPrintf("=== INITIALISATION SURESERVO ===\n");
        logPrintf("Limite physique: %.2f\" = %lu PUU\n", maxRangeInches, maxRangePuu);
    }
    
    logPrintln("=== INITIALISATION SURESERVO ===");
    logPrintln("Position interne: pas encore fiable - utilisation nodes");
   
    
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
        
        logPrintln(errorMsg);
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

    logPrintf("Pulses per unit: %.4f\n", pulsesPerUnit);
    
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
        logPrintln("ATTENTION !!! Le couple max est a 0, on le met a 100 %");
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
    
    logPrintf("Nodes obligatoires: OK (%zu)\n", 17); // Nombre de nodes obligatoires
    logPrintf("Nodes optionnels: %zu configurés\n", nodes.countOptional());
    
    // Liste des nodes optionnels configurés
    if (nodes.countOptional() > 0) {
        logPrintln("Nodes optionnels disponibles:");
        if (nodes.servoReady) logPrintln("  - servoReady");
        if (nodes.servoActivated) logPrintln("  - servoActivated");
        if (nodes.zeroSpeed) logPrintln("  - zeroSpeed");
        if (nodes.targetSpeedRated) logPrintln("  - targetSpeedRated");
        if (nodes.targetPositionReached) logPrintln("  - targetPositionReached");
        if (nodes.servoAlarm) logPrintln("  - servoAlarm");
        if (nodes.homeDone) logPrintln("  - homeDone");
        if (nodes.modbusError) logPrintln("  - modbusError");
        if (nodes.driveInitialised) logPrintln("  - driveInitialised");
        if (nodes.absolutePositionLost) logPrintln("  - absolutePositionLost");
        if (nodes.batteryAlarm) logPrintln("  - batteryAlarm");
        if (nodes.multipleTurnsOverflow) logPrintln("  - multipleTurnsOverflow");
        if (nodes.puuOverflow) logPrintln("  - puuOverflow");
        if (nodes.absoluteCoordonateNotSet) logPrintln("  - absoluteCoordonateNotSet");
    }
    
    // ========================================
    // LECTURE INITIALE DES REGISTRES
    // ========================================
    
    // Lecture de l'état initial (nodes garantis non-null par validateRequired)
    status.statusRegister = nodes.status->getValue();
    status.decodeStatusRegister();
    logPrintf("Status initial: 0x%04X\n", status.statusRegister);
    
    status.alarmsRegister = nodes.alarms->getValue();
    logPrintf("Alarms initial: 0x%04X\n", status.alarmsRegister);
    
    status.coordSystemRegister = nodes.absoluteCoordonateSystemStatus->getValue();
    status.decodeCoordSystemRegister();
    logPrintf("Coord system initial: 0x%04X\n", status.coordSystemRegister);
    
    logPrintf("Position initiale: %lu PUU\n", nodes.position->getValue());
    
    // ========================================
    // MISE À JOUR INITIALE DES NODES DE SORTIE
    // ========================================
    
    updateOptionalOutputNodes();
    
    // ========================================
    // VÉRIFICATION DE SANTÉ INITIALE
    // ========================================
    
    if (status.servoAlarm) {
        logPrintf("ATTENTION: Servo en alarme au démarrage (0x%04X)\n", status.alarmsRegister);
        setError("Servo en alarme au démarrage");
    } else if (!status.servoReady) {
        logPrintln("INFO: Servo pas encore prêt - initialisation requise");
    } else {
        logPrintln("INFO: Servo prêt");
    }

    initializeEEPROMConfiguration();

    logPrintln("Configuration EEPROM initialisée (auto-save désactivé)");
    
    logPrintln("=== SURESERVO INITIALISÉ ===");
} // constructeur SureServo

void SureServo::process() {
#ifdef SURESERVO_PERF_MONITOR
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
                logPrintln("   → movementCancelled réinitialisé - mouvements autorisés");
            }
            
            logPrintf("✅ immediateStop relâché après %lu ms (arrêt propre terminé)\n", elapsed);
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
           // logPrintf("🎯 MOVING: État SureServo = %d\n", currentStatusValue);
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
            logPrintln("✅ homeAtCurrentPosition guard expiré - détection redémarrage réactivée");
        }
    }

    // ========================================
    // DÉTECTION DE REDÉMARRAGE DU DRIVE
    // ========================================
    detectDriveRestart(now);

    // Surveillance par instance (ex-static locaux) : le délai de grâce de MODBUS_TIMEOUT_MS
    // démarre au premier appel de process(), comme l'ancien "static ... = now".
    if (!commWatchStarted) {
        lastValidCommunication = now;
        commWatchStarted = true;
    }

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
            logPrintf("🚨 SureServo: Perte communication Modbus depuis %lu ms\n",
                         now - lastValidCommunication);

            // ✅ MESSAGE UTILISATEUR POUR PERTE COMMUNICATION
            if (now - logThrottle.commErrorMessage > 10000) { // Une fois toutes les 10 secondes max
                notifyUser(ERROR,
                    "Perte de communication avec le drive servo - Vérifiez les connexions");
                logThrottle.commErrorMessage = now;
            }

            // Notifier la classe métier quel que soit l'état courant (idle inclus)
            notifyBusinessLogicFault("Perte communication Modbus avec le drive servo");

            // Interrompre les opérations en cours
            if (homingState == OperationState::DRIVE_IN_PROGRESS) {
                logPrintln("⚠️ Interruption du homing - perte communication");
                setError("Perte communication pendant homing");
                homingState = OperationState::DRIVE_FAILED;
            }

            if (moveState == OperationState::DRIVE_IN_PROGRESS) {
                logPrintln("⚠️ Interruption du mouvement - perte communication");
                setError("Perte communication pendant mouvement");
                moveState = OperationState::DRIVE_FAILED;
                releaseFastStatusPolling();
            }
        }
    }

    bool currentlyReady = getIsReady();
    
    if (!wasReady && currentlyReady) {
        logPrintln("🔄 Servo redevenu ready - Reset conditions resync");
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
        logPrintln("EEPROM: Sauvegarde terminée, mode restauré");
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
                // logPrintf("❌ Position interne invalidée - divergence: %ld PUU\n", (long)difference);
            }
        }
    }
    
    bool currentlyMoving = !status.zeroSpeed;

    if (!wasMoving && currentlyMoving) {
        if (hasReliablePosition) {
            hasReliablePosition = false;
            logPrintf("🔄 Re-sync status change: %s → %s\n", "FIABLE", "NON-FIABLE");
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
    
    if (now - logThrottle.diagnostic > 30000) { // Toutes les 30 secondes
        // Log de santé périodique
        if (consecutiveTimeouts > 10) {
            logPrintf("⚠️ SureServo: %lu timeouts consécutifs détectés\n", consecutiveTimeouts);
        }
        
        // Vérification de cohérence d'état
        if (initialized && !getIsReady() && !getHasAlarms()) {
            logPrintln("⚠️ SureServo: État incohérent - initialisé mais pas ready sans alarme");
        }
        
        logThrottle.diagnostic = now;
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
#ifdef SURESERVO_PERF_MONITOR
    if (pm_active) {
        uint32_t _d = micros() - _pm_t0;
        pm_processTimeCumul += _d;
        pm_processCallCount++;
        if (_d > pm_processTimeMax) pm_processTimeMax = _d;
    }
#endif
}// process

bool SureServo::startInitialize() {
    logPrintln("🔧 SureServo::startInitialize() - DÉBUT");
    // Forcer alarmsReset à false au démarrage.
    // hideValue=false par défaut donc setValue(false) dans le constructeur
    // n'a pas déclenché d'écriture hardware — le relais peut avoir retenu
    // l'état true du cycle précédent.
    if (nodes.alarmsReset) {
        nodes.alarmsReset->setValue(true);   // force un changement de hideValue
        nodes.alarmsReset->setValue(false);  // écrit false au hardware
        logPrintln("✅ SureServo: alarmsReset forcé à false (démarrage)");
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
        logPrintln("⚠️ SureServo: Servo en alarme détecté - Tentative de reset automatique");
        
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
        
        logPrintln("🔄 SureServo: Reset automatique démarré avant initialisation");
        
        // Passer en mode d'initialisation avec auto-recovery
        initState = OperationState::DRIVE_IN_PROGRESS;
        initPhase = 100;  // Phase spéciale pour auto-recovery
        operationStartTime = millis();
        phaseStartTime = millis();
        lastError = "";
        
        logPrintln("✅ SureServo: Initialisation avec auto-recovery démarrée");
        return true;
    }
    
    // ✅ DÉMARRAGE NORMAL : Initialiser les variables d'état
    initState = OperationState::DRIVE_IN_PROGRESS;
    initPhase = 0;
    operationStartTime = millis();   // sera fixé en phase 0, après les writes hardware
    phaseStartTime = millis();
    
    // ✅ Clear l'erreur précédente
    lastError = "";
    
    logPrintf("✅ SureServo: Initialisation normale démarrée (phase=%d)\n", initPhase);
    return true;
}

bool SureServo::startReset() {
    if (isPlcSuspended()) {
        setError("Reset refusé - PLC suspendu");
        logPrintln("❌ SureServo: startReset refusé - PLC suspendu");
        return false;
    }

    logPrintln("🔧 SureServo::startReset() - DÉBUT");
    
    // ✅ GESTION SIMPLE DES ÉTATS
    switch (resetState) {
        case OperationState::DRIVE_IDLE:
            // ✅ Prêt - continuer
            logPrintln("✅ SureServo: Reset - état IDLE");
            break;
            
        case OperationState::DRIVE_IN_PROGRESS:
            // ❌ Vraiment en cours - refuser
            setError("Reset déjà en cours");
            return false;
            
        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT:
            // ✅ Auto-clear des erreurs - retry autorisé
            logPrintf("⚠️ SureServo: Reset après erreur - retry autorisé\n");
            resetState = OperationState::DRIVE_IDLE;
            resetPhase = 0;
            lastError = "";
            break;
    }
    
    // ✅ DÉMARRAGE (inchangé)
    resetState = OperationState::DRIVE_IN_PROGRESS;
    resetPhase = 0;
    resetSuccessStartTime = 0;  // nouvelle fenêtre de stabilité de 200 ms (audit MI-20)
    operationStartTime = millis();
    phaseStartTime = millis();
    lastError = "";
    
    logPrintf("✅ SureServo: Reset démarré (phase=%d)\n", resetPhase);
    return true;
}
    
bool SureServo::startGoToPosition(int32_t positionPuu, bool waitForSync, uint32_t timeout) {
    if (isPlcSuspended()) {
        setError("Mouvement refusé - PLC suspendu");
        logPrintf("❌ SureServo: startGoToPosition(%lu) refusé - PLC suspendu\n", 
                     (unsigned long)positionPuu);
        return false;
    }

    logPrintf("🔧 SureServo::startGoToPosition(%lu) - DÉBUT\n", (unsigned long)positionPuu);
    chainedPR1Complete = false;
    chainedMovingAway   = 0;
    chainedPrevPosition = nodes.position->getValueAsInt32();
    logPrintf("🔍 DEBUG chainedPR1Target=%ld, prevPos=%ld\n", (long)chainedPR1Target, (long)chainedPrevPosition);


    // ========================================
    // 0. PROTECTION: BLOCAGE POST-ANNULATION
    // ========================================
    
    if (movementCancelled) {
        setError("Mouvement bloqué - annulation récente (attendez cycle complet)");
        logPrintln("⛔ SureServo: startGoToPosition bloqué - movementCancelled actif");
        return false;
    }
    
    // ========================================
    // 1. GESTION INTELLIGENTE DES ÉTATS
    // ========================================
    
   switch (moveState) {
        case OperationState::DRIVE_IDLE:
            logPrintln("✅ SureServo: État IDLE - démarrage autorisé");
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
        logPrintf("❌ SureServo: Refus - servo pas prêt (ready=%s, initialized=%s)\n",
                     getServoReady() ? "true" : "false",
                     initialized ? "true" : "false");
        return false;
    }
    
    if (getHasAlarms()) {
        setError("Servo en alarme - mouvement impossible");
        logPrintf("❌ SureServo: Refus - alarme active (0x%04X)\n", getAlarmCode());
        return false;
    }
    
    // ========================================
    // 2.5 VÉRIFICATION LIMITE PHYSIQUE (butée droite)
    // ========================================

    if (maxRangePuu > 0 && positionPuu > (int32_t)(maxRangePuu + PHYSICAL_LIMIT_MARGIN_PUU)) {
        setError("Position cible au-delà de la butée physique");
        logPrintf("⛔ SureServo: Refus - cible %ld PUU > limite %lu PUU (marge: %lu)\n",
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
        logPrintf("ℹ️ SureServo: Déjà à la position cible (écart: %ld PUU <= tolérance: %lu)\n",
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
        logPrintln("❌ SureServo: startHoming refusé - PLC suspendu");
        return false;
    }

    currentHomingTimeoutMs = (timeoutMs > 0) ? timeoutMs : HOMING_TIMEOUT_MS;

    logPrintln("🔧 SureServo::startHoming() - DÉBUT");

    // Vérifications...
    if (!getIsReady()) return false;

    homeDoneInvalidated = false;  // un nouveau homing revalide le home

    // ✅ ENVOI IMMÉDIAT DE LA COMMANDE
    nodes.trigger->setValue(120);  // Reset
    nodes.trigger->setValue(0);    // Commande homing
    logPrintln("🏠 Commande homing envoyée");
    
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
        logPrintln("❌ SureServo: jogForward refusé - PLC suspendu");
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
        logPrintf("⛔ SureServo: Limite forward atteinte (%ld >= %ld)\n", pos, limit);
        return false;
    }

    // ✅ PREMIER APPEL — Séquence 2 cycles : DIRECTION puis VITESSE
    if (!jogForwardActive) {
        if (!_jogSpeedRestored) {
            // **CYCLE 1 — Envoyer DIRECTION d'abord**
            _savedJogSpeed = currentJogSpeed;
            acquireFastPositionPolling();
            nodes.jogSpeed->setValue(fwdJogValue);  // ← CHANGEMENT : direction d'abord
            _jogSpeedRestored = true;
            logPrintln("JOG FWD Cycle 1: DIRECTION (FWD_VALUE)");
            return true;  // ← attendre prochain cycle
        }
        
        // **CYCLE 2 — Envoyer VITESSE ensuite**
        // Calculer vitesse selon zone AU MOMENT DU DÉMARRAGE
        uint16_t speed = currentJogSpeed;
        if (slowZonePuu > 0 && nodes.homeDone && nodes.homeDone->getValue() && pos >= slowLimit) {
            speed = slowSpeed;  // Démarrage dans slow zone
            logPrintf("JOG FWD Cycle 2: Démarrage dans SLOW ZONE (%u RPM)\n", speed);
        } else {
            logPrintf("JOG FWD Cycle 2: Démarrage vitesse normale (%u RPM)\n", speed);
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
            logPrintf("JOG FWD: Entrée slow zone → %u RPM\n", slowSpeed);
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
        logPrintln("❌ SureServo: jogReverse refusé - PLC suspendu");
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
        logPrintf("⛔ SureServo: Limite reverse atteinte (%ld <= %ld)\n", pos, limit);
        return false;
    }

    // ✅ PREMIER APPEL — Séquence 2 cycles : DIRECTION puis VITESSE
    if (!jogReverseActive) {
        if (!_jogSpeedRestored) {
            // **CYCLE 1 — Envoyer DIRECTION d'abord**
            _savedJogSpeed = currentJogSpeed;
            acquireFastPositionPolling();
            nodes.jogSpeed->setValue(rwdJogValue);  // ← CHANGEMENT : direction d'abord
            _jogSpeedRestored = true;
            logPrintln("JOG REV Cycle 1: DIRECTION (RWD_VALUE)");
            return true;  // ← attendre prochain cycle
        }
        
        // **CYCLE 2 — Envoyer VITESSE ensuite**
        // Calculer vitesse selon zone AU MOMENT DU DÉMARRAGE
        uint16_t speed = currentJogSpeed;
        if (slowZonePuu > 0 && nodes.homeDone && nodes.homeDone->getValue() &&
            pos <= slowLimit) {
            speed = slowSpeed;  // Démarrage dans slow zone
            logPrintf("JOG REV Cycle 2: Démarrage dans SLOW ZONE (%u RPM)\n", speed);
        } else {
            logPrintf("JOG REV Cycle 2: Démarrage vitesse normale (%u RPM)\n", speed);
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
            logPrintf("JOG REV: Entrée slow zone → %u RPM\n", slowSpeed);
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
    
    logPrintln("✅ SureServo: Jog arrêté - Re-sync programmée (non-bloquant)");
    
    if (moveState != OperationState::DRIVE_IDLE &&
        moveState != OperationState::DRIVE_FAILED) {
        logPrintf("Correction moveState après JOG: %d → IDLE\n",
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
    
    logPrintf("Vitesse jog définie: %lu\n", speed);
    return true;
}

uint32_t SureServo::getJogSpeed() const {
    return currentJogSpeed;
}

bool SureServo::setServoEnabled(bool enable) {
    nodes.servoOn->setValue(enable);
    logPrintf("✅ SureServo: Servo %s\n", enable ? "activé" : "désactivé");
    return true;
}

bool SureServo::setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) {
    if (isPlcSuspended()) {
        setError("Commande refusée - PLC suspendu");
        logPrintln("❌ SureServo: setSpeedAndRamp refusé - PLC suspendu");
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
        logPrintf("⚠️ SureServo: rampIndex %u hors excursion, clampé à %u\r\n", rampIndex, ramp);
    }

    uint32_t driveValue = ((uint32_t)speed << 16)   // vitesse PR (0-15)
                         | ((uint32_t)ramp << 12)    // décélération
                         | ((uint32_t)ramp << 8)     // accélération
                         | PR_TYPE_SINGLE;

    logPrintf("New user speed: %u, ramp: %u. Will set register with value 0x%08x\r\n",
                  speed, ramp, driveValue);
    nodes.pr1Speed->setValue(driveValue);
    nodes.pr2Speed->setValue(driveValue);
    nodes.pr3Speed->setValue(driveValue);

    currentPrSpeed = speed;
    currentRampIndex = ramp;

    logPrintf("Vitesse PR définie: %u, rampe: %u (table servo drive)\n", speed, ramp);
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
    logPrintf("✅ SureServo: Couple max défini à %u%%\n", torquePercent);
    return true;
}

uint8_t SureServo::getMaxTorque() const {
    return nodes.maxTorque->getValue();
}

void SureServo::setMaxRangeInches(float maxRangeInches) {
    if (maxRangeInches > 0) {
        maxRangePuu = (uint32_t)((double)maxRangeInches * pulsesPerUnit);
        logPrintf("Limite physique mise à jour à chaud: %.2f\" = %lu PUU\n", maxRangeInches, maxRangePuu);
    } else {
        maxRangePuu = 0;
        logPrintln("Limite physique désactivée (maxRangeInches <= 0)");
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

    logPrintln("🚨 SureServo: Arrêt d'urgence activé");
    return true;
}

bool SureServo::saveParameters() {
    return triggerManualEEPROMSave();
}

int32_t SureServo::getPosition() const {
    if (hasReliablePosition) {
        logPrintln("Retour de la position effective (resychro terminé)");
        return reliablePosition;   // position effective (re-sync terminée)
    } else {
         logPrintln("Retour de la position théorique (re synchro en cours)");
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
    for (size_t i = 0; i < ALARMS_COUNT; i++) {
        if (ALARMS[i].code == alarmCode) {
            return ALARMS[i].description;
        }
    }
    
    return "Alarme inconnue";
}

const char* SureServo::getAlarmAction() const {
    uint16_t alarmCode = getAlarmCode();
    
    // Recherche dans la table
    for (size_t i = 0; i < ALARMS_COUNT; i++) {
        if (ALARMS[i].code == alarmCode) {
            return ALARMS[i].action;
        }
    }
    
    return "Consulter le manuel";
}

void SureServo::printStatus() const {
    logPrintln("==============================");
    logPrintln("     SURESERVO STATUS");
    logPrintln("==============================");
    
    // ÉTAT GÉNÉRAL
    logPrintf("Ready: %s\n", getIsReady() ? "true" : "false");
    logPrintf("Initialized: %s\n", getIsInitialized() ? "true" : "false");
    logPrintf("Servo Activated: %s\n", getServoActivated() ? "true" : "false");
    logPrintf("Zero Speed: %s\n", status.zeroSpeed ? "true" : "false");
    logPrintf("Home Done: %s\n", getHomeDone() ? "true" : "false");
    
    // ALARMES
    if (getHasAlarms()) {
        logPrintf("ALARMES: OUI (0x%04X)\n", getAlarmCode());
        logPrintf("Description: %s\n", getAlarmDescription());
        logPrintf("Action: %s\n", getAlarmAction());
    } else {
        logPrintln("ALARMES: Aucune");
    }
    
    // POSITION (CŒUR DU SYSTÈME)
    if (hasReliablePosition) {
        logPrintf("Position Interne: %ld PUU (FIABLE)\n", (long)reliablePosition);
        logPrintf("Position Nodes: %ld PUU\n", (long)nodes.position->getValueAsInt32());

        int32_t difference = (reliablePosition > nodes.position->getValueAsInt32()) ?
                             (reliablePosition - nodes.position->getValueAsInt32()) :
                             (nodes.position->getValueAsInt32() - reliablePosition);
        logPrintf("Écart: %ld PUU\n", (long)difference);
        
        // Conversion en unités physiques
        logPrintf("Position (inch): %.3f\"\n", (float)reliablePosition / pulsesPerUnit);
    } else {
        logPrintf("Position: %lu PUU (depuis nodes - PAS FIABLE)\n", getPosition());
        logPrintf("Position (inch): %.3f\"\n", (float)getPosition() / pulsesPerUnit);
        
        // Info sur re-sync en cours
        if (resyncInProgress) {
            logPrintf("Re-sync: EN COURS (%d/5 lectures stables)\n", idleStableReadingsCount);
            logPrintf("Re-sync interval: %lu ms\n", getHomingPositionReadInterval());
        } else {
            logPrintln("Re-sync: En attente d'immobilité");
        }
    }
    
    // OPÉRATIONS EN COURS
    logPrintln("--- OPÉRATIONS ---");
    logPrintf("Init State: %s\n", 
                 initState == OperationState::DRIVE_IDLE ? "IDLE" :
                 initState == OperationState::DRIVE_IN_PROGRESS ? "IN_PROGRESS" :
                 initState == OperationState::DRIVE_FAILED ? "FAILED" : "OTHER");
    
    logPrintf("Move State: %s\n", 
                 moveState == OperationState::DRIVE_IDLE ? "IDLE" :
                 moveState == OperationState::DRIVE_IN_PROGRESS ? "IN_PROGRESS" :
                 moveState == OperationState::DRIVE_FAILED ? "FAILED" : "OTHER");
    
    logPrintf("Homing State: %s\n", 
                 homingState == OperationState::DRIVE_IDLE ? "IDLE" :
                 homingState == OperationState::DRIVE_IN_PROGRESS ? "IN_PROGRESS" :
                 homingState == OperationState::DRIVE_FAILED ? "FAILED" : "OTHER");
    
    // DÉTAILS HOMING SI EN COURS
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        logPrintf("Homing Phase: %d\n", homingPhase);
        if (homingPhase == 1) {
            logPrintf("Homing Lectures stables: %d/3\n", homingStableReadingsCount);
            logPrintf("Homing Interval: %lu ms\n", getHomingPositionReadInterval());
        }
    }
    
    // JOG ET VITESSES
    logPrintln("--- JOG & VITESSES ---");
    uint16_t jogValue = nodes.jogSpeed->getValue();
    if (jogValue != STOP_VALUE) {
        logPrintf("JOG: %s (%u)\n", 
                     jogValue == fwdJogValue ? "FORWARD" :
                     jogValue == rwdJogValue ? "REVERSE" : "UNKNOWN",
                     jogValue);
    } else {
        logPrintln("JOG: STOPPED");
    }
    
    logPrintf("Jog Speed: %lu\n", currentJogSpeed);
    logPrintf("PR Speed: %u\n", currentPrSpeed);
    logPrintf("Current PR: %u\n", currentPr);
    logPrintf("Max Torque: %u%%\n", nodes.maxTorque->getValue());
    
    // CONFIGURATION
    logPrintln("--- CONFIGURATION ---");
    logPrintf("Pulses per unit: %.4f\n", pulsesPerUnit);
    logPrintf("Tolerance: %lu PUU\n", (unsigned long)TOLERANCE_PUU);
    logPrintf("Nodes Required: %s\n", nodes.validateRequired() ? "OK" : "MANQUANTS");
    logPrintf("Nodes Optional: %zu configurés\n", nodes.countOptional());
    
    // ERREURS
    if (!lastError.isEmpty()) {
        logPrintln("--- ERREUR ---");
        logPrintf("Last Error: %s\n", lastError.c_str());
    }
    
    logPrintln("==============================");
}

void SureServo::printAlarms() const {
    if (getHasAlarms()) {
        logPrintln("=== ALARMES ACTIVES ===");
        logPrintf("Code: 0x%04X\n", getAlarmCode());
        logPrintf("Description: %s\n", getAlarmDescription());
        logPrintf("Action: %s\n", getAlarmAction());
        logPrintln("=======================");
    } else {
        logPrintln("Aucune alarme active");
    }
}

void SureServo::printConfiguration() const {
    logPrintln("=== CONFIGURATION SERVO MOTOR ===");
    logPrintf("Pulses per unit: %.4f\n", pulsesPerUnit);
    logPrintf("Nodes required: %s\n", nodes.validateRequired() ? "OK" : "MANQUANTS");
    logPrintf("Nodes optional: %zu configurés\n", nodes.countOptional());
    logPrintln("================================");
}

// ========================================
// MÉTHODES PRIVÉES - MACHINES À ÉTATS
// ========================================

void SureServo::processInitialize(uint32_t now) {

    // ========================================
    // ATTENTE MODBUS VIVANT SUR ADRESSE DU DRIVE
    // ========================================
    uint16_t slaveAddr = getSlaveAddress();
    if (slaveAddr != 0 && MyModbus::getInstance().isSlaveConsideredDead(slaveAddr)) {
        // Geler le timer - ne pas consommer le budget de 15s pendant que le drive ne repond pas
        operationStartTime = now;
        if (waitModbusStartTime == 0) {
            waitModbusStartTime = now;
            logPrintf("Attente drive sur adresse Modbus %u...\n", slaveAddr);
        }

        // Message repetitif apres 5s d'attente
        if (now - waitModbusStartTime > 5000) {
            if (now - logThrottle.initWaitDrive > 5000) {
                logPrintf("Attente drive addr %u - toujours injoignable...\n", slaveAddr);
                logThrottle.initWaitDrive = now;
            }
        }
        return;
    }

    // Adresse vivante - eteindre le message periodique si actif et reset du chrono d'attente
    if (waitModbusStartTime != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Drive servo addr %u: pas de reponse - verifier alimentation", slaveAddr);
        notifyUserPeriodic(false, WARNING, msg);
        waitModbusStartTime = 0;
        logPrintf("Drive addr %u repond - demarrage initialisation\n", slaveAddr);
    }

    // Position marquee non fiable au demarrage
    if (initPhase == 0 && !initialized) {
        hasReliablePosition = false;
        logPrintln("Initialisation : Position marquee NON fiable (demarrage)");
    }

    // VERIFIER TIMEOUT GLOBAL
    if (now - operationStartTime > INIT_TIMEOUT_MS) {
        logPrintf("Timeout initialisation apres %lu ms\n", now - operationStartTime);
        setError("Timeout initialisation");
        initState = OperationState::DRIVE_TIMEOUT;
        return;
    }

    // GESTION PHASE AUTO-RECOVERY
    if (initPhase == 100) {
        switch (resetState) {
            case OperationState::DRIVE_IN_PROGRESS:
                if (now - logThrottle.initAutoRecovery > 2000) {
                    logPrintln("SureServo: Reset automatique en cours...");
                    logThrottle.initAutoRecovery = now;
                }
                return;

            case OperationState::DRIVE_IDLE:
                logPrintln("SureServo: Reset automatique reussi - Passage a l'initialisation normale");
                initPhase = 0;
                phaseStartTime = now;
                resetState = OperationState::DRIVE_IDLE;
                resetPhase = 0;
                break;

            case OperationState::DRIVE_FAILED:
            case OperationState::DRIVE_TIMEOUT:
                logPrintln("SureServo: Reset automatique echoue");
                setError("Reset automatique echoue - Servo toujours en alarme");
                initState = OperationState::DRIVE_FAILED;
                return;

            default:
                logPrintln("SureServo: Etat reset inattendu pendant auto-recovery");
                setError("Etat reset inattendu pendant auto-recovery");
                initState = OperationState::DRIVE_FAILED;
                return;
        }
    }  // initPhase == 100

    // PHASES NORMALES D'INITIALISATION
    switch (initPhase) {
        case 0:
            logPrintln("SureServo Init Phase 0: Configuration");
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
                logPrintln("SureServo Init Phase 1: Verification etat");
                initPhase = 2;
                phaseStartTime = now;
            }
            break;

        case 2:
            // servoActivated non requis au repos
            if (status.servoReady && !status.servoAlarm) {
                logPrintln("SureServo: Initialisation reussie");

                if (getHomeDone()) {
                    uint32_t lastRefresh = nodes.position->getLastRefresh();
                    if (lastRefresh < operationStartTime) {
                        if (now - logThrottle.initWaitPosition > 500) {
                            logPrintf("Attente lecture position (last refresh: %lu, start: %lu)...\n",
                                        lastRefresh, operationStartTime);
                            logThrottle.initWaitPosition = now;
                        }
                        return;
                    }

                    int32_t currentPos = nodes.position->getValueAsInt32();
                    bool positionError = false;

                    if (currentPos < 0 && currentPos > -(int32_t)TOLERANCE_PUU) {
                        logPrintf("Position legerement negative: %ld PUU -> corrigee a 0\n", (long)currentPos);
                        currentPos = 0;
                        notifyUser(WARNING, "Position negative corrigee a 0");
                    } else if (_homePositionPolicy == HomePositionPolicy::NEGATIVE_INVALIDATES_HOME && currentPos < 0) {
                        // Position négative au-delà de la tolérance : homeDone n'est plus fiable,
                        // mais ce n'est pas une vraie sortie de course (le drive a juste dérivé sous 0)
                        logPrintf("Position negative detectee: %ld PUU -> home invalide, re-homing requis\n",
                                    (long)currentPos);
                        homeDoneInvalidated = true;
                        notifyUser(WARNING, "Position negative - re-homing requis");
                    } else if (maxRangePuu > 0 && (uint32_t)currentPos > maxRangePuu) {
                        logPrintf("ERREUR: Position hors limites: %ld PUU (max: %lu)\n", (long)currentPos, maxRangePuu);
                        setError("Position hors limites machine, veuillez reseter le drive");
                        positionError = true;
                    }

                    if (positionError) {
                        nodes.immediateStop->setValue(true);
                        nodes.servoOn->setValue(false);
                        initialized = false;
                        hasReliablePosition = false;
                        initState = OperationState::DRIVE_FAILED;
                        notifyUserPeriodic(true, ERROR,
                            "Position incoherente - Eteignez et rallumez le drive");
                        return;
                    }

                    if (homeDoneInvalidated) {
                        logPrintln("Home invalide - Homing necessaire");
                        hasReliablePosition = false;
                    } else {
                        reliablePosition = currentPos;
                        hasReliablePosition = true;
                        logPrintf("HomeDone - Position: %ld PUU (%.2f\")\n",
                                    (long)reliablePosition, (float)reliablePosition / pulsesPerUnit);
                    }
                } else {
                    logPrintln("HomeDone=false - Homing necessaire");
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
                    logPrintln("SureServo: Initialisation complete avec succes");
                } else {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Unable to set speed: %u, probably not in range\r\n", currentPrSpeed);
                    setError(String(msg));
                }

            } else if (now - phaseStartTime > 5000) {
                // Servo pas encore ready apres 5s - log periodique en attendant le timeout global
                if (now - logThrottle.initWaitReady > 2000) {
                    logPrintf("Phase 2: attente servoReady (status=0x%04X alarm=0x%04X)\n",
                        nodes.status->getValue(), nodes.alarms->getValue());
                    logThrottle.initWaitReady = now;
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
        resetSuccessStartTime = 0;  // fenêtre de stabilité abandonnée (audit MI-20)
        return;
    }
    
    switch (resetPhase) {
        case 0:
            logPrintln("🔧 SureServo Reset Phase 0: Activation relais");
            
            // S'assurer que le relais est d'abord OFF
            if (nodes.alarmsReset->getValue()) {
                nodes.alarmsReset->setValue(false);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            
            // Activer le relais
            nodes.alarmsReset->setValue(true);
            resetPhase = 1;
            phaseStartTime = now;
            logPrintln("✅ SureServo Reset: Relais activé");
            break;
            
       case 1: // Maintenir le pulse
            if (now - phaseStartTime >= RESET_PULSE_MS) {
                logPrintln("🔧 SureServo Reset Phase 1: Désactivation relais");
                nodes.alarmsReset->setValue(false);   
                resetPhase = 2;
                phaseStartTime = now;
                logPrintln("✅ SureServo Reset: Relais désactivé");
            }
            break;
            
        case 2: // Stabilisation
            if (now - phaseStartTime >= RESET_STABILIZE_MS) {
                logPrintln("🔧 SureServo Reset Phase 2: Vérification continue");
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

                logPrintf("🔍 Reset check: servoOn=%d, immediateStop=%d, alarmRaw=%d\n", 
                    nodes.servoOn->getValue(), 
                    nodes.immediateStop->getValue(),
                    nodes.alarms->getValue());
                
                bool success = currentServoReady && !currentServoAlarm && (currentAlarms == 0);

                if (success && !currentServoActivated) {
                    logPrintln("🔧 Servo ready mais pas activated - activation...");
                    nodes.servoOn->setValue(true);
                }
                
                if (success) {
                    // ✅ VÉRIFICATION STABILITÉ (succès pendant 200ms minimum)
                    // (membre resetSuccessStartTime : ex-static local partagé entre instances)
                    if (resetSuccessStartTime == 0) {
                        resetSuccessStartTime = now;
                        logPrintln("🔍 SureServo Reset: Début période de stabilité");
                    }
                    
                    if (now - resetSuccessStartTime >= 200) { // 200ms de stabilité
                        logPrintln("✅ SureServo Reset: Succès stable confirmé");
                        resetState = OperationState::DRIVE_IDLE;
                        resetSuccessStartTime = 0;
                        // Un reset réussi réinitialise tous les états bloqués
                        // (moveState, initState, driveInitStarted, initFailureReported, homingState)
                        clearMovementLock();
                    } else {
                        // Log périodique pendant stabilité
                        if (now - logThrottle.resetStability > 50) {
                            logPrintf("🔍 Stabilité: %lu ms / 200 ms\n", now - resetSuccessStartTime);
                            logThrottle.resetStability = now;
                        }
                    }
                    
                } else {
                    if (now - phaseStartTime > 2000) { // 2s pour la vérification
                        String error = "Reset SureServo échoué: ";
                        error += "ready=" + String(currentServoReady ? "1" : "0");
                        error += ", activated=" + String(currentServoActivated ? "1" : "0");
                        error += ", alarm=" + String(currentServoAlarm ? "1" : "0");
                        error += ", alarmCode=0x" + String(currentAlarms, HEX);
                        
                        logPrintf("❌ SureServo Reset: Échec définitif - %s\n", error.c_str());
                        setError(error);
                        resetState = OperationState::DRIVE_FAILED;
                        resetSuccessStartTime = 0;  // fenêtre de stabilité abandonnée (audit MI-20)
                    } else {
                        // Log périodique d'attente
                        if (now - logThrottle.resetWait > 500) {
                            logPrintf("🔄 SureServo Reset: Attente... ready=%s, activated=%s, alarm=%s, code=0x%04X\n",
                                        currentServoReady ? "✅" : "❌",
                                        currentServoActivated ? "✅" : "❌", 
                                        currentServoAlarm ? "🚨" : "✅",
                                        currentAlarms);
                            logThrottle.resetWait = now;
                        }
                    }
                }
            }
            break;
            
        default:
            setError("Phase reset SureServo inconnue: " + String(resetPhase));
            resetState = OperationState::DRIVE_FAILED;
            resetSuccessStartTime = 0;
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
        logPrintf("🚨 BUG: processMove() appelé avec moveState=%d (pas IN_PROGRESS)\n", 
                     static_cast<int>(moveState));
        return;
    }
    
    // ✅ VÉRIFIER TIMEOUT GLOBAL 
    uint32_t elapsed = now - operationStartTime;
    if (elapsed > currentMoveTimeoutMs) {
        logPrintf("⏰ SureServo: TIMEOUT mouvement après %lu ms (limite: %lu ms)\n",
                     elapsed, currentMoveTimeoutMs);
        
        // ✅ LOG DÉTAILLÉ DU TIMEOUT
        logPrintf("📊 TIMEOUT - État au moment du timeout:\n");
        logPrintf("   Position actuelle: %lu PUU (%.2f\")\n", 
                     (unsigned long)nodes.position->getValue(),
                     (float)nodes.position->getValue() / pulsesPerUnit);
        logPrintf("   Target: %ld PUU (%.2f\")\n",
                     (long)targetPosition,
                     (float)targetPosition / pulsesPerUnit);
        logPrintf("   Phase: %d, PR: %d\n", movePhase, currentPr);
        logPrintf("   Signal validé: %s\n", signalValidated ? "OUI" : "NON");
        logPrintf("   TargetReached: %s\n", 
                     nodes.targetPositionReached->getValue() ? "TRUE" : "FALSE");
        
        setError("Timeout mouvement après " + String(currentMoveTimeoutMs / 1000) + " secondes");
        moveState = OperationState::DRIVE_TIMEOUT;
        movePhase = 0; // Reset pour cohérence
        releaseFastStatusPolling();
        return;
    }

    // ✅ VÉRIFICATION ALARMES CONTINUES
    if (getHasAlarms()) {
        logPrintf("🚨 Alarme pendant mouvement (0x%04X) - Arrêt immédiat\n", getAlarmCode());
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
                logPrintf("🔧 SureServo Move Phase 0: Mode chaîné - target PR%d déjà configuré\n", currentPr);
            } else {
                logPrintf("🔧 SureServo Move Phase 0: Envoi target %ld PUU au PR%d\n",
                             (long)targetPosition, currentPr);
                sendTargetToPR(targetPosition);
            }
            
            movePhase = 1;
            phaseStartTime = now;
            
            logPrintf("✅ SureServo: Target envoyé au PR%d - Attente 1 cycle PLC\n", currentPr);
            break;
        }
        
        case 1: {
            // ✅ PHASE 1: ATTENTE 1 CYCLE PLC AVANT TRIGGER
#ifdef SURESERVO_TRIGGER_SMART_DELAY
            Node* targetNode = (currentPr == 1) ?
                (Node*)nodes.targetA : (Node*)nodes.targetB;
            bool targetWritten = (targetNode->getLastRefresh() > phaseStartTime);
            bool safetyTimeout = (now - phaseStartTime >= TRIGGER_DELAY_MS);
            #ifdef SURESERVO_PERF_MONITOR
            if (pm_active) {
                if (targetWritten && !safetyTimeout) pm_smartDelayCount++;
                if (safetyTimeout)                  pm_safetyTimeoutCount++;
            }
            #endif

            if (targetWritten || safetyTimeout) {
            //if (now - phaseStartTime >= TRIGGER_DELAY_MS) {
                if (safetyTimeout && !targetWritten)
                    logPrintf("⚠️ TRIGGER safety timeout %lu ms\n", now - phaseStartTime);
#else
            if (now - phaseStartTime >= TRIGGER_DELAY_MS) {
#endif
                logPrintf("🚀 SureServo Move Phase 1: Déclenchement trigger PR%d\n", currentPr);
                
                nodes.trigger->setValue(currentPr, true);
#ifdef SURESERVO_PERF_MONITOR
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
                logPrintf("✅ SureServo: Trigger déclenché PR%d vers %ld PUU\n",
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
#ifdef SURESERVO_PERF_MONITOR
                    if (pm_active && pm_triggerSentTime > 0) {
                        uint32_t lat = now - pm_triggerSentTime;
                        pm_driveLatencyCumul += lat;
                        if (lat > pm_driveLatencyMax) pm_driveLatencyMax = lat;
                        pm_signalValidatedTime = now;
                    }
#endif
                    logPrintln("✅ SureServo: Mouvement long - transition false détectée");
                } else if (!status.zeroSpeed) {
                    // Mouvement confirmé par vitesse non nulle
                    signalValidated = true;
#ifdef SURESERVO_PERF_MONITOR
                    if (pm_active && pm_triggerSentTime > 0) {
                        uint32_t lat = now - pm_triggerSentTime;
                        pm_driveLatencyCumul += lat;
                        if (lat > pm_driveLatencyMax) pm_driveLatencyMax = lat;
                        pm_signalValidatedTime = now;
                    }
#endif
                    logPrintln("✅ SureServo: Mouvement détecté - vitesse non nulle");
                } else if (timeSinceTrigger > 150 && 
                    nodes.position->getLastRefresh() > phaseStartTime &&
                    nodes.status->getLastRefresh() > phaseStartTime &&
                    !chainedPRMode) {
                    signalValidated = true;
#ifdef SURESERVO_PERF_MONITOR
                    if (pm_active && pm_triggerSentTime > 0) {
                        uint32_t lat = now - pm_triggerSentTime;
                        pm_driveLatencyCumul += lat;
                        if (lat > pm_driveLatencyMax) pm_driveLatencyMax = lat;
                        pm_signalValidatedTime = now;
                    }
#endif
                    logPrintln("⚠️ SureServo: Mouvement court - status confirmé après trigger");
                }
                // Log périodique
                if (now - logThrottle.moveWaitStart > 2000) {
                    logPrintf("⏳ Attente début mouvement... (%lu ms)\n", timeSinceTrigger);
                    logThrottle.moveWaitStart = now;
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
            #ifdef SURESERVO_PERF_MONITOR
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
                logPrintf("✅ Mouvement terminé vers %ld PUU%s\n",
                            (long)targetPosition,
                            waitForSyncRequested ? " - resync en attente" : "");
                return;
            }

            // Log périodique
            if (now - logThrottle.moveWaitReached > 2000) {
                logPrintf("⏳ Attente target reached... (%lu ms)\n", timeSinceTrigger);
                logThrottle.moveWaitReached = now;
            }
            break;
        }
        
        default: {
            logPrintf("❌ SureServo: Phase mouvement inconnue: %d\n", movePhase);
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
        logPrintf("⏰ SureServo: Timeout homing après %lu ms (limite: %lu ms)\n", totalElapsed, currentHomingTimeoutMs);
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
        logPrintf("🚨 Alarme pendant homing (0x%04X)\n", getAlarmCode());
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
                logPrintln("✅ HomeDone détecté - début détection stabilisation position");
                
                // Initialiser la détection de stabilisation
                uint32_t currentPos = nodes.position->getValue();
                previousHomingPosition = currentPos;
                lastHomingPositionReadTime = now;
                homingStableReadingsCount = 0;
                homingPhase = 1;
                
                uint32_t readInterval = getHomingPositionReadInterval();
                logPrintf("🎯 Position initiale: %lu PUU (intervalle lecture: %lu ms)\n", 
                             currentPos, readInterval);
                logPrintln("🔄 Attente stabilisation (déplacement vers index si nécessaire)...");
            } else {
                // Attendre HomeDone
                if (now - logThrottle.homingWaitHomeDone > 2000) {
                    logPrintf("⏳ Attente HomeDone... (%lu ms écoulées)\n", totalElapsed);
                    logThrottle.homingWaitHomeDone = now;
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
                    logPrintf("🚨 HOMING: Position aberrante: %ld PUU (int32)\n",
                                 (long)currentPosition_signed);
                    logPrintln("🔧 CORRECTION: Position forcée à 0 pour sécurité");
                    finalHomingPosition = 0;
                    // Grace period : laisse une chance à la prochaine lecture (potentiellement
                    // non polluée) avant que le contrôle de cohérence (process()) ne réinvalide
                    // hasReliablePosition sur la base de cette même lecture aberrante.
                    homingCorrectionTime = now;

                    notifyUser(WARNING,
                        "Position homing corrigée - valeur aberrante détectée");
                } else if (currentPosition_signed < 0 && currentPosition_signed > -TOLERANCE_PUU) {
                    // Valeur légèrement négative (-50 à -1) → acceptable, corriger à 0
                    logPrintf("ℹ️ Position légèrement négative: %ld PUU → 0 PUU\n", 
                                 (long)currentPosition_signed);
                    finalHomingPosition = 0;
                }
                
                // ✅ Utiliser la position validée
                reliablePosition = finalHomingPosition;
                hasReliablePosition = true;
                homeDoneInvalidated = false; // home re-validé après homing réussi

                logPrintf("✅ HOMING TERMINÉ - Position finale stabilisée: %lu PUU\n", reliablePosition);
                logPrintf("📊 Temps total homing: %lu ms\n", totalElapsed);
                logPrintln("🎯 Position fiable établie après index");
                logPrintln("🏠 Homing avec détection d'index terminé avec succès");
                
                homingState = OperationState::DRIVE_IDLE;
                homingPhase = 0;
                
                setError(""); // Effacer toute erreur précédente
#ifdef DEBUG_SURESERVO                        
                notifyUser(SUCCESS, "Homing terminé avec succès !!!");
#endif
                logPrintf("[32m%lu:: SUCCESS: Homing terminé avec succès !!! Position: %lu PUU[0m\n", 
                            now, reliablePosition);
                return;
            }
            
            // Continuer à attendre la stabilisation...
            if (now - logThrottle.homingStabilization > 1000) {
                logPrintf("🔄 Attente stabilisation... Position: %ld PUU (int32)\n", 
                             (long)currentPosition_signed);
                logThrottle.homingStabilization = now;
            }
            break;
        }
        
        default: {
            logPrintf("❌ Phase homing invalide: %d\n", homingPhase);
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
    logPrintln("=== SURESERVO HOMING STATUS ===");
    logPrintf("Homing State: %d (%s)\n", 
                 static_cast<int>(homingState),
                 homingState == OperationState::DRIVE_IDLE ? "IDLE" :
                 homingState == OperationState::DRIVE_IN_PROGRESS ? "IN_PROGRESS" :
                 homingState == OperationState::DRIVE_FAILED ? "FAILED" :
                 homingState == OperationState::DRIVE_TIMEOUT ? "TIMEOUT" : "UNKNOWN");
    logPrintf("Homing Phase: %d\n", homingPhase);
    logPrintf("Home Done: %s\n", getHomeDone() ? "true" : "false");
    logPrintf("Is Ready: %s\n", getIsReady() ? "true" : "false");
    logPrintf("Has Alarms: %s\n", getHasAlarms() ? "true" : "false");
    if (getHasAlarms()) {
        logPrintf("Alarm Code: 0x%04X (%s)\n", getAlarmCode(), getAlarmDescription());
    }
    logPrintf("Position: %lu PUU (%.2f\")\n", 
                 getPosition(), (float)getPosition() / pulsesPerUnit);
    
    if (!lastError.isEmpty()) {
        logPrintf("Last Error: %s\n", lastError.c_str());
    }
    
    logPrintln("==============================");
}

bool SureServo::isAtPosition(int32_t targetPuu) const {
    // targetPuu est déjà signé (contrat ServoDrive) — plus besoin de cast local
    int32_t current_signed = nodes.position->getValueAsInt32();
    int32_t diff = current_signed - targetPuu;
    uint32_t difference = (diff >= 0) ? diff : -diff;  // Valeur absolue

    bool atPosition = difference <= TOLERANCE_PUU;
    
    if (atPosition) {
        logPrintf("✅ SureServo: Position atteinte - Écart: %lu PUU (tolérance: %lu)\n",
                     (unsigned long)difference, (unsigned long)TOLERANCE_PUU);
    }
    
    return atPosition;
}

void SureServo::sendTargetToPR(int32_t targetPuu) {
    logPrintf("📤 SureServo: Envoi target %ld PUU au PR%d\n",
                 (long)targetPuu, currentPr);

    switch (currentPr) {
        case 1:
            // ✅ targetA est un node OBLIGATOIRE - pas de validation nécessaire
            nodes.targetA->setValueFromInt32(targetPuu);
            logPrintf("✅ Target A défini: %ld PUU\n", (long)targetPuu);
            break;

        case 2:
            // ✅ targetB est un node OBLIGATOIRE - pas de validation nécessaire
            nodes.targetB->setValueFromInt32(targetPuu);
            logPrintf("✅ Target B défini: %ld PUU\n", (long)targetPuu);
            break;

        case 3:
            // ✅ targetC est un node OBLIGATOIRE - pas de validation nécessaire
            nodes.targetC->setValueFromInt32(targetPuu);
            logPrintf("✅ Target C défini: %ld PUU\n", (long)targetPuu);
            break;
            
        default:
            logPrintf("❌ PR invalide: %d\n", currentPr);
            setError("PR invalide: " + String(currentPr));
            break;
    }
#ifdef SURESERVO_PERF_MONITOR
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
    logPrintf("❌ SureServo Error: %s\n", enhancedError.c_str());
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

const AlarmInfo* SureServo::findAlarmInfo(uint16_t alarmCode) const {
    for (size_t i = 0; i < ALARMS_COUNT; i++) {
        if (ALARMS[i].code == alarmCode) {
            return &ALARMS[i];
        }
    }
    return nullptr;
}

void SureServo::logStatusChange(const char* context) const {
    if (context) {
        logPrintf("SureServo Status Change [%s]: Ready=%s, Alarm=%s\n", 
                     context,
                     getIsReady() ? "true" : "false",
                     getHasAlarms() ? "true" : "false");
    }
}

void SureServo::logAlarmChange(const char* context) const {
    if (getHasAlarms()) {
        Serial.printf("\n");
        logPrintf("========================================\n");
        logPrintf("🚨 ALARME SERVO DÉTECTÉE [%s]\n", context ? context : "Unknown");
        logPrintf("========================================\n");
        logPrintf("Code:        0x%04X (%u)\n", getAlarmCode(), getAlarmCode());
        logPrintf("Description: %s\n", getAlarmDescription());
        logPrintf("Action:      %s\n", getAlarmAction());
        logPrintf("========================================\n");
        
        // Message utilisateur aussi
        char txt[512];
        snprintf(txt, sizeof(txt), "DRIVE: %s - %s", getAlarmDescription(),  getAlarmAction());
        logPrintln(txt);      
    } else {
        // Alarme effacée
        logPrintf("✅ Alarme servo effacée [%s]\n", context ? context : "Unknown");
    }
}

void SureServo::logOperationStateChange(const char* operation, 
                                       ServoDrive::OperationState oldState, 
                                       ServoDrive::OperationState newState) const {
    logPrintf("SureServo %s: %s -> %s\n", operation, ServoDrive::getOperationStateText(oldState), ServoDrive::getOperationStateText(newState));
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
    //   getAlarmDescription() cherche alarmsRegister dans sureservo::ALARMS[]
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
        logPrintf("EEPROM: Auto-save désactivé (valeur: %u)\n", EEPROM_AUTO_SAVE_DISABLED);
    } else {
        logPrintln("ERREUR: Node auxFunctions non disponible pour config EEPROM");
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
    
    logPrintf("EEPROM mode configuré: %d (registre: %u)\n", 
                 static_cast<int>(mode), registerValue);
    
    return true;
}

bool SureServo::triggerManualEEPROMSave() {
    uint32_t now = millis();
    
    // Éviter les sauvegardes trop fréquentes
    if (eepromSaveInProgress || (now - lastEEPROMSaveTime) < EEPROM_SAVE_DELAY_MS) {
        logPrintln("EEPROM: Sauvegarde déjà en cours ou trop récente");
        return false;
    }
    
    // Déclencher la sauvegarde
    nodes.auxFunctions->setValue(EEPROM_MANUAL_SAVE_TRIGGER);
    
    eepromSaveInProgress = true;
    lastEEPROMSaveTime = now;
    
    logPrintln("EEPROM: Sauvegarde manuelle déclenchée");
    
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
    logPrintf("🔄 SureServo::clearError() - États avant: move=%d, init=%d, reset=%d\n",
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
    
    logPrintln("✅ SureServo: Erreurs effacées");
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
        logPrintln("🚨 SureServo: REDÉMARRAGE DRIVE DÉTECTÉ !");
        logPrintf("   Position actuelle: %lu PUU\n", getPosition());
        logPrintln("   - Perte de l'état HomeDone");
        logPrintln("   - Drive probablement redémarré");

        // ✅ MESSAGE UTILISATEUR
        notifyUser(ERROR,
            "Drive servo redémarré - Position home perdue - Refaire le homing");

        notifyBusinessLogicFault("Drive redémarré - État home perdu");

        // Si homing en cours, le marquer comme échoué
        if (homingState == OperationState::DRIVE_IN_PROGRESS) {
            logPrintln("   - Interruption du homing en cours");
            homingState = OperationState::DRIVE_FAILED;
        }

        // Si mouvement en cours, le marquer comme échoué aussi
        if (moveState == OperationState::DRIVE_IN_PROGRESS) {
            logPrintln("   - Interruption du mouvement en cours");
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

    logPrintf("✅ Position interne fiable: %ld PUU\n", (long)reliablePosition);
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
    logPrintf("🏠 Homing - Current: %ld PUU, Previous: %ld PUU, Écart: %lu PUU, Lectures stables: %d/%d\n",
                 (long)current_signed, (long)previous_signed, 
                 (unsigned long)positionDifference, homingStableReadingsCount, 3);
    
    if (positionDifference <= TOLERANCE_PUU) {
        // Position stable - incrémenter le compteur
        homingStableReadingsCount++;
        logPrintf("✅ Position homing stable (%d/%d lectures)\n", homingStableReadingsCount, 3);
    } else {
        // Position bouge encore (déplacement vers index) - remettre le compteur à zéro
        homingStableReadingsCount = 0;
        logPrintf("🔄 Déplacement vers index en cours (écart: %lu PUU > tolérance: %lu PUU)\n", 
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
        logPrintln("Position interne invalidée (JOG détecté)");
        
        // Arrêter toute re-synchronisation en cours
        resyncInProgress = false;
        idleStableReadingsCount = 0;
    }
    
    // NOUVEAU: Marquer qu'une re-sync sera nécessaire après JOG
    // mais seulement si le servo est en bon état
    if (getIsReady() && !getHasAlarms()) {
        //logPrintln("Re-sync post-JOG programmée");
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
    if (now - logThrottle.idleResync > 5000) { // Log seulement toutes les 5 secondes
        logPrintf("🔄 Re-sync: Position: %ld PUU, Écart: %lu, Stables: %d/5\n",
                     (long)current_signed, positionDifference, idleStableReadingsCount);
        logThrottle.idleResync = now;
    }
    
    if (positionDifference <= TOLERANCE_PUU) {
        // Position stable - incrémenter le compteur
        idleStableReadingsCount++;
    } else {
        // Position bouge - moteur en mouvement, arrêter la re-sync
        endPositionResync(false);
        logPrintln("🔄 Re-sync interrompue (mouvement détecté)");
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

    logPrintf("Début re-sync position: %lu PUU (refresh rapide activé)\n",
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

    bool statusChanged = (hasReliablePosition != resyncLastHadReliablePosition);
    bool hasProblem = !hasReliablePosition;
    
    if (statusChanged || (hasProblem && now - logThrottle.resyncConditions > 10000)) {
        if (statusChanged) {
            logPrintf("🔄 Re-sync status change: %s → %s\n",
                         resyncLastHadReliablePosition ? "FIABLE" : "NON-FIABLE",
                         hasReliablePosition ? "FIABLE" : "NON-FIABLE");
        }
        
        if (hasProblem) {
            logPrintf("🔍 Re-sync conditions - zeroSpeed:%s, homeDone:%s, ready:%s, resyncInProgress:%s\n", 
                        status.zeroSpeed ? "OUI" : "NON", 
                        getHomeDone() ? "OUI" : "NON", 
                        getIsReady() ? "OUI" : "NON",
                        resyncInProgress ? "OUI" : "NON");
        }
        
        logThrottle.resyncConditions = now;
        resyncLastHadReliablePosition = hasReliablePosition;
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
            logPrintln("🔄 Re-sync interrompue (opération en cours)");
        }
        return;
    }
    
    // Ne pas re-sync si servo pas prêt ou en alarme (existant)
    if (!getIsReady() || getHasAlarms()) {
        if (resyncInProgress) {
            endPositionResync(false);
            logPrintln("🔄 Re-sync interrompue (servo pas prêt ou alarme)");
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
                logPrintf("🚨 Position négative détectée au début re-sync: %ld PUU\n", (long)currentPositionSigned);
                currentPosition = 0;
                logPrintln("🔧 CORRECTION: Utilisation de 0 pour la re-sync");
                
                // Valider immédiatement avec 0
                reliablePosition = 0;
                hasReliablePosition = true;
                
                logPrintln("✅ Position corrigée et validée immédiatement à 0");
                notifyUser(WARNING, "Position négative corrigée à 0");
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
            logPrintf("🚨 RE-SYNC: Position légèrement négative détectée: %ld PUU\n", (long)finalPosition);
            logPrintln("🔧 CORRECTION: Position forcée à 0 pour sécurité");
            finalPosition = 0;

            notifyUser(WARNING,
                "Re-sync: position corrigée - valeur négative détectée");
        }

        // ✅ Position stable - RE-SYNCHRONISATION RÉUSSIE avec valeur validée !
        reliablePosition = finalPosition;  // Utiliser la position corrigée
        hasReliablePosition = true;
        endPositionResync(true);

        logPrintf("✅ Re-sync réussie ! Position interne: %ld PUU\n", (long)reliablePosition);
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
    logPrintf("🎯 Move - Position: %ld PUU, Écart: %lu PUU, Lectures stables: %d/3\n",
                 (long)current_signed, positionDifference, moveStableReadingsCount);
    
    if (positionDifference <= TOLERANCE_PUU) {
        moveStableReadingsCount++;
        logPrintf("✅ Position stable (%d/3 lectures)\n", moveStableReadingsCount);
    } else {
        moveStableReadingsCount = 0;
        logPrintf("🔄 Mouvement en cours (écart: %lu PUU)\n", positionDifference);
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
            logPrintf("✅ Initialisation terminée avec succès\n");
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
                logPrintf("❌ %s\n", errorMsg);
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
            logPrintf("✅ Homing terminé\n");
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
            logPrintf("❌ %s\n", errorMsg);
            status = ServoDrive::OperationStatus::SERVO_DRIVE_ERROR;
            return true;
        }
            
        case OperationState::DRIVE_IN_PROGRESS: {
            uint32_t now = millis();
            if (now - logThrottle.handleHomingProgress > 5000) {
                uint32_t elapsed = (now - operationStartTime) / 1000;
                char progressMsg[200];
                snprintf(progressMsg, sizeof(progressMsg), 
                    "[%s] HOMING en cours... (%lu sec) [Position: %lu PUU]",
                    servoId.c_str(), (unsigned long)elapsed, (unsigned long)getPosition());
#ifdef DEBUG_SURESERVO
                BorneUniverselle::prepareMessage(INFO, progressMsg);
#endif
                logThrottle.handleHomingProgress = now;
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
        logPrintf("🚨 %s\n", msg);
        
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
        uint32_t now = millis();
        
        if (now - logThrottle.handleMoveProgress > 1000) {
            logPrintf("🔄 Mouvement en cours vers %ld PUU...\n",
                         (long)targetPosition);
            logThrottle.handleMoveProgress = now;
        }
        
        status = ServoDrive::OperationStatus::IN_PROGRESS;
        return true;
    }
    
    // CAS 2 : Mouvement terminé (DRIVE_IDLE) - vérifier si on doit attendre sync
    if (moveState == OperationState::DRIVE_IDLE && targetPosition == pos) {
        // ✅ On a déjà fait ce mouvement
        
        // Si waitForSync demandé ET pas encore synchronisé
        if (waitForSyncRequested && !getHasReliablePosition()) {  // ✅ CORRIGÉ: waitForSyncRequested au lieu de waitForSync
            logPrintf("⏳ Attente re-synchronisation position...\n");
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

uint16_t SureServo::getSlaveAddress() const {
    ModbusNode* modbusNode = dynamic_cast<ModbusNode*>(nodes.status);
    if (modbusNode != nullptr) {
        return modbusNode->getModbusAddress();
    }
    // Pas un node Modbus (ex: mode debug avec VirtualNode) - pas de filtrage
    logPrintf("getSlaveAddress: nodes.status n'est pas un ModbusNode\n");
    return 0;
}

bool SureServo::stopMovement() {
    logPrintln("🛑 SureServo::stopMovement() - Arrêt propre demandé");
    
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
        logPrintf("   → Mouvement annulé (phase %d)\n", movePhase);
        moveState = OperationState::DRIVE_IDLE;  // ✅ Retour à IDLE (pas FAILED)
        movePhase = 0;
        targetPosition = 0;
        releaseFastStatusPolling();
    }
    
    // Annuler le homing en cours
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        logPrintln("   → Homing annulé");
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
    
    logPrintln("   → Flag movementCancelled activé (blocage redémarrage)");
    
    // ========================================
    // 5. PROGRAMMATION DU RELÂCHEMENT DIFFÉRÉ
    // ========================================
    
    // Le relais a besoin de 500ms pour coller/décoller de façon fiable
    pendingReleaseImmediateStop = true;
    
    logPrintf("✅ SureServo: Arrêt propre demandé - immediateStop actif pendant %lu ms\n", 
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
        logPrintln("   → moveState réinitialisé (clearMovementLock)");
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
        logPrintln("   → Init réinitialisée (clearMovementLock) - relance de l'initialisation");
        initState = OperationState::DRIVE_IDLE;
        initPhase = 0;
        driveInitStarted = false;
        initFailureReported = false;
        if (!startInitialize()) {
            logPrintf("   ⚠️ Échec relance initialisation: %s\n", lastError.c_str());
        }
    }

    // Remettre homingState à IDLE si bloqué en FAILED/TIMEOUT
    if (homingState != OperationState::DRIVE_IDLE) {
        logPrintln("   → Homing réinitialisé (clearMovementLock)");
        homingState = OperationState::DRIVE_IDLE;
        driveHomingStarted = false;
    }

    targetPosition = 0;
}

// ========================================
// PERF MONITOR
// ========================================
// Méthodes toujours compilées (API stable pour RessortRoyal2 / main.cpp / code généré) ;
// la collecte dans process()/processMove()/sendTargetToPR() dépend de SURESERVO_PERF_MONITOR.

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

    Serial.print("\r\n");
    logPrintln("╔══════════════════════════════════════════════════╗");
    logPrintln("║   PERF MONITOR — Cycle complet                   ║");
#ifdef SURESERVO_TRIGGER_SMART_DELAY
    logPrintln("║   Mode: SMART DELAY                              ║");
#else
    logPrintf("║   Mode: FIXED DELAY (%3lums)                      ║\r\n", (uint32_t)TRIGGER_DELAY_MS);
#endif
    logPrintln("╠══════════════════════════════════════════════════╣");
    logPrintf("║ Durée totale cycle   : %5lu ms  (100%%)          ║\r\n", cycleTotalMs);
    logPrintf("║ refresh() cumulé     : %5lu ms  (%3lu%%)          ║\r\n", pm_refreshCumul, pctRef);
    logPrintf("║ process() cumulé     : %5lu ms  (%3lu%%)          ║\r\n", procMs, pctProc);
    logPrintf("║ process() pic        : %5lu µs                   ║\r\n", pm_processTimeMax);
    logPrintf("║ process() appels     : %5lu                      ║\r\n", pm_processCallCount);
    logPrintln("╠══════════════════════════════════════════════════╣");
    logPrintf("║ Mouvements complétés : %5lu                      ║\r\n", pm_moveCount);
    logPrintf("║ Inter-mvt cumulé     : %5lu ms  (%3lu%%)          ║\r\n", pm_interMoveCumul, pctInterMvt);
    logPrintf("║ Inter-mvt pic        : %5lu ms                   ║\r\n", pm_interMoveMax);
    logPrintln("╠══════════════════════════════════════════════════╣");
    logPrintf("║ Targets envoyés      : %5lu                      ║\r\n", pm_writeTargetCount);
    logPrintf("║ Target→Trigger cumulé: %5lu ms  (%3lu%%)          ║\r\n", pm_targetToTrigCumul, pctTTrig);
    logPrintf("║ Target→Trigger pic   : %5lu ms                   ║\r\n", pm_targetToTrigMax);
    logPrintf("║ Triggers envoyés     : %5lu                      ║\r\n", pm_writeTriggerCount);
    logPrintln("╠══════════════════════════════════════════════════╣");
    logPrintf("║ T1 (latence) cumulé  : %5lu ms  (%3lu%%)          ║\r\n", pm_driveLatencyCumul, pctT1);
    logPrintf("║ T1 (latence) pic     : %5lu ms                   ║\r\n", pm_driveLatencyMax);
    logPrintf("║ T2 (mvt) cumulé      : %5lu ms  (%3lu%%)          ║\r\n", pm_moveTimeCumul, pctT2);
    logPrintf("║ T2 (mvt) pic         : %5lu ms                   ║\r\n", pm_moveTimeMax);
#ifdef SURESERVO_TRIGGER_SMART_DELAY
    logPrintln("╠══════════════════════════════════════════════════╣");
    logPrintf("║ Smart delay confirmé : %5lu fois               ║\r\n", pm_smartDelayCount);
    logPrintf("║ Safety timeout       : %5lu fois               ║\r\n", pm_safetyTimeoutCount);
#endif
    logPrintln("╚══════════════════════════════════════════════════╝");
    logPrintln("T1=trigger→signalValidated  T2=signalValidated→reached&&zeroSpeed");
    pm_active = false;
}

void SureServo::pmAccumulateRefresh(uint32_t ms) {
    pm_refreshCumul += ms;
}

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

    logPrintf("✅ configurePR: PR%d target=%ld SPD_IDX=%u chain=%s config=0x%08lX\n",
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

    logPrintln("✅ clearPRConfig: tous les PR restaurés");
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

    logPrintf("✅ clearPRConfig: PR%d restauré config=0x%08lX (SPD_IDX=%u)\n", prNumber, config, currentPrSpeed);
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
        logPrintln("❌ setHomeAtCurrentPosition: refusé - moteur en mouvement");
        notifyUser(WARNING, "setHomeAtCurrentPosition: moteur en mouvement");
        homeAtCurrentPositionInProgress = false;   // refus avant écriture → reset immédiat
        return false;
    }

    // Guard 2 : drive occupé
    if (initState   != OperationState::DRIVE_IDLE ||
        resetState  != OperationState::DRIVE_IDLE ||
        moveState   != OperationState::DRIVE_IDLE ||
        homingState != OperationState::DRIVE_IDLE) {
        logPrintln("❌ setHomeAtCurrentPosition: refusé - drive occupé");
        notifyUser(WARNING, "setHomeAtCurrentPosition: drive occupé");
        homeAtCurrentPositionInProgress = false;   // refus avant écriture → reset immédiat
        return false;
    }

    // Guard 3 : nodes disponibles
    if (!nodes.homingModeRead || !nodes.homingModeWrite || !nodes.trigger) {
        logPrintln("❌ setHomeAtCurrentPosition: refusé - nodes manquants");
        notifyUser(ERROR, "setHomeAtCurrentPosition: nodes manquants");
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
            logPrintln("✍️  homeAtCurrentPosition phase 1: mode 0x08 écrit");
            homeAtCurrentPositionPhase = 2;
            break;

        case 2:
            // Mode 0x08 est maintenant sur le drive — envoyer trigger PR0
            nodes.trigger->setValue(0, true);   // force write
            logPrintln("🏠 homeAtCurrentPosition phase 2: trigger PR0 envoyé");
            homeAtCurrentPositionPhase = 3;
            break;

        case 3:
            // Trigger exécuté — restaurer le mode homing original
            nodes.homingModeWrite->setValue(homeAtCurrentPositionSavedMode);
            logPrintf("↩️  homeAtCurrentPosition phase 3: mode restauré = 0x%04X\n",
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
            logPrintln("✅ homeAtCurrentPosition: position remise à 0, home virtuel confirmé");
            break;
    }
}

 void SureServo::setChainedPRMode(bool enabled) {
        chainedPRMode = enabled;
        if (enabled){
         currentPr = 1; 
        }
    }
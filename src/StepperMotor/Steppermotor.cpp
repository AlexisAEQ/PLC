/**
 * @file StepperMotor.cpp
 * @brief Implémentation du driver StepperMotor
 * 
 * Wrapper FastAccelStepper intégré dans l'architecture BorneUniverselle.
 * Modèle identique à SureServo pour cohérence du code.
 * 
 * @author Thierry
 * @date 2025
 */

#include "ServoDrive/ServoDrive.h"
#include "StepperMotor/StepperMotor.h"
#include "BorneUniverselle/BorneUniverselle.h"

// ========================================
// CONSTRUCTEUR
// ========================================

StepperMotor::StepperMotor(uint32_t stepsPerUnit,
                           const StepperNodes& nodes,
                           uint32_t maxRangeSteps,
                           const char* servoId)
    : ServoDrive(servoId),
      stepsPerUnit(stepsPerUnit),        // const - ligne 232
      maxRangeSteps(maxRangeSteps),      // const - ligne 233
      nodes(nodes),                      // const - ligne 234
      // engine - constructeur par défaut - ligne 235
      fasMotor(nullptr),                 // ✅ ligne 236
      stepPinNumber(0),                  // ✅ ligne 238
      dirPinNumber(0),                   // ✅ ligne 239
      enablePinNumber(0),                // ✅ ligne 240
      motorId(servoId),                  // ✅ ligne 242
      initState(OperationState::DRIVE_IDLE),   
      resetState(OperationState::DRIVE_IDLE),   
      moveState(OperationState::DRIVE_IDLE),   
      homingState(OperationState::DRIVE_IDLE), 
      initPhase(0),                      // ✅ ligne 249
      resetPhase(0),                     // ✅ ligne 250
      movePhase(0),                      // ✅ ligne 251
      homingPhase(0),                    // ✅ ligne 252
      operationStartTime(0),             // ✅ ligne 254
      phaseStartTime(0),                 // ✅ ligne 255
      initTimeoutMs(15000),              // ✅ ligne 302 - 15 secondes - DÉPLACÉ ICI
      resetTimeoutMs(5000),              // ✅ ligne 303 - 5 secondes - DÉPLACÉ ICI
      moveTimeoutMs(60000),              // ✅ ligne 304 - 60 secondes - DÉPLACÉ ICI
      homingTimeoutMs(30000),            // ✅ ligne 305 - 30 secondes - DÉPLACÉ ICI
      targetPositionInternal(0),         // ✅ ligne 307
      currentJogSpeed(1000),             // ✅ ligne 308
      accelerationEnabled(false),        // ✅ ligne 310
      initialized(false),                // ✅ ligne 311
      homingDone(false),                 // ✅ ligne 312
      lastError("")                      // ✅ ligne 315
  {
    Serial.printf("\n========================================\n");
    Serial.printf("INITIALISATION STEPPERMOTOR [%s]\n", motorId.c_str());
    Serial.printf("========================================\n");
    Serial.printf("\n========================================\n");
    Serial.printf("INITIALISATION STEPPERMOTOR [%s]\n", motorId.c_str());
    Serial.printf("========================================\n");
    
    // Afficher maxRangeSteps si spécifié
    if (maxRangeSteps > 0) {
        Serial.printf("Limite physique: %lu steps\n", maxRangeSteps);
    }
    
    // ========================================
    // 1. VALIDATION DES NODES OBLIGATOIRES
    // ========================================
    
    if (!nodes.validateRequired()) {
        String errorMsg = "StepperMotor: Nodes obligatoires manquants - ";
        
        // Diagnostic détaillé (comme SureServo)
        if (!nodes.servoOn) errorMsg += "servoOn ";
        if (!nodes.immediateStop) errorMsg += "immediateStop ";
        if (!nodes.alarmsReset) errorMsg += "alarmsReset ";
        if (!nodes.stepPin) errorMsg += "stepPin ";
        if (!nodes.dirPin) errorMsg += "dirPin ";
        if (!nodes.enable) errorMsg += "enablePin ";
        if (!nodes.position) errorMsg += "position ";
        if (!nodes.targetPosition) errorMsg += "targetPosition ";
        if (!nodes.targetPositionReached) errorMsg += "targetPositionReached ";
        if (!nodes.maxSpeed) errorMsg += "maxSpeed ";
        if (!nodes.jogSpeed) errorMsg += "jogSpeed ";
        
        Serial.println(errorMsg);
        setError(errorMsg.c_str());
        return; // Construction échouée
    }
    
    Serial.println("✅ Tous les nodes obligatoires présents");
    Serial.printf("   Nodes optionnels: %zu\n", nodes.countOptional());
    
    // ========================================
    // 2. VALIDATION DES PARAMÈTRES
    // ========================================
    
    if (stepsPerUnit == 0) {
        setError("StepperMotor: stepsPerUnit ne peut pas être zéro");
        return;
    }
    
    Serial.printf("Steps per unit: %lu\n", stepsPerUnit);
    
    // ========================================
    // 3. INITIALISATION DES NODES DE CONTRÔLE
    // ========================================
    
    // Comme SureServo ligne 153-156
    nodes.servoOn->setValue(false);
    nodes.immediateStop->setValue(false);
    nodes.alarmsReset->setValue(false);
    
    Serial.println("✅ Nodes de contrôle initialisés");
    
    // ========================================
    // 4. EXTRACTION DES NUMÉROS DE PINS
    // ========================================
    
    Serial.println("\n--- Extraction des pins GPIO ---");
    
    if (!extractPinNumber(nodes.stepPin, "STEP", stepPinNumber) ||
        !extractPinNumber(nodes.dirPin, "DIR", dirPinNumber) ) {
        setError("Échec extraction des pins GPIO");
        return;
    }

    if (nodes.enable) {
        if (nodes.enable->classType() == CLASS_HW_BOOLEAN_OUTPUT_NODE) {
            // Hardware : récupérer GPIO
            HardwareBooleanOutputNode* hwEnable = (HardwareBooleanOutputNode*)nodes.enable;
            enablePinNumber = hwEnable->getPinNumber();
        } else {
            // Software : pas de GPIO
            enablePinNumber = 0xFF;
        }
    }
    
    Serial.printf("✅ Pins extraites :\n");
    Serial.printf("   STEP   = GPIO %d\n", stepPinNumber);
    Serial.printf("   DIR    = GPIO %d\n", dirPinNumber);
    
    // ========================================
    // 5. INITIALISATION FASTACCELSTEPPER
    // ========================================
    
    Serial.println("\n--- Initialisation FastAccelStepper ---");
    
    engine.init();
    Serial.println("✅ FastAccelStepperEngine initialisé");
    
    fasMotor = engine.stepperConnectToPin(stepPinNumber);
    
    if (!fasMotor) {
        setError("Échec connexion FastAccelStepper");
        Serial.printf("❌ Impossible de connecter au GPIO %d\n", stepPinNumber);
        return;
    }
    
    Serial.printf("✅ Stepper connecté au GPIO %d\n", stepPinNumber);
    
    // Configuration des pins
     // Configuration DIR (toujours nécessaire)
    fasMotor->setDirectionPin(dirPinNumber);
    
    // Configuration ENABLE (conditionnel selon le type de node)
    if (enablePinNumber != 0xFF) {
        // ✅ CAS 1: Hardware enable - FastAccelStepper gère le GPIO
        fasMotor->setEnablePin(enablePinNumber);
        fasMotor->setAutoEnable(true);
        Serial.printf("✅ DIR = GPIO %d, ENABLE = GPIO %d (FastAccelStepper)\n", 
                      dirPinNumber, enablePinNumber);
    } else {
        // ✅ CAS 2: Software enable - PLC gère en externe
        // Ne pas configurer enable dans FastAccelStepper
        Serial.printf("✅ DIR = GPIO %d, ENABLE = externe (géré par PLC)\n", 
                      dirPinNumber);
    }
    
    Serial.printf("✅ DIR = GPIO %d, ENABLE = GPIO %d\n", dirPinNumber, enablePinNumber);
    
    // ========================================
    // 6. CONFIGURATION PAR DÉFAUT
    // ========================================
    
    Serial.println("\n--- Configuration initiale ---");
    
    uint32_t defaultMaxSpeed = 10000;
    uint32_t defaultAccel = 5000;
    
    // Utiliser les valeurs des nodes si disponibles
    if (nodes.maxSpeed && nodes.maxSpeed->getValue() > 0) {
        defaultMaxSpeed = nodes.maxSpeed->getValue();
    }
    
    if (nodes.acceleration && nodes.acceleration->getValue() > 0) {
        defaultAccel = nodes.acceleration->getValue();
    }
    
    fasMotor->setSpeedInHz(defaultMaxSpeed);
    fasMotor->setAcceleration(defaultAccel);
    accelerationEnabled = true;
    
    Serial.printf("✅ Vitesse max: %lu steps/sec\n", defaultMaxSpeed);
    Serial.printf("✅ Accélération: %lu steps/sec²\n", defaultAccel);
    
    // ========================================
    // 7. INITIALISATION DES NODES DE STATUS
    // ========================================
    
    nodes.targetPositionReached->setValue(false);
    
    if (nodes.stepperReady) {
        nodes.stepperReady->setValue(false);
    }
    
    if (nodes.stepperActivated) {
        nodes.stepperActivated->setValue(false);
    }
    
    if (nodes.homeDone) {
        nodes.homeDone->setValue(false);
    }
    
    status.reset();

    Serial.println("\n========================================");
    Serial.printf("[%s] Initialisation terminée\n", motorId.c_str());
    Serial.println("========================================\n");
}

// ========================================
// DESTRUCTEUR
// ========================================

StepperMotor::~StepperMotor() {
    Serial.printf("[%s] Destruction StepperMotor\n", motorId.c_str());
    
    if (fasMotor) {
        fasMotor->forceStopAndNewPosition(0);
    }
}

// ========================================
// PROCESS - MÉTHODE PRINCIPALE
// ========================================

void StepperMotor::process() {
    uint32_t now = millis();
    
    // ========================================
    // GESTION SERVOON (comme SureServo)
    // ========================================
    
    static bool wasServoOn = false;
    bool currentServoOn = nodes.servoOn->getValue();
    
    if (!wasServoOn && currentServoOn) {
        // ServoOn activé
        Serial.println("ServoOn activé");
        if (fasMotor) {
            fasMotor->enableOutputs();
        }
        if (nodes.stepperActivated) {
            nodes.stepperActivated->setValue(true);
        }
    } else if (wasServoOn && !currentServoOn) {
        // ServoOn désactivé
        Serial.println("⚠️ ServoOn désactivé");
        if (fasMotor) {
            fasMotor->disableOutputs();
            fasMotor->forceStop();
        }
        if (nodes.stepperActivated) {
            nodes.stepperActivated->setValue(false);
        }
    }
    
    wasServoOn = currentServoOn;
    
    // ========================================
    // GESTION IMMEDIATE STOP
    // ========================================
    
    if (nodes.immediateStop->getValue()) {
        Serial.println("⚠️ ImmediateStop activé");
        emergencyStop();
        // Remettre à false après traitement
        nodes.immediateStop->setValue(false);
    }
    
    // ========================================
    // GESTION ALARMS RESET
    // ========================================
    
    if (nodes.alarmsReset->getValue()) {
        Serial.println("🔧 AlarmsReset activé");
        
        // ✅ Réinitialiser les états d'opération (sources des alarmes)
        // Les nodes seront mis à jour automatiquement via updateStatusNodes()
        moveState = OperationState::DRIVE_IDLE;
        homingState = OperationState::DRIVE_IDLE;
        resetState = OperationState::DRIVE_IDLE;
        
        if (nodes.limitError) {
            nodes.limitError->setValue(false);
        }
        
        lastError = "";
        nodes.alarmsReset->setValue(false);
        
        Serial.println("✅ Alarmes réinitialisées");
    }
    
    // ========================================
    // MISE À JOUR POSITION DEPUIS FASTACCELSTEPPER
    // ========================================
    
    // Mettre à jour la structure de statut interne
    status.updateFromMotor(fasMotor, *this);
    
    updateStatusNodes();

    // ========================================
// SURVEILLANCE ALARME MATÉRIELLE DU DRIVER (ÉTAPE 3)
// ========================================

if (nodes.hardwareStepperAlarm) {
    static bool wasHardwareAlarm = false;
    bool currentHardwareAlarm = nodes.hardwareStepperAlarm->getValue();
    
    if (!wasHardwareAlarm && currentHardwareAlarm) {
        // Détection du passage à true → DÉCLENCHER L'ALARME
        Serial.printf("[%s] 🚨 ALARME MATÉRIELLE DRIVER DÉTECTÉE!\n", motorId.c_str());
        
        // Arrêt immédiat du moteur pour sécurité
        if (fasMotor) {
            fasMotor->forceStop();
            Serial.printf("[%s] Moteur arrêté (forceStop)\n", motorId.c_str());
        }
        
        // Déclencher l'alarme (setError met automatiquement stepperAlarm à true)
        setError("Alarme matérielle driver détectée");
        
        // Passer les états en échec
        if (moveState == OperationState::DRIVE_IN_PROGRESS) {
            moveState = OperationState::DRIVE_FAILED;
        }
        
        if (homingState == OperationState::DRIVE_IN_PROGRESS) {
            homingState = OperationState::DRIVE_FAILED;
        }
        
        Serial.printf("[%s] ⚠️  ALARME ACTIVE - Acquittement manuel requis (alarmsReset)\n", 
                     motorId.c_str());
    } else if (wasHardwareAlarm && !currentHardwareAlarm) {
        // Le node repasse à false, mais on ne lève PAS l'alarme automatiquement
        // L'alarme reste verrouillée jusqu'à acquittement manuel via alarmsReset
        Serial.printf("[%s] ℹ️  Signal alarme matérielle désactivé (hardware OK)\n", 
                     motorId.c_str());
        Serial.printf("[%s] Note: L'alarme reste active jusqu'à acquittement manuel\n", 
                     motorId.c_str());
    }
    
    wasHardwareAlarm = currentHardwareAlarm;
    
    // Message utilisateur répétitif tant que l'alarme matérielle est active
    PLC_Tools::sendPeriodicMessage(
        currentHardwareAlarm, 
        ERROR, 
        "Alarme matérielle driver détectée - Réinitialisez les défauts"
    );
}
    
    // ========================================
    // SYNCHRONISATION PARAMÈTRES DEPUIS NODES
    // ========================================
    
    syncParametersFromNodes();
    
    // ========================================
    // VÉRIFICATION LIMIT SWITCHES
    // ========================================
    
    checkAndHandleLimitSwitches();
    
    // ========================================
    // TRAITEMENT DES MACHINES À ÉTATS
    // ========================================
    
    if (initState == OperationState::DRIVE_IN_PROGRESS) {
        processInitialize(now);
    }
    
    if (resetState == OperationState::DRIVE_IN_PROGRESS) {
        processReset(now);
    }
    
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        processMove(now);
    }
    
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        processHoming(now);
    }

    if (millis() - now > 100){
        Serial.printf("[%s] WARNING: Process stepper motor took %lu ms\n", motorId.c_str(), millis() - now);
    }
    
}

// ========================================
// PROCESS INITIALIZE
// ========================================

void StepperMotor::processInitialize(uint32_t now) {
    // ========================================
    // VÉRIFICATION TIMEOUT GLOBAL
    // ========================================
    
    uint32_t elapsed = now - operationStartTime;
    
    if (elapsed > initTimeoutMs) {
        Serial.printf("[%s] [INIT] ❌ Timeout après %lu ms (max: %lu ms)\n", 
                     motorId.c_str(), elapsed, initTimeoutMs);
        setError("Init timeout");
        initState = OperationState::DRIVE_TIMEOUT;
        initPhase = 0;
        return;  // ← IMPORTANT : Sortir immédiatement
    }
    
    // ========================================
    // MACHINE À ÉTATS
    // ========================================
    
    switch (initPhase) {
        // ====================================
        // PHASE 0 : ACTIVATION DU STEPPER
        // ====================================
        case 0: {
            Serial.printf("[%s] [INIT] Phase 0: Activation du stepper\n", motorId.c_str());
            
            if (!fasMotor) {
                setError("FastAccelStepper non initialisé");
                initState = OperationState::DRIVE_FAILED;
                initPhase = 0;
                return;
            }
            
            fasMotor->enableOutputs();
            
            // Mettre à jour les nodes
            if (nodes.servoOn) {
                nodes.servoOn->setValue(true);
            }
            if (nodes.stepperActivated) {
                nodes.stepperActivated->setValue(true);
            }
            
            Serial.printf("[%s] [INIT] ✅ Outputs activés\n", motorId.c_str());
            
            // Passer à la phase de stabilisation
            initPhase = 1;
            phaseStartTime = now;
            break;
        }
        
        // ====================================
        // PHASE 1 : STABILISATION
        // ====================================
        case 1: {
            uint32_t phaseElapsed = now - phaseStartTime;
            
            // Attendre 100ms de stabilisation
            if (phaseElapsed > 100) {
                Serial.printf("[%s] [INIT] ✅ Initialisation terminée (durée totale: %lu ms)\n", 
                             motorId.c_str(), elapsed);
                
                initialized = true;
                
                // Mettre à jour node driveInitialised
                if (nodes.driveInitialised) {
                    nodes.driveInitialised->setValue(true);
                }
                
                initState = OperationState::DRIVE_IDLE;
                initPhase = 0;
            }
            break;
        }
        
        // ====================================
        // PHASE INVALIDE
        // ====================================
        default: {
            Serial.printf("[%s] [INIT] ❌ Phase invalide: %d\n", motorId.c_str(), initPhase);
            setError("Phase init invalide");
            initState = OperationState::DRIVE_FAILED;
            initPhase = 0;
            break;
        }
    }
}

// ========================================
// PROCESS RESET
// ========================================

void StepperMotor::processReset(uint32_t now) {
    if (resetPhase == 0) {
        Serial.println("[RESET] Phase 0: Arrêt et reset position");
        
        if (fasMotor) {
            fasMotor->forceStopAndNewPosition(0);
        }
        
        resetPhase = 1;
        phaseStartTime = now;
    }
    
    if (resetPhase == 1) {
        // Attendre stabilisation
        if (now - phaseStartTime > 200) {
            Serial.println("[RESET] ✅ Reset terminé");
            
            // ✅ IMPORTANT: Le moteur RESTE initialisé après un reset
            // initialized = true; (déjà vrai, pas besoin de le refaire)
            
            // ✅ IMPORTANT: Le homing doit être refait après un reset
            homingDone = false;
            if (nodes.homeDone) {
                nodes.homeDone->setValue(false);
            }
            Serial.println("[RESET] 🏠 Homing à refaire");
            
            resetState = OperationState::DRIVE_IDLE;
            resetPhase = 0;
        }
    }
    
    // Timeout
    if (now - operationStartTime > resetTimeoutMs) {
        Serial.println("[RESET] ❌ Timeout");
        setError("Reset timeout");
        resetState = OperationState::DRIVE_FAILED;
        resetPhase = 0;
    }
}

// ========================================
// PROCESS MOVE
// ========================================

void StepperMotor::processMove(uint32_t now) {
    if (!fasMotor) {
        moveState = OperationState::DRIVE_FAILED;
        return;
    }
    
    // Vérifier si mouvement terminé
    if (!fasMotor->isRunning()) {
        int32_t currentPos = fasMotor->getCurrentPosition();
        int32_t diff = abs(currentPos - (int32_t)targetPositionInternal);
        
        if (diff < 10) {  // Tolérance 10 steps
            Serial.printf("[MOVE] ✅ Position atteinte: %ld steps\n", currentPos);
            
            nodes.targetPositionReached->setValue(true);
            moveState = OperationState::DRIVE_IDLE;
            movePhase = 0;
            
            return;
        }
    }
    
    // Timeout
    if (now - operationStartTime > moveTimeoutMs) {
        Serial.println("[MOVE] ❌ Timeout");
        setError("Move timeout");
        moveState = OperationState::DRIVE_FAILED;
        movePhase = 0;
        
        if (fasMotor) {
            fasMotor->forceStop();
        }
    }
}

// ========================================
// PROCESS HOMING
// ========================================

void StepperMotor::processHoming(uint32_t now) {
    // ========================================
    // GARDE: Vérifier que homing est en cours
    // ========================================
    
    if (homingState != OperationState::DRIVE_IN_PROGRESS) {
        return;
    }
    
    uint32_t totalElapsed = now - operationStartTime;
    uint32_t phaseElapsed = now - phaseStartTime;
    
    // ========================================
    // TIMEOUT GLOBAL DE SÉCURITÉ (PRIORITAIRE)
    // ========================================
    
    if (totalElapsed > homingTimeoutMs) {
        Serial.printf("[%s] TIMEOUT homing apres %lu ms (max: %lu ms)\n", 
                     motorId.c_str(), totalElapsed, homingTimeoutMs);
    
        setError("Homing timeout - home switch non detecte dans le delai imparti");
        
        // Arrêter le moteur
        if (fasMotor) {
            fasMotor->forceStop();
        }
        
        // Échec du homing
        homingState = OperationState::DRIVE_TIMEOUT;
        homingPhase = 0;
        
        return;  // SORTIE IMMÉDIATE
    }
    
    // ========================================
    // VÉRIFICATION ALARMES (si nodes disponibles)
    // ========================================
    
    if (nodes.stepperAlarm && nodes.stepperAlarm->getValue()) {
        Serial.printf("[%s] Alarme detectee pendant homing\n", motorId.c_str());
        
        setError("Alarme stepper pendant homing");
        
        // Arrêter le moteur
        if (fasMotor) {
            fasMotor->forceStop();
        }
        
        homingState = OperationState::DRIVE_FAILED;
        homingPhase = 0;
        
        return;
    }
    
    // ========================================
    // MACHINE À ÉTATS HOMING
    // ========================================
    
    switch (homingPhase) {
        
        // ====================================
        // PHASE 0: DÉMARRAGE DU HOMING
        // ====================================
        
        case 0: {
            Serial.printf("[%s] HOMING Phase 0: Demarrage\n", motorId.c_str());
            
            if (!fasMotor) {
                setError("FastAccelStepper non initialise");
                homingState = OperationState::DRIVE_FAILED;
                return;
            }
            
            // Si pas de homeSwitch configuré, faire simple reset position
            if (!nodes.homeSwitch) {
                Serial.printf("[%s] HOMING: Pas de homeSwitch - Reset position a 0\n", 
                             motorId.c_str());
                
                fasMotor->setCurrentPosition(0);
                
                // Aller directement à la finalisation
                homingPhase = 2;
                phaseStartTime = now;
                
            } else {
                // Lancer mouvement vers origine (direction négative)
                Serial.printf("[%s] HOMING: Demarrage mouvement vers origine\n", motorId.c_str());
                
                uint32_t homingSpeed;

                if (currentHomingSpeed > 0) {
                    // Priorité 1 : Vitesse spécifiée dans startHoming()
                    homingSpeed = currentHomingSpeed;
                    Serial.printf("[%s] HOMING: Vitesse specifiee = %lu steps/sec\n", 
                                 motorId.c_str(), homingSpeed);
                                
                } else if (currentJogSpeed > 0) {
                    // Priorité 2 : Vitesse JOG
                    homingSpeed = currentJogSpeed;
                    Serial.printf("[%s] HOMING: Vitesse JOG = %lu steps/sec\n", 
                                 motorId.c_str(), homingSpeed);
                                
                } else {
                    // Priorité 3 : Valeur par défaut
                    homingSpeed = 1000;
                    Serial.printf("[%s] HOMING: Vitesse par defaut = %lu steps/sec\n",
                                 motorId.c_str(), homingSpeed);
                }

                // Configuration de l'accélération AVANT le mouvement
                if (nodes.acceleration && nodes.acceleration->getValue() > 0) {
                    uint32_t accel = nodes.acceleration->getValue();
                    fasMotor->setAcceleration(accel);
                    Serial.printf("[%s] HOMING: Acceleration = %lu steps/sec²\n", 
                                 motorId.c_str(), accel);
                } else {
                    // Valeur par défaut si node absent
                    uint32_t defaultAccel = 5000;  // À ajuster selon votre machine
                    fasMotor->setAcceleration(defaultAccel);
                    Serial.printf("[%s] HOMING: Acceleration par defaut = %lu steps/sec²\n",
                                 motorId.c_str(), defaultAccel);
                }

                fasMotor->setSpeedInHz(homingSpeed);
                fasMotor->moveTo(-99999999);  // Mouvement vers négatif infini
                
                // Passer à la phase de détection
                homingPhase = 1;
                phaseStartTime = now;
            }
            
            break;
        }
        
        // ====================================
        // PHASE 1: ATTENTE DÉTECTION HOMESWITCH
        // ====================================
        
        case 1: {
            // Vérifier détection homeSwitch avec isChanged() pour message unique
            if (nodes.homeSwitch && nodes.homeSwitch->getValue()) {
                
                // Message unique lors du changement d'état
                if (nodes.homeSwitch->getIsChanged()) {
                    Serial.printf("[%s] ✅ HOMING: Home switch detecte apres %lu ms\n", 
                                 motorId.c_str(), phaseElapsed);
                }
                
                // Arrêter immédiatement le moteur et définir position = 0
                fasMotor->forceStopAndNewPosition(0);
                
                Serial.printf("[%s] HOMING: Position definie a 0\n", motorId.c_str());
                
                // Passer à la finalisation
                homingPhase = 2;
                phaseStartTime = now;
                
            } else {
                // Vérifier si le moteur est toujours en mouvement
                if (!fasMotor->isRunning()) {
                    // Moteur arrêté sans avoir détecté homeSwitch
                    Serial.printf("[%s] HOMING: Mouvement termine sans detecter homeSwitch\n", 
                                 motorId.c_str());
                    
                    setError("Home switch non detecte - fin de course atteinte");
                    
                    homingState = OperationState::DRIVE_FAILED;
                    homingPhase = 0;
                    
                } else {
                    // Moteur toujours en mouvement, attendre
                    // Log périodique pour debug (optionnel - actuellement désactivé)
                    /*
                    static uint32_t lastLog = 0;
                    if (now - lastLog > 2000) {  // Toutes les 2 secondes
                        Serial.printf("[%s] HOMING: Attente homeSwitch... (%lu ms ecoulees)\n", 
                                     motorId.c_str(), phaseElapsed);
                        lastLog = now;
                    }
                    */
                }
            }
            
            break;
        }
        
        // ====================================
        // PHASE 2: FINALISATION
        // ====================================
        
        case 2: {
            // Délai de stabilisation (100ms)
            static constexpr uint32_t STABILIZATION_DELAY_MS = 100;
            
            if (phaseElapsed > STABILIZATION_DELAY_MS) {
                Serial.printf("[%s] HOMING: Finalisation (stabilisation OK)\n", 
                             motorId.c_str());
                
                // Marquer homing comme terminé
                homingDone = true;
                
                // Mettre à jour node homeDone
                if (nodes.homeDone) {
                    nodes.homeDone->setValue(true);
                }
                
                // FORCER position = 0 dans le node (garantie cohérence)
                if (nodes.position) {
                    if (nodes.position->getValue() != 0) {
                        Serial.printf("[%s] HOMING: Correction node position de %ld a 0\n", 
                                     motorId.c_str(), nodes.position->getValue());
                        nodes.position->setValue(0);
                    }
                }
                
                Serial.printf("[%s] HOMING: COMPLETE - Position = 0, temps total = %lu ms\n", 
                             motorId.c_str(), totalElapsed);
                
                // Homing terminé avec succès
                homingState = OperationState::DRIVE_IDLE;
                homingPhase = 0;
            }
            
            break;
        }
        
        // ====================================
        // PHASE INVALIDE
        // ====================================
        
        default: {
            Serial.printf("[%s] ERREUR: homingPhase invalide = %d\n", 
                         motorId.c_str(), homingPhase);
            
            setError("Phase homing invalide");
            homingState = OperationState::DRIVE_FAILED;
            homingPhase = 0;
            
            break;
        }
    }
}

// ========================================
// DÉMARRAGE D'OPÉRATIONS
// ========================================

bool StepperMotor::startInitialize() {
    Serial.printf("[%s] startInitialize()\n", motorId.c_str());
    
    if (initState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Initialisation déjà en cours");
        return false;
    }
    
    initState = OperationState::DRIVE_IN_PROGRESS;
    initPhase = 0;
    operationStartTime = millis();
    
    return true;
}

bool StepperMotor::startReset() {
    Serial.printf("[%s] startReset()\n", motorId.c_str());
    
    if (resetState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Reset déjà en cours");
        return false;
    }
    
    resetState = OperationState::DRIVE_IN_PROGRESS;
    resetPhase = 0;
    operationStartTime = millis();
    
    return true;
}

bool StepperMotor::startGoToPosition(int32_t positionSteps, bool waitForSync, uint32_t timeout) {
    moveTimeoutMs = (uint32_t)timeout;

    Serial.printf("[%s] startGoToPosition(%lu, sync=%s, timeout=%lums)\n",
                 motorId.c_str(), positionSteps, waitForSync ? "true" : "false", moveTimeoutMs);

    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        setError("Mouvement déjà en cours");
        return false;
    }
    
    if (!nodes.servoOn->getValue()) {
        setError("ServoOn doit être activé avant le mouvement");
        return false;
    }
    
    // Démarrer le mouvement
    targetPositionInternal = positionSteps;
    fasMotor->moveTo(positionSteps);
    
    moveState = OperationState::DRIVE_IN_PROGRESS;
    movePhase = 0;
    operationStartTime = millis();
    
    nodes.targetPositionReached->setValue(false);
    
    Serial.printf("✅ Mouvement démarré vers %lu steps\n", positionSteps);
    
    return true;
}

bool StepperMotor::startHoming(uint32_t /*timeoutMs*/) {
    // Pas de paramètre
    // Pas de stockage vitesse (fait dans handleHoming)
    homingState = OperationState::DRIVE_IN_PROGRESS;
    homingPhase = 0;
    operationStartTime = millis();
    return true;
}

// ========================================
// ÉTAT DES OPÉRATIONS
// ========================================

ServoDrive::OperationState StepperMotor::getInitializeState() const {
    return initState;
}

ServoDrive::OperationState StepperMotor::getResetState() const {
    return resetState;
}

ServoDrive::OperationState StepperMotor::getMoveState() const {
    return moveState;
}

ServoDrive::OperationState StepperMotor::getHomingState() const {
    return homingState;
}

// ========================================
// CONTRÔLES JOG
// ========================================

bool StepperMotor::jogForward(int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    Serial.printf("[%s] jogForward()\n", motorId.c_str());
    
    if (!fasMotor) {
        setError("FastAccelStepper non initialisé");
        return false;
    }
    
    if (!nodes.servoOn->getValue()) {
        setError("ServoOn doit être activé");
        return false;
    }

    if (limitPuu != 0 && nodes.position->getValue() >= limitPuu && nodes.homeDone->getValue()) {
        jogStop();
        Serial.printf("⛔ StepperMotor: Limite reverse atteinte (%lu <= %lu)\n",
                      nodes.position->getValue(), limitPuu);
        return false;
    }
    
    fasMotor->setSpeedInHz(currentJogSpeed);
    fasMotor->runForward();
    
    return true;
}

bool StepperMotor::jogReverse(int32_t limitPuu, uint32_t slowZonePuu, uint16_t slowSpeed) {
    Serial.printf("[%s] jogReverse()\n", motorId.c_str());
    
    if (!fasMotor) {
        setError("FastAccelStepper non initialisé");
        return false;
    }
    
    if (!nodes.servoOn->getValue()) {
        setError("ServoOn doit être activé");
        return false;
    }

    if (limitPuu != 0 && nodes.position->getValue() <= limitPuu && nodes.homeDone->getValue()) {
        jogStop();
        Serial.printf("⛔ StepperMotor: Limite reverse atteinte (%lu <= %lu)\n",
                      nodes.position->getValue(), limitPuu);
        return false;
    }
    
    fasMotor->setSpeedInHz(currentJogSpeed);
    fasMotor->runBackward();
    
    return true;
}

bool StepperMotor::jogStop() {
    Serial.printf("[%s] jogStop()\n", motorId.c_str());
    
    if (!fasMotor) {
        setError("FastAccelStepper non initialisé");
        return false;
    }
    
    fasMotor->stopMove();
    
    return true;
}

bool StepperMotor::setJogSpeed(uint32_t speed) {
    Serial.printf("[%s] setJogSpeed(%lu)\n", motorId.c_str(), speed);
    
    if (speed == 0 || speed > MAX_JOG_SPEED) {
        setError("Vitesse jog invalide");
        return false;
    }
    
    currentJogSpeed = speed;
    
    if (nodes.jogSpeed) {
        nodes.jogSpeed->setValue(speed);
    }
    
    return true;
}

uint32_t StepperMotor::getJogSpeed() const {
    return currentJogSpeed;
}

// ========================================
// CONTRÔLES SERVO
// ========================================

bool StepperMotor::setServoEnabled(bool enable) {
    Serial.printf("[%s] setServoEnabled(%s)\n", motorId.c_str(), enable ? "true" : "false");
    
    nodes.servoOn->setValue(enable);
    
    return true;
}

bool StepperMotor::setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) {
    // TODO: rampe accel/décel — non traité à cette étape
    (void)rampIndex;

    Serial.printf("[%s] setSpeedAndRamp(%u)\n", motorId.c_str(), speed);

    uint32_t speedHz = speed * 1000;
    
    if (!fasMotor) {
        setError("FastAccelStepper non initialisé");
        return false;
    }
    
    fasMotor->setSpeedInHz(speedHz);
    
    if (nodes.maxSpeed) {
        nodes.maxSpeed->setValue(speedHz);
    }
    
    return true;
}

bool StepperMotor::setMaxTorque(uint8_t torquePercent) {
    // N/A pour stepper
    return true;
}

bool StepperMotor::emergencyStop() {
    Serial.printf("[%s] emergencyStop()\n", motorId.c_str());
    
    if (fasMotor) {
        fasMotor->forceStop();
        fasMotor->disableOutputs();
    }
    
    moveState = OperationState::DRIVE_FAILED;
    initState = OperationState::DRIVE_FAILED;
    homingState = OperationState::DRIVE_FAILED;
    
    nodes.servoOn->setValue(false);

    // ✅ stepperAlarm sera mis à jour automatiquement via updateStatusNodes()
    // car status.stepperAlarm détecte moveState/homingState = DRIVE_FAILED
    
    return true;
}

bool StepperMotor::saveParameters() {
    // N/A pour FastAccelStepper
    return true;
}

// ========================================
// ACCESSEURS
// ========================================

int32_t StepperMotor::getPosition() const {
    if (fasMotor) {
        int32_t pos = fasMotor->getCurrentPosition();
        return pos >= 0 ? pos : 0;
    }
    return 0;
}

const char* StepperMotor::getLastError() const {
    return lastError.c_str();
}

// ========================================
// HANDLERS
// ========================================

bool StepperMotor::handleInitializing(OperationStatus& status) {
    // ✅ ÉTAPE 1 : Démarrer l'initialisation au premier appel
    if (!stepperInitStarted) {
        if (!startInitialize()) {
            char msg[128];
            snprintf(msg, sizeof(msg), "[%s] Impossible de démarrer l'initialisation", motorId.c_str());
            BorneUniverselle::prepareMessage(ERROR, msg);
            
            lastError = "Échec démarrage initialisation";
            status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
        
        Serial.printf("[%s] ✅ Initialisation démarrée\n", motorId.c_str());
        stepperInitStarted = true;
        status = OperationStatus::IN_PROGRESS;
        return true;
    }
    
    // ✅ ÉTAPE 2 : Surveiller l'état
    OperationState currentInitState = getInitializeState();
    
    switch (currentInitState) {
        case OperationState::DRIVE_IDLE:
            if (initialized) {
                // ✅ Initialisation terminée avec succès
                char successMsg[128];
                snprintf(successMsg, sizeof(successMsg), "[%s] Initialisation terminée avec succès", motorId.c_str());
                BorneUniverselle::prepareMessage(SUCCESS, successMsg);
                stepperInitStarted = false;  // Reset pour prochain cycle
                status = OperationStatus::COMPLETED;
                return true;
            } else {
                // Pas encore initialisé
                status = OperationStatus::IN_PROGRESS;
                return true;
            }
            
        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT: {
            stepperInitStarted = false;
            const char* errorMsg = getLastError();
            
            char userMessage[200];
            snprintf(userMessage, sizeof(userMessage), "[%s] Erreur initialisation: %s", motorId.c_str(), errorMsg);
            PLC_Tools::sendPeriodicMessage(true, ERROR, userMessage);
            
            status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
            
        case OperationState::DRIVE_IN_PROGRESS:
        default:
            status = OperationStatus::IN_PROGRESS;
            return true;
    }
}

bool StepperMotor::handleHoming(OperationStatus& status, uint32_t homingSpeed, uint32_t /*timeoutMs*/) {
    if (!stepperHomingStarted) {
        // ✅ Configurer vitesse AVANT de demarrer le homing
        if (homingSpeed > 0) {
            currentHomingSpeed = homingSpeed;
            Serial.printf("[%s] handleHoming: Vitesse configuree = %lu steps/sec\n",
                         motorId.c_str(), homingSpeed);
        } else {
            currentHomingSpeed = 0;  // Utilise jogSpeed ou defaut
            Serial.printf("[%s] handleHoming: Vitesse auto (jogSpeed ou defaut)\n",
                         motorId.c_str());
        }
        
        if (!startHoming()) {
            // ✅ MESSAGE UTILISATEUR pour échec démarrage
            char msg[128];
            snprintf(msg, sizeof(msg), "[%s] ERREUR: Impossible de démarrer le homing", motorId.c_str());
            BorneUniverselle::prepareMessage(ERROR, msg);
            
            status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
        
        stepperHomingStarted = true;
    }
    
    // Surveiller l'etat du homing
    OperationState homingState = getHomingState();
    
    switch (homingState) {
        case OperationState::DRIVE_IDLE: {
            // ✅ MESSAGE UTILISATEUR pour succès
            char successMsg[128];
            snprintf(successMsg, sizeof(successMsg), 
                "[%s] HOMING terminé avec succès! Position: %ld steps", 
                motorId.c_str(), fasMotor ? fasMotor->getCurrentPosition() : 0);
            BorneUniverselle::prepareMessage(SUCCESS, successMsg);
            Serial.printf("[%s] ✅ Homing terminé\n", motorId.c_str());
            
            status = OperationStatus::COMPLETED;
            stepperHomingStarted = false;
            break;
        }
            
        case OperationState::DRIVE_IN_PROGRESS: {
            // ✅ MESSAGES DE PROGRESSION périodiques (comme SureServo)
            static uint32_t lastProgressLog = 0;
            uint32_t now = millis();
            
            if (now - lastProgressLog > 5000) {
                uint32_t elapsed = (now - operationStartTime) / 1000;
                char progressMsg[200];
                snprintf(progressMsg, sizeof(progressMsg), 
                    "[%s] HOMING en cours... (%lu sec) [Position: %ld steps]",
                    motorId.c_str(), (unsigned long)elapsed, 
                    fasMotor ? fasMotor->getCurrentPosition() : 0);
                BorneUniverselle::prepareMessage(INFO, progressMsg);
                lastProgressLog = now;
            }
            
            status = OperationStatus::IN_PROGRESS;
            break;
        }
            
        case OperationState::DRIVE_FAILED:
        case OperationState::DRIVE_TIMEOUT: {
            // ✅ MESSAGE UTILISATEUR CLAIR pour échec/timeout
            const char* errorMsg = getLastError();
            
            char userMessage[200];
            snprintf(userMessage, sizeof(userMessage), 
                "[%s] Échec homing: %s", motorId.c_str(), errorMsg);
            
            // ✅ MESSAGE PÉRIODIQUE pour timeout
            PLC_Tools::sendPeriodicMessage(true, ERROR, userMessage);
            Serial.printf("[%s] ❌ %s\n", motorId.c_str(), errorMsg);
            
            if (homingState == OperationState::DRIVE_TIMEOUT) {
                status = OperationStatus::SERVO_DRIVE_ERROR;
            } else {
                status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            }
            stepperHomingStarted = false;
            break;
        }
    }
    
    return true;
}

bool StepperMotor::handleJogging(OperationStatus& status) {
    if (fasMotor && fasMotor->isRunning()) {
        status = OperationStatus::IN_PROGRESS;
    } else {
        status = OperationStatus::COMPLETED;
    }
    return true;
}

bool StepperMotor::handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync, uint32_t timeoutMs) {

    if (!fasMotor) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[%s] ERREUR: FastAccelStepper non initialisé", motorId.c_str());
        BorneUniverselle::prepareMessage(ERROR, msg);
        status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
        return false;
    }

    // CAS 1 : Mouvement en cours
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        static uint32_t lastProgressLog = 0;
        uint32_t now = millis();
        if (now - lastProgressLog > 1000) {
            Serial.printf("[%s] Mouvement en cours vers %lu steps...\n",
                         motorId.c_str(), (unsigned long)targetPositionInternal);
            lastProgressLog = now;
        }
        status = OperationStatus::IN_PROGRESS;
        return true;
    }

    // CAS 2 : Mouvement terminé
    if (moveState == OperationState::DRIVE_IDLE && targetPositionInternal == (int32_t)pos) {
        char successMsg[128];
        snprintf(successMsg, sizeof(successMsg), "[%s] Position %lu steps atteinte",
                 motorId.c_str(), (unsigned long)pos);
        BorneUniverselle::prepareMessage(SUCCESS, successMsg);
        status = OperationStatus::COMPLETED;
        return true;
    }

    // CAS 3 : Erreur ou timeout
    if (moveState == OperationState::DRIVE_FAILED || moveState == OperationState::DRIVE_TIMEOUT) {
        char errorMsg[200];
        snprintf(errorMsg, sizeof(errorMsg), "[%s] Mouvement échoué vers %lu steps: %s",
                 motorId.c_str(), (unsigned long)pos, getLastError());
        BorneUniverselle::prepareMessage(ERROR, errorMsg);
        status = OperationStatus::SERVO_DRIVE_ERROR;
        return true;
    }

    // CAS 4 : Nouveau mouvement à démarrer
    if (moveState == OperationState::DRIVE_IDLE) {
        if (!startGoToPosition(pos, waitForSync, (uint16_t)timeoutMs)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "[%s] ERREUR: Impossible de démarrer le mouvement vers %lu steps",
                     motorId.c_str(), (unsigned long)pos);
            BorneUniverselle::prepareMessage(ERROR, msg);
            status = OperationStatus::SERVO_DRIVE_FATAL_ERROR;
            return false;
        }
        status = OperationStatus::IN_PROGRESS;
        return true;
    }

    status = OperationStatus::IN_PROGRESS;
    return true;
}

// ========================================
// CONFIGURATION ACCÉLÉRATION
// ========================================

bool StepperMotor::setAccelerationProfile(uint32_t maxSpeedStepsPerSec, uint32_t accelStepsPerSec2) {
    Serial.printf("[%s] setAccelerationProfile(max=%lu, accel=%lu)\n", 
                  motorId.c_str(), (long unsigned int)maxSpeedStepsPerSec, (long unsigned int)accelStepsPerSec2);
    
    if (!fasMotor) {
        setError("FastAccelStepper non initialisé");
        return false;
    }

    // Clamp vitesse
    if (maxSpeedStepsPerSec == 0 || maxSpeedStepsPerSec > MAX_SERVO_SPEED) {
        Serial.printf("[%s] ⚠️ vitesse clampée: %lu -> %lu\n", 
                      motorId.c_str(), (long unsigned int)maxSpeedStepsPerSec, (long unsigned int)MAX_SERVO_SPEED);
        maxSpeedStepsPerSec = MAX_SERVO_SPEED;
    }

    fasMotor->setSpeedInHz(maxSpeedStepsPerSec);
    fasMotor->setAcceleration(accelStepsPerSec2);
    accelerationEnabled = true;
    
    return true;
}

uint32_t StepperMotor::getCurrentSpeed() const {
    if (fasMotor) {
        return fasMotor->getCurrentSpeedInMilliHz() / 1000;
    }
    return 0;
}

// ========================================
// MÉTHODES PRIVÉES
// ========================================

bool StepperMotor::extractPinNumber(HardwareBooleanOutputNode* node, const char* pinName, uint8_t& pinNumber) {
    if (!node) {
        char error[100];
        snprintf(error, sizeof(error), "Node %s est NULL", pinName);
        setError(error);
        return false;
    }
    
    if (node->classType() != CLASS_HW_BOOLEAN_OUTPUT_NODE) {
        char error[100];
        snprintf(error, sizeof(error), "Node %s n'est pas HardwareBooleanOutputNode (type=%d)", 
                pinName, node->classType());
        setError(error);
        return false;
    }
    
    HardwareBooleanOutputNode* hwNode = static_cast<HardwareBooleanOutputNode*>(node);
    pinNumber = hwNode->getPinNumber();
    
    return true;
}

bool StepperMotor::checkAndHandleLimitSwitches() {
    
    // VÉRIFICATION LIMIT SWITCH POSITIVE (+)
    if (nodes.limitSwitchPositive && nodes.limitSwitchPositive->getValue()) {
        
        // Génération alarme
        nodes.limitError->setValue(true);
        
        // Messages
        if (nodes.limitSwitchPositive && nodes.limitSwitchPositive->getIsChanged()){
            setError("Fin de course POSITIVE atteinte");
        }
        PLC_Tools::sendPeriodicMessage(true, ERROR, "⚠️ Limite positive atteinte");
    }
    
    // VÉRIFICATION LIMIT SWITCH NEGATIVE (-)
    if (nodes.limitSwitchNegative && nodes.limitSwitchNegative->getValue()) {
        
        // Génération alarme
        nodes.limitError->setValue(true);
        
        // Messages
        if (nodes.limitSwitchNegative && nodes.limitSwitchNegative->getIsChanged()){
            setError("Fin de course NEGATIVE atteinte");
        }
        PLC_Tools::sendPeriodicMessage(true, ERROR, "⚠️ Limite négative atteinte");
    }

    if (nodes.limitError && nodes.limitError->getValue()){
        // Échec des opérations en cours
        moveState = OperationState::DRIVE_FAILED;
        homingState = OperationState::DRIVE_FAILED;
         // Arrêt immédiat
        fasMotor->forceStop();
    }
    return nodes.limitError && nodes.limitError->getValue();
}

void StepperMotor::updateStatusNodes() {
    // ========================================
    // MISE À JOUR POSITION (depuis FastAccelStepper directement)
    // ========================================
    if (nodes.position) {
        int32_t currentPos = fasMotor->getCurrentPosition();
        uint32_t pos = currentPos >= 0 ? currentPos : 0;
        
        if (nodes.position->getValue() != pos){
            nodes.position->setValue(pos);
        }
    }
    
    // ========================================
    // MISE À JOUR NODES OPTIONNELS (depuis structure status)
    // ========================================
    // Similaire à SureServo::updateOptionalOutputNodes()
    
    // === NODE OBLIGATOIRE - Position atteinte ===
    if (nodes.targetPositionReached) {
        nodes.targetPositionReached->setValue(status.targetPositionReached);
    }
    
    // === NODES OPTIONNELS - État principal ===
    if (nodes.stepperReady) {
        nodes.stepperReady->setValue(status.stepperReady);
    }
    
    if (nodes.stepperActivated) {
        nodes.stepperActivated->setValue(status.stepperActivated);
    }
    
    if (nodes.zeroSpeed) {
        nodes.zeroSpeed->setValue(status.zeroSpeed);
    }
    
    if (nodes.stepperAlarm) {
        bool wasAlarm = nodes.stepperAlarm->getValue();
        nodes.stepperAlarm->setValue(status.stepperAlarm);
        if (status.stepperAlarm && !wasAlarm) {
            // Log Serial
            char msg[128];
            snprintf(msg, sizeof(msg), "🚨 STEPPER EN ALARME AL%03u: %s", getAlarmCode(), getAlarmDescription());
            Serial.println(msg);
            // Message utilisateur
            BorneUniverselle::prepareMessage(ERROR, msg);
            PLC_Tools::logDiagnostic(msg);
            // Callback projet
            if (onAlarmCallback) onAlarmCallback(getAlarmCode(), getLastError());
        }
    }
    
    if (nodes.homeDone) {
        nodes.homeDone->setValue(status.homeDone);
    }
    
    if (nodes.limitError) {
        nodes.limitError->setValue(status.limitError);
    }
    
    // === NODES CALCULÉS ===
    if (nodes.driveInitialised) {
        nodes.driveInitialised->setValue(initialized);
    }
    
    // targetSpeedRated n'a pas de sens pour un stepper, mais on le garde pour compatibilité
    if (nodes.targetSpeedRated) {
        // Pour un stepper, on considère la vitesse atteinte si on ne bouge pas
        nodes.targetSpeedRated->setValue(status.zeroSpeed);
    }
}

void StepperMotor::syncParametersFromNodes() {
    // Sync maxSpeed
    if (nodes.maxSpeed && nodes.maxSpeed->getIsChanged()) {
        uint32_t newMaxSpeed = nodes.maxSpeed->getValue();
        if (newMaxSpeed > 0 && newMaxSpeed <= MAX_SERVO_SPEED && fasMotor) {
            fasMotor->setSpeedInHz(newMaxSpeed);
        }
    }
    
    // Sync acceleration
    if (nodes.acceleration && nodes.acceleration->getIsChanged()) {
        uint32_t newAccel = nodes.acceleration->getValue();
        if (newAccel > 0 && newAccel <= MAX_ACCELERATION && fasMotor) {
            fasMotor->setAcceleration(newAccel);
        }
    }
    
    // Sync jogSpeed — déjà protégé par setJogSpeed(), appel délégué
    if (nodes.jogSpeed && nodes.jogSpeed->getIsChanged()) {
        uint32_t newJogSpeed = nodes.jogSpeed->getValue();
        if (newJogSpeed > 0) {
            setJogSpeed(newJogSpeed);  // setJogSpeed() applique déjà MAX_JOG_SPEED
        }
    }
}

bool StepperMotor::isMovementComplete() const {
    if (!fasMotor) return true;
    return !fasMotor->isRunning();
}

void StepperMotor::printConfiguration() const {
    Serial.println("=== CONFIGURATION STEPPER MOTOR ===");
    Serial.printf("Steps per unit: %lu\n", stepsPerUnit);
    Serial.printf("Nodes required: %s\n", nodes.validateRequired() ? "OK" : "MANQUANTS");
    Serial.printf("Nodes optional: %zu configurés\n", nodes.countOptional());
    Serial.println("================================");
}

bool StepperMotor::getIsReady() const {
    // Ready si initialisé et pas d'alarme
    return initialized && (!nodes.stepperAlarm || !nodes.stepperAlarm->getValue());
}

bool StepperMotor::getIsInitialized() const {
    return initialized;
}

bool StepperMotor::getHomeDone() const {
    return homingDone;
}

bool StepperMotor::getServoActivated() const {
    return nodes.servoOn && nodes.servoOn->getValue();
}

bool StepperMotor::getTargetPositionReached() const {
    return nodes.targetPositionReached && nodes.targetPositionReached->getValue();
}

/**
 * @brief Met à jour tous les indicateurs de statut depuis FastAccelStepper
 * 
 * Cette méthode calcule tous les états en interrogeant FastAccelStepper,
 * similaire à comment SureServo décode les registres Modbus.
 */
void StepperMotor::StepperStatus::updateFromMotor(FastAccelStepper* motor, 
                                                   const StepperMotor& stepperMotor) {
    if (!motor) {
        // Moteur non initialisé - tout à false
        reset();
        return;
    }
    
    // === ÉTATS DE BASE ===
    
    // stepperActivated = servoOn actif
    stepperActivated = stepperMotor.nodes.servoOn && 
                      stepperMotor.nodes.servoOn->getValue();
    
    // stepperAlarm = alarme présente
    // ✅ Calculé depuis les VRAIES sources d'alarmes
    bool hasHardwareAlarm = stepperMotor.nodes.hardwareStepperAlarm && 
                        stepperMotor.nodes.hardwareStepperAlarm->getValue();
    bool hasOperationFailure = stepperMotor.moveState == OperationState::DRIVE_FAILED ||
                            stepperMotor.homingState == OperationState::DRIVE_FAILED ||
                            stepperMotor.resetState == OperationState::DRIVE_FAILED;

    stepperAlarm = hasHardwareAlarm || hasOperationFailure || limitError;
    
    // stepperReady = initialisé ET pas d'alarme
    stepperReady = stepperMotor.initialized && !stepperAlarm;
    
    // === ÉTATS DE MOUVEMENT ===
    
    // zeroSpeed = moteur arrêté (vitesse nulle)
    // FastAccelStepper::isRunning() retourne true si le moteur est en mouvement
    zeroSpeed = !motor->isRunning();
    
    // === ÉTATS DE POSITION ===
    
    // targetPositionReached = calculé en comparant position actuelle et cible
    // Utilise la même logique que dans processMove()
    int32_t currentPos = motor->getCurrentPosition();
    int32_t diff = abs(currentPos - (int32_t)stepperMotor.targetPositionInternal);
    targetPositionReached = (diff < 10);  // Tolérance 10 steps (comme dans processMove)
    
    // === ÉTATS DE HOMING ===
    
    // homeDone = membre privé homingDone
    homeDone = stepperMotor.homingDone;
    
    // === ÉTATS D'ERREUR ===
    
    // limitError = fin de course activée
    limitError = stepperMotor.nodes.limitError && 
                stepperMotor.nodes.limitError->getValue();
}

uint16_t StepperMotor::getAlarmCode() const {
    if (nodes.hardwareStepperAlarm && nodes.hardwareStepperAlarm->getValue())
        return STEPPER_ALARM_HARDWARE;
    if (status.limitError)
        return STEPPER_ALARM_LIMIT_ERROR;
    // moveState/homingState/resetState DRIVE_FAILED
    return STEPPER_ALARM_OPERATION_FAILED;
}

const char*  StepperMotor::getAlarmDescription() const {
    uint16_t code = getAlarmCode();
    for (const auto& alarm : STEPPER_ALARMS) {
        if (alarm.code == code) return alarm.description;
    }
    return "Alarme inconnue";
}

bool StepperMotor::stopMovement() {
    Serial.printf("[%s] 🛑 stopMovement() - Arrêt propre demandé\n", motorId.c_str());
    
    // ========================================
    // 1. ARRÊT PHYSIQUE DU MOTEUR
    // ========================================
    if (fasMotor) {
        fasMotor->stopMove();
    }
    
    // ========================================
    // 2. ANNULATION DES OPÉRATIONS EN COURS (SANS FAILED)
    // ========================================
    
    // Annuler le mouvement en cours
    if (moveState == OperationState::DRIVE_IN_PROGRESS) {
        Serial.printf("   → Mouvement annulé (phase %d)\n", movePhase);
        moveState = OperationState::DRIVE_IDLE;  // ✅ Retour à IDLE (pas FAILED)
        movePhase = 0;
        targetPositionInternal = 0;
    }
    
    // Annuler le homing en cours
    if (homingState == OperationState::DRIVE_IN_PROGRESS) {
        Serial.println("   → Homing annulé");
        homingState = OperationState::DRIVE_IDLE;  // ✅ Retour à IDLE (pas FAILED)
        homingPhase = 0;
    }
    
    Serial.println("✅ StepperMotor: Arrêt propre effectué");
    
    return true;
}






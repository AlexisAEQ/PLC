# Documentation Développeur - Classe SureServo

## Table des Matières

- [Vue d'ensemble](#vue-densemble)
- [Architecture](#architecture)
- [Configuration Initiale](#configuration-initiale)
- [Intégration dans une Classe Métier](#intégration-dans-une-classe-métier)
- [Utilisation dans les Handlers d'État](#utilisation-dans-les-handlers-détat)
- [Gestion des Alarmes](#gestion-des-alarmes-dans-le-plc)
- [Contrôles Manuels (JOG)](#contrôles-manuels-jog)
- [Configuration et Paramètres](#configuration-et-paramètres)
- [Débogage et Diagnostics](#débogage-et-diagnostics)
- [Exemple Complet](#exemple-complet-dintégration)
- [Points d'Attention](#points-dattention-spécifiques-au-plc)

## Vue d'ensemble

La classe `SureServo` est une implémentation spécialisée pour contrôler les servo drives **SureServo 2** dans l'écosystème du PLC. Elle est conçue pour être intégrée dans une classe métier qui hérite de `BusinessLogic` et être appelée depuis la méthode `logicExecutor()`.

### Contexte d'Architecture PLC

```
PLC Framework
    ↓
BusinessLogic (classe de base)
    ↓
MaClasseMetier : public BusinessLogic
    ↓
logicExecutor() → sureServo->process()
```

### Design Patterns Utilisés

- **State Pattern** : Machines à états internes pour chaque opération du servo
- **Strategy Pattern** : Décodage des registres et gestion des alarmes  
- **Template Method** : Méthode `process()` avec hooks d'extension
- **Observer Pattern** : Mise à jour automatique des nodes
- **Facade Pattern** : Interface simplifiée cachant la complexité SureServo

## Architecture

```cpp
ServoDrive (Interface abstraite)
    ↓
SureServo (Implémentation concrète)
    ↓
ServoNodes (Structure de configuration)
```

## Configuration Initiale

### 1. Structure ServoNodes

```cpp
ServoNodes servoNodes;

// === NODES OBLIGATOIRES ===
servoNodes.servoOn = monServoOnNode;
servoNodes.immediateStop = monImmediateStopNode;
servoNodes.alarmsReset = monAlarmsResetNode;
servoNodes.position = monPositionNode;
servoNodes.targetA = monTargetANode;
servoNodes.targetB = monTargetBNode; 
servoNodes.targetC = monTargetCNode;
servoNodes.trigger = monTriggerNode;
servoNodes.jogSpeed = monJogSpeedNode;
servoNodes.maxTorque = monMaxTorqueNode;
servoNodes.pr1Speed = monPr1SpeedNode;
servoNodes.pr2Speed = monPr2SpeedNode;
servoNodes.pr3Speed = monPr3SpeedNode;
servoNodes.auxFunctions = monAuxFunctionsNode;
servoNodes.status = monStatusNode;
servoNodes.alarms = monAlarmsNode;
servoNodes.absoluteCoordonateSystemStatus = monAbsoluteStatusNode;

// === NODES OPTIONNELS ===
servoNodes.servoReady = monServoReadyNode;        // nullptr si non utilisé
servoNodes.servoActivated = monServoActivatedNode;
servoNodes.homeDone = monHomeDoneNode;
// ... autres nodes optionnels
```

### 2. Validation des Nodes

La structure `ServoNodes` fournit des méthodes de validation :

```cpp
// Vérifier que tous les nodes obligatoires sont présents
if (!servoNodes.validateRequired()) {
    Serial.println("Erreur : nodes obligatoires manquants");
    return false;
}

// Compter les nodes optionnels configurés
size_t optionalCount = servoNodes.countOptional();
Serial.printf("Nodes optionnels configurés : %zu\n", optionalCount);
```

## Intégration dans une Classe Métier

### 1. Structure de Base

```cpp
#include "BusinessLogic/BusinessLogic.h"
#include "SureServo/SureServo.h"

class MaClasseMetier : public BusinessLogic {
private:
    std::unique_ptr<SureServo> sureServo = nullptr;
    
    // Vos nodes déclarés dans le header
    BooleanOutputNode* servoOn = nullptr;
    BooleanOutputNode* immediateStop = nullptr;
    // ... autres nodes
    
public:
    MaClasseMetier();
    
    // Méthodes obligatoires de BusinessLogic
    bool logicExecutor() override;
    bool initializePointers(const char* contexte) override;
    const char* stateToString(int state) const override;
    
    // Hooks de BusinessLogic
    void onStateEnter(int newState, int oldState) override;
    void onStateExit(int oldState, int newState) override;
};
```

### 2. Initialisation dans initializePointers()

```cpp
bool MaClasseMetier::initializePointers(const char* contexte) {
    Serial.println("MaClasseMetier::initializePointers - Début");
    
    // 1. Initialiser tous vos nodes avec initNodePtr()
    initNodePtr(servoOn, HASH_SERVO_ON, contexte);
    initNodePtr(immediateStop, HASH_IMMEDIATE_STOP, contexte);
    initNodePtr(position, HASH_POSITION, contexte);
    // ... tous les autres nodes
    
    // 2. Vérifier si des erreurs se sont produites
    if (BorneUniverselle::getInstance()->isPlcBroken()) {
        Serial.println("Erreur détectée pendant l'initialisation des nodes");
        return false;
    }
    
    // 3. Créer la structure ServoNodes
    ServoNodes servoNodes;
    configureServoNodes(servoNodes);
    
    // 4. Créer l'instance SureServo
    sureServo = std::make_unique<SureServo>(PULSES_PER_INCH, servoNodes);
    
    if (!sureServo) {
        BorneUniverselle::getInstance()->setPlcBroken("Échec création SureServo");
        return false;
    }
    
    // 5. Vérifier la création
    const char* lastError = sureServo->getLastError();
    if (lastError && strlen(lastError) > 0 && strcmp(lastError, "Pas d'erreur") != 0) {
        String errorMsg = "Erreur SureServo: ";
        errorMsg += lastError;
        BorneUniverselle::getInstance()->setPlcBroken(errorMsg.c_str());
        return false;
    }
    
    Serial.println("✅ SureServo créé avec succès");
    return true;
}

private:
void configureServoNodes(ServoNodes& nodes) {
    // === NODES OBLIGATOIRES ===
    nodes.servoOn = servoOn;
    nodes.immediateStop = immediateStop;
    nodes.alarmsReset = alarmsReset;
    nodes.position = position;
    nodes.targetA = targetA;
    nodes.targetB = targetB;
    nodes.targetC = targetC;
    nodes.trigger = trigger;
    nodes.jogSpeed = jogSpeed;
    nodes.maxTorque = maxTorque;
    nodes.pr1Speed = pr1Speed;
    nodes.pr2Speed = pr2Speed;
    nodes.pr3Speed = pr3Speed;
    nodes.auxFunctions = auxFunctions;
    nodes.status = status;
    nodes.alarms = alarms;
    nodes.absoluteCoordonateSystemStatus = absoluteCoordonateSystemStatus;
    
    // === NODES OPTIONNELS ===
    nodes.servoReady = servoReady;
    nodes.servoActivated = servoActivated;
    nodes.targetPositionReached = targetPositionReached;
    nodes.homeDone = homeDone;
    // ... autres nodes optionnels (nullptr si non utilisés)
}
```

### 3. Intégration dans logicExecutor()

```cpp
bool MaClasseMetier::logicExecutor() {
    uint32_t now = millis();
    
    // 1. OBLIGATOIRE : Traitement du SureServo
    if (sureServo) {
        sureServo->process(); // Cette ligne fait tout le travail !
    }
    
    // 2. Vérifications critiques du système
    if (BorneUniverselle::getInstance()->isPlcBroken()) {
        Serial.printf("PLC broken, exiting\n");
        return false; // Arrêt du PLC
    }
    
    // 3. Votre logique métier (gestion d'états)
    State currentState = getCurrentState();
    
    switch (currentState) {
        case State::INITIALIZING:
            return handleInitializingState(now);
            
        case State::IDLE:
            return handleIdleState(now);
            
        case State::MOVING:
            return handleMovingState(now);
            
        case State::HOMING:
            return handleHomingState(now);
            
        // ... autres états
            
        default:
            Serial.printf("État inconnu: %d\n", currentState);
            BorneUniverselle::getInstance()->setPlcBroken("État machine inconnu");
            return false; // Erreur fatale
    }
    
    return true; // Succès
}
```

## Utilisation dans les Handlers d'État

### 1. Initialisation du Servo

```cpp
bool MaClasseMetier::handleInitializingState(uint32_t now) {
    static bool initStarted = false;
    
    if (!initStarted) {
        if (sureServo && sureServo->startInitialize()) {
            initStarted = true;
            Serial.println("Initialisation SureServo démarrée");
        } else {
            Serial.println("Erreur démarrage initialisation");
            transitionToState(State::ERROR);
            return true; // Pas d'erreur fatale PLC
        }
    }
    
    // Vérifier l'état de l'initialisation
    if (sureServo) {
        switch (sureServo->getInitializeState()) {
            case ServoDrive::OperationState::DRIVE_IN_PROGRESS:
                // Affichage périodique du progrès
                static uint32_t lastProgressMsg = 0;
                if (now - lastProgressMsg > 2000) {
                    Serial.println("Initialisation en cours...");
                    BorneUniverselle::prepareMessage(INFO, "Initialisation servo...");
                    lastProgressMsg = now;
                }
                break;
                
            case ServoDrive::OperationState::DRIVE_SUCCESS:
                Serial.println("Initialisation SureServo réussie");
                BorneUniverselle::prepareMessage(SUCCESS, "Servo initialisé");
                initStarted = false; // Reset pour la prochaine fois
                transitionToState(State::IDLE);
                break;
                
            case ServoDrive::OperationState::DRIVE_FAILED:
                Serial.printf("Initialisation échouée: %s\n", sureServo->getLastError());
                BorneUniverselle::prepareMessage(ERROR, sureServo->getLastError());
                initStarted = false;
                transitionToState(State::ERROR);
                break;
                
            case ServoDrive::OperationState::DRIVE_TIMEOUT:
                Serial.println("Timeout initialisation");
                BorneUniverselle::prepareMessage(ERROR, "Timeout initialisation servo");
                initStarted = false;
                transitionToState(State::ERROR);
                break;
        }
    }
    
    return true; // Pas d'erreur fatale
}
```

### 2. Mouvements de Position

```cpp
bool MaClasseMetier::handleMovingState(uint32_t now) {
    static bool moveStarted = false;
    static uint32_t targetPosition = 0;
    
    if (!moveStarted) {
        // Récupérer la position cible depuis votre logique métier
        targetPosition = calculateTargetPosition();
        
        if (sureServo && sureServo->startGoToPosition(targetPosition)) {
            moveStarted = true;
            Serial.printf("Mouvement vers %lu démarré\n", targetPosition);
            BorneUniverselle::prepareMessage(INFO, "Mouvement démarré");
        } else {
            Serial.println("Erreur démarrage mouvement");
            transitionToState(State::ERROR);
            return true;
        }
    }
    
    // Surveiller le mouvement
    if (sureServo) {
        switch (sureServo->getMoveState()) {
            case ServoDrive::OperationState::DRIVE_IN_PROGRESS:
                // Affichage périodique de la position
                static uint32_t lastPositionLog = 0;
                if (now - lastPositionLog > 1000) {
                    uint32_t currentPos = sureServo->getPosition();
                    Serial.printf("Position: %lu / %lu\n", currentPos, targetPosition);
                    lastPositionLog = now;
                }
                break;
                
            case ServoDrive::OperationState::DRIVE_SUCCESS:
                Serial.println("Position atteinte !");
                BorneUniverselle::prepareMessage(SUCCESS, "Position atteinte");
                moveStarted = false;
                transitionToState(State::IDLE);
                break;
                
            case ServoDrive::OperationState::DRIVE_FAILED:
                Serial.printf("Mouvement échoué: %s\n", sureServo->getLastError());
                BorneUniverselle::prepareMessage(ERROR, "Mouvement échoué");
                moveStarted = false;
                transitionToState(State::ERROR);
                break;
                
            case ServoDrive::OperationState::DRIVE_TIMEOUT:
                Serial.println("Timeout mouvement");
                BorneUniverselle::prepareMessage(ERROR, "Timeout mouvement");
                moveStarted = false;
                transitionToState(State::ERROR);
                break;
        }
    }
    
    return true;
}
```

### 3. Procédure de Homing

```cpp
bool MaClasseMetier::handleHomingState(uint32_t now) {
    static bool homingStarted = false;
    
    if (!homingStarted) {
        if (sureServo && sureServo->startHoming()) {
            homingStarted = true;
            Serial.println("Homing démarré");
            BorneUniverselle::prepareMessage(INFO, "Procédure de homing...");
        } else {
            Serial.println("Erreur démarrage homing");
            transitionToState(State::ERROR);
            return true;
        }
    }
    
    // Surveiller le homing
    if (sureServo) {
        switch (sureServo->getHomingState()) {
            case ServoDrive::OperationState::DRIVE_IN_PROGRESS:
                static uint32_t lastHomingMsg = 0;
                if (now - lastHomingMsg > 3000) {
                    Serial.println("Homing en cours...");
                    lastHomingMsg = now;
                }
                break;
                
            case ServoDrive::OperationState::DRIVE_SUCCESS:
                Serial.println("Homing terminé avec succès");
                BorneUniverselle::prepareMessage(SUCCESS, "Homing réussi");
                homingStarted = false;
                
                // Sauvegarder la position home si nécessaire
                uint32_t homePosition = sureServo->getPosition();
                saveHomePosition(homePosition);
                
                transitionToState(State::IDLE);
                break;
                
            case ServoDrive::OperationState::DRIVE_FAILED:
                Serial.printf("Homing échoué: %s\n", sureServo->getLastError());
                BorneUniverselle::prepareMessage(ERROR, "Échec homing");
                homingStarted = false;
                transitionToState(State::ERROR);
                break;
                
            case ServoDrive::OperationState::DRIVE_TIMEOUT:
                Serial.println("Timeout homing");
                BorneUniverselle::prepareMessage(ERROR, "Timeout homing");
                homingStarted = false;
                transitionToState(State::ERROR);
                break;
        }
    }
    
    return true;
}
```

## Gestion des Alarmes dans le PLC

### 1. Surveillance Continue

```cpp
bool MaClasseMetier::handleIdleState(uint32_t now) {
    // Vérification des alarmes
    checkServoAlarms();
    
    // Traitement des commandes utilisateur
    handleUserCommands();
    
    // Transitions d'état basées sur les entrées
    if (startButton->getValue()) {
        if (sureServo && sureServo->getIsReady()) {
            transitionToState(State::MOVING);
        } else {
            BorneUniverselle::prepareMessage(ERROR, "Servo non prêt");
        }
    }
    
    if (homeButton->getValue()) {
        transitionToState(State::HOMING);
    }
    
    return true;
}

void checkServoAlarms() {
    if (sureServo && sureServo->getHasAlarms()) {
        uint16_t alarmCode = sureServo->getAlarmCode();
        const char* description = sureServo->getAlarmDescription();
        const char* action = sureServo->getAlarmAction();
        
        // Message pour l'interface utilisateur
        char alarmMsg[256];
        snprintf(alarmMsg, sizeof(alarmMsg), 
                "ALARME SERVO: %s (0x%04X) - %s", 
                description, alarmCode, action);
        BorneUniverselle::prepareMessage(ERROR, alarmMsg);
        
        // Log diagnostic
        char diagnosticMsg[300];
        snprintf(diagnosticMsg, sizeof(diagnosticMsg),
                "SERVO_ALARM_%04X: %s - %s",
                alarmCode, description, action);
        PLC_Tools::logDiagnostic(diagnosticMsg);
        
        // Transition vers état d'alarme
        transitionToState(State::ALARM);
    }
}
```

### 2. Reset des Alarmes

```cpp
bool MaClasseMetier::handleAlarmState(uint32_t now) {
    static bool resetStarted = false;
    
    // Bouton de reset des alarmes
    if (resetAlarmsButton->getValue() && !resetStarted) {
        if (sureServo && sureServo->startReset()) {
            resetStarted = true;
            Serial.println("Reset des alarmes démarré");
            BorneUniverselle::prepareMessage(INFO, "Reset des alarmes...");
        } else {
            BorneUniverselle::prepareMessage(ERROR, "Impossible de démarrer le reset");
        }
    }
    
    if (resetStarted && sureServo) {
        switch (sureServo->getResetState()) {
            case ServoDrive::OperationState::DRIVE_SUCCESS:
                Serial.println("Reset des alarmes réussi");
                BorneUniverselle::prepareMessage(SUCCESS, "Alarmes réinitialisées");
                resetStarted = false;
                transitionToState(State::IDLE);
                break;
                
            case ServoDrive::OperationState::DRIVE_FAILED:
                Serial.println("Échec reset des alarmes");
                BorneUniverselle::prepareMessage(ERROR, "Échec reset alarmes");
                resetStarted = false;
                break;
                
            case ServoDrive::OperationState::DRIVE_TIMEOUT:
                Serial.println("Timeout reset des alarmes");
                BorneUniverselle::prepareMessage(ERROR, "Timeout reset");
                resetStarted = false;
                break;
        }
    }
    
    return true;
}
```

### 3. Table des Alarmes SureServo

La classe SureServo inclut une table complète des alarmes :

| Code    | Description | Action Recommandée |
|---------|-------------|-------------------|
| `0x2110` | Surtension | Vérifier tension d'alimentation |
| `0x2120` | Sous-tension | Vérifier tension d'alimentation |
| `0x3110` | Survitesse | Réduire vitesse/vérifier paramètres |
| `0x4110` | Erreur encodeur | Vérifier connexion encodeur |
| `0x6010` | Arrêt d'urgence | Désactiver arrêt d'urgence |
| ... | ... | ... |

> **Note :** La table complète contient plus de 25 codes d'alarme avec descriptions et actions détaillées.

## Contrôles Manuels (JOG)

```cpp
void handleUserCommands() {
    // Contrôles de JOG
    if (jogForwardButton->getIsChanged()) {
        if (jogForwardButton->getValue()) {
            if (sureServo) sureServo->jogForward();
        } else {
            if (sureServo) sureServo->jogStop();
        }
    }
    
    if (jogReverseButton->getIsChanged()) {
        if (jogReverseButton->getValue()) {
            if (sureServo) sureServo->jogReverse();
        } else {
            if (sureServo) sureServo->jogStop();
        }
    }
    
    // Changement de vitesse de jog
    if (jogSpeedNode->getIsChanged()) {
        if (sureServo) {
            sureServo->setJogSpeed(jogSpeedNode->getValue());
        }
    }
    
    // Activation/désactivation servo
    if (servoEnableButton->getIsChanged()) {
        if (sureServo) {
            sureServo->setServoEnabled(servoEnableButton->getValue());
        }
    }
}
```

## Configuration et Paramètres

### 1. Configuration des Vitesses

```cpp
void configureServoSpeeds() {
    if (sureServo) {
        // Vitesse de jog (tours/minute)
        sureServo->setJogSpeed(1000);
        
        // Vitesse de cycle selon table SureServo (0-15)
        sureServo->setCycleSpeed(8);  // Vitesse moyenne
        
        // Couple maximum
        sureServo->setMaxTorque(80);  // 80%
        
        Serial.println("Vitesses configurées");
    }
}
```

### 2. Méthodes Spécifiques SureServo

```cpp
// Vitesse PR directement selon table servo drive (0-15)
sureServo->setPrSpeed(10);

// États détaillés du servo
bool ready = sureServo->getServoReady();
bool activated = sureServo->getServoActivated();
bool zeroSpeed = sureServo->getZeroSpeed();
bool targetReached = sureServo->getTargetPositionReached();
bool homeDone = sureServo->getHomeDone();

// Système de coordonnées absolues
bool posLost = sureServo->getAbsolutePositionLost();
bool batteryAlarm = sureServo->getBatteryAlarm();
bool puuOverflow = sureServo->getPuuOverflow();
```

### 3. Sauvegarde des Paramètres

```cpp
void saveServoParameters() {
    if (sureServo) {
        if (sureServo->saveParameters()) {
            BorneUniverselle::prepareMessage(SUCCESS, "Paramètres sauvegardés");
        } else {
            BorneUniverselle::prepareMessage(ERROR, "Échec sauvegarde");
        }
    }
}
```

## Débogage et Diagnostics

### 1. Méthodes de Debug Intégrées

```cpp
void debugServoStatus() {
    if (sureServo) {
        // Affichage de l'état complet
        sureServo->printStatus();
        
        // Affichage des alarmes actives
        sureServo->printAlarms();
        
        // Affichage de la configuration
        sureServo->printConfiguration();
    }
}
```

### 2. Surveillance Continue

```cpp
void monitorServo() {
    static uint32_t lastCheck = 0;
    static bool wasMoving = false;
    static bool hadAlarms = false;
    
    uint32_t now = millis();
    
    // Vérification toutes les secondes
    if (now - lastCheck > 1000) {
        bool isMoving = sureServo->getIsMoving();
        bool hasAlarms = sureServo->getHasAlarms();
        
        // Détecter changement d'état de mouvement
        if (isMoving != wasMoving) {
            Serial.printf("Mouvement : %s\n", isMoving ? "DÉMARRÉ" : "ARRÊTÉ");
            wasMoving = isMoving;
        }
        
        // Détecter nouvelles alarmes
        if (hasAlarms != hadAlarms) {
            if (hasAlarms) {
                Serial.printf("NOUVELLE ALARME : 0x%04X - %s\n", 
                             sureServo->getAlarmCode(), 
                             sureServo->getAlarmDescription());
            } else {
                Serial.println("Alarmes résolues");
            }
            hadAlarms = hasAlarms;
        }
        
        lastCheck = now;
    }
}
```

### 3. Diagnostic des États

```cpp
void checkServoStatus() {
    if (sureServo) {
        Serial.printf("=== SERVO STATUS ===\n");
        Serial.printf("Position: %lu PUU\n", sureServo->getPosition());
        Serial.printf("Ready: %s\n", sureServo->getIsReady() ? "YES" : "NO");
        Serial.printf("Moving: %s\n", sureServo->getIsMoving() ? "YES" : "NO");
        Serial.printf("Initialized: %s\n", sureServo->getIsInitialized() ? "YES" : "NO");
        
        // États détaillés SureServo
        Serial.printf("Servo activé : %s\n", sureServo->getServoActivated() ? "YES" : "NO");
        Serial.printf("Position cible atteinte : %s\n", sureServo->getTargetPositionReached() ? "YES" : "NO");
        Serial.printf("Home terminé : %s\n", sureServo->getHomeDone() ? "YES" : "NO");
        
        if (sureServo->getHasAlarms()) {
            Serial.printf("ALARM: 0x%04X - %s\n", 
                         sureServo->getAlarmCode(),
                         sureServo->getAlarmDescription());
        }
        Serial.printf("====================\n");
    }
}
```

## Exemple Complet d'Intégration

```cpp
// Extrait de Formaca2.cpp (exemple réel du code fourni)
bool Formaca2::logicExecutor() {
    uint32_t now = millis();

    // OBLIGATOIRE : Traitement du SureServo
    if (sureServo) {
        sureServo->process(); // Cette ligne fait tout le travail !
    }
    
    // Vérification critique du système
    if (BorneUniverselle::getInstance()->isPlcBroken()) {
        Serial.printf("PLC broken, exiting\n");
        return false;
    }

    // Gestion des interruptions et conditions d'urgence
    handleInterrupts();
    updateOutputs();

    // Machine à états principale
    State currentFormacaState = getCurrentFormacaState();
    
    switch (currentFormacaState) {
        case State::INITIALIZING:
            return handleInitialisingState(now);

        case State::IDLE:
            return handleIdleState(now);

        case State::MOVING:
            return handleMovingState(now);

        case State::HOMING:
            return handleHomingState(now);
            
        // ... autres états

        default:
            Serial.printf("État inconnu: %s\n", 
                         stateToString(static_cast<int>(currentFormacaState)));
            BorneUniverselle::getInstance()->setPlcBroken("État machine inconnu");
            return false;
    }
    
    return true;
}
```

### Initialisation Complète dans Formaca2

```cpp
bool Formaca2::initializePointers(const char* contexte) {
    // ... initialisation des nodes avec initNodePtr()
    
    Serial.println("=== CRÉATION SURESERVO ===");
    
    // Configuration de la structure des nodes pour SureServo
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
    
    // === NODES OPTIONNELS ===
    servoNodes.servoReady = servoReady;
    servoNodes.servoActivated = servoActivated;
    // ... autres nodes optionnels
    
    // Création du SureServo
    sureServo = std::make_unique<SureServo>(PULSES_PER_INCH, servoNodes);
    
    if (!sureServo) {
        BorneUniverselle::getInstance()->setPlcBroken("Échec création SureServo");
        return false;
    }
    
    // Vérification de la création
    const char* lastError = sureServo->getLastError();
    if (lastError && strlen(lastError) > 0 && strcmp(lastError, "Pas d'erreur") != 0) {
        String errorMsg = "Erreur SureServo: ";
        errorMsg += lastError;
        BorneUniverselle::getInstance()->setPlcBroken(errorMsg.c_str());
        Serial.println(errorMsg);
        return false;
    }
    
    Serial.println("✅ SureServo créé avec succès");
    
    // Test basique de fonctionnement
    sureServo->printConfiguration();
    
    return true;
}
```

## Points d'Attention Spécifiques au PLC

### ⚠️ Obligations Critiques

#### Appel de `process()`
```cpp
// OBLIGATOIRE dans logicExecutor() - Le PLC ne fonctionne pas sans cela
if (sureServo) {
    sureServo->process();
}
```

#### Valeurs de Retour de `logicExecutor()`
- **`return false`** → **Arrêt définitif du PLC**
- **`return true`** → **Continuation normale**

```cpp
bool MaClasse::logicExecutor() {
    // Traitement normal
    sureServo->process();
    
    // Erreur récupérable - continuer
    if (errorRecuperable) {
        handleError();
        return true; // ✅ PLC continue
    }
    
    // Erreur fatale - arrêter le PLC
    if (errorFatale) {
        BorneUniverselle::getInstance()->setPlcBroken("Erreur critique");
        return false; // ❌ PLC s'arrête
    }
    
    return true; // ✅ Fonctionnement normal
}
```

#### Gestion d'Erreurs PLC
```cpp
// Pour les erreurs fatales système
BorneUniverselle::getInstance()->setPlcBroken("Raison de l'arrêt");

// Pour les messages utilisateur
BorneUniverselle::prepareMessage(ERROR, "Message d'erreur");
BorneUniverselle::prepareMessage(SUCCESS, "Opération réussie");
BorneUniverselle::prepareMessage(INFO, "Information");
BorneUniverselle::prepareMessage(WARNING, "Avertissement");

// Pour la traçabilité technique
PLC_Tools::logDiagnostic("Message de diagnostic technique");
```

### ⚠️ Intégration avec BusinessLogic

#### Transitions d'État
```cpp
// Utiliser la méthode héritée de BusinessLogic
transitionToState(State::MOVING);

// Obtenir l'état actuel
State current = static_cast<State>(getCurrentState());
State previous = static_cast<State>(getPreviousState());
```

#### Hooks de Transition
```cpp
void onStateEnter(int newState, int oldState) override {
    State formacaNewState = static_cast<State>(newState);
    
    switch (formacaNewState) {
        case State::INITIALIZING:
            // Réinitialiser les variables d'état
            initStarted = false;
            break;
    }
}
```

### ⚠️ Performance et Timing PLC

#### Cycle PLC Recommandé
- **Fréquence** : 10-50ms
- **`logicExecutor()` doit être rapide** : < 10ms idéalement
- **Pas d'opérations bloquantes** : Tout doit être non-bloquant

```cpp
bool MaClasse::logicExecutor() {
    uint32_t startTime = millis();
    
    // Votre traitement...
    sureServo->process();
    
    // Vérification performance (debug uniquement)
    uint32_t executionTime = millis() - startTime;
    if (executionTime > 20) {
        Serial.printf("⚠️ logicExecutor() lent: %lu ms\n", executionTime);
    }
    
    return true;
}
```

#### Gestion Mémoire
```cpp
// ✅ Bon : Utilisation de std::unique_ptr
std::unique_ptr<SureServo> sureServo = std::make_unique<SureServo>(10000, nodes);

// ❌ Éviter : Allocations fréquentes dans logicExecutor()
bool logicExecutor() {
    // Ne pas faire d'allocations ici !
    // String message = "Long message..."; // ❌ Éviter
    
    sureServo->process();
    return true;
}

// ✅ Bon : Variables statiques ou membres de classe
bool logicExecutor() {
    static char message[100]; // ✅ Allocation unique
    snprintf(message, sizeof(message), "Position: %lu", sureServo->getPosition());
    return true;
}
```

### ⚠️ Debugging et Logging

#### Logging Approprié
```cpp
// ✅ Pour le développement et debug
Serial.printf("Debug: Position actuelle = %lu\n", position);

// ✅ Pour la traçabilité production
PLC_Tools::logDiagnostic("SERVO_INIT_SUCCESS: Servo ready");

// ✅ Pour les messages utilisateur
BorneUniverselle::prepareMessage(INFO, "Servo initialisé");

// ❌ Éviter : Logs trop fréquents en production
bool logicExecutor() {
    Serial.println("logicExecutor called"); // ❌ Spam inutile
    return true;
}
```

#### Gestion des Timeouts
```cpp
// ✅ Toujours implémenter des timeouts manuels
bool handleMovingState(uint32_t now) {
    static uint32_t moveStartTime = 0;
    const uint32_t MOVE_TIMEOUT_MS = 30000; // 30 secondes
    
    if (moveStartTime == 0) {
        moveStartTime = now;
    }
    
    // Vérification timeout manuel
    if (now - moveStartTime > MOVE_TIMEOUT_MS) {
        Serial.println("Timeout mouvement manuel");
        transitionToState(State::ERROR);
        moveStartTime = 0; // Reset
        return true;
    }
    
    // ... logique de mouvement
}
```

## Constantes et Configuration

### Paramètres SureServo Prédéfinis

```cpp
// Valeurs de commande JOG
static constexpr uint16_t FWD_VALUE = 4998;
static constexpr uint16_t RWD_VALUE = 4999;
static constexpr uint16_t STOP_VALUE = 0;

// Paramètres de mouvement
static constexpr uint32_t TOLERANCE_PUU = 50;
static constexpr uint32_t MOVE_TIMEOUT_MS = 30000;
static constexpr uint32_t RESET_TIMEOUT_MS = 5000;
static constexpr uint32_t INIT_TIMEOUT_MS = 15000;

// Valeurs PR (Program Register)
static constexpr uint8_t PR1_START = 1;
static constexpr uint8_t PR2_START = 2;
static constexpr uint8_t PR3_START = 3;
static constexpr uint16_t PR2_DONE = 20002;
```

### Masques de Bits pour Status

```cpp
// Registre Status principal
#define STATUS_SERVO_READY          0x0001  // Bit 0
#define STATUS_SERVO_ACTIVATED      0x0002  // Bit 1
#define STATUS_ZERO_SPEED           0x0004  // Bit 2
#define STATUS_TARGET_SPEED_RATED   0x0008  // Bit 3
#define STATUS_TARGET_REACHED       0x0010  // Bit 4
#define STATUS_SERVO_ALARM          0x0040  // Bit 6
#define STATUS_HOME_DONE            0x0100  // Bit 8

// Registre Système de Coordonnées Absolues
#define COORD_ABS_POSITION_LOST     0x0001  // Bit 0
#define COORD_BATTERY_ALARM         0x0002  // Bit 1
#define COORD_MULTI_TURNS_OVERFLOW  0x0004  // Bit 2
#define COORD_PUU_OVERFLOW          0x0008  // Bit 3
#define COORD_ABS_NOT_SET           0x0010  // Bit 4
```

## Résolution de Problèmes

### Problèmes Courants

#### 1. Servo non initialisé
```cpp
// Symptôme : getIsReady() retourne false
// Solution : Vérifier l'initialisation
if (!sureServo->getIsInitialized()) {
    Serial.println("Servo non initialisé - démarrer l'initialisation");
    sureServo->startInitialize();
}
```

#### 2. Nodes manquants
```cpp
// Symptôme : Erreur lors de la création SureServo
// Solution : Vérifier la structure ServoNodes
if (!servoNodes.validateRequired()) {
    Serial.println("Nodes obligatoires manquants");
    // Diagnostic détaillé disponible dans les logs
}
```

#### 3. Mouvements qui ne démarrent pas
```cpp
// Vérifications à effectuer :
if (!sureServo->getIsReady()) {
    Serial.println("Servo non prêt");
}
if (sureServo->getHasAlarms()) {
    Serial.printf("Alarme active : 0x%04X\n", sureServo->getAlarmCode());
}
if (sureServo->getIsMoving()) {
    Serial.println("Mouvement déjà en cours");
}
```

#### 4. Alarmes récurrentes
```cpp
// Diagnostic approfondi des alarmes
void diagnoseAlarms() {
    if (sureServo->getHasAlarms()) {
        uint16_t code = sureServo->getAlarmCode();
        const char* desc = sureServo->getAlarmDescription();
        const char* action = sureServo->getAlarmAction();
        
        Serial.printf("=== DIAGNOSTIC ALARME ===\n");
        Serial.printf("Code: 0x%04X\n", code);
        Serial.printf("Description: %s\n", desc);
        Serial.printf("Action: %s\n", action);
        
        // Vérifications supplémentaires selon le type d'alarme
        switch (code & 0xF000) {
            case 0x2000: // Erreurs d'alimentation
                Serial.println("Vérifier l'alimentation électrique");
                break;
            case 0x3000: // Erreurs de contrôle
                Serial.println("Vérifier les paramètres de mouvement");
                break;
            case 0x4000: // Erreurs encodeur/moteur
                Serial.println("Vérifier les connexions mécaniques");
                break;
        }
    }
}
```

### Outils de Diagnostic

#### 1. Status Complet
```cpp
void fullDiagnostic() {
    if (sureServo) {
        Serial.println("=== DIAGNOSTIC COMPLET SURESERVO ===");
        
        // Configuration
        sureServo->printConfiguration();
        
        // État actuel
        sureServo->printStatus();
        
        // Alarmes
        sureServo->printAlarms();
        
        // États des opérations
        Serial.printf("Init State: %d\n", static_cast<int>(sureServo->getInitializeState()));
        Serial.printf("Move State: %d\n", static_cast<int>(sureServo->getMoveState()));
        Serial.printf("Homing State: %d\n", static_cast<int>(sureServo->getHomingState()));
        Serial.printf("Reset State: %d\n", static_cast<int>(sureServo->getResetState()));
        
        Serial.println("=====================================");
    }
}
```

#### 2. Monitoring Continu
```cpp
void continuousMonitoring() {
    static uint32_t lastMonitor = 0;
    uint32_t now = millis();
    
    if (now - lastMonitor > 5000) { // Toutes les 5 secondes
        Serial.printf("Monitor: Pos=%lu, Ready=%s, Moving=%s, Alarms=%s\n",
                     sureServo->getPosition(),
                     sureServo->getIsReady() ? "Y" : "N",
                     sureServo->getIsMoving() ? "Y" : "N",
                     sureServo->getHasAlarms() ? "Y" : "N");
        lastMonitor = now;
    }
}
```

## Bonnes Pratiques

### ✅ À Faire

1. **Toujours appeler `process()`** dans `logicExecutor()`
2. **Vérifier les états** avant de démarrer des opérations
3. **Implémenter des timeouts** pour toutes les opérations
4. **Gérer les alarmes** de manière proactive
5. **Utiliser les logs** pour la traçabilité
6. **Tester la validation** des nodes lors de l'initialisation

### ❌ À Éviter

1. **Opérations bloquantes** dans `logicExecutor()`
2. **Ignorer les états d'erreur** des opérations
3. **Allocations mémoire fréquentes** dans la boucle principale
4. **Logs excessifs** en production
5. **Retourner `false`** dans `logicExecutor()` pour des erreurs mineures
6. **Oublier de vérifier** `isPlcBroken()` régulièrement

---

> **Note :** Cette documentation est basée sur l'implémentation réelle de la classe SureServo dans le projet Formaca2. Pour des exemples concrets d'utilisation, consultez le fichier `Formaca2.cpp` qui montre une intégration complète dans un environnement de production.
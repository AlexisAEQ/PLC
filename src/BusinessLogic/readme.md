# Documentation BusinessLogic - Guide Développeur

## Table des Matières

1. [Vue d'ensemble](#vue-densemble)
2. [Design Patterns Implémentés](#design-patterns-implémentés)
3. [Architecture de Classe](#architecture-de-classe)
4. [Création d'une Nouvelle Application](#création-dune-nouvelle-application)
5. [Gestion des Transitions](#gestion-des-transitions)
6. [Handlers d'État](#handlers-détat)
7. [Gestion des Nœuds](#gestion-des-nœuds)
8. [Callbacks et Observers](#callbacks-et-observers)
9. [Exemple Complet](#exemple-complet)
10. [Bonnes Pratiques](#bonnes-pratiques)
11. [Gestion d'Erreurs](#gestion-derreurs)
12. [Débogage et Diagnostics](#débogage-et-diagnostics)

## Vue d'ensemble

La classe `BusinessLogic` est une classe abstraite qui fournit une infrastructure robuste pour la gestion d'états dans les applications PLC. Elle implémente plusieurs design patterns pour offrir une base solide et extensible pour développer de nouvelles logiques métier.

### Caractéristiques principales

- **Gestion d'états centralisée** avec machine à états finie
- **Hooks personnalisables** pour la logique spécifique
- **Système de callbacks** pour la notification des changements
- **Gestion des timeouts** intégrée
- **Validation automatique des types** de nœuds
- **Logging et debugging** intégrés

## Design Patterns Implémentés

### 🎯 State Pattern
Gestion centralisée des états avec transitions contrôlées et hooks personnalisables.

### 🏗️ Template Method Pattern
Structure commune avec points d'extension pour les classes dérivées.

### 🔌 Strategy Pattern
Les classes dérivées implémentent leur propre logique d'état.

### 👁️ Observer Pattern
Système de callbacks pour notifier les changements d'état.

## Architecture de Classe

```cpp
class BusinessLogic {
public:
    // ==========================================
    // INTERFACE À IMPLÉMENTER (ABSTRACT)
    // ==========================================
    virtual const char* stateToString(int state) const = 0;
    
    // ==========================================
    // GESTION D'ÉTAT (TEMPLATE METHOD)
    // ==========================================
    virtual void transition(int newState);
    virtual void printCurrentState();
    void resetStatePrintFlag();
    
    // ==========================================
    // GETTERS PUBLICS
    // ==========================================
    int getCurrentState() const;
    int getPreviousState() const;
    uint32_t getStateStartTime();
    uint32_t getTimeInCurrentState();
    
    // ==========================================
    // OBSERVER PATTERN
    // ==========================================
    void registerTransitionCallback(std::function<void(int, int)> callback);

protected:
    // ==========================================
    // VARIABLES D'ÉTAT PROTÉGÉES
    // ==========================================
    int currentState = 0;
    int previousState = 0;
    uint32_t stateStartTime = 0;
    bool stateAlreadyPrinted = false;

    // ==========================================
    // HOOKS VIRTUELS (STRATEGY PATTERN)
    // ==========================================
    virtual void onStateEnter(int newState, int oldState) {}
    virtual void onStateExit(int oldState, int newState) {}
    virtual void onTransitionComplete(int newState, int oldState) {}
    
    // ==========================================
    // UTILITAIRES PROTÉGÉS
    // ==========================================
    bool isStateTimeoutExceeded(uint32_t timeoutMs) const;
    bool hasBeenInStateFor(uint32_t durationMs) const;
    bool transitionWithTimeout(int newState, uint32_t currentTimeoutMs = 0, const char* reason = nullptr);
    void logStateTransition(int fromState, int toState, const char* reason = nullptr);
    void recordTransition(const char* reason = nullptr);
    
    // ==========================================
    // GESTION DES NŒUDS
    // ==========================================
    static std::vector<NodeInfo> nodeInfos;
    Node* getNodePointerForHash(uint32_t hash, const char* context);
    template<typename ExpectedType>
    ExpectedType* getTypedPointerForHash(uint32_t hash, const char* context);
};
```

## Création d'une Nouvelle Application

### 1. Définition des États

Créez une énumération fortement typée pour vos états :

```cpp
class MonApplication : public BusinessLogic {
public:
    // ==========================================
    // ÉNUMÉRATION DES ÉTATS
    // ==========================================
    enum class State : int {
        UNDEFINED = 0,
        INITIALIZING = 1,
        IDLE = 2,
        RUNNING = 3,
        PAUSED = 4,
        ERROR = 5,
        SHUTDOWN = 6
    };

private:
    // ==========================================
    // HELPERS POUR CONVERSION TYPE-SAFE
    // ==========================================
    State getCurrentAppState() const {
        return static_cast<State>(BusinessLogic::getCurrentState());
    }
    
    void transitionToState(State newState, const char* reason = nullptr) {
        if (reason) {
            logStateTransition(getCurrentState(), static_cast<int>(newState), reason);
        }
        BusinessLogic::transition(static_cast<int>(newState));
    }
};
```

### 2. Implémentation des Méthodes Abstraites

#### A. Conversion État → String

```cpp
const char* MonApplication::stateToString(int state) const {
    State appState = static_cast<State>(state);
    switch (appState) {
        case State::UNDEFINED:    return "UNDEFINED";
        case State::INITIALIZING: return "INITIALIZING";
        case State::IDLE:         return "IDLE";
        case State::RUNNING:      return "RUNNING";
        case State::PAUSED:       return "PAUSED";
        case State::ERROR:        return "ERROR";
        case State::SHUTDOWN:     return "SHUTDOWN";
        default:                  return "UNKNOWN";
    }
}

### 3. Constructeur et Initialisation

```cpp
// ==========================================
// CONSTRUCTEUR
// ==========================================
MonApplication::MonApplication() : BusinessLogic() {
    // Initialiser l'état au début
    currentState = static_cast<int>(State::UNDEFINED);
    previousState = static_cast<int>(State::UNDEFINED);
    stateStartTime = millis();
    
    // Configuration des observers
    setupStateObservers();
    
    // Variables spécifiques à l'application
    stopRequested = false;
    emergencyStopActive = false;
    manualResetRequested = false;
    currentProcessSpeed = DEFAULT_SPEED;
    
    Serial.println("MonApplication initialisée");
}
```

## Gestion des Transitions

### 1. Hooks d'État

#### A. Entrée dans un État

```cpp
void MonApplication::onStateEnter(int newState, int oldState) {
    State newAppState = static_cast<State>(newState);
    State oldAppState = static_cast<State>(oldState);
    
    switch (newAppState) {
        case State::INITIALIZING:
            Serial.println("🔧 Début initialisation");
            initializationStartTime = millis();
            resetInternalVariables();
            break;
            
        case State::RUNNING:
            Serial.println("▶️ Démarrage du processus");
            processStartTime = millis();
            stopRequested = false;
            startProcess();
            break;
            
        case State::ERROR:
            Serial.printf("🚨 Erreur détectée depuis %s\n", stateToString(oldState));
            errorStartTime = millis();
            handleError();
            activateErrorIndicators();
            break;
            
        case State::IDLE:
            Serial.println("⏸️ Mode attente");
            if (oldAppState == State::ERROR) {
                Serial.println("✅ Récupération après erreur");
                clearErrorIndicators();
            }
            if (oldAppState == State::RUNNING) {
                uint32_t runDuration = millis() - processStartTime;
                Serial.printf("📊 Processus terminé après %lu ms\n", runDuration);
            }
            break;
            
        case State::PAUSED:
            Serial.println("⏸️ Processus en pause");
            pauseStartTime = millis();
            pauseProcess();
            break;
            
        case State::SHUTDOWN:
            Serial.println("🔌 Arrêt du système");
            shutdownSequence();
            break;
    }
    
    // Mise à jour de l'interface pour tous les états
    updateStateDisplay();
}
```

#### B. Sortie d'un État

```cpp
void MonApplication::onStateExit(int oldState, int newState) {
    State oldAppState = static_cast<State>(oldState);
    State newAppState = static_cast<State>(newState);
    
    switch (oldAppState) {
        case State::RUNNING:
            Serial.println("⏹️ Arrêt du processus");
            stopProcess();
            break;
            
        case State::ERROR:
            Serial.println("🔄 Sortie du mode erreur");
            clearErrorFlags();
            break;
            
        case State::PAUSED:
            Serial.println("▶️ Sortie de pause");
            uint32_t pauseDuration = millis() - pauseStartTime;
            Serial.printf("📊 Pause durée: %lu ms\n", pauseDuration);
            break;
    }
}
```

#### C. Fin de Transition

```cpp
void MonApplication::onTransitionComplete(int newState, int oldState) {
    // Log spécial pour certaines transitions critiques
    State newAppState = static_cast<State>(newState);
    State oldAppState = static_cast<State>(oldState);
    
    if (newAppState == State::ERROR || oldAppState == State::ERROR) {
        char criticalMsg[200];
        snprintf(criticalMsg, sizeof(criticalMsg),
                "TRANSITION_CRITIQUE: %s → %s @ %lu ms",
                stateToString(oldState), stateToString(newState), millis());
        PLC_Tools::logDiagnostic(criticalMsg);
    }
    
    // Mise à jour finale de l'interface
    finalizeStateTransition();
}
```

## Handlers d'État

### 1. Machine à États Principale

```cpp
bool MonApplication::logicExecutor() {
    uint32_t now = millis();
    
    // Vérification des conditions d'urgence globales
    checkEmergencyConditions();
    
    // Traitement des interfaces utilisateur
    handleUserInterface();
    
    // Machine à états principale
    switch (getCurrentAppState()) {
        case State::UNDEFINED:
            Serial.println("État UNDEFINED détecté");
            transitionToState(State::INITIALIZING, "Démarrage initial");
            break;
            
        case State::INITIALIZING:
            if (handleInitializingState(now)) {
                transitionToState(State::IDLE, "Initialisation terminée");
            }
            break;
            
        case State::IDLE:
            handleIdleState(now);
            break;
            
        case State::RUNNING:
            if (!handleRunningState(now)) {
                transitionToState(State::ERROR, "Erreur pendant exécution");
                return false; // Erreur critique
            }
            break;
            
        case State::PAUSED:
            handlePausedState(now);
            break;
            
        case State::ERROR:
            handleErrorState(now);
            break;
            
        case State::SHUTDOWN:
            return handleShutdownState(now);
            
        default:
            Serial.printf("État inconnu: %d\n", getCurrentState());
            transitionToState(State::ERROR, "État inconnu");
            return false;
    }
    
    // Mise à jour des sorties
    updateOutputs();
    
    return true; // Succès
}
```

### 2. Handlers Spécifiques avec Timeouts

```cpp
bool MonApplication::handleInitializingState(uint32_t now) {
    static constexpr uint32_t INIT_TIMEOUT_MS = 30000; // 30 secondes
    static uint8_t initPhase = 0;
    static uint32_t phaseStartTime = 0;
    
    // Vérification timeout global
    if (isStateTimeoutExceeded(INIT_TIMEOUT_MS)) {
        Serial.println("❌ Timeout initialisation");
        transitionToState(State::ERROR, "Timeout initialisation");
        return false;
    }
    
    switch (initPhase) {
        case 0: // Phase matériel
            Serial.println("🔧 Phase 0: Initialisation matériel");
            phaseStartTime = now;
            if (initializeHardware()) {
                initPhase = 1;
            } else if (now - phaseStartTime > 10000) {
                Serial.println("❌ Échec initialisation matériel");
                transitionToState(State::ERROR, "Échec init matériel");
                return false;
            }
            break;
            
        case 1: // Phase logiciel
            Serial.println("🔧 Phase 1: Initialisation logiciel");
            if (initializeSoftware()) {
                initPhase = 2;
            } else if (now - phaseStartTime > 5000) {
                Serial.println("❌ Échec initialisation logiciel");
                transitionToState(State::ERROR, "Échec init logiciel");
                return false;
            }
            break;
            
        case 2: // Vérification finale
            Serial.println("🔧 Phase 2: Vérification finale");
            if (verifySystemReady()) {
                Serial.println("✅ Initialisation terminée");
                initPhase = 0; // Reset pour prochaine fois
                return true; // Prêt pour transition
            }
            break;
    }
    
    // Affichage périodique du progrès
    static uint32_t lastProgressMsg = 0;
    if (now - lastProgressMsg > 5000) {
        Serial.printf("🔄 Initialisation phase %d... (%lu ms)\n", 
                     initPhase, getTimeInCurrentState());
        lastProgressMsg = now;
    }
    
    return false; // Pas encore prêt
}

bool MonApplication::handleRunningState(uint32_t now) {
    // Vérification des conditions d'arrêt
    if (stopRequested) {
        Serial.println("⏹️ Arrêt demandé");
        transitionToState(State::IDLE, "Arrêt utilisateur");
        return true;
    }
    
    // Vérification des erreurs
    if (checkErrorConditions()) {
        Serial.println("❌ Condition d'erreur détectée");
        transitionToState(State::ERROR, "Condition d'erreur");
        return false;
    }
    
    // Gestion de la pause
    if (pauseRequested) {
        Serial.println("⏸️ Pause demandée");
        transitionToState(State::PAUSED, "Pause utilisateur");
        return true;
    }
    
    // Logique principale du processus
    if (!processMainLogic()) {
        Serial.println("❌ Erreur dans la logique principale");
        return false;
    }
    
    // Vérification de fin de processus
    if (isProcessComplete()) {
        Serial.println("✅ Processus terminé avec succès");
        transitionToState(State::IDLE, "Processus terminé");
        return true;
    }
    
    // Mise à jour périodique du statut
    static uint32_t lastStatusUpdate = 0;
    if (now - lastStatusUpdate > 1000) {
        updateProcessStatus();
        lastStatusUpdate = now;
    }
    
    return true;
}

bool MonApplication::handleErrorState(uint32_t now) {
    static uint32_t lastRecoveryAttempt = 0;
    static uint8_t recoveryAttempts = 0;
    static constexpr uint8_t MAX_RECOVERY_ATTEMPTS = 3;
    static constexpr uint32_t RECOVERY_INTERVAL_MS = 10000; // 10 secondes
    
    // Tentative de récupération automatique
    if (hasBeenInStateFor(5000) && // Après 5 secondes
        now - lastRecoveryAttempt > RECOVERY_INTERVAL_MS &&
        recoveryAttempts < MAX_RECOVERY_ATTEMPTS) {
        
        Serial.printf("🔄 Tentative de récupération %d/%d\n", 
                     recoveryAttempts + 1, MAX_RECOVERY_ATTEMPTS);
        
        if (attemptErrorRecovery()) {
            Serial.println("✅ Récupération automatique réussie");
            recoveryAttempts = 0; // Reset compteur
            transitionToState(State::IDLE, "Récupération automatique");
            return true;
        } else {
            recoveryAttempts++;
            lastRecoveryAttempt = now;
            Serial.printf("❌ Échec récupération (tentative %d)\n", recoveryAttempts);
        }
    }
    
    // Affichage périodique de l'erreur
    static uint32_t lastErrorMsg = 0;
    if (now - lastErrorMsg > 15000) {
        if (recoveryAttempts >= MAX_RECOVERY_ATTEMPTS) {
            Serial.println("🚨 Système en erreur - intervention manuelle requise");
        } else {
            Serial.printf("⚠️ Système en erreur - prochaine tentative dans %lu s\n",
                         (RECOVERY_INTERVAL_MS - (now - lastRecoveryAttempt)) / 1000);
        }
        lastErrorMsg = now;
    }
    
    // Vérification des commandes de reset manuel
    if (manualResetRequested) {
        Serial.println("🔄 Reset manuel détecté");
        clearAllErrors();
        recoveryAttempts = 0; // Reset compteur
        manualResetRequested = false;
        transitionToState(State::INITIALIZING, "Reset manuel");
    }
    
    return true;
}

void MonApplication::handleIdleState(uint32_t now) {
    // Traitement des commandes utilisateur
    if (startRequested && isSystemReady()) {
        Serial.println("▶️ Démarrage demandé");
        startRequested = false;
        transitionToState(State::RUNNING, "Démarrage utilisateur");
        return;
    }
    
    // Vérification périodique du système
    static uint32_t lastSystemCheck = 0;
    if (now - lastSystemCheck > 30000) { // Toutes les 30 secondes
        if (!performSystemCheck()) {
            Serial.println("⚠️ Problème détecté lors de la vérification système");
            transitionToState(State::ERROR, "Échec vérification système");
        }
        lastSystemCheck = now;
    }
    
    // Mode économie d'énergie après inactivité prolongée
    if (hasBeenInStateFor(300000)) { // 5 minutes
        Serial.println("💤 Passage en mode économie d'énergie");
        enablePowerSaveMode();
    }
}
```

## Gestion des Nœuds

### 1. Initialisation des Pointeurs

```cpp
bool MonApplication::initializePointers(const char* context) {
    Serial.println("MonApplication::initializePointers - Début");
    
    // ==========================================
    // TEMPLATE HELPER POUR INITIALISATION SÉCURISÉE
    // ==========================================
    auto initNodePtr = [this, context](auto*& ptr, uint32_t hash, const char* name) -> bool {
        if (hash == 0) {
            Serial.printf("❌ Hash = 0 pour %s\n", name);
            BorneUniverselle::getInstance()->setPlcBroken("Hash invalide");
            return false;
        }
        
        auto tmpNode = BorneUniverselle::getInstance()->findNode(context, hash);
        if (tmpNode) {
            ptr = dynamic_cast<decltype(ptr)>(tmpNode);
            if (!ptr) {
                Serial.printf("❌ Type incorrect pour %s (attendu: %s)\n", 
                             name, typeid(decltype(ptr)).name());
                BorneUniverselle::getInstance()->setPlcBroken("Type de nœud incorrect");
                return false;
            }
            Serial.printf("✅ %s initialisé\n", name);
        } else {
            Serial.printf("❌ Nœud %s non trouvé (hash: %lu)\n", name, hash);
            BorneUniverselle::getInstance()->setPlcBroken("Nœud manquant");
            return false;
        }
        return true;
    };
    
    // ==========================================
    // INITIALISATION DES NŒUDS D'ENTRÉE
    // ==========================================
    Serial.println("Initialisation des nœuds d'entrée...");
    if (!initNodePtr(startButton, START_BUTTON_HASH, "startButton") ||
        !initNodePtr(stopButton, STOP_BUTTON_HASH, "stopButton") ||
        !initNodePtr(pauseButton, PAUSE_BUTTON_HASH, "pauseButton") ||
        !initNodePtr(emergencyStop, EMERGENCY_STOP_HASH, "emergencyStop") ||
        !initNodePtr(resetButton, RESET_BUTTON_HASH, "resetButton")) {
        return false;
    }
    
    // ==========================================
    // INITIALISATION DES NŒUDS DE SORTIE
    // ==========================================
    Serial.println("Initialisation des nœuds de sortie...");
    if (!initNodePtr(statusLed, STATUS_LED_HASH, "statusLed") ||
        !initNodePtr(runningLed, RUNNING_LED_HASH, "runningLed") ||
        !initNodePtr(errorLed, ERROR_LED_HASH, "errorLed") ||
        !initNodePtr(buzzer, BUZZER_HASH, "buzzer") ||
        !initNodePtr(outputRelay, OUTPUT_RELAY_HASH, "outputRelay")) {
        return false;
    }
    
    // ==========================================
    // INITIALISATION DES NŒUDS DE DONNÉES
    // ==========================================
    Serial.println("Initialisation des nœuds de données...");
    if (!initNodePtr(processSpeed, PROCESS_SPEED_HASH, "processSpeed") ||
        !initNodePtr(currentValue, CURRENT_VALUE_HASH, "currentValue") ||
        !initNodePtr(targetValue, TARGET_VALUE_HASH, "targetValue") ||
        !initNodePtr(statusText, STATUS_TEXT_HASH, "statusText")) {
        return false;
    }
    
    // ==========================================
    // VÉRIFICATION FINALE
    // ==========================================
    if (BorneUniverselle::getInstance()->isPlcBroken()) {
        Serial.println("❌ Erreur détectée pendant l'initialisation des pointeurs");
        return false;
    }
    
    Serial.println("✅ Tous les pointeurs initialisés avec succès");
    return true;
}
```

### 2. Traitement des Interfaces

```cpp
void MonApplication::handleUserInterface() {
    static uint32_t lastButtonPressTime = 0;
    static constexpr uint32_t BUTTON_DEBOUNCE_MS = 100;
    uint32_t now = millis();
    
    // Protection anti-rebond globale
    if (now - lastButtonPressTime < BUTTON_DEBOUNCE_MS) {
        return;
    }
    
    // ==========================================
    // BOUTON DE DÉMARRAGE
    // ==========================================
    if (startButton->getIsChanged() && startButton->getValue()) {
        lastButtonPressTime = now;
        
        if (getCurrentAppState() == State::IDLE) {
            Serial.println("▶️ Démarrage demandé par utilisateur");
            startRequested = true;
        } else if (getCurrentAppState() == State::PAUSED) {
            Serial.println("▶️ Reprise depuis pause");
            resumeRequested = true;
        } else {
            Serial.printf("⚠️ Démarrage impossible dans l'état %s\n", 
                         stateToString(getCurrentState()));
            activateBuzzer(500); // Signal sonore d'erreur
        }
    }
    
    // ==========================================
    // BOUTON D'ARRÊT
    // ==========================================
    if (stopButton->getIsChanged() && stopButton->getValue()) {
        lastButtonPressTime = now;
        
        if (getCurrentAppState() == State::RUNNING || 
            getCurrentAppState() == State::PAUSED) {
            Serial.println("⏹️ Arrêt demandé par utilisateur");
            stopRequested = true;
        }
    }
    
    // ==========================================
    // BOUTON DE PAUSE
    // ==========================================
    if (pauseButton->getIsChanged() && pauseButton->getValue()) {
        lastButtonPressTime = now;
        
        if (getCurrentAppState() == State::RUNNING) {
            Serial.println("⏸️ Pause demandée par utilisateur");
            pauseRequested = true;
        } else if (getCurrentAppState() == State::PAUSED) {
            Serial.println("▶️ Reprise demandée par utilisateur");
            resumeRequested = true;
        }
    }
    
    // ==========================================
    // BOUTON D'URGENCE
    // ==========================================
    if (emergencyStop->getIsChanged() && emergencyStop->getValue()) {
        lastButtonPressTime = now;
        Serial.println("🚨 ARRÊT D'URGENCE ACTIVÉ");
        emergencyStopActive = true;
        // Transition immédiate sans attendre le prochain cycle
        transitionToState(State::ERROR, "Arrêt d'urgence");
    }
    
    // ==========================================
    // BOUTON DE RESET
    // ==========================================
    if (resetButton->getIsChanged() && resetButton->getValue()) {
        lastButtonPressTime = now;
        
        if (getCurrentAppState() == State::ERROR) {
            Serial.println("🔄 Reset demandé par utilisateur");
            manualResetRequested = true;
        } else {
            Serial.println("ℹ️ Reset possible seulement en mode erreur");
        }
    }
    
    // ==========================================
    // TRAITEMENT DES VALEURS D'ENTRÉE
    // ==========================================
    if (processSpeed->getIsChanged()) {
        uint32_t newSpeed = processSpeed->getValue();
        if (newSpeed >= MIN_SPEED && newSpeed <= MAX_SPEED) {
            currentProcessSpeed = newSpeed;
            Serial.printf("🎛️ Nouvelle vitesse: %lu\n", newSpeed);
            
            // Appliquer immédiatement si en cours d'exécution
            if (getCurrentAppState() == State::RUNNING) {
                applyNewSpeed(newSpeed);
            }
        } else {
            Serial.printf("⚠️ Vitesse hors limites: %lu (min: %lu, max: %lu)\n", 
                         newSpeed, MIN_SPEED, MAX_SPEED);
            // Restaurer la valeur précédente
            processSpeed->setValue(currentProcessSpeed);
        }
    }
    
    if (targetValue->getIsChanged()) {
        float newTarget = targetValue->getValue();
        if (newTarget >= MIN_TARGET && newTarget <= MAX_TARGET) {
            currentTarget = newTarget;
            Serial.printf("🎯 Nouvelle cible: %.2f\n", newTarget);
        } else {
            Serial.printf("⚠️ Cible hors limites: %.2f (min: %.2f, max: %.2f)\n", 
                         newTarget, MIN_TARGET, MAX_TARGET);
            targetValue->setValue(currentTarget);
        }
    }
    
    // ==========================================
    // MISE À JOUR DES INDICATEURS
    // ==========================================
    updateStatusIndicators();
}

void MonApplication::updateStatusIndicators() {
    State currentAppState = getCurrentAppState();
    uint32_t now = millis();
    
    // ==========================================
    // GESTION DES LEDS
    // ==========================================
    switch (currentAppState) {
        case State::UNDEFINED:
        case State::INITIALIZING:
            // Clignotement pour états transitoires
            {
                bool blink = (now / 250) % 2; // Clignotement rapide
                statusLed->setValue(blink);
                runningLed->setValue(false);
                errorLed->setValue(false);
            }
            break;
            
        case State::IDLE:
            statusLed->setValue(true);
            runningLed->setValue(false);
            errorLed->setValue(false);
            break;
            
        case State::RUNNING:
            statusLed->setValue(false);
            runningLed->setValue(true);
            errorLed->setValue(false);
            break;
            
        case State::PAUSED:
            statusLed->setValue(false);
            {
                bool blink = (now / 500) % 2; // Clignotement lent
                runningLed->setValue(blink);
            }
            errorLed->setValue(false);
            break;
            
        case State::ERROR:
            statusLed->setValue(false);
            runningLed->setValue(false);
            errorLed->setValue(true);
            break;
            
        case State::SHUTDOWN:
            // Toutes les LEDs éteintes
            statusLed->setValue(false);
            runningLed->setValue(false);
            errorLed->setValue(false);
            break;
    }
    
    // ==========================================
    // GESTION DU BUZZER
    // ==========================================
    manageBuzzerState(currentAppState, now);
    
    // ==========================================
    // MISE À JOUR DES VALEURS DE SORTIE
    // ==========================================
    if (currentValue) {
        currentValue->setValue(getCurrentProcessValue());
    }
    
    if (statusText) {
        updateStatusText(currentAppState);
    }
    
    // ==========================================
    // GESTION DES RELAIS DE SORTIE
    // ==========================================
    if (outputRelay) {
        bool relayState = (currentAppState == State::RUNNING) && !emergencyStopActive;
        outputRelay->setValue(relayState);
    }
}

void MonApplication::manageBuzzerState(State currentAppState, uint32_t now) {
    static uint32_t lastBuzzerToggle = 0;
    static bool buzzerActive = false;
    static uint32_t buzzerEndTime = 0;
    
    // Buzzer temporaire (activé par activateBuzzer())
    if (buzzerEndTime > 0) {
        if (now < buzzerEndTime) {
            bool toggle = (now / 100) % 2; // Clignotement rapide
            buzzer->setValue(toggle);
            return;
        } else {
            buzzerEndTime = 0;
            buzzer->setValue(false);
        }
    }
    
    // Buzzer pour erreurs persistantes
    if (currentAppState == State::ERROR && !buzzerSilenced) {
        if (now - lastBuzzerToggle > 1000) {
            buzzerActive = !buzzerActive;
            buzzer->setValue(buzzerActive);
            lastBuzzerToggle = now;
        }
    } else {
        buzzer->setValue(false);
        buzzerActive = false;
    }
}

void MonApplication::updateStatusText(State currentAppState) {
    const char* statusMsg;
    
    switch (currentAppState) {
        case State::UNDEFINED:
            statusMsg = "Système non défini";
            break;
        case State::INITIALIZING:
            statusMsg = "Initialisation en cours...";
            break;
        case State::IDLE:
            statusMsg = "Prêt - En attente";
            break;
        case State::RUNNING:
            statusMsg = "Processus en cours";
            break;
        case State::PAUSED:
            statusMsg = "Processus en pause";
            break;
        case State::ERROR:
            statusMsg = "Erreur système";
            break;
        case State::SHUTDOWN:
            statusMsg = "Arrêt en cours";
            break;
        default:
            statusMsg = "État inconnu";
            break;
    }
    
    statusText->setValue(statusMsg);
}
```

## Callbacks et Observers

### 1. Configuration des Observers

```cpp
void MonApplication::setupStateObservers() {
    // ==========================================
    // CALLBACK PRINCIPAL DE TRANSITION
    // ==========================================
    registerTransitionCallback([this](int newState, int oldState) {
        // Log personnalisé avec horodatage
        logDetailedStateTransition(oldState, newState);
        
        // Notification à d'autres systèmes
        notifyExternalSystems(newState, oldState);
        
        // Mise à jour de l'interface
        updateUserInterface();
        
        // Sauvegarde de l'état si nécessaire
        if (shouldSaveState(newState)) {
            saveStateToMemory(newState);
        }
        
        // Métriques de performance
        updatePerformanceMetrics(newState, oldState);
    });
    
    // ==========================================
    // CALLBACK SPÉCIALISÉ POUR ERREURS
    // ==========================================
    registerTransitionCallback([this](int newState, int oldState) {
        State newAppState = static_cast<State>(newState);
        State oldAppState = static_cast<State>(oldState);
        
        if (newAppState == State::ERROR) {
            handleErrorTransition(oldAppState);
        } else if (oldAppState == State::ERROR && newAppState != State::ERROR) {
            handleErrorRecoveryTransition(newAppState);
        }
    });
}

void MonApplication::logDetailedStateTransition(int from, int to) {
    State fromState = static_cast<State>(from);
    State toState = static_cast<State>(to);
    
    // Log standard
    char message[300];
    snprintf(message, sizeof(message), 
             "TRANSITION: %s → %s (durée: %lu ms, cycle: %lu)",
             stateToString(from), stateToString(to), 
             getTimeInCurrentState(), transitionCounter++);
    
    PLC_Tools::logDiagnostic(message);
    
    // Log spécial pour transitions critiques
    if (toState == State::ERROR || fromState == State::ERROR) {
        char criticalMsg[400];
        snprintf(criticalMsg, sizeof(criticalMsg),
                "CRITICAL_TRANSITION: %s → %s @ %lu ms | Heap: %zu | Uptime: %lu",
                stateToString(from), stateToString(to), millis(),
                (size_t)ESP.getFreeHeap(), millis());
        PLC_Tools::logDiagnostic(criticalMsg);
    }
    
    // Historique des transitions (dernières 10)
    transitionHistory.push_back({fromState, toState, millis()});
    if (transitionHistory.size() > 10) {
        transitionHistory.erase(transitionHistory.begin());
    }
}

void MonApplication::handleErrorTransition(State fromState) {
    Serial.printf("🚨 Entrée en erreur depuis %s\n", stateToString(static_cast<int>(fromState)));
    
    // Sauvegarder le contexte d'erreur
    errorContext.previousState = fromState;
    errorContext.errorTime = millis();
    errorContext.systemState = captureSystemState();
    
    // Notifications d'urgence
    sendEmergencyNotification();
    
    // Activation des indicateurs d'erreur
    activateErrorIndicators();
}

void MonApplication::handleErrorRecoveryTransition(State toState) {
    Serial.printf("✅ Récupération d'erreur vers %s\n", stateToString(static_cast<int>(toState)));
    
    // Calculer la durée de l'erreur
    uint32_t errorDuration = millis() - errorContext.errorTime;
    
    // Log de récupération
    char recoveryMsg[200];
    snprintf(recoveryMsg, sizeof(recoveryMsg),
            "ERROR_RECOVERY: Durée %lu ms, de %s vers %s",
            errorDuration, 
            stateToString(static_cast<int>(errorContext.previousState)),
            stateToString(static_cast<int>(toState)));
    PLC_Tools::logDiagnostic(recoveryMsg);
    
    // Nettoyage du contexte d'erreur
    clearErrorContext();
    
    // Désactivation des indicateurs d'erreur
    clearErrorIndicators();
}
```

### 2. Gestion des Notifications

```cpp
void MonApplication::notifyExternalSystems(int newState, int oldState) {
    State newAppState = static_cast<State>(newState);
    
    // ==========================================
    // NOTIFICATION VIA INTERFACE WEB
    // ==========================================
    if (webInterface) {
        JsonDocument statusUpdate;
        statusUpdate["state"] = stateToString(newState);
        statusUpdate["timestamp"] = millis();
        statusUpdate["previousState"] = stateToString(oldState);
        
        webInterface->sendStatusUpdate(statusUpdate);
    }
    
    // ==========================================
    // NOTIFICATION VIA MODBUS (si configuré)
    // ==========================================
    if (modbusInterface && statusRegister) {
        statusRegister->setValue(static_cast<uint16_t>(newState));
    }
    
    // ==========================================
    // NOTIFICATION LOCALE (AUTRES MODULES)
    // ==========================================
    for (auto& observer : externalObservers) {
        observer->onStateChanged(newState, oldState);
    }
}

bool MonApplication::shouldSaveState(int state) {
    State appState = static_cast<State>(state);
    
    // Sauvegarder seulement les états importants
    return (appState == State::IDLE || 
            appState == State::ERROR || 
            appState == State::SHUTDOWN);
}

void MonApplication::saveStateToMemory(int state) {
    // Sauvegarde en EEPROM ou fichier de configuration
    JsonDocument stateDoc;
    stateDoc["lastState"] = state;
    stateDoc["timestamp"] = millis();
    stateDoc["uptime"] = getSystemUptime();
    
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    persistence.saveJsonToFile("/last_state.json", stateDoc);
}
```

## Exemple Complet

### 1. Fichier Header (.h)

```cpp
#ifndef MON_APPLICATION_H
#define MON_APPLICATION_H

#include "BusinessLogic/BusinessLogic.h"
#include "Node/Node.h"

// ==========================================
// CONSTANTES DE L'APPLICATION
// ==========================================
#define MIN_SPEED 100
#define MAX_SPEED 5000
#define DEFAULT_SPEED 1000
#define MIN_TARGET 0.0f
#define MAX_TARGET 100.0f

// Hash des nœuds (à générer selon votre configuration)
#define START_BUTTON_HASH     0x12345678
#define STOP_BUTTON_HASH      0x23456789
#define PAUSE_BUTTON_HASH     0x34567890
#define EMERGENCY_STOP_HASH   0x45678901
#define RESET_BUTTON_HASH     0x56789012
#define STATUS_LED_HASH       0x67890123
#define RUNNING_LED_HASH      0x78901234
#define ERROR_LED_HASH        0x89012345
#define BUZZER_HASH           0x90123456
#define OUTPUT_RELAY_HASH     0x01234567
#define PROCESS_SPEED_HASH    0x11234567
#define CURRENT_VALUE_HASH    0x21234567
#define TARGET_VALUE_HASH     0x31234567
#define STATUS_TEXT_HASH      0x41234567

class MonApplication : public BusinessLogic {
public:
    // ==========================================
    // ÉNUMÉRATION DES ÉTATS
    // ==========================================
    enum class State : int {
        UNDEFINED = 0,
        INITIALIZING = 1,
        IDLE = 2,
        RUNNING = 3,
        PAUSED = 4,
        ERROR = 5,
        SHUTDOWN = 6
    };

    // ==========================================
    // CONSTRUCTEUR ET MÉTHODES PRINCIPALES
    // ==========================================
    MonApplication();
    virtual ~MonApplication() = default;
    
    // Interface BusinessLogic
    const char* stateToString(int state) const override;
    
    // Méthodes principales
    bool initializePointers(const char* context);
    bool logicExecutor();
    bool startStateMachine();

protected:
    // ==========================================
    // HOOKS DE TRANSITION
    // ==========================================
    void onStateEnter(int newState, int oldState) override;
    void onStateExit(int oldState, int newState) override;
    void onTransitionComplete(int newState, int oldState) override;

private:
    // ==========================================
    // STRUCTURE POUR CONTEXTE D'ERREUR
    // ==========================================
    struct ErrorContext {
        State previousState = State::UNDEFINED;
        uint32_t errorTime = 0;
        String systemState;
        uint8_t recoveryAttempts = 0;
    };
    
    // ==========================================
    // STRUCTURE POUR HISTORIQUE DES TRANSITIONS
    // ==========================================
    struct TransitionRecord {
        State fromState;
        State toState;
        uint32_t timestamp;
    };
    
    // ==========================================
    // VARIABLES D'ÉTAT PRIVÉES
    // ==========================================
    bool stopRequested = false;
    bool pauseRequested = false;
    bool resumeRequested = false;
    bool startRequested = false;
    bool emergencyStopActive = false;
    bool manualResetRequested = false;
    bool buzzerSilenced = false;
    
    uint32_t currentProcessSpeed = DEFAULT_SPEED;
    float currentTarget = 50.0f;
    uint32_t transitionCounter = 0;
    
    // Timestamps pour différentes phases
    uint32_t initializationStartTime = 0;
    uint32_t processStartTime = 0;
    uint32_t pauseStartTime = 0;
    uint32_t errorStartTime = 0;
    
    // Contexte et historique
    ErrorContext errorContext;
    std::vector<TransitionRecord> transitionHistory;
    
    // ==========================================
    // POINTEURS VERS LES NŒUDS
    // ==========================================
    // Nœuds d'entrée
    BooleanInputNode* startButton = nullptr;
    BooleanInputNode* stopButton = nullptr;
    BooleanInputNode* pauseButton = nullptr;
    BooleanInputNode* emergencyStop = nullptr;
    BooleanInputNode* resetButton = nullptr;
    
    // Nœuds de sortie
    BooleanOutputNode* statusLed = nullptr;
    BooleanOutputNode* runningLed = nullptr;
    BooleanOutputNode* errorLed = nullptr;
    BooleanOutputNode* buzzer = nullptr;
    BooleanOutputNode* outputRelay = nullptr;
    
    // Nœuds de données
    Uint32InputNode* processSpeed = nullptr;
    FloatInputNode* targetValue = nullptr;
    FloatOutputNode* currentValue = nullptr;
    TextOutputNode* statusText = nullptr;
    
    // ==========================================
    // HELPERS POUR CONVERSION TYPE-SAFE
    // ==========================================
    State getCurrentAppState() const {
        return static_cast<State>(BusinessLogic::getCurrentState());
    }
    
    void transitionToState(State newState, const char* reason = nullptr) {
        if (reason) {
            logStateTransition(getCurrentState(), static_cast<int>(newState), reason);
        }
        BusinessLogic::transition(static_cast<int>(newState));
    }
    
    // ==========================================
    // HANDLERS D'ÉTAT
    // ==========================================
    bool handleInitializingState(uint32_t now);
    bool handleRunningState(uint32_t now);
    bool handleErrorState(uint32_t now);
    void handleIdleState(uint32_t now);
    void handlePausedState(uint32_t now);
    bool handleShutdownState(uint32_t now);
    
    // ==========================================
    // GESTION DES INTERFACES
    // ==========================================
    void handleUserInterface();
    void updateStatusIndicators();
    void manageBuzzerState(State currentAppState, uint32_t now);
    void updateStatusText(State currentAppState);
    void updateOutputs();
    
    // ==========================================
    // LOGIQUE MÉTIER
    // ==========================================
    bool initializeHardware();
    bool initializeSoftware();
    bool verifySystemReady();
    bool isSystemReady() const;
    bool processMainLogic();
    bool isProcessComplete() const;
    void startProcess();
    void stopProcess();
    void pauseProcess();
    void resumeProcess();
    
    // ==========================================
    // GESTION D'ERREURS
    // ==========================================
    void checkEmergencyConditions();
    bool checkErrorConditions() const;
    bool attemptErrorRecovery();
    void clearAllErrors();
    void handleError();
    void activateErrorIndicators();
    void clearErrorIndicators();
    String captureSystemState() const;
    void clearErrorContext();
    
    // ==========================================
    // CALLBACKS ET OBSERVERS
    // ==========================================
    void setupStateObservers();
    void logDetailedStateTransition(int from, int to);
    void handleErrorTransition(State fromState);
    void handleErrorRecoveryTransition(State toState);
    void notifyExternalSystems(int newState, int oldState);
    bool shouldSaveState(int state);
    void saveStateToMemory(int state);
    
    // ==========================================
    // UTILITAIRES
    // ==========================================
    void activateBuzzer(uint32_t durationMs);
    bool performSystemCheck();
    void enablePowerSaveMode();
    float getCurrentProcessValue() const;
    void applyNewSpeed(uint32_t speed);
    void updateProcessStatus();
    void updatePerformanceMetrics(int newState, int oldState);
    uint32_t getSystemUptime() const;
    void resetInternalVariables();
    void updateStateDisplay();
    void finalizeStateTransition();
    void sendEmergencyNotification();
    
    // ==========================================
    // VARIABLES POUR BUZZER TEMPORAIRE
    // ==========================================
    uint32_t buzzerEndTime = 0;
    
    // ==========================================
    // INTERFACES EXTERNES (si configurées)
    // ==========================================
    class WebInterface* webInterface = nullptr;
    class ModbusInterface* modbusInterface = nullptr;
    Uint16OutputNode* statusRegister = nullptr;
    std::vector<class StateObserver*> externalObservers;
};

#endif // MON_APPLICATION_H
```

### 2. Implémentation Complète (.cpp)

```cpp
#include "MonApplication.h"
#include "BorneUniverselle/BorneUniverselle.h"

// ==========================================
// CONSTRUCTEUR
// ==========================================
MonApplication::MonApplication() : BusinessLogic() {
    // Initialiser l'état au début
    currentState = static_cast<int>(State::UNDEFINED);
    previousState = static_cast<int>(State::UNDEFINED);
    stateStartTime = millis();
    
    // Configuration des observers
    setupStateObservers();
    
    // Variables spécifiques à l'application
    stopRequested = false;
    emergencyStopActive = false;
    manualResetRequested = false;
    currentProcessSpeed = DEFAULT_SPEED;
    
    Serial.println("MonApplication initialisée");
}

// ==========================================
// MÉTHODES ABSTRAITES OBLIGATOIRES
// ==========================================
const char* MonApplication::stateToString(int state) const {
    State appState = static_cast<State>(state);
    switch (appState) {
        case State::UNDEFINED:    return "UNDEFINED";
        case State::INITIALIZING: return "INITIALIZING";
        case State::IDLE:         return "IDLE";
        case State::RUNNING:      return "RUNNING";
        case State::PAUSED:       return "PAUSED";
        case State::ERROR:        return "ERROR";
        case State::SHUTDOWN:     return "SHUTDOWN";
        default:                  return "UNKNOWN";
    }
}

// ==========================================
// MÉTHODE PRINCIPALE DE DÉMARRAGE
// ==========================================
bool MonApplication::startStateMachine() {
    Serial.println("Démarrage de la machine à états...");
    
    if (!initializePointers("MonApplication")) {
        Serial.println("❌ Échec initialisation des pointeurs");
        return false;
    }
    
    // Transition vers l'état initial
    transitionToState(State::INITIALIZING, "Démarrage initial");
    
    Serial.println("✅ Machine à états démarrée");
    return true;
}

// ==========================================
// LOGIQUE PRINCIPALE (À APPELER DANS LA BOUCLE)
// ==========================================
bool MonApplication::logicExecutor() {
    uint32_t now = millis();
    
    // Vérification des conditions d'urgence globales
    checkEmergencyConditions();
    
    // Traitement des interfaces utilisateur
    handleUserInterface();
    
    // Machine à états principale
    switch (getCurrentAppState()) {
        case State::UNDEFINED:
            Serial.println("État UNDEFINED détecté");
            transitionToState(State::INITIALIZING, "Démarrage initial");
            break;
            
        case State::INITIALIZING:
            if (handleInitializingState(now)) {
                transitionToState(State::IDLE, "Initialisation terminée");
            }
            break;
            
        case State::IDLE:
            handleIdleState(now);
            break;
            
        case State::RUNNING:
            if (!handleRunningState(now)) {
                transitionToState(State::ERROR, "Erreur pendant exécution");
                return false; // Erreur critique
            }
            break;
            
        case State::PAUSED:
            handlePausedState(now);
            break;
            
        case State::ERROR:
            handleErrorState(now);
            break;
            
        case State::SHUTDOWN:
            return handleShutdownState(now);
            
        default:
            Serial.printf("État inconnu: %d\n", getCurrentState());
            transitionToState(State::ERROR, "État inconnu");
            return false;
    }
    
    return true; // Succès
}

// ==========================================
// IMPLÉMENTATION DES UTILITAIRES CRITIQUES
// ==========================================
void MonApplication::checkEmergencyConditions() {
    // Vérification arrêt d'urgence physique
    if (emergencyStop && emergencyStop->getValue() && !emergencyStopActive) {
        Serial.println("🚨 ARRÊT D'URGENCE PHYSIQUE DÉTECTÉ");
        emergencyStopActive = true;
        transitionToState(State::ERROR, "Arrêt d'urgence physique");
        return;
    }
    
    // Libération arrêt d'urgence
    if (emergencyStop && !emergencyStop->getValue() && emergencyStopActive) {
        Serial.println("🔓 Arrêt d'urgence libéré");
        emergencyStopActive = false;
        // Ne pas changer d'état automatiquement, attendre reset manuel
    }
    
    // Vérification mémoire critique
    if (ESP.getFreeHeap() < 10000) {
        Serial.printf("🚨 MÉMOIRE CRITIQUE: %zu octets libres\n", (size_t)ESP.getFreeHeap());
        transitionToState(State::ERROR, "Mémoire critique");
        return;
    }
    
    // Vérification communication (exemple avec Modbus)
    if (modbusInterface && !modbusInterface->isConnected()) {
        static uint32_t lastCommCheck = 0;
        if (millis() - lastCommCheck > 5000) { // Vérifier toutes les 5 secondes
            Serial.println("⚠️ Perte de communication Modbus");
            lastCommCheck = millis();
            // Peut déclencher une erreur selon criticité
        }
    }
}

void MonApplication::activateBuzzer(uint32_t durationMs) {
    buzzerEndTime = millis() + durationMs;
}

bool MonApplication::isSystemReady() const {
    return !emergencyStopActive && 
           ESP.getFreeHeap() > 15000 &&
           (modbusInterface ? modbusInterface->isConnected() : true);
}
```

## Bonnes Pratiques

### ✅ DO (À FAIRE)

#### **Architecture et Design**
- **Utilisez des énumérations fortement typées** (`enum class`) pour vos états
- **Implémentez des helpers type-safe** pour les conversions d'états
- **Séparez la logique métier** de la gestion d'états
- **Utilisez les hooks appropriés** (`onStateEnter`, `onStateExit`, `onTransitionComplete`)

#### **Gestion d'États**
- **Implémentez des timeouts** pour éviter les blocages
- **Validez les transitions** avant de les effectuer
- **Loggez les transitions importantes** pour le débogage
- **Gérez les états transitoires** correctement

#### **Gestion des Nœuds**
- **Validez tous les pointeurs de nœuds** lors de l'initialisation
- **Utilisez des hash non-zéro** pour tous les nœuds
- **Gérez les erreurs de type** de nœuds gracieusement
- **Implémentez un système de validation** des types

#### **Robustesse**
- **Gérez les erreurs de manière gracieuse** (pas d'arrêt brutal)
- **Implémentez une récupération automatique** quand possible
- **Surveillez les ressources système** (mémoire, communication)
- **Prévoyez des mécanismes de fallback**

### ❌ DON'T (À ÉVITER)

#### **Architecture et Design**
- **Ne faites pas de transitions dans les hooks** `onStateEnter/onStateExit`
- **N'utilisez pas d'entiers bruts** pour les états (préférez enum class)
- **Ne mélangez pas logique métier et gestion d'états** dans le même handler
- **N'ignorez pas les valeurs de retour** des méthodes critiques

#### **Gestion d'États**
- **N'oubliez pas de retourner `true`** dans `logicExecutor()` sauf erreur fatale
- **Ne bloquez jamais l'exécution** dans les handlers d'état
- **Ne négligez pas les états d'erreur** et de récupération
- **N'effectuez pas d'opérations longues** dans les transitions

#### **Gestion des Nœuds**
- **N'initialisez pas de pointeurs** dans les handlers d'état
- **Ne négligez pas la validation** des hash de nœuds
- **N'assumez jamais qu'un pointeur est valide** sans vérification
- **N'utilisez pas de cast dangereux** sans validation de type

#### **Performance et Ressources**
- **Ne créez pas d'objets temporaires** dans les boucles rapides
- **N'allouez pas de mémoire dynamique** dans les handlers critiques
- **Ne négligez pas la surveillance** des ressources système
- **N'ignorez pas les fuites mémoire** potentielles

## Gestion d'Erreurs

### 1. Stratégie Globale d'Erreurs

```cpp
class MonApplication : public BusinessLogic {
private:
    // ==========================================
    // ÉNUMÉRATION DES TYPES D'ERREURS
    // ==========================================
    enum class ErrorType {
        NONE = 0,
        HARDWARE_FAILURE = 1,
        COMMUNICATION_LOSS = 2,
        MEMORY_CRITICAL = 3,
        USER_EMERGENCY = 4,
        TIMEOUT = 5,
        CONFIGURATION_ERROR = 6,
        EXTERNAL_SYSTEM_ERROR = 7
    };
    
    // ==========================================
    // STRUCTURE POUR DÉTAILS D'ERREUR
    // ==========================================
    struct ErrorDetails {
        ErrorType type = ErrorType::NONE;
        String description;
        uint32_t timestamp = 0;
        uint8_t severity = 0; // 1=Info, 2=Warning, 3=Error, 4=Critical
        bool autoRecoverable = false;
        uint8_t recoveryAttempts = 0;
        uint32_t lastRecoveryAttempt = 0;
    };
    
    ErrorDetails currentError;
    std::vector<ErrorDetails> errorHistory;
    
public:
    // ==========================================
    // MÉTHODES DE GESTION D'ERREURS
    // ==========================================
    void reportError(ErrorType type, const String& description, uint8_t severity = 3, bool autoRecoverable = false);
    void clearError();
    bool hasActiveError() const;
    ErrorType getCurrentErrorType() const;
    const String& getCurrentErrorDescription() const;
};

// ==========================================
// IMPLÉMENTATION DE LA GESTION D'ERREURS
// ==========================================
void MonApplication::reportError(ErrorType type, const String& description, uint8_t severity, bool autoRecoverable) {
    currentError.type = type;
    currentError.description = description;
    currentError.timestamp = millis();
    currentError.severity = severity;
    currentError.autoRecoverable = autoRecoverable;
    currentError.recoveryAttempts = 0;
    currentError.lastRecoveryAttempt = 0;
    
    // Ajouter à l'historique
    errorHistory.push_back(currentError);
    if (errorHistory.size() > 50) { // Limiter la taille de l'historique
        errorHistory.erase(errorHistory.begin());
    }
    
    // Log selon la sévérité
    const char* severityStr[] = {"", "INFO", "WARNING", "ERROR", "CRITICAL"};
    if (severity <= 4) {
        Serial.printf("🚨 %s: %s (Type: %d)\n", 
                     severityStr[severity], description.c_str(), static_cast<int>(type));
    }
    
    // Log diagnostic
    char diagMsg[300];
    snprintf(diagMsg, sizeof(diagMsg), "ERROR_%s: %s (Severity: %d, Auto-recoverable: %s)",
             severityStr[severity], description.c_str(), severity, autoRecoverable ? "Yes" : "No");
    PLC_Tools::logDiagnostic(diagMsg);
    
    // Transition vers erreur si nécessaire
    if (severity >= 3 && getCurrentAppState() != State::ERROR) {
        transitionToState(State::ERROR, description.c_str());
    }
}

bool MonApplication::attemptErrorRecovery() {
    if (!hasActiveError() || !currentError.autoRecoverable) {
        return false;
    }
    
    uint32_t now = millis();
    
    // Limiter les tentatives de récupération
    if (currentError.recoveryAttempts >= 3) {
        Serial.println("❌ Nombre maximum de tentatives de récupération atteint");
        return false;
    }
    
    // Intervalle entre tentatives
    if (now - currentError.lastRecoveryAttempt < 10000) { // 10 secondes minimum
        return false;
    }
    
    currentError.recoveryAttempts++;
    currentError.lastRecoveryAttempt = now;
    
    Serial.printf("🔄 Tentative de récupération %d/3 pour erreur: %s\n", 
                 currentError.recoveryAttempts, currentError.description.c_str());
    
    bool recoverySuccess = false;
    
    // Stratégies de récupération selon le type d'erreur
    switch (currentError.type) {
        case ErrorType::COMMUNICATION_LOSS:
            recoverySuccess = recoverCommunication();
            break;
            
        case ErrorType::MEMORY_CRITICAL:
            recoverySuccess = recoverMemory();
            break;
            
        case ErrorType::HARDWARE_FAILURE:
            recoverySuccess = recoverHardware();
            break;
            
        case ErrorType::TIMEOUT:
            recoverySuccess = recoverFromTimeout();
            break;
            
        case ErrorType::EXTERNAL_SYSTEM_ERROR:
            recoverySuccess = recoverExternalSystem();
            break;
            
        default:
            Serial.println("⚠️ Type d'erreur non récupérable automatiquement");
            recoverySuccess = false;
            break;
    }
    
    if (recoverySuccess) {
        Serial.println("✅ Récupération automatique réussie");
        clearError();
        return true;
    } else {
        Serial.printf("❌ Échec de la récupération (tentative %d/3)\n", currentError.recoveryAttempts);
        return false;
    }
}

// ==========================================
// STRATÉGIES DE RÉCUPÉRATION SPÉCIFIQUES
// ==========================================
bool MonApplication::recoverCommunication() {
    Serial.println("🔄 Tentative de récupération communication...");
    
    // Réinitialiser les interfaces de communication
    if (modbusInterface) {
        modbusInterface->disconnect();
        delay(1000);
        return modbusInterface->reconnect();
    }
    
    return false;
}

bool MonApplication::recoverMemory() {
    Serial.println("🔄 Tentative de récupération mémoire...");
    
    // Forcer le garbage collector
    ESP.restart(); // Solution drastique mais efficace
    
    // Ou tentatives plus douces:
    // clearCaches();
    // freeTemporaryBuffers();
    // return ESP.getFreeHeap() > 15000;
}

bool MonApplication::recoverHardware() {
    Serial.println("🔄 Tentative de récupération matériel...");
    
    // Réinitialiser les composants matériels
    bool success = true;
    
    // Reset des sorties
    if (statusLed) statusLed->setValue(false);
    if (runningLed) runningLed->setValue(false);
    if (buzzer) buzzer->setValue(false);
    if (outputRelay) outputRelay->setValue(false);
    
    delay(500);
    
    // Test des composants critiques
    if (startButton && !startButton->isValid()) {
        Serial.println("❌ Bouton de démarrage défaillant");
        success = false;
    }
    
    if (emergencyStop && !emergencyStop->isValid()) {
        Serial.println("❌ Arrêt d'urgence défaillant");
        success = false;
    }
    
    return success;
}

bool MonApplication::recoverFromTimeout() {
    Serial.println("🔄 Récupération depuis timeout...");
    
    // Reset des timers internes
    initializationStartTime = millis();
    processStartTime = millis();
    
    // Reset des flags de timeout
    stopRequested = false;
    pauseRequested = false;
    
    return true;
}

bool MonApplication::recoverExternalSystem() {
    Serial.println("🔄 Récupération système externe...");
    
    // Tentative de reconnexion aux systèmes externes
    bool allSystemsOk = true;
    
    if (webInterface && !webInterface->isConnected()) {
        allSystemsOk &= webInterface->reconnect();
    }
    
    // Autres systèmes...
    
    return allSystemsOk;
}

// ==========================================
// GESTION AVANCÉE D'ERREURS DANS logicExecutor
// ==========================================
bool MonApplication::logicExecutor() {
    try {
        uint32_t now = millis();
        
        // Vérification des conditions d'urgence globales
        checkEmergencyConditions();
        
        // Traitement des interfaces utilisateur
        handleUserInterface();
        
        // Machine à états principale avec gestion d'erreurs intégrée
        switch (getCurrentAppState()) {
            case State::UNDEFINED:
                Serial.println("État UNDEFINED détecté");
                transitionToState(State::INITIALIZING, "Démarrage initial");
                break;
                
            case State::INITIALIZING:
                if (handleInitializingState(now)) {
                    transitionToState(State::IDLE, "Initialisation terminée");
                }
                break;
                
            case State::IDLE:
                handleIdleState(now);
                break;
                
            case State::RUNNING:
                if (!handleRunningState(now)) {
                    reportError(ErrorType::EXTERNAL_SYSTEM_ERROR, 
                               "Erreur pendant l'exécution du processus", 
                               4, true);
                    return false; // Erreur critique
                }
                break;
                
            case State::PAUSED:
                handlePausedState(now);
                break;
                
            case State::ERROR:
                handleErrorState(now);
                break;
                
            case State::SHUTDOWN:
                return handleShutdownState(now);
                
            default:
                reportError(ErrorType::CONFIGURATION_ERROR, 
                           String("État inconnu: ") + String(getCurrentState()), 
                           4, false);
                return false;
        }
        
        return true; // Succès
        
    } catch (const std::exception& e) {
        reportError(ErrorType::EXTERNAL_SYSTEM_ERROR, 
                   String("Exception dans logicExecutor: ") + String(e.what()), 
                   4, false);
        Serial.printf("Exception dans logicExecutor: %s\n", e.what());
        transitionToState(State::ERROR, "Exception système");
        return false;
    } catch (...) {
        reportError(ErrorType::EXTERNAL_SYSTEM_ERROR, 
                   "Exception inconnue dans logicExecutor", 
                   4, false);
        Serial.println("Exception inconnue dans logicExecutor");
        transitionToState(State::ERROR, "Exception inconnue");
        return false;
    }
}
```

## Débogage et Diagnostics

### 1. Système de Logs Avancé

```cpp
class MonApplication : public BusinessLogic {
private:
    // ==========================================
    // SYSTÈME DE DIAGNOSTIC
    // ==========================================
    struct DiagnosticInfo {
        uint32_t totalTransitions = 0;
        uint32_t errorCount = 0;
        uint32_t recoverySuccessCount = 0;
        uint32_t recoveryFailureCount = 0;
        uint32_t totalUptime = 0;
        uint32_t lastDiagnosticTime = 0;
        
        // Temps passé dans chaque état
        std::map<State, uint32_t> stateTimeStats;
        
        // Performance
        uint32_t minLoopTime = UINT32_MAX;
        uint32_t maxLoopTime = 0;
        uint32_t avgLoopTime = 0;
        uint32_t loopCount = 0;
    };
    
    DiagnosticInfo diagnostics;
    
public:
    // ==========================================
    // MÉTHODES DE DÉBOGAGE
    // ==========================================
    void printDebugInfo() const;
    void printDiagnosticReport() const;
    void printTransitionHistory() const;
    void printPerformanceStats() const;
    void printNodeStatus() const;
    void exportDiagnosticData() const;
    void resetDiagnosticCounters();
    
    // Méthodes de surveillance
    void updatePerformanceMetrics(uint32_t loopStartTime);
    void recordStateTime();
};

// ==========================================
// IMPLÉMENTATION DES DIAGNOSTICS
// ==========================================
void MonApplication::printDebugInfo() const {
    Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
    Serial.println("║                    DEBUG INFO - MonApplication                    ║");
    Serial.println("╠════════════════════════════════════════════════════════════════╣");
    
    // État actuel
    Serial.printf("║ État actuel        : %-42s ║\n", stateToString(getCurrentState()));
    Serial.printf("║ État précédent     : %-42s ║\n", stateToString(getPreviousState()));
    Serial.printf("║ Temps dans l'état  : %-32lu ms      ║\n", getTimeInCurrentState());
    Serial.printf("║ Début état        : %-32lu ms      ║\n", getStateStartTime());
    
    // Système
    Serial.printf("║ Uptime système     : %-32lu ms      ║\n", millis());
    Serial.printf("║ Heap libre         : %-32zu bytes   ║\n", (size_t)ESP.getFreeHeap());
    Serial.printf("║ Heap minimum       : %-32zu bytes   ║\n", (size_t)ESP.getMinFreeHeap());
    
    // Flags d'état
    Serial.println("╠════════════════════════════════════════════════════════════════╣");
    Serial.printf("║ Stop demandé       : %-42s ║\n", stopRequested ? "OUI" : "NON");
    Serial.printf("║ Pause demandée     : %-42s ║\n", pauseRequested ? "OUI" : "NON");
    Serial.printf("║ Arrêt urgence actif: %-42s ║\n", emergencyStopActive ? "OUI" : "NON");
    Serial.printf("║ Reset manuel       : %-42s ║\n", manualResetRequested ? "OUI" : "NON");
    
    // Processus
    Serial.println("╠════════════════════════════════════════════════════════════════╣");
    Serial.printf("║ Vitesse processus  : %-32lu        ║\n", currentProcessSpeed);
    Serial.printf("║ Cible actuelle     : %-32.2f        ║\n", currentTarget);
    Serial.printf("║ Valeur actuelle    : %-32.2f        ║\n", getCurrentProcessValue());
    
    // Erreur actuelle
    if (hasActiveError()) {
        Serial.println("╠════════════════════════════════════════════════════════════════╣");
        Serial.printf("║ ERREUR ACTIVE      : %-42s ║\n", getCurrentErrorDescription().c_str());
        Serial.printf("║ Type d'erreur      : %-42d ║\n", static_cast<int>(getCurrentErrorType()));
        Serial.printf("║ Tentatives récup.  : %-42d ║\n", currentError.recoveryAttempts);
    }
    
    Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
}

void MonApplication::printDiagnosticReport() const {
    Serial.println("\n╔═══════════════════════════════════════════════════════════════╗");
    Serial.println("║                    RAPPORT DIAGNOSTIQUE                          ║");
    Serial.println("╠═══════════════════════════════════════════════════════════════╣");
    
    // Statistiques générales
    Serial.printf("║ Transitions totales    : %-35lu ║\n", diagnostics.totalTransitions);
    Serial.printf("║ Erreurs totales        : %-35lu ║\n", diagnostics.errorCount);
    Serial.printf("║ Récupérations réussies : %-35lu ║\n", diagnostics.recoverySuccessCount);
    Serial.printf("║ Récupérations échouées : %-35lu ║\n", diagnostics.recoveryFailureCount);
    
    // Taux de réussite
    if (diagnostics.errorCount > 0) {
        float successRate = (float)diagnostics.recoverySuccessCount / diagnostics.errorCount * 100.0f;
        Serial.printf("║ Taux de récupération   : %-30.1f%%    ║\n", successRate);
    }
    
    // Temps dans chaque état
    Serial.println("╠═══════════════════════════════════════════════════════════════╣");
    Serial.println("║                    TEMPS PAR ÉTAT                                ║");
    Serial.println("╠═══════════════════════════════════════════════════════════════╣");
    
    for (const auto& statePair : diagnostics.stateTimeStats) {
        const char* stateName = stateToString(static_cast<int>(statePair.first));
        uint32_t timeInState = statePair.second;
        float percentage = (float)timeInState / millis() * 100.0f;
        
        Serial.printf("║ %-18s : %8lu ms (%5.1f%%)            ║\n", 
                     stateName, timeInState, percentage);
    }
    
    Serial.println("╚═══════════════════════════════════════════════════════════════╝\n");
}

void MonApplication::printTransitionHistory() const {
    Serial.println("\n╔═══════════════════════════════════════════════════════════════╗");
    Serial.println("║                  HISTORIQUE DES TRANSITIONS                      ║");
    Serial.println("╠═══════════════════════════════════════════════════════════════╣");
    
    if (transitionHistory.empty()) {
        Serial.println("║ Aucune transition enregistrée                                    ║");
    } else {
        Serial.println("║  #  │      Temps    │     Depuis → Vers                        ║");
        Serial.println("╠═════╪═══════════════╪══════════════════════════════════════════╣");
        
        for (size_t i = 0; i < transitionHistory.size(); ++i) {
            const auto& transition = transitionHistory[i];
            Serial.printf("║ %2zu  │ %8lu ms │ %10s → %-10s              ║\n",
                         i + 1,
                         transition.timestamp,
                         stateToString(static_cast<int>(transition.fromState)),
                         stateToString(static_cast<int>(transition.toState)));
        }
    }
    
    Serial.println("╚═══════════════════════════════════════════════════════════════╝\n");
}

void MonApplication::printPerformanceStats() const {
    Serial.println("\n╔═══════════════════════════════════════════════════════════════╗");
    Serial.println("║                  STATISTIQUES PERFORMANCE                        ║");
    Serial.println("╠═══════════════════════════════════════════════════════════════╣");
    
    Serial.printf("║ Boucles exécutées      : %-35lu ║\n", diagnostics.loopCount);
    Serial.printf("║ Temps boucle MIN       : %-32lu ms  ║\n", diagnostics.minLoopTime);
    Serial.printf("║ Temps boucle MAX       : %-32lu ms  ║\n", diagnostics.maxLoopTime);
    Serial.printf("║ Temps boucle MOYEN     : %-32lu ms  ║\n", diagnostics.avgLoopTime);
    
    if (diagnostics.loopCount > 0) {
        float frequency = 1000.0f / diagnostics.avgLoopTime;
        Serial.printf("║ Fréquence moyenne      : %-30.1f Hz   ║\n", frequency);
    }
    
    // Utilisation mémoire
    size_t freeHeap = ESP.getFreeHeap();
    size_t totalHeap = ESP.getHeapSize();
    size_t usedHeap = totalHeap - freeHeap;
    float heapUsage = (float)usedHeap / totalHeap * 100.0f;
    
    Serial.printf("║ Mémoire totale         : %-32zu bytes ║\n", totalHeap);
    Serial.printf("║ Mémoire utilisée       : %-32zu bytes ║\n", usedHeap);
    Serial.printf("║ Mémoire libre          : %-32zu bytes ║\n", freeHeap);
    Serial.printf("║ Utilisation mémoire    : %-30.1f%%    ║\n", heapUsage);
    
    Serial.println("╚═══════════════════════════════════════════════════════════════╝\n");
}

void MonApplication::printNodeStatus() const {
    Serial.println("\n╔═══════════════════════════════════════════════════════════════╗");
    Serial.println("║                       ÉTAT DES NŒUDS                             ║");
    Serial.println("╠═══════════════════════════════════════════════════════════════╣");
    
    // Helper pour afficher l'état d'un nœud
    auto printNodeState = [](const char* name, Node* node) {
        if (node) {
            String state = node->isValid() ? "OK" : "ERREUR";
            String value = node->getStringValue();
            Serial.printf("║ %-20s │ %-8s │ %-25s ║\n", name, state.c_str(), value.c_str());
        } else {
            Serial.printf("║ %-20s │ %-8s │ %-25s ║\n", name, "NULL", "Non initialisé");
        }
    };
    
    Serial.println("║ Nom du nœud           │   État   │ Valeur                    ║");
    Serial.println("╠════════════════════════╪══════════╪═══════════════════════════╣");
    
    // Nœuds d'entrée
    printNodeState("Start Button", startButton);
    printNodeState("Stop Button", stopButton);
    printNodeState("Pause Button", pauseButton);
    printNodeState("Emergency Stop", emergencyStop);
    printNodeState("Reset Button", resetButton);
    
    Serial.println("╠════════════════════════╪══════════╪═══════════════════════════╣");
    
    // Nœuds de sortie
    printNodeState("Status LED", statusLed);
    printNodeState("Running LED", runningLed);
    printNodeState("Error LED", errorLed);
    printNodeState("Buzzer", buzzer);
    printNodeState("Output Relay", outputRelay);
    
    Serial.println("╠════════════════════════╪══════════╪═══════════════════════════╣");
    
    // Nœuds de données
    printNodeState("Process Speed", processSpeed);
    printNodeState("Target Value", targetValue);
    printNodeState("Current Value", currentValue);
    printNodeState("Status Text", statusText);
    
    Serial.println("╚═══════════════════════════════════════════════════════════════╝\n");
}

// ==========================================
// MÉTHODES DE SURVEILLANCE EN TEMPS RÉEL
// ==========================================
void MonApplication::updatePerformanceMetrics(uint32_t loopStartTime) {
    uint32_t loopTime = millis() - loopStartTime;
    
    diagnostics.loopCount++;
    
    if (loopTime < diagnostics.minLoopTime) {
        diagnostics.minLoopTime = loopTime;
    }
    
    if (loopTime > diagnostics.maxLoopTime) {
        diagnostics.maxLoopTime = loopTime;
    }
    
    // Calcul de la moyenne mobile
    diagnostics.avgLoopTime = ((diagnostics.avgLoopTime * (diagnostics.loopCount - 1)) + loopTime) / diagnostics.loopCount;
    
    // Alerte si la boucle est trop lente
    if (loopTime > 100) { // Plus de 100ms
        Serial.printf("⚠️ Boucle lente détectée: %lu ms\n", loopTime);
    }
}

void MonApplication::recordStateTime() {
    State currentAppState = getCurrentAppState();
    uint32_t timeInState = getTimeInCurrentState();
    
    diagnostics.stateTimeStats[currentAppState] += timeInState;
}

// ==========================================
// EXEMPLE D'UTILISATION DANS LA BOUCLE PRINCIPALE
// ==========================================
void loop() {
    static MonApplication app;
    static bool initialized = false;
    static uint32_t lastDiagnosticPrint = 0;
    static uint32_t lastPerformanceUpdate = 0;
    
    uint32_t loopStartTime = millis();
    
    // Initialisation une seule fois
    if (!initialized) {
        if (app.startStateMachine()) {
            initialized = true;
            Serial.println("✅ Application initialisée avec succès");
        } else {
            Serial.println("❌ Échec initialisation application");
            delay(5000);
            return;
        }
    }
    
    // Exécution de la logique principale
    if (!app.logicExecutor()) {
        Serial.println("❌ Erreur critique dans logicExecutor");
        // L'application continue mais signale l'erreur
    }
    
    // Mise à jour des métriques de performance (toutes les 10 boucles)
    if (millis() - lastPerformanceUpdate > 100) {
        app.updatePerformanceMetrics(loopStartTime);
        lastPerformanceUpdate = millis();
    }
    
    // Affichage périodique des diagnostics (toutes les 30 secondes)
    if (millis() - lastDiagnosticPrint > 30000) {
        Serial.println("\n🔍 Rapport périodique:");
        app.printDebugInfo();
        lastDiagnosticPrint = millis();
    }
    
    // Commandes de débogage via Serial
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command == "debug") {
            app.printDebugInfo();
        } else if (command == "diag") {
            app.printDiagnosticReport();
        } else if (command == "history") {
            app.printTransitionHistory();
        } else if (command == "perf") {
            app.printPerformanceStats();
        } else if (command == "nodes") {
            app.printNodeStatus();
        } else if (command == "help") {
            Serial.println("Commandes disponibles:");
            Serial.println("  debug    - Affiche les informations de débogage");
            Serial.println("  diag     - Affiche le rapport diagnostique");
            Serial.println("  history  - Affiche l'historique des transitions");
            Serial.println("  perf     - Affiche les statistiques de performance");
            Serial.println("  nodes    - Affiche l'état de tous les nœuds");
            Serial.println("  help     - Affiche cette aide");
        }
    }
    
    // Petit délai pour éviter de saturer le CPU
    delay(10);
}
```

## Conclusion

Cette documentation complète vous fournit tous les éléments nécessaires pour créer une application robuste basée sur la classe `BusinessLogic`. L'architecture proposée garantit :

### 🎯 **Robustesse**
- Gestion complète des erreurs avec récupération automatique
- Validation systématique des pointeurs et des types
- Surveillance des ressources système
- Mécanismes de fallback

### 🔧 **Maintenabilité**
- Code bien structuré avec des responsabilités claires
- Système de logs détaillé pour le débogage
- Documentation inline et commentaires explicites
- Tests de régression facilités

### ⚡ **Performance**
- Surveillance en temps réel des performances
- Optimisation des boucles critiques
- Gestion efficace de la mémoire
- Métriques de performance intégrées

### 🔍 **Observabilité**
- Historique des transitions d'état
- Diagnostics détaillés en temps réel
- Export des données de diagnostic
- Interface de débogage interactive

### 📈 **Évolutivité**
- Architecture modulaire facilement extensible
- Hooks personnalisables pour nouvelle logique
- Système d'observers pour intégrations futures
- Support multi-interface (Web, Modbus, etc.)

Cette base solide vous permettra de développer des applications PLC fiables et maintenables, tout en conservant la flexibilité nécessaire pour s'adapter aux besoins spécifiques de vos projets.
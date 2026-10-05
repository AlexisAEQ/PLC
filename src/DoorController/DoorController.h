#ifndef DOOR_CONTROLLER_H
#define DOOR_CONTROLLER_H

#include "Node/Node.h"

/**
 * @brief Contrôleur de porte simplifié pour système avec automate de sécurité externe
 * 
 * LOGIQUE SIMPLIFIÉE :
 * - doorCaptor : FEEDBACK de l'automate (true = porte verrouillée)
 * - e_stopA : URGENCE grave (true = porte arrachée ou autre problème)
 * - doorLocked : COMMANDE vers l'automate (true = demande fermeture)
 * - Timeout 1 seconde pour attente feedback
 * - Pas de gestion inertie scie (géré par automate)
 */
class DoorController {
public:
    enum class State {
        UNKNOWN,                // État initial
        OPEN,                   // Porte ouverte
        WAITING_LOCK,           // Attente feedback verrouillage (~1 sec)
        LOCKED,                 // Porte verrouillée
        WAITING_UNLOCK,         // Attente feedback déverrouillage (~1 sec)
        UNLOCKED_CLOSED,        // Porte fermée non verrouillée
        EMERGENCY               // État d'urgence (e_stopA actif)
    };

    // Constructeur
    DoorController(
        BooleanInputNode* doorStatusInput,
        BooleanInputNode* eStopA,
        VirtualBooleanInputNode* vDoorRequest,
        BooleanOutputNode* doorLocked
    );

    // Méthode principale (appelée depuis checkDoorSafetyLogic)
    void update(uint32_t now);

    // Accesseurs pour Formaca2
    bool isLocked() const { return currentState == State::LOCKED; }
    bool isOpen() const { 
        return currentState == State::OPEN || 
               currentState == State::EMERGENCY ||
               currentState == State::UNLOCKED_CLOSED; 
    }
    State getCurrentState() const { return currentState; }
    
    bool isDoorCompromised() const;

    bool isInTransition() const { 
        return currentState == State::WAITING_LOCK || 
            currentState == State::WAITING_UNLOCK; 
    }

    bool isEmergencyActive() const;
    bool isSafeToOperate() const;

    // Pour debug/logging
    const char* getStateString() const;


private:
    // Timeout unique pour tous les feedbacks
    static const uint32_t FEEDBACK_TIMEOUT_MS = 1000;  // 1 seconde

    // Références vers les nœuds
    BooleanInputNode* doorStatusInput;
    BooleanInputNode* eStopA;
    VirtualBooleanInputNode* vDoorRequest;
    BooleanOutputNode* doorLocked;

    // État interne
    State currentState;
    uint32_t stateStartTime;

    // Méthodes privées de transition
    void transitionTo(State newState, uint32_t now);
    void handleOpenState(uint32_t now);
    void handleWaitingLockState(uint32_t now);
    void handleLockedState(uint32_t now);
    void handleWaitingUnlockState(uint32_t now);
    void handleUnlockedClosedState(uint32_t now);
    void handleEmergencyState(uint32_t now);
    
    // Helpers
    bool checkTimeout(uint32_t now) const;
    bool isCaptorClosed() const { return doorStatusInput->getValue(); }
    
    // Pour debug
    const char* getStateString(State state) const;

    bool isPhysicallyLocked() const {
        return doorStatusInput->getValue();  // Feedback automate : true = porte verrouillée
    }

   // ⚠️ TEMPORAIRE - Filtre anti-rebond E-Stop (bug automate)
    static const uint32_t ESTOP_DEBOUNCE_MS = 200;  // 200ms minimum
    mutable uint32_t eStopActivationTime; 
    mutable bool eStopWasActive;  
};

#endif // DOOR_CONTROLLER_H
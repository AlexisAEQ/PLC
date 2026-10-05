#include "DoorController/DoorController.h"
#include "BorneUniverselle/BorneUniverselle.h"
#include "PLC_Tools/PLC_Tools.h"

DoorController::DoorController(
    BooleanInputNode* doorStatusInput,
    BooleanInputNode* eStopA,
    VirtualBooleanInputNode* vDoorRequest,
    BooleanOutputNode* doorLocked
) : doorStatusInput(doorStatusInput),
    eStopA(eStopA),
    vDoorRequest(vDoorRequest),
    doorLocked(doorLocked),

    currentState(State::UNKNOWN),
    stateStartTime(0),
    eStopActivationTime(0),
    eStopWasActive(false)
    {
        //Serial.printf("🔍 DoorController INIT: eStopA node name='%s', value=%d\n", eStopA->getName(), eStopA->getValue());

        if (isEmergencyActive()) {
        currentState = State::EMERGENCY;
    } else if (isCaptorClosed()) {
        currentState = State::LOCKED;
    } else {
        currentState = State::OPEN;
    }
}

void DoorController::update(uint32_t now) {
    // Machine d'états
    switch(currentState) {
        case State::OPEN:
            handleOpenState(now);
            break;
        case State::WAITING_LOCK:
            handleWaitingLockState(now);
            break;
        case State::LOCKED:
            handleLockedState(now);
            break;
        case State::WAITING_UNLOCK:
            handleWaitingUnlockState(now);
            break;
        case State::UNLOCKED_CLOSED:
            handleUnlockedClosedState(now);
            break;
        case State::EMERGENCY:
            handleEmergencyState(now);
            break;
        default:
            // État inconnu, réinitialiser
            transitionTo(State::OPEN, now);
            break;
    }
}

void DoorController::transitionTo(State newState, uint32_t now) {
    if (currentState == newState) return;
    
    Serial.printf("DoorController: %s -> %s\n", 
                  getStateString(currentState), 
                  getStateString(newState));
    
    currentState = newState;
    stateStartTime = now;
}

// =====================================================
// HANDLERS PAR ÉTAT
// =====================================================
void DoorController::handleOpenState(uint32_t now) {
    // ⚠️ TEMPORAIRE - Bug capteur porte
    // On accepte la demande de verrouillage même si capteur dit "ouvert"
    if (vDoorRequest != nullptr && !vDoorRequest->getValue()) {
        doorLocked->setValue(false);  // FALSE = verrouiller
        transitionTo(State::WAITING_LOCK, now);
        BorneUniverselle::prepareMessage(INFO, "Verrouillage en cours");
        return;
    }
}

/*
void DoorController::handleOpenState(uint32_t now) {
    // ✅ ÉTAPE 12 - Détecter fermeture physique de la porte
    if (isCaptorClosed()) {
        // Porte fermée → transition vers UNLOCKED_CLOSED
        transitionTo(State::UNLOCKED_CLOSED, now);
        BorneUniverselle::prepareMessage(INFO, "Porte fermée, prête à verrouiller");
        return;
    }
    
    // 🔁 CAS RECHERCHÉ : Demande verrouillage alors que porte OUVERTE
    if (vDoorRequest != nullptr && !vDoorRequest->getValue()) {
        // ❌ Porte encore ouverte → MESSAGE RÉPÉTITIF
        PLC_Tools::sendPeriodicMessage(true, WARNING, "Fermez d'abord la porte avant de verrouiller");
        return;
    }
}
    */
void DoorController::handleWaitingLockState(uint32_t now) {
    // ⚠️ COMMENTÉ TEMPORAIREMENT - Bug capteur porte
    /*
    if (!isCaptorClosed()) {
        transitionTo(State::OPEN, now);
        BorneUniverselle::prepareMessage(WARNING, "Porte rouverte pendant verrouillage");
        return;
    }
    */
    
    // Détection annulation (bouton passe à TRUE)
    if (vDoorRequest != nullptr && vDoorRequest->getValue()) {
        doorLocked->setValue(true);
        transitionTo(State::UNLOCKED_CLOSED, now);
        BorneUniverselle::prepareMessage(INFO, "Verrouillage annulé");
        return;
    }
    
    // Vérification après timeout
    if (checkTimeout(now)) {
        if (isCaptorClosed()) {
            transitionTo(State::LOCKED, now);
            BorneUniverselle::prepareMessage(SUCCESS, "Porte verrouillée avec succès");
        } else {
            // ⚠️ TEMPORAIRE - Capteur défaillant, on verrouille quand même
            transitionTo(State::LOCKED, now);
            BorneUniverselle::prepareMessage(WARNING, "Porte verrouillée (capteur défaillant)");
        }
    }
}

/*
void DoorController::handleWaitingLockState(uint32_t now) {
    // ✅ ÉTAPE 3.1 - Détecter si la porte a été rouverte pendant l'attente
    if (!isCaptorClosed()) {
        transitionTo(State::OPEN, now);
        BorneUniverselle::prepareMessage(WARNING, "Porte rouverte pendant verrouillage");
        return;
    }
    
    // ✅ ÉTAPE 3.2 - Détecter annulation (commutateur → OPEN)
    if (vDoorRequest != nullptr && vDoorRequest->getValue()) {
        doorLocked->setValue(true);  // TRUE = déverrouiller/annuler
        transitionTo(State::UNLOCKED_CLOSED, now);
        BorneUniverselle::prepareMessage(INFO, "Verrouillage annulé");
        return;
    }
    
    // ✅ ÉTAPE 3.3 - Vérification après timeout
    if (checkTimeout(now)) {
        if (isCaptorClosed()) {
            transitionTo(State::LOCKED, now);
            BorneUniverselle::prepareMessage(SUCCESS, "Porte verrouillée avec succès");
        } else {
            PLC_Tools::sendPeriodicMessage(!isCaptorClosed(), WARNING, 
                "Attente confirmation verrouillage...");
        }
    }
}
*/
void DoorController::handleLockedState(uint32_t now) {
    // ✅ ÉTAPE 1 - LOGIQUE COMMUTATEUR : TRUE = déverrouiller
    if (vDoorRequest != nullptr && vDoorRequest->getValue()) {
        // Commutateur à TRUE → déverrouiller
        doorLocked->setValue(true);  // ✅ TRUE = déverrouiller
        transitionTo(State::WAITING_UNLOCK, now);
        BorneUniverselle::prepareMessage(INFO, "Déverrouillage en cours");
        return;
    }
}

void DoorController::handleWaitingUnlockState(uint32_t now) {
    if (!isCaptorClosed()) {
        transitionTo(State::OPEN, now);
        BorneUniverselle::prepareMessage(SUCCESS, "Porte déverrouillée");
        return;
    }
    
    // ✅ ÉTAPE 3.4 - Détecter demande de re-verrouillage pendant l'attente
    if (vDoorRequest != nullptr && !vDoorRequest->getValue()) {
        doorLocked->setValue(false);  // FALSE = verrouiller
        transitionTo(State::WAITING_LOCK, now);
        BorneUniverselle::prepareMessage(INFO, "Re-verrouillage demandé");
        return;
    }
    
    if (checkTimeout(now)) {
        PLC_Tools::sendPeriodicMessage(isCaptorClosed(), WARNING, 
            "Attente libération automate de sécurité...");
    }
}

void DoorController::handleUnlockedClosedState(uint32_t now) {
    // Détection réouverture (si capteur fonctionne)
    if (!isCaptorClosed()) {
        transitionTo(State::OPEN, now);
        BorneUniverselle::prepareMessage(INFO, "Porte rouverte");
        return;
    }
    
    // Demande verrouillage - acceptée sans vérification capteur
    if (vDoorRequest != nullptr && !vDoorRequest->getValue()) {
        doorLocked->setValue(false);
        transitionTo(State::WAITING_LOCK, now);
        BorneUniverselle::prepareMessage(INFO, "Verrouillage en cours");
        return;
    }
}

/*
void DoorController::handleUnlockedClosedState(uint32_t now) {
    // ✅ ÉTAPE 12 - Détecter si la porte se rouvre
    if (!isCaptorClosed()) {
        // Porte rouverte → retour OPEN
        transitionTo(State::OPEN, now);
        BorneUniverselle::prepareMessage(INFO, "Porte rouverte");
        return;
    }
    
    // Demande de verrouillage (porte déjà fermée, OK pour verrouiller)
    if (vDoorRequest != nullptr && !vDoorRequest->getValue()) {
        // ✅ Porte fermée + demande verrouillage → commander
        doorLocked->setValue(false);  // ✅ ÉTAPE 13: FALSE = verrouiller
        transitionTo(State::WAITING_LOCK, now);
        BorneUniverselle::prepareMessage(INFO, "Verrouillage en cours");
        return;
    }
}
    */

void DoorController::handleEmergencyState(uint32_t now) {
    // ✅ ÉTAPE 1.7 - Simplification de la sortie d'urgence
    if (!isEmergencyActive()) {
        // Déterminer l'état cible selon la commande doorLocked
       if (isCaptorClosed()) { 
            transitionTo(State::LOCKED, now);
        } else {
            transitionTo(State::OPEN, now);
        }
    }
}

// =====================================================
// HELPERS
// =====================================================

bool DoorController::checkTimeout(uint32_t now) const {
    return (now - stateStartTime) > FEEDBACK_TIMEOUT_MS;
}

const char* DoorController::getStateString() const {
    return getStateString(currentState);
}

const char* DoorController::getStateString(State state) const {
    switch(state) {
        case State::UNKNOWN: return "UNKNOWN";
        case State::OPEN: return "OPEN";
        case State::WAITING_LOCK: return "WAITING_LOCK";
        case State::LOCKED: return "LOCKED";
        case State::WAITING_UNLOCK: return "WAITING_UNLOCK";
        case State::UNLOCKED_CLOSED: return "UNLOCKED_CLOSED";
        case State::EMERGENCY: return "EMERGENCY";
        default: return "INVALID";
    }
}

bool  DoorController::isDoorCompromised() const {
    // Cas 1 : Était verrouillée mais porte maintenant ouverte (arrachée)
    if (currentState == State::LOCKED && !isCaptorClosed()) {
        return true;
    }
    
    // Autres états : pas d'incohérence critique
    return false;
}

bool DoorController::isEmergencyActive() const {
    // ⚠️ TEMPORAIRE - Filtre anti-rebond pour bug automate sécurité
    // L'E-Stop doit être actif pendant au moins ESTOP_DEBOUNCE_MS
    // avant d'être considéré comme une vraie urgence
    
    bool currentEStop = eStopA->getValue();
    uint32_t now = millis();
    
    // Détection front montant (passage à urgence)
    if (currentEStop && !eStopWasActive) {
        // E-Stop vient de s'activer, enregistrer le moment
        eStopActivationTime = now;
        eStopWasActive = true;
        return false;  // Pas encore confirmé, on attend
    }
    
    // Détection front descendant (fin d'urgence)
    if (!currentEStop && eStopWasActive) {
        // E-Stop désactivé avant confirmation → c'était un glitch
        eStopWasActive = false;
        return false;
    }
    
    // E-Stop actif : vérifier durée
    if (currentEStop && eStopWasActive) {
        uint32_t duration = now - eStopActivationTime;
        if (duration >= ESTOP_DEBOUNCE_MS) {
            // Confirmé après 200ms
            return true;
        } else {
            // Toujours en attente de confirmation
            return false;
        }
    }
    
    // E-Stop inactif
    eStopWasActive = false;
    return false;
}

bool DoorController::isSafeToOperate() const {
    return !isEmergencyActive();
}
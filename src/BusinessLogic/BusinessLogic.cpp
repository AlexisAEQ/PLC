#include "BusinessLogic/BusinessLogic.h"
#include "BorneUniverselle/BorneUniverselle.h"  // Include complet maintenant possible
#include "BusinessLogic.h"

// le 17 septembre 2025 remove checkNodeTypeConsistency, getTypedPointerForHashImpl

// Définition du membre statique nodeInfos
//std::vector<NodeInfo> BusinessLogic::nodeInfos;

// ========================================
// GESTION D'ÉTAT (State Pattern)
// ========================================

void BusinessLogic::transition(int newState) {
    if (currentState != newState) {
        int oldState = currentState;
        
        // Détection spéciale pour EMERGENCY
        bool isEmergencyTransition = (newState == static_cast<int>(BaseState::EMERGENCY));

        // Log de la transition
        if (isEmergencyTransition) {
            // ⚠️ Pas de log ici - transitionWithContext() le fait déjà avec la raison
            // Juste continuer avec la transition
        } else {
            Serial.printf("%lu:: 🚨Transition from %s to %s\r\n", 
                         (unsigned long)millis(),
                         stateToString(currentState), 
                         stateToString(newState));
        }
        
        
        // Hook de sortie de l'ancien état
        onStateExit(oldState, newState);
        
        // Mise à jour des variables d'état
        previousState = currentState;
        currentState = newState;
        stateStartTime = millis();
        stateAlreadyPrinted = false;
        
        // Hook d'entrée dans le nouvel état
        onStateEnter(newState, oldState);
        
        // Notification des observateurs
        notifyTransitionObservers(newState, oldState);
        
        // Hook de transition complète
        onTransitionComplete(newState, oldState);
    }
}

void BusinessLogic::printCurrentState() {
    if (!stateAlreadyPrinted) {
        Serial.printf("ÉTAT: %s (état précédent: %s, temps dans l'état: %lu ms)\r\n", 
                      stateToString(currentState), 
                      stateToString(previousState),
                      (unsigned long)getTimeInCurrentState());
        stateAlreadyPrinted = true;
    }
}

void BusinessLogic::notifyTransitionObservers(int newState, int oldState) {
    for (auto& callback : transitionCallbacks) {
        try {
            callback(newState, oldState);
        } catch (const std::exception& e) {
            Serial.printf("Erreur dans callback de transition: %s\r\n", e.what());
        }
    }
}

// ========================================
// UTILITAIRES POUR LES TYPES
// ========================================

std::string BusinessLogic::getTypeName(int classType) {
    switch (classType) {
        case CLASS_NODE: return "Node";
        case CLASS_INPUT_NODE: return "InputNode";
        case CLASS_OUTPUT_NODE: return "OutputNode";
        case CLASS_BOOLEAN_INPUT_NODE: return "BooleanInputNode";
        case CLASS_BOOLEAN_OUTPUT_NODE: return "BooleanOutputNode";
        case CLASS_UINT16_INPUT_NODE: return "Uint16InputNode";
        case CLASS_UINT16_OUTPUT_NODE: return "Uint16OutputNode";
        case CLASS_UINT32_INPUT_NODE: return "Uint32InputNode";
        case CLASS_UINT32_OUTPUT_NODE: return "Uint32OutputNode";
        case CLASS_HW_BOOLEAN_INPUT_NODE: return "HardwareBooleanInputNode";
        case CLASS_HW_BOOLEAN_OUTPUT_NODE: return "HardwareBooleanOutputNode";
        case CLASS_VIRTUAL_BOOLEAN_INPUT_NODE: return "VirtualBooleanInputNode";
        case CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE: return "VirtualBooleanOutputNode";
        case CLASS_PFC8574_BOOLEAN_INPUT_NODE: return "PF8574BooleanInputNode";
        case CLASS_PFC8574_BOOLEAN_OUTPUT_NODE: return "PF8574BooleanOutputNode";
        case CLASS_PCA9554_BOOLEAN_OUTPUT_NODE: return "PCA9554BooleanOutputNode";
        case CLASS_VIRTUAL_UINT32_INPUT_NODE: return "VirtualUint32InputNode";
        case CLASS_VIRTUAL_UINT32_OUTPUT_NODE: return "VirtualUint32OutputNode";
        case CLASS_MODBUS_READ_COIL: return "ModbusReadCoil";
        case CLASS_MODBUS_WRITE_COIL_NODE: return "ModbusWriteCoilNode";
        case CLASS_MODBUS_READHOLDINGREGISTER: return "ModbusReadHoldingRegister";
        case CLASS_MODBUS_WRITEHOLDINGREGISTER: return "ModbusWriteHoldingRegister";
        case CLASS_MODBUS_READINPUTREGISTER: return "ModbusReadInputRegister";
        case CLASS_MODBUS_READ_DOUBLE_INPUTREGISTER: return "ModbusReadDoubleInputRegister";
        case CLASS_MODBUS_WRITE_DOUBLE_INPUTREGISTER: return "ModbusWriteDoubleInputRegister";
        case CLASS_MODBUS_READ_DOUBLE_HOLDING_REGISTER: return "ModbusReadDoubleHoldingRegister";
        case CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER: return "ModbusWriteDoubleHoldingRegister";
        case CLASS_MODBUS_WRITE_MULTIPLE_COILS: return "ModbusWriteMultipleCoils";
        case CLASS_MODBUS_READ_MULTIPLE_INPUTS_STATUS: return "ModbusReadMultipleInputsStatus";
        case CLASS_FLOAT_INPUT_NODE: return "FloatInputNode";
        case CLASS_FLOAT_OUTPUT_NODE: return "FloatOutputNode";
        case CLASS_VIRTUAL_FLOAT_INPUT_NODE: return "VirtualFloatInputNode";
        case CLASS_VIRTUAL_FLOAT_OUTPUT_NODE: return "VirtualFloatOutputNode";
        case CLASS_TEXT_INPUT_NODE: return "TextInputNode";
        case CLASS_VIRTUAL_TEXT_INPUT_NODE: return "VirtualTextInputNode";
        case CLASS_TEXT_OUTPUT_NODE: return "TextOutputNode";

        default: return "UnknownType";
    }
}

int BusinessLogic::getClassTypeFromTypeInfo(const std::type_info& typeInfo) {
    Serial.printf("Type demandé: %s\n", typeInfo.name());

    // Classes de base
    if (typeInfo == typeid(Node*)) return CLASS_NODE;
    if (typeInfo == typeid(InputNode*)) return CLASS_INPUT_NODE;
    if (typeInfo == typeid(OutputNode*)) return CLASS_OUTPUT_NODE;
    if (typeInfo == typeid(BooleanInputNode*)) return CLASS_BOOLEAN_INPUT_NODE;
    if (typeInfo == typeid(BooleanOutputNode*)) return CLASS_BOOLEAN_OUTPUT_NODE;
    if (typeInfo == typeid(Uint16InputNode*)) return CLASS_UINT16_INPUT_NODE;
    if (typeInfo == typeid(Uint16OutputNode*)) return CLASS_UINT16_OUTPUT_NODE;
    if (typeInfo == typeid(Uint32InputNode*)) return CLASS_UINT32_INPUT_NODE;
    if (typeInfo == typeid(Uint32OutputNode*)) return CLASS_UINT32_OUTPUT_NODE;
    if (typeInfo == typeid(HardwareBooleanInputNode*)) return CLASS_HW_BOOLEAN_INPUT_NODE;
    if (typeInfo == typeid(HardwareBooleanOutputNode*)) return CLASS_HW_BOOLEAN_OUTPUT_NODE;
    if (typeInfo == typeid(VirtualBooleanInputNode*)) return CLASS_VIRTUAL_BOOLEAN_INPUT_NODE;
    if (typeInfo == typeid(VirtualBooleanOutputNode*)) return CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE;
    if (typeInfo == typeid(PF8574BooleanInputNode*)) return CLASS_PFC8574_BOOLEAN_INPUT_NODE;
    if (typeInfo == typeid(PF8574BooleanOutputNode*)) return CLASS_PFC8574_BOOLEAN_OUTPUT_NODE;
    if (typeInfo == typeid(PCA9554BooleanOutputNode*)) return CLASS_PCA9554_BOOLEAN_OUTPUT_NODE;
    if (typeInfo == typeid(VirtualUint32InputNode*)) return CLASS_VIRTUAL_UINT32_INPUT_NODE;
    if (typeInfo == typeid(VirtualUint32OutputNode*)) return CLASS_VIRTUAL_UINT32_OUTPUT_NODE;
    if (typeInfo == typeid(ModbusReadCoilNode*)) return CLASS_MODBUS_READ_COIL;
    if (typeInfo == typeid(ModbusWriteCoilNode*)) return CLASS_MODBUS_WRITE_COIL_NODE;
    if (typeInfo == typeid(ModbusReadHoldingRegister*)) return CLASS_MODBUS_READHOLDINGREGISTER;
    if (typeInfo == typeid(ModbusWriteHoldingRegister*)) return CLASS_MODBUS_WRITEHOLDINGREGISTER;
    if (typeInfo == typeid(ModbusReadInputRegister*)) return CLASS_MODBUS_READINPUTREGISTER;
    if (typeInfo == typeid(ModbusReadDobbleInputRegisters*)) return CLASS_MODBUS_READ_DOUBLE_INPUTREGISTER;
  
    if (typeInfo == typeid(ModbusReadDoubleHoldingRegisters*)) return CLASS_MODBUS_READ_DOUBLE_HOLDING_REGISTER;
    if (typeInfo == typeid(ModbusWriteDoubleHoldingRegister*)) return CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER;
    if (typeInfo == typeid(ModbusWriteMultipleCoilslNode*)) return CLASS_MODBUS_WRITE_MULTIPLE_COILS;
    if (typeInfo == typeid(ModbusReadMultipleInputsRegistersNode*)) return CLASS_MODBUS_READ_MULTIPLE_INPUTS_STATUS;
    if (typeInfo == typeid(FloatInputNode*)) return CLASS_FLOAT_INPUT_NODE;
    if (typeInfo == typeid(FloatOutputNode*)) return CLASS_FLOAT_OUTPUT_NODE;
    if (typeInfo == typeid(VirtualFloatInputNode*)) return CLASS_VIRTUAL_FLOAT_INPUT_NODE;
    if (typeInfo == typeid(VirtualFloatOutputNode*)) return CLASS_VIRTUAL_FLOAT_OUTPUT_NODE;
    if (typeInfo == typeid(TextInputNode*)) return CLASS_TEXT_INPUT_NODE;
    if (typeInfo == typeid(VirtualTextInputNode*)) return CLASS_VIRTUAL_TEXT_INPUT_NODE;
    if (typeInfo == typeid(TextOutputNode*)) return CLASS_TEXT_OUTPUT_NODE;
    if (typeInfo == typeid(VirtualTextOutputNode*)) return CLASS_VIRTUAL_TEXT_OUTPUT_NODE;
    
    char buff[256];
    sprintf(buff, "Type inconnu pour le hash: %s", typeInfo.name());
    return -1; // Type inconnu
}

Node* BusinessLogic::getNodePointerForHash(uint32_t hash, const char* context) {
    return BorneUniverselle::getInstance()->findNode(context, hash);
}

 // ========================================
    // MÉTHODES PROTÉGÉES POUR LES CLASSES DÉRIVÉES
    // ========================================
    
    /**
     * @brief Effectue une transition avec vérification de timeout
     * @param newState Nouvel état
     * @param currentTimeoutMs Timeout de l'état actuel (0 = pas de timeout)
     * @param reason Raison de la transition
     * @return true si la transition s'est bien déroulée
     */
bool  BusinessLogic::transitionWithTimeout(int newState, uint32_t currentTimeoutMs, const char* reason) {
    // Vérifier le timeout si spécifié
    if (currentTimeoutMs > 0 && isStateTimeoutExceeded(currentTimeoutMs)) {
        char timeoutReason[128];
        snprintf(timeoutReason, sizeof(timeoutReason), 
                "Timeout de %lu ms dépassé dans l'état %s", 
                (unsigned long)currentTimeoutMs, stateToString(currentState));
        
        logStateTransition(currentState, newState, timeoutReason);
        transition(newState);
        return false; // Indique que c'était un timeout
    }
    
    // Transition normale
    if (reason) {
        logStateTransition(currentState, newState, reason);
    }
    transition(newState);
    return true;
}

bool BusinessLogic::isStateTimeoutExceeded(uint32_t timeoutMs) const {
        return stateStartTime > 0 && (millis() - stateStartTime > timeoutMs);
}

void BusinessLogic::logStateTransition(int fromState, int toState, const char* reason) {
    Serial.printf("%lu:: Transition %s -> %s", 
                    (unsigned long)millis(), 
                    stateToString(fromState), 
                    stateToString(toState));
    
    if (reason) {
        Serial.printf(" : %s", reason);
    }
    
    Serial.println();
}

uint32_t BusinessLogic::getTimeInCurrentState() {
        return stateStartTime > 0 ? (millis() - stateStartTime) : 0;
}

void BusinessLogic::handleExecutorFailure() {
    // 🔧 CODE CENTRALISÉ dans BusinessLogic
    BorneUniverselle::prepareMessage(ERROR, "Attention: the PLC is broken");
    Serial.println("❌ BusinessLogic: Logic execution failed - PLC will be marked as broken");
}

void BusinessLogic::addToBaseHistory(int state, StateType type, uint32_t context) {
    // Éviter les doublons consécutifs
    if (!baseStateHistory.empty()) {
        const auto& last = baseStateHistory.back();
        if (last.state == state && 
            last.context == context &&
            (millis() - last.timestamp) < 100) {
            return;
        }
    }
    
    // Ajouter la nouvelle entrée
    baseStateHistory.emplace_back(state, type, millis(), context);
    
    if (baseStateHistory.size() > DEFAULT_MAX_HISTORY_SIZE) {
        baseStateHistory.erase(baseStateHistory.begin());
    }
    
    if (BorneUniverselle::getInstance()->isMachineStateLogging()) {
        Serial.printf("📝 History: %s [%s] context:%s\n", 
                      stateToString(state),
                      stateTypeToString(type),
                      getContextName(context));
        
        const char* desc = getStateDescription(state);
        if (desc) {
            Serial.printf("   └─ %s\n", desc);
        }
    }
}

void BusinessLogic::clearStateHistory() {
    size_t sizeBefore = baseStateHistory.size();  // Capturer avant clear()
    baseStateHistory.clear();
    
    if (BorneUniverselle::getInstance()->isMachineStateLogging()) {
        Serial.printf("🗑️ History cleared (%zu entries removed)\n", sizeBefore);
    }
}

size_t BusinessLogic::getHistorySize() const {
    return baseStateHistory.size();
}

void BusinessLogic::printBaseStateHistory() const {
    // Calcul du temps actuel pour les durées relatives
    uint32_t now = millis();
    
    Serial.println("\n╔════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                      DETAILED STATE HISTORY                       ║");
    Serial.println("╠════════════════════════════════════════════════════════════════════╣");
    Serial.printf("║ Total Entries: %2d / %2d                                           ║\n", 
                  baseStateHistory.size(), 
                  DEFAULT_MAX_HISTORY_SIZE);
    Serial.printf("║ Current Time: %lu ms                                              ║\n", 
                  (unsigned long)now);
    Serial.println("╠════════════════════════════════════════════════════════════════════╣");
    
    if (baseStateHistory.empty()) {
        Serial.println("║                                                                    ║");
        Serial.println("║                    📭 No history recorded yet                     ║");
        Serial.println("║                                                                    ║");
    } else {
        for (size_t i = 0; i < baseStateHistory.size(); i++) {
            const auto& entry = baseStateHistory[i];
            
            // Calcul du temps écoulé
            uint32_t elapsed = now - entry.timestamp;
            char timeStr[20];
            
            // Formatage intelligent du temps
            if (elapsed < 1000) {
                snprintf(timeStr, sizeof(timeStr), "%lu ms ago", (unsigned long)elapsed);
            } else if (elapsed < 60000) {
                snprintf(timeStr, sizeof(timeStr), "%.1f sec ago", elapsed / 1000.0);
            } else if (elapsed < 3600000) {
                snprintf(timeStr, sizeof(timeStr), "%lu min ago", elapsed / 60000);
            } else {
                snprintf(timeStr, sizeof(timeStr), "%.1f hrs ago", elapsed / 3600000.0);
            }
            
            // Obtenir les informations via les méthodes virtuelles
            const char* stateName = stateToString(entry.state);  // Utilise la méthode existante
            const char* stateDesc = nullptr;  // Pas de getStateDescription pour l'instant
            const char* contextName = getContextName(entry.context);
            const char* typeName = stateTypeToString(entry.type);
            
            // Icône selon le type
            const char* icon = "📍";
            switch(entry.type) {
                case StateType::STABLE:    icon = "✅"; break;
                case StateType::UTILITY:   icon = "🔧"; break;
                case StateType::EMERGENCY: icon = "🚨"; break;
                case StateType::TRANSIENT: icon = "⏳"; break;
            }
            
            // En-tête de l'entrée
            Serial.println("║                                                                    ║");
            Serial.printf("║ ┌─[Entry %2d]────────────────────────────────────────────────────┐ ║\n", i);
            
            // État
            Serial.printf("║ │ %s State: %-16s (value: %3d)                   │ ║\n", 
                         icon, stateName, entry.state);
            
            // Description si disponible
            if (stateDesc) {
                // Tronquer la description si trop longue
                char descBuffer[50];
                snprintf(descBuffer, sizeof(descBuffer), "%.48s", stateDesc);
                Serial.printf("║ │    └─ %-56s │ ║\n", descBuffer);
            }
            
            // Type
            Serial.printf("║ │ Type: %-10s                                              │ ║\n", 
                         typeName);
            
            // Contexte
            Serial.printf("║ │ Context: %-20s (code: %3lu)                      │ ║\n", 
                         contextName, entry.context);
            
            // Timestamps
            Serial.printf("║ │ Timestamp: %10lu ms  (%s)              │ ║\n", 
                         (unsigned long)entry.timestamp, timeStr);
            
            // Ligne de fermeture
            Serial.println("║ └──────────────────────────────────────────────────────────────┘ ║");
        }
        
        // Section de résumé
        Serial.println("║                                                                    ║");
        Serial.println("╠════════════════════════════════════════════════════════════════════╣");
        Serial.println("║                            SUMMARY                                ║");
        Serial.println("╠════════════════════════════════════════════════════════════════════╣");
        
        // Compter les types
        int stableCount = 0, utilityCount = 0, emergencyCount = 0, transientCount = 0;
        for (const auto& entry : baseStateHistory) {
            switch(entry.type) {
                case StateType::STABLE:    stableCount++; break;
                case StateType::UTILITY:   utilityCount++; break;
                case StateType::EMERGENCY: emergencyCount++; break;
                case StateType::TRANSIENT: transientCount++; break;
            }
        }
        
        Serial.printf("║ ✅ Stable states:    %2d                                           ║\n", stableCount);
        Serial.printf("║ 🔧 Utility states:   %2d                                           ║\n", utilityCount);
        Serial.printf("║ 🚨 Emergency states: %2d                                           ║\n", emergencyCount);
        Serial.printf("║ ⏳ Transient states: %2d                                           ║\n", transientCount);
        
        // Dernière transition
        if (baseStateHistory.size() >= 2) {
            const auto& prev = baseStateHistory[baseStateHistory.size() - 2];
            const auto& last = baseStateHistory.back();
            Serial.println("║                                                                    ║");
            Serial.printf("║ Last transition: %s → %s                                         \n", 
                         stateToString(prev.state), 
                         stateToString(last.state));
            
            // Ajuster l'affichage pour que le cadre reste aligné
            size_t len1 = strlen(stateToString(prev.state));
            size_t len2 = strlen(stateToString(last.state));
            size_t totalLen = len1 + len2 + 20; // "Last transition: " + " → "
            
            // Ajouter des espaces pour compléter jusqu'à 68 caractères
            for (size_t i = totalLen; i < 68; i++) {
                Serial.print(" ");
            }
            Serial.println("║");
        }
    }
    
    Serial.println("╚════════════════════════════════════════════════════════════════════╝\n");
}

const char* BusinessLogic::stateTypeToString(StateType type) {
    switch (type) {
        case StateType::STABLE:
            return "STABLE";
        case StateType::UTILITY:
            return "UTILITY";
        case StateType::EMERGENCY:
            return "EMERGENCY";
        case StateType::TRANSIENT:
            return "TRANSIENT";
        default:
            return "UNKNOWN";
    }
}

int BusinessLogic::findLastStateOfTypeInHistory(StateType targetType) const {
    for (auto it = baseStateHistory.rbegin(); it != baseStateHistory.rend(); ++it) {
        if (it->type == targetType && it->state != currentState) {
            if (BorneUniverselle::getInstance()->isMachineStateLogging()) {
                Serial.printf("🔍 Found last %s: %s (state %d)\n", 
                              stateTypeToString(targetType),
                              stateToString(it->state),
                              it->state);
            }
            return it->state;
        }
    }
    
    if (BorneUniverselle::getInstance()->isMachineStateLogging()) {
        Serial.printf("🔍 No %s state found in history\n", 
                      stateTypeToString(targetType));
    }
    
    return -1;
}

const char* BusinessLogic::getContextName(uint32_t context) const {
        // Implémentation par défaut
        static char buffer[16];
        snprintf(buffer, sizeof(buffer), "CTX_%lu", context);
        return buffer;
}

uint32_t BusinessLogic::detectTransitionContext(
    int oldState, 
    int newState,
    StateType oldType,
    StateType newType) const {
    
    // Logique générique de détection basée sur les types
    // Les valeurs retournées sont génériques (0, 1, 2, etc.)
    
    if (newType == StateType::EMERGENCY) {
        return 10;  // Code générique pour entrée urgence
    } else if (newType == StateType::UTILITY) {
        return 2;   // Code générique pour avant utilitaire
    } else if (oldType == StateType::EMERGENCY) {
        return 11;  // Code générique pour sortie urgence
    } else if (oldType == StateType::UTILITY) {
        return 3;   // Code générique pour succès mouvement
    } else {
        return 0;   // Code générique pour normal
    }
}

void BusinessLogic::transitionWithContext(int newState, uint32_t context) {
    int oldState = currentState;
    
    // Si le contexte est 0 ou 1 (AUTO_DETECT), détecter automatiquement
    if (context <= 1) {
        StateType oldType = classifyState(oldState);
        StateType newType = classifyState(newState);
        context = detectTransitionContext(oldState, newState, oldType, newType);
    }
    
    // Ajouter à l'historique avant transition
    if (oldState != newState) {
        StateType type = classifyState(oldState);
        addToBaseHistory(oldState, type, context);
        
        bool isEmergencyTransition = (newState == static_cast<int>(BaseState::EMERGENCY));

        if (isEmergencyTransition) {
            // Préparer le texte de la raison
            char reasonText[128];
            const char* contextText = contextToString(context);
            
            if (strcmp(contextText, "CTX_") == 0 || strstr(contextText, "CTX_") != nullptr) {
                // Contexte inconnu, afficher aussi le code pour debug
                snprintf(reasonText, sizeof(reasonText), "%s (code: %lu)", 
                        contextText, (unsigned long)context);
            } else {
                // Contexte connu, le texte suffit
                snprintf(reasonText, sizeof(reasonText), "%s", contextText);
            }
            
            // Créer le message complet pour diagnostic
            char diagnosticText[256];
            snprintf(diagnosticText, sizeof(diagnosticText), 
                    "EMERGENCY TRANSITION: %s -> %s, Reason: %s", 
                    stateToString(oldState), stateToString(newState), reasonText);
            PLC_Tools::logDiagnostic(diagnosticText);
            
        } else {
            // Transition normale
            const char* contextText = contextToString(context);
            Serial.printf("🔄 Transition: %s -> %s, Context: %s\r\n", 
                         stateToString(oldState), stateToString(newState), contextText);
        }
    }
    
    // Effectuer la transition
    transition(newState);
}

/**
 * @brief Convertit un contexte en chaîne de caractères (implémentation par défaut)
 * 
 * Fournit une représentation textuelle basique pour les contextes génériques.
 * Les classes dérivées peuvent override cette méthode pour leurs contextes spécifiques.
 * 
 * @param context Le code contexte
 * @return Description du contexte ou "CTX_X" pour les contextes inconnus
 */
const char* BusinessLogic::contextToString(uint32_t context) const {
    switch(context) {
        case 0: return "Normal";
        case 1: return "Détection automatique";
        default: {
            static char buffer[16];
            snprintf(buffer, sizeof(buffer), "CTX_%lu", context);
            return buffer;
        }
    }
}

/**
 * @brief Classifie un contexte dans une catégorie (implémentation par défaut)
 * 
 * Fournit une classification basique pour les contextes génériques.
 * Les classes dérivées peuvent override pour une classification plus précise.
 * 
 * @param context Le code contexte
 * @return La catégorie (NORMAL pour 0-1, UNKNOWN pour le reste)
 */
BusinessLogic::ContextCategory BusinessLogic::classifyContext(uint32_t context) const {
    if (context <= 1) {
        return ContextCategory::NORMAL;
    }
    return ContextCategory::UNKNOWN;
}

void BusinessLogic::handleSuspendRequest(bool suspend) {
    if (suspend) {
        // Demande de SUSPENSION - vérifier si IDLE
        if (!isIdle()) {
            Serial.printf("%lu:: ❌ Suspend REFUSED - not in IDLE state\r\n", millis());
            
            // Notifier le refus
            if (suspendCallback) {
                suspendCallback(true, false, "Machine not in IDLE state");
            }
            return;
        }
        
        // OK pour suspendre
        Serial.printf("%lu:: ✅ Suspend ACCEPTED - machine is IDLE\r\n", millis());
        
        // Hook pour actions spécifiques (optionnel)
        onSuspendRequested();
        
        // Notifier l'acceptation
        if (suspendCallback) {
            suspendCallback(true, true, nullptr);
        }
        
    } else {
        // Demande de REPRISE - toujours OK
        Serial.printf("%lu:: ✅ Resume ACCEPTED\r\n", millis());
        
        // Hook pour actions spécifiques (optionnel)
        onResumeRequested();
        
        // Notifier la reprise
        if (suspendCallback) {
            suspendCallback(false, true, nullptr);
        }
    }
}

void BusinessLogic::handleIsPLCIdleRequest() const {
    Serial.printf("%lu:: 🔵 PLC isIdle() = %s\r\n", millis(), isIdle() ? "true" : "false");
    JsonDocument doc;
    doc[PLC_IS_IDLE] = isIdle();
    char chain[100];
    serializeJson(doc, chain, sizeof(chain));
    BorneUniverselle::getInstance()->sendTextToClient(chain);
}

bool BusinessLogic::isIdle() const {
    int idle = static_cast<int>(BaseState::IDLE);
    int stop = static_cast<int>(BaseState::STOP);
    int emergency = static_cast<int>(BaseState::EMERGENCY);
    int initializing = static_cast<int>(BaseState::INITIALIZING);
    // JOGGING volontairement EXCLU : un axe bouge pendant le jog → pas sûr pour écrire en flash.
    // Idle = uniquement les 4 états immobiles de base (INITIALIZING, STOP, IDLE, EMERGENCY).
    // Tout état métier (valeur >= 100) est par défaut "non idle" → fail-closed.
    return (currentState == idle || currentState == stop || currentState == emergency) || (currentState == initializing);
}

void BusinessLogic::requestRestart(uint32_t const timeBeforRestartMs, char const *reason) {
    if (restartPending) {
        Serial.printf("%lu:: ⚠️ Restart already pending, ignoring new request\r\n", (unsigned long)millis());
        return;
    }
    restartPending = true;
    restartRequestedAt = millis();  // Moment de la requête
    restartDelayMs = timeBeforRestartMs;  // Délai demandé

    char text[300];
    snprintf(text, sizeof(text), "System restart requested in %lu ms. Reason: %s", 
             (unsigned long)timeBeforRestartMs,
             reason ? reason : "No reason provided");
    BorneUniverselle::prepareMessage(ERROR, text);
   
}

/**
 * @brief Implémentation par défaut de logicExecutor
 * 
 * Cette méthode vérifie d'abord si un redémarrage est en attente.
 * Les classes dérivées peuvent override pour ajouter leur logique spécifique.
 */
bool BusinessLogic::logicExecutor() {
    // Vérifier si un redémarrage est demandé
    if (restartPending) {
        uint32_t now = millis();
        uint32_t elapsed = now - restartRequestedAt;
        
        if (elapsed >= RESTART_GRACE_PERIOD_MS) {
            // Le délai de grâce est écoulé, redémarrer maintenant
            Serial.println("========================================");
            Serial.println("   RESTART REQUESTED - SHUTTING DOWN");
            Serial.println("========================================");
            
            // Appeler le hook pour les classes dérivées
            onBeforeRestart();
            
            // Donner un peu de temps pour flush les logs
            delay(500);
            
            // Redémarrer
            ESP.restart();
            
            // Ne devrait jamais être atteint
            return false;
        } else {
            // En période de grâce - informer
            static uint32_t lastLog = 0;
            if (now - lastLog > 500) {
                Serial.printf("⏳ Restart in %lu ms...\r\n", 
                             RESTART_GRACE_PERIOD_MS - elapsed);
                lastLog = now;
            }
        }
    } // restart pending
    
    // Logique commune à tous les projets (peut être étendue)
    // Par exemple: vérifications de sécurité, timeouts, etc.
    doBusinessLogic();
    // Sauvegarde différée — commun à tous les projets
    if (isIdle()){
        PLC_Persistence::getInstance().process(millis());
        PLC_Tools::flushDiagnosticBufferIfIdle();  // B-2 : vider le diagnostic accumulé pendant le mouvement
    }
    return true;
}

void  BusinessLogic::onTabChange(const char* tabName) {
       PLC_Persistence::getInstance().flushAll();
       Serial.printf("BusinessLogic::onTabChange: %s (default handler, no action)\r\n", tabName);
}

void BusinessLogic::onFileSaved(const char* path, bool success) {
    Serial.printf("BusinessLogic::onFileSaved called for %s, success=%s\r\n", path ? path : "NULL", success ? "true" : "false");
    // Implémentation par défaut : ne rien faire
    // Les classes dérivées (Formaca2) peuvent override
}

bool BusinessLogic::validateConfigFile(const char* path, const JsonDocument& doc, String& errorMsg) {
    // Implémentation par défaut : pas de validation spécifique
    // Les classes dérivées (Formaca2) peuvent override pour leurs fichiers
    return true;
}

bool BusinessLogic::shouldProcessFileCallback(const char* path) {
    if (!path) {
        Serial.println("BusinessLogic: Callback REJETÉ - chemin NULL");
        return false;
    }

    Serial.printf("BusinessLogic: Vérification du callback pour %s\r\n", path);
    
    if (strcmp(lastSave.path, path) == 0) {
        unsigned long elapsed = millis() - lastSave.timestamp;
        if (elapsed < SAVE_CALLBACK_DEBOUNCE_MS) {
            Serial.printf("BusinessLogic: Callback FILTRÉ pour %s (sauvegardé il y a %lu ms)\r\n", 
                         path, elapsed);
            
            // Réinitialiser pour éviter de filtrer uploads futurs
            lastSave.path[0] = '\0';
            lastSave.timestamp = 0;
            
            return false;
        }
    }
    
    // NE PLUS mettre à jour lastSave ici (c'est fait par markInternalSave)
    
    Serial.printf("BusinessLogic: Callback ACCEPTÉ pour %s\r\n", path);
    return true;
}

void BusinessLogic::markInternalSave(const char* path) {
    if (!path) {
        return;
    }
    
    // Enregistrer le fichier et le timestamp MAINTENANT (avant la sauvegarde)
    strncpy(lastSave.path, path, sizeof(lastSave.path) - 1);
    lastSave.path[sizeof(lastSave.path) - 1] = '\0';
    lastSave.timestamp = millis();
    
    Serial.printf("BusinessLogic::markInternalSave: Enregistré '%s' (timestamp: %lu)\r\n", 
                  path, lastSave.timestamp);
}

void BusinessLogic::clearClock() {
    char *txt = clockNode->getValue();

    if (txt[0] != '-' ) {
        Serial.println("BusinessLogic: Clearing clock node");
        clockNode->setValue("-");
    }
    
}

void BusinessLogic::updateClockNode(VirtualTextOutputNode* _clockNode) {
    if (!_clockNode) return;

    clockNode = _clockNode;  // Stocker la référence pour les mises à jour futures
    
    // Si on n'est pas dans un état approprié, effacer l'horloge
    if (!shouldShowClock()) {
        clearClock();
    }
    
    // Obtenir le service RTC
    RTC_Service* rtc = RTC_Service::getInstance();
    if (!rtc) {
        clearClock();
        return;
    }
    
    // Obtenir l'heure formatée
    char chain[128];
    bool success = rtc->getTimestamp(chain, sizeof(chain));
    
    // Mettre à jour la variable d'interface
    if (clockNode && success) {
        const char* currentTime = clockNode->getValue();
        if (currentTime && strcmp(chain, currentTime) != 0) {  // Si différent
            Serial.printf("BusinessLogic: Updating clock node to '%s'\r\n", chain);
            clockNode->setValue(chain);  
        }
    }
}
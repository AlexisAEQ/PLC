#ifndef BUSINESS_LOGIC_INL
#define BUSINESS_LOGIC_INL

#include "BorneUniverselle/borneUniverselle.h"
#include "BusinessLogic/BusinessLogic.h"

template<typename T>
bool isTypeCompatible(int classType) {
    // BooleanInputNode est compatible avec tous les Input Boolean concrets
    if (std::is_same<T, BooleanInputNode>::value) {
        return (classType == CLASS_BOOLEAN_INPUT_NODE ||
                classType == CLASS_HW_BOOLEAN_INPUT_NODE ||
                classType == CLASS_PFC8574_BOOLEAN_INPUT_NODE ||
                classType == CLASS_VIRTUAL_BOOLEAN_INPUT_NODE ||
                classType == CLASS_MODBUS_READ_COIL ||
                classType == CLASS_MODBUS_READ_MULTIPLE_INPUTS_STATUS);
    }
    
    // BooleanOutputNode est compatible avec tous les Output Boolean concrets
    if (std::is_same<T, BooleanOutputNode>::value) {
        return (classType == CLASS_BOOLEAN_OUTPUT_NODE ||
                classType == CLASS_HW_BOOLEAN_OUTPUT_NODE ||
                classType == CLASS_PFC8574_BOOLEAN_OUTPUT_NODE ||
                classType == CLASS_PCA9554_BOOLEAN_OUTPUT_NODE ||
                classType == CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE ||
                classType == CLASS_MODBUS_WRITE_COIL_NODE ||
                classType == CLASS_MODBUS_WRITE_MULTIPLE_COILS);
    }
    
    // Uint16InputNode et ses dérivés
    if (std::is_same<T, Uint16InputNode>::value) {
        return (classType == CLASS_UINT16_INPUT_NODE ||
                classType == CLASS_MODBUS_READHOLDINGREGISTER ||
                classType == CLASS_MODBUS_READINPUTREGISTER);
    }
    
    // Uint16OutputNode et ses dérivés
    if (std::is_same<T, Uint16OutputNode>::value) {
        return (classType == CLASS_UINT16_OUTPUT_NODE ||
                classType == CLASS_MODBUS_WRITEHOLDINGREGISTER);
    }
    
    // Uint32InputNode et ses dérivés
    if (std::is_same<T, Uint32InputNode>::value) {
        return (classType == CLASS_UINT32_INPUT_NODE ||
                classType == CLASS_VIRTUAL_UINT32_INPUT_NODE ||
                classType == CLASS_MODBUS_READ_DOUBLE_INPUTREGISTER ||
                classType == CLASS_MODBUS_READ_DOUBLE_HOLDING_REGISTER);
    }
    
    // Uint32OutputNode et ses dérivés
    if (std::is_same<T, Uint32OutputNode>::value) {
        return (classType == CLASS_UINT32_OUTPUT_NODE ||
                classType == CLASS_VIRTUAL_UINT32_OUTPUT_NODE ||
                classType == CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER);
    }
    
    // FloatInputNode et ses dérivés
    if (std::is_same<T, FloatInputNode>::value) {
        return (classType == CLASS_FLOAT_INPUT_NODE ||
                classType == CLASS_VIRTUAL_FLOAT_INPUT_NODE);
    }
    
    // FloatOutputNode et ses dérivés
    if (std::is_same<T, FloatOutputNode>::value) {
        return (classType == CLASS_FLOAT_OUTPUT_NODE ||
                classType == CLASS_VIRTUAL_FLOAT_OUTPUT_NODE);
    }
    
    // InputNode (très abstrait) - compatible avec TOUS les inputs
    if (std::is_same<T, InputNode>::value) {
        return (classType == CLASS_INPUT_NODE ||
                classType == CLASS_BOOLEAN_INPUT_NODE ||
                classType == CLASS_UINT16_INPUT_NODE ||
                classType == CLASS_UINT32_INPUT_NODE ||
                classType == CLASS_FLOAT_INPUT_NODE ||
                classType == CLASS_TEXT_INPUT_NODE ||
                classType == CLASS_HW_BOOLEAN_INPUT_NODE ||
                classType == CLASS_VIRTUAL_BOOLEAN_INPUT_NODE ||
                classType == CLASS_PFC8574_BOOLEAN_INPUT_NODE ||
                classType == CLASS_VIRTUAL_UINT32_INPUT_NODE ||
                classType == CLASS_VIRTUAL_FLOAT_INPUT_NODE ||
                classType == CLASS_VIRTUAL_TEXT_INPUT_NODE ||
                classType == CLASS_MODBUS_READ_COIL ||
                classType == CLASS_MODBUS_READHOLDINGREGISTER ||
                classType == CLASS_MODBUS_READINPUTREGISTER ||
                classType == CLASS_MODBUS_READ_DOUBLE_INPUTREGISTER ||
                classType == CLASS_MODBUS_READ_DOUBLE_HOLDING_REGISTER ||
                classType == CLASS_MODBUS_READ_MULTIPLE_INPUTS_STATUS);
    }
    
    // OutputNode (très abstrait) - compatible avec TOUS les outputs
    if (std::is_same<T, OutputNode>::value) {
        return (classType == CLASS_OUTPUT_NODE ||
                classType == CLASS_BOOLEAN_OUTPUT_NODE ||
                classType == CLASS_UINT16_OUTPUT_NODE ||
                classType == CLASS_UINT32_OUTPUT_NODE ||
                classType == CLASS_FLOAT_OUTPUT_NODE ||
                classType == CLASS_TEXT_OUTPUT_NODE ||
                classType == CLASS_HW_BOOLEAN_OUTPUT_NODE ||
                classType == CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE ||
                classType == CLASS_PFC8574_BOOLEAN_OUTPUT_NODE ||
                classType == CLASS_PCA9554_BOOLEAN_OUTPUT_NODE ||
                classType == CLASS_VIRTUAL_UINT32_OUTPUT_NODE ||
                classType == CLASS_VIRTUAL_FLOAT_OUTPUT_NODE ||
                classType == CLASS_VIRTUAL_TEXT_OUTPUT_NODE ||
                classType == CLASS_MODBUS_WRITE_COIL_NODE ||
                classType == CLASS_MODBUS_WRITEHOLDINGREGISTER ||
                classType == CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER ||
                classType == CLASS_MODBUS_WRITE_MULTIPLE_COILS);
    }
    
    // Node (racine) - compatible avec TOUT
    if (std::is_same<T, Node>::value) {
        return true; // Tout est un Node
    }
    
    return false;
}

template<typename T>
void handleTypeMismatchError(Node* tmpNode, uint32_t hash, const char* context) {
    const char* expectedType = "UNKNOWN";
    const char* actualType = "UNKNOWN";
    
    // Déterminer le type attendu
    if (std::is_same<T, BooleanOutputNode>::value) expectedType = "tx-bool";
    else if (std::is_same<T, BooleanInputNode>::value) expectedType = "rx-bool";
    else if (std::is_same<T, Uint16InputNode>::value) expectedType = "rx-uint16";
    else if (std::is_same<T, Uint16OutputNode>::value) expectedType = "tx-uint16";
    else if (std::is_same<T, Uint32InputNode>::value) expectedType = "rx-uint32";
    else if (std::is_same<T, Uint32OutputNode>::value) expectedType = "tx-uint32";
    else if (std::is_same<T, FloatInputNode>::value) expectedType = "rx-float";
    else if (std::is_same<T, FloatOutputNode>::value) expectedType = "tx-float";
    else if (std::is_same<T, VirtualFloatInputNode>::value) expectedType = "rx-float";
    else if (std::is_same<T, VirtualFloatOutputNode>::value) expectedType = "tx-float";
    else if (std::is_same<T, InputNode>::value) expectedType = "rx-*";
    else if (std::is_same<T, OutputNode>::value) expectedType = "tx-*";
    
    // Déterminer le type actuel avec un switch (comme dans votre code original)
    int classType = tmpNode->classType();
    switch (classType) {
        case CLASS_BOOLEAN_INPUT_NODE: 
        case CLASS_HW_BOOLEAN_INPUT_NODE:
        case CLASS_PFC8574_BOOLEAN_INPUT_NODE:
        case CLASS_VIRTUAL_BOOLEAN_INPUT_NODE:
        case CLASS_MODBUS_READ_COIL:
            actualType = "rx-bool"; 
            break;
            
        case CLASS_BOOLEAN_OUTPUT_NODE:
        case CLASS_HW_BOOLEAN_OUTPUT_NODE:
        case CLASS_PFC8574_BOOLEAN_OUTPUT_NODE:
        case CLASS_PCA9554_BOOLEAN_OUTPUT_NODE:
        case CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE:
        case CLASS_MODBUS_WRITE_COIL_NODE:
            actualType = "tx-bool";
            break;
            
        case CLASS_UINT16_INPUT_NODE:
        case CLASS_MODBUS_READHOLDINGREGISTER:
        case CLASS_MODBUS_READINPUTREGISTER:
            actualType = "rx-uint16";
            break;
            
        case CLASS_UINT16_OUTPUT_NODE:
        case CLASS_MODBUS_WRITEHOLDINGREGISTER:
            actualType = "tx-uint16";
            break;
            
        case CLASS_UINT32_INPUT_NODE:
        case CLASS_VIRTUAL_UINT32_INPUT_NODE:
        case CLASS_MODBUS_READ_DOUBLE_INPUTREGISTER:
        case CLASS_MODBUS_READ_DOUBLE_HOLDING_REGISTER:
            actualType = "rx-uint32";
            break;
            
        case CLASS_UINT32_OUTPUT_NODE:
        case CLASS_VIRTUAL_UINT32_OUTPUT_NODE:
        case CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER:
            actualType = "tx-uint32";
            break;
            
        case CLASS_FLOAT_INPUT_NODE:
        case CLASS_VIRTUAL_FLOAT_INPUT_NODE:
            actualType = "rx-float";
            break;
            
        case CLASS_FLOAT_OUTPUT_NODE:
        case CLASS_VIRTUAL_FLOAT_OUTPUT_NODE:
            actualType = "tx-float";
            break;
            
        default:
            actualType = "unknown";
            break;
    }
    
    char buff[200];
    snprintf(buff, sizeof(buff), 
            "Type error: Node %s (hash %lu): expected %s in header, got %s in config.json",
            tmpNode->getName(), (unsigned long)hash, expectedType, actualType);
    
    BorneUniverselle::getInstance()->setPlcBroken(buff);
}

template<typename T>
inline bool BusinessLogic::initNodePtr(T*& ptr, uint32_t hash, const char* context) {
    if (hash == 0) {
        Serial.println();
        Serial.println("========================================");
        Serial.println("ERROR: Hash = 0 detected!");
        Serial.printf("Context: %s\n", context);
        Serial.printf("Template type: %s\n", typeid(T).name());
        Serial.println("Check your #define in class header!");
        Serial.println("========================================");
        Serial.println();
        
        BorneUniverselle::getInstance()->setPlcBroken("Hash = 0 detected in initNodePtr");
        return false;
    }
    
    auto tmpNode = BorneUniverselle::getInstance()->findNode(context, hash);
    if (tmpNode) {
        // Essayer d'abord le dynamic_cast
        ptr = dynamic_cast<T*>(tmpNode);
        
        // Si le dynamic_cast échoue, vérifier la compatibilité logique
        if (!ptr && isTypeCompatible<T>(tmpNode->classType())) {
            ptr = static_cast<T*>(tmpNode);
            
            #ifdef DEBUG_FIND_NODE
            Serial.printf("INFO: Compatibility resolved: classType %d -> %s\n", 
                        tmpNode->classType(), typeid(T).name());
            #endif
            
            return true;
        }
        
        if (ptr) {
            return true;
        } else {
            // ❌ TYPES INCOMPATIBLES - DIAGNOSTIC AMÉLIORÉ
            
            // Déterminer le type attendu et sa direction (INPUT/OUTPUT)
            const char* expectedType = "UNKNOWN";
            bool expectedIsInput = false;
            bool expectedIsOutput = false;
            
            if (std::is_same<T, BooleanOutputNode>::value) { 
                expectedType = "BooleanOutputNode"; 
                expectedIsOutput = true; 
            }
            else if (std::is_same<T, BooleanInputNode>::value) { 
                expectedType = "BooleanInputNode"; 
                expectedIsInput = true; 
            }
            else if (std::is_same<T, Uint16InputNode>::value) { 
                expectedType = "Uint16InputNode"; 
                expectedIsInput = true; 
            }
            else if (std::is_same<T, Uint16OutputNode>::value) { 
                expectedType = "Uint16OutputNode"; 
                expectedIsOutput = true; 
            }
            else if (std::is_same<T, Uint32InputNode>::value) { 
                expectedType = "Uint32InputNode"; 
                expectedIsInput = true; 
            }
            else if (std::is_same<T, Uint32OutputNode>::value) { 
                expectedType = "Uint32OutputNode"; 
                expectedIsOutput = true; 
            }
            else if (std::is_same<T, FloatInputNode>::value) { 
                expectedType = "FloatInputNode"; 
                expectedIsInput = true; 
            }
            else if (std::is_same<T, FloatOutputNode>::value) { 
                expectedType = "FloatOutputNode"; 
                expectedIsOutput = true; 
            }
            else if (std::is_same<T, TextInputNode>::value) { 
                expectedType = "TextInputNode"; 
                expectedIsInput = true; 
            }
            else if (std::is_same<T, TextOutputNode>::value) { 
                expectedType = "TextOutputNode"; 
                expectedIsOutput = true; 
            }
            else if (std::is_same<T, InputNode>::value) { 
                expectedType = "InputNode (generic)"; 
                expectedIsInput = true; 
            }
            else if (std::is_same<T, OutputNode>::value) { 
                expectedType = "OutputNode (generic)"; 
                expectedIsOutput = true; 
            }
            else if (std::is_same<T, VirtualBooleanInputNode>::value) { 
                expectedType = "VirtualBooleanInputNode"; 
                expectedIsInput = true; 
            }
            else if (std::is_same<T, VirtualBooleanOutputNode>::value) { 
                expectedType = "VirtualBooleanOutputNode"; 
                expectedIsOutput = true; 
            }
            else if (std::is_same<T, VirtualUint32InputNode>::value) { 
                expectedType = "VirtualUint32InputNode"; 
                expectedIsInput = true; 
            }
            else if (std::is_same<T, VirtualUint32OutputNode>::value) { 
                expectedType = "VirtualUint32OutputNode"; 
                expectedIsOutput = true; 
            }
            else if (std::is_same<T, VirtualFloatInputNode>::value) { 
                expectedType = "VirtualFloatInputNode"; 
                expectedIsInput = true; 
            }
            else if (std::is_same<T, VirtualFloatOutputNode>::value) { 
                expectedType = "VirtualFloatOutputNode"; 
                expectedIsOutput = true; 
            }
            
            std::string actualType = BusinessLogic::getTypeName(tmpNode->classType());
            int actualClassType = tmpNode->classType();
            
            // Détecter si le nœud trouvé est INPUT ou OUTPUT
            bool actualIsInput = (
                actualClassType == CLASS_BOOLEAN_INPUT_NODE ||
                actualClassType == CLASS_HW_BOOLEAN_INPUT_NODE ||
                actualClassType == CLASS_PFC8574_BOOLEAN_INPUT_NODE ||
                actualClassType == CLASS_VIRTUAL_BOOLEAN_INPUT_NODE ||
                actualClassType == CLASS_UINT16_INPUT_NODE ||
                actualClassType == CLASS_UINT32_INPUT_NODE ||
                actualClassType == CLASS_VIRTUAL_UINT32_INPUT_NODE ||
                actualClassType == CLASS_FLOAT_INPUT_NODE ||
                actualClassType == CLASS_VIRTUAL_FLOAT_INPUT_NODE ||
                actualClassType == CLASS_TEXT_INPUT_NODE ||
                actualClassType == CLASS_VIRTUAL_TEXT_INPUT_NODE ||
                actualClassType == CLASS_MODBUS_READ_COIL ||
                actualClassType == CLASS_MODBUS_READHOLDINGREGISTER ||
                actualClassType == CLASS_MODBUS_READINPUTREGISTER
            );
            
            bool actualIsOutput = (
                actualClassType == CLASS_BOOLEAN_OUTPUT_NODE ||
                actualClassType == CLASS_HW_BOOLEAN_OUTPUT_NODE ||
                actualClassType == CLASS_PFC8574_BOOLEAN_OUTPUT_NODE ||
                actualClassType == CLASS_PCA9554_BOOLEAN_OUTPUT_NODE ||
                actualClassType == CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE ||
                actualClassType == CLASS_UINT16_OUTPUT_NODE ||
                actualClassType == CLASS_UINT32_OUTPUT_NODE ||
                actualClassType == CLASS_VIRTUAL_UINT32_OUTPUT_NODE ||
                actualClassType == CLASS_FLOAT_OUTPUT_NODE ||
                actualClassType == CLASS_VIRTUAL_FLOAT_OUTPUT_NODE ||
                actualClassType == CLASS_TEXT_OUTPUT_NODE ||
                actualClassType == CLASS_VIRTUAL_TEXT_OUTPUT_NODE ||
                actualClassType == CLASS_MODBUS_WRITE_COIL_NODE ||
                actualClassType == CLASS_MODBUS_WRITEHOLDINGREGISTER
            );
            
            // Message d'erreur amélioré
            Serial.println();
            Serial.println("===========================================================");
            Serial.println("ERROR: NODE TYPE INCOMPATIBILITY");
            Serial.println("===========================================================");
            Serial.printf("Context        : %s\n", context);
            Serial.printf("Hash           : %lu\n", (unsigned long)hash);
            Serial.printf("Node name      : %s\n", tmpNode->getName());
            Serial.printf("Expected type  : %s\n", expectedType);
            Serial.printf("Found type     : %s\n", actualType.c_str());
            
            // Diagnostic spécifique INPUT vs OUTPUT
            if ((expectedIsInput && actualIsOutput) || (expectedIsOutput && actualIsInput)) {
                Serial.println("-----------------------------------------------------------");
                Serial.println("WARNING: INPUT/OUTPUT CONFLICT DETECTED!");
                Serial.println("-----------------------------------------------------------");
                if (expectedIsInput && actualIsOutput) {
                    Serial.println("You are trying to use an OUTPUT node as INPUT");
                } else {
                    Serial.println("You are trying to use an INPUT node as OUTPUT");
                }
                Serial.println();
                Serial.println("PROBABLE CAUSE:");
                Serial.println("  You are using the SAME HASH for TWO different nodes!");
                Serial.println();
                Serial.println("SOLUTION:");
                Serial.println("  1. Verify that each node has a UNIQUE hash");
                Serial.println("  2. If you have two nodes (ex: servoOn and v_servoOn),");
                Serial.println("     they must have DIFFERENT hashes");
                Serial.println("  3. Create a new #define with a unique hash");
                Serial.println("     Example: #define V_SERVOON 1234567890");
            }
            Serial.println("===========================================================");
            Serial.println();
            
            char buff[256];
            snprintf(buff, sizeof(buff), 
                    "Type mismatch: Node '%s' (hash %lu): expected %s, got %s%s",
                    tmpNode->getName(), (unsigned long)hash, expectedType, actualType.c_str(),
                    ((expectedIsInput && actualIsOutput) || (expectedIsOutput && actualIsInput)) ? 
                    " [POSSIBLE HASH CONFLICT!]" : "");
            
            BorneUniverselle::getInstance()->setPlcBroken(buff);
            return false;
        }
    } else {
        // Node non trouvé
        char buff[100];
        snprintf(buff, sizeof(buff), "Node not found for hash %lu in context %s", 
                (unsigned long)hash, context);
        BorneUniverselle::getInstance()->setPlcBroken(buff);
        return false;
    }
}

#endif // BUSINESS_LOGIC_INL

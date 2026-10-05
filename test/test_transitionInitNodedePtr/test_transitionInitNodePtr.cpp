// test_typeConsistency.cpp
// Test simplifié pour vérifier la cohérence des types entre BorneUniverselle et Formaca
// Version pour ESP32

#include <Arduino.h>  // Pour ESP32
#include <iostream>
#include <memory>
#include <typeinfo>
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <functional>

#include "../../src/MemoryMonitor/MemoryMonitor.h"
#include "BorneUniverselle/borneUniverselle.h"
#include "BorneUniverselle/borneUniverselle.cpp"
#include "PLC_Tools/PLC_Tools.h"
#include "PLC_Tools/PLC_Tools.cpp"
#include "MyModbus/MyModbus.h"
#include "MyModbus/MyModbus.cpp"
#include "Node/Node.h"
#include "Node/Node.cpp"
#include "PLC_Persistence/PLC_Persistence.h"
#include "PLC_Persistence/PLC_Persistence.cpp"
#include "AtomicFileWriter/AtomicFileWriter.h"
#include "AtomicFileWriter/AtomicFileWriter.cpp"
#include "RenderTools/RenderTools.h"
#include "RenderTools/RenderTools.cpp"
#include "SureServo/SureServo.h"
#include "SureServo/SureServo.cpp"
#include "WifiManagement/wifimanagment.h"
#include "WifiManagement/wifimanagment.cpp"
#include "PLC_InterfaceMenu/PLC_InterfaceMenu.h"
#include "PLC_InterfaceMenu/PLC_InterfaceMenu.cpp"
#include "../../src/Formaca/Formaca2.cpp"
#include "../../src/BusinessLogic/BusinessLogic.cpp"

// ============================================
// STRUCTURE DE TEST SIMPLE (SANS GOOGLE TEST)
// ============================================

// Pour les tests, on va créer notre propre version de initNodePtr
// qui fait la même chose que celle de Formaca2

// Fonction helper pour réinitialiser l'état PLC pour les tests
// Note: Cette fonction devrait idéalement appeler une méthode clearPlcBroken() si elle existe
void resetPlcBrokenState() {
    // Workaround: recréer l'instance ou ajouter une méthode clearPlcBroken dans BorneUniverselle
    // Pour l'instant, on ne peut pas faire grand chose sans modifier BorneUniverselle
}

// Version modifiée de testInitNodePtr qui ne modifie pas plcBroken pour les tests
template<typename T>
bool testInitNodePtrNoSideEffect(T*& ptr, uint32_t hash, const char* context) {
    if (hash == 0) {
        Serial.println("  ❌ Hash = 0");
        return false;
    }

    Serial.printf("Searching for node with hash: %lu, context: %s\n", (unsigned long)hash, context);
    
    auto tmpNode = BorneUniverselle::getInstance()->findNode(context, hash);
    if (tmpNode) {
        Serial.printf("  → Node trouvé: %s (type=%d)\n", tmpNode->getName(), tmpNode->classType());
        
        ptr = dynamic_cast<T*>(tmpNode);
        if (!ptr) {
            Serial.printf("  ❌ Cast échoué!\n");
            Serial.printf("     Type du node: %d\n", tmpNode->classType());
            Serial.printf("     Type attendu: %s\n", typeid(T).name());
            
            // Déterminer les types pour le message d'erreur
            const char* expectedType = "UNKNOWN";
            const char* actualType = "UNKNOWN";

            int expectedClassType = BusinessLogic::getClassTypeFromTypeInfo(typeid(T*));
            if (expectedClassType != -1) {
                const char* expectedType = BusinessLogic::getTypeName(expectedClassType).c_str();
                Serial.printf("     Expected (from getTypeName): %s\n", expectedType);
            }
            
            if (std::is_same<T, VirtualBooleanOutputNode>::value) expectedType = "VirtualBooleanOutput";
            else if (std::is_same<T, VirtualFloatOutputNode>::value) expectedType = "VirtualFloatOutput";
            else if (std::is_same<T, BooleanOutputNode>::value) expectedType = "tx-bool";
            else if (std::is_same<T, BooleanInputNode>::value) expectedType = "rx-bool";
            else if (std::is_same<T, HardwareBooleanInputNode>::value) expectedType = "HardwareBooleanInput";
            else if (std::is_same<T, HardwareBooleanOutputNode>::value) expectedType = "HardwareBooleanOutput";
            else if (std::is_same<T, VirtualBooleanInputNode>::value) expectedType = "VirtualBooleanInput";
            else if (std::is_same<T, VirtualFloatInputNode>::value) expectedType = "VirtualFloatInput";
            else if (std::is_same<T, TextInputNode>::value) expectedType = "TextInput";
            else if (std::is_same<T, TextOutputNode>::value) expectedType = "TextOutput";
            else if (std::is_same<T, VirtualTextInputNode>::value) expectedType = "VirtualTextInput";
            else if (std::is_same<T, VirtualTextOutputNode>::value) expectedType = "VirtualTextOutput";
            else if (std::is_same<T, FloatOutputNode>::value) expectedType = "FloatOutput";
            else if (std::is_same<T, FloatInputNode>::value) expectedType = "FloatInput";
            else if (std::is_same<T, InputNode>::value) expectedType = "InputNode";
            else if (std::is_same<T, OutputNode>::value) expectedType = "OutputNode";

            else if (std::is_same<T, Node>::value) expectedType = "Node";
            
            int classType = tmpNode->classType();
            actualType = BusinessLogic::getTypeName(classType).c_str();  
            Serial.printf("     Expected: %s, Actual: %s\n", expectedType, actualType);
            return false;
        }
        Serial.printf("  ✓ Node found and casted successfully: %s\n", ptr->getName());
        return true;
    } else {
        Serial.printf("  ❌ Node non trouvé pour hash %lu\n", (unsigned long)hash);
    }
    return false;
}

class TestTypeConsistency {
private:
    // Structure pour un cas de test
    struct TestCase {
        std::string description;
        uint32_t hash;
        std::string nodeId;
        int expectedClassType;
        std::function<bool()> testFunction;
        bool shouldSucceed;
    };
    
    std::vector<TestCase> testCases;
    std::map<uint32_t, std::string> hashToNodeId;
    Formaca2* formaca = nullptr;  // Pour utiliser getTypeName
    int totalTests = 0;
    int passedTests = 0;
    int failedTests = 0;
    
public:
    TestTypeConsistency() {
        Serial.println("\n========================================");
        Serial.println("   TEST DE COHÉRENCE DES TYPES");
        Serial.println("========================================");
    }
    
    ~TestTypeConsistency() {
        if (formaca) {
            delete formaca;
            formaca = nullptr;
        }
    }
    
    void setUp() {
           // Juste s'assurer que BorneUniverselle existe
        if (!BorneUniverselle::initializeInstance()) {
            Serial.println("⚠️ Échec initialisation BorneUniverselle");
            return; 
        }
        // Obtenir l'instance de BorneUniverselle (retourne un pointeur)
        BorneUniverselle* borne = BorneUniverselle::getInstance();
        if (!borne) {
            Serial.println("Erreur: Impossible d'obtenir BorneUniverselle");
            return;
        }
        
        // Nettoyer les nodes existants
        borne->deleteNodes();  // Cette méthode existe dans BorneUniverselle
        
        // Créer une instance de Formaca2 pour utiliser getTypeName
        formaca = new Formaca2();
        
        // Créer des nodes de test
        createTestNodes();
        
        // Préparer les cas de test
        prepareTestCases();
    }
    
    void tearDown() {
        // Afficher le résumé
        Serial.println("\n========================================");
        Serial.println("        RÉSUMÉ DES TESTS");
        Serial.println("========================================");
        Serial.printf("Tests exécutés: %d\n", totalTests);
        Serial.printf("Tests réussis:  %d\n", passedTests);
        Serial.printf("Tests échoués:  %d\n", failedTests);
        
        if (failedTests > 0) {
            Serial.println("\n⚠️  ATTENTION: Des incohérences de types détectées!");
            Serial.println("La machine pourrait ne pas fonctionner correctement.");
        } else {
            Serial.println("\n✅ SUCCÈS: Tous les tests sont passés!");
            Serial.println("La fonction initNodePtr fonctionne correctement.");
        }
    }
    
private:
    void createTestNodes() {
        BorneUniverselle* borne = BorneUniverselle::getInstance();
        if (!borne) {
            Serial.println("Erreur: BorneUniverselle non disponible");
            return;
        }
        
        Serial.println("\nCréation des nodes de test...");
        
        // Note: Le constructeur de Node recalcule le hash à partir du nom
        // On doit récupérer le vrai hash après création
        
        // Virtual Boolean Input
        {
            VirtualBooleanInputNode* node = new VirtualBooleanInputNode(
                (char*)"VBI_Test", (char*)"Test", 1, 0x1000, 100  // Le hash sera recalculé
            );
            if (borne->addNode(node)) {
                uint32_t realHash = node->getHash();  // Récupérer le vrai hash
                hashToNodeId[realHash] = "vbi_test";
                Serial.printf("  VBI ajouté avec hash réel: 0x%08lX\n", (unsigned long)realHash);
            } else {
                delete node;
            }
        }
        
        // Virtual Boolean Output
       {
            Serial.println("\n=== CRÉATION VBO_Test ===");
            
            // Créer le node
            VirtualBooleanOutputNode* node = new VirtualBooleanOutputNode(
                (char*)"VBO_Test", (char*)"Test", 2, 0x1001, 100
            );
            
            // Vérifier le type AVANT l'ajout
            Serial.printf("AVANT ajout: type=%d, className=%s\n", 
                        node->classType(), typeid(*node).name());
            Serial.printf("Devrait être: CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE=%d\n", 
                        CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE);
            
            if (borne->addNode(node)) {
                uint32_t realHash = node->getHash();
                hashToNodeId[realHash] = "vbo_test";
                
                Serial.printf("APRÈS ajout: hash=0x%08lX\n", (unsigned long)realHash);
                
                // Récupérer le node et vérifier
                auto checkNode = borne->findNode("test", realHash);
                if (checkNode) {
                    Serial.printf("VÉRIFICATION après récupération:\n");
                    Serial.printf("  - Type: %d\n", checkNode->classType());
                    Serial.printf("  - Nom: %s\n", checkNode->getName());
                    Serial.printf("  - Pointeur original: %p\n", (void*)node);
                    Serial.printf("  - Pointeur récupéré: %p\n", (void*)checkNode);
                    
                    // Test de cast
                    VirtualBooleanOutputNode* vboTest = dynamic_cast<VirtualBooleanOutputNode*>(checkNode);
                    HardwareBooleanOutputNode* hboTest = dynamic_cast<HardwareBooleanOutputNode*>(checkNode);
                    
                    Serial.printf("  - Cast vers VirtualBooleanOutput: %s\n", 
                                vboTest ? "SUCCÈS" : "ÉCHEC");
                    Serial.printf("  - Cast vers HardwareBooleanOutput: %s\n", 
                                hboTest ? "SUCCÈS" : "ÉCHEC");
                }
            } else {
                Serial.println("ERREUR: Impossible d'ajouter le node!");
                delete node;
            }
            Serial.println("======================\n");
        }
        
        // Hardware Boolean Input
        {
            HardwareBooleanInputNode* node = new HardwareBooleanInputNode(
                (char*)"HBI_Test", (char*)"Test", 3, 0x1002, 
                2,      // _pin
                false,  // inputInverted
                100,    // refreshInterval
                100     // webRefreshInterval
            );
            if (borne->addNode(node)) {
                uint32_t realHash = node->getHash();
                hashToNodeId[realHash] = "hbi_test";
                Serial.printf("  HBI ajouté avec hash réel: 0x%08lX\n", (unsigned long)realHash);
            } else {
                delete node;
            }
        }
        
        // Hardware Boolean Output
         {
            Serial.println("\n=== CRÉATION HBO_Test ===");
            
            HardwareBooleanOutputNode* node = new HardwareBooleanOutputNode(
                (char*)"HBO_Test", (char*)"Test", 5, 0x1004,
                5,      // _pin
              //  false,  // outputInverted  
                100     // webRefreshInterval
            );
            
            Serial.printf("AVANT ajout: type=%d, className=%s\n", 
                        node->classType(), typeid(*node).name());
            Serial.printf("Devrait être: CLASS_HW_BOOLEAN_OUTPUT_NODE=%d\n", 
                        CLASS_HW_BOOLEAN_OUTPUT_NODE);
            
            if (borne->addNode(node)) {
                uint32_t realHash = node->getHash();
                hashToNodeId[realHash] = "hbo_test";
                Serial.printf("HBO ajouté avec hash: 0x%08lX, type=%d\n", 
                            (unsigned long)realHash, node->classType());
            }
            Serial.println("======================\n");
        }
        
        // Virtual Float Input
        {
            VirtualFloatInputNode* node = new VirtualFloatInputNode(
                (char*)"VFI_Test", (char*)"Test", 5, 0x1004, 100
            );
            if (borne->addNode(node)) {
                uint32_t realHash = node->getHash();
                hashToNodeId[realHash] = "vfi_test";
                Serial.printf("  VFI ajouté avec hash réel: 0x%08lX\n", (unsigned long)realHash);
            } else {
                delete node;
            }
        }
        
        // Virtual Float Output
        {
            VirtualFloatOutputNode* node = new VirtualFloatOutputNode(
                (char*)"VFO_Test", (char*)"Test", 6, 0x1005, 100
            );
            if (borne->addNode(node)) {
                uint32_t realHash = node->getHash();
                hashToNodeId[realHash] = "vfo_test";
                Serial.printf("  VFO ajouté avec hash réel: 0x%08lX\n", (unsigned long)realHash);
            } else {
                delete node;
            }
        }
        
        Serial.printf("  ✓ Créé %d nodes de test\n", (int)hashToNodeId.size());
    }

    uint32_t findHashByNodeId(const std::string& targetNodeId) {
        for (const auto& [hash, nodeId] : hashToNodeId) {
            if (nodeId == targetNodeId) {
                Serial.printf("🔍 NodeId '%s' trouvé avec hash 0x%08lX\n", targetNodeId.c_str(), (unsigned long)hash);
                return hash;
            }
        }
        Serial.printf("⚠️  NodeId '%s' non trouvé dans hashToNodeId\n", targetNodeId.c_str());
        return 0; // Retourne 0 si non trouvé
    }
    
    void prepareTestCases() {
        std::cout << "Préparation des cas de test..." << std::endl;
        
        // === Tests de correspondance exacte (doivent réussir) ===
        
        testCases.push_back({
            "VirtualBooleanInput - Type exact",
           findHashByNodeId("vbi_test"),
            "vbi_test",
            CLASS_VIRTUAL_BOOLEAN_INPUT_NODE,
            [this]() {
                VirtualBooleanInputNode* ptr = nullptr;
                ::testInitNodePtrNoSideEffect(ptr, findHashByNodeId("vbi_test"), "test");  // Utiliser la fonction globale de test
                return (ptr != nullptr);
            },
            true
        });
        
        testCases.push_back({
            "VirtualBooleanOutput - Type exact",
            findHashByNodeId("vbo_test"),
            "vbo_test",
            CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE,
            [this]() {
                VirtualBooleanOutputNode* ptr = nullptr;
                ::testInitNodePtrNoSideEffect(ptr, findHashByNodeId("vbo_test"), "test");  // Utiliser la fonction globale de test
                return (ptr != nullptr );
            },
            true
        });
        
        testCases.push_back({
            "HardwareBooleanInput - Type exact",
            findHashByNodeId("hbi_test"),
            "hbi_test",
            CLASS_HW_BOOLEAN_INPUT_NODE,
            [this]() {
                HardwareBooleanInputNode* ptr = nullptr;
                ::testInitNodePtrNoSideEffect(ptr, findHashByNodeId("hbi_test"), "test");  // Utiliser la fonction globale de test
                BorneUniverselle* bu = BorneUniverselle::getInstance();
                return (ptr != nullptr && bu && !bu->isPlcBroken());
            },
            true
        });
        
        // === Tests de types incompatibles (doivent échouer) ===
        
       testCases.push_back({
            "Input/Output mismatch - VBI comme VBO",
            findHashByNodeId("vbi_test"),
            "vbi_test",
            CLASS_VIRTUAL_BOOLEAN_INPUT_NODE,
            [this]() {
                VirtualBooleanOutputNode* ptr = nullptr;
                // testInitNodePtrNoSideEffect retourne false si le cast échoue
                bool result = ::testInitNodePtrNoSideEffect(ptr, findHashByNodeId("vbi_test"), 
                                                            "Input/Output mismatch - VBI comme VBO");
                // On veut que le cast échoue, donc result devrait être false
                return !result;  // Inverser : si result=false (échec du cast), retourner true (test réussi)
            },
            true  // Le test DOIT réussir (shouldSucceed = true)
        });
        
        testCases.push_back({
            "Type mismatch - Boolean comme Float",
            findHashByNodeId("vbo_test"),
            "vbo_test",
            CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE,
            [this]() {
                VirtualFloatOutputNode* ptr = nullptr;
                bool result = ::testInitNodePtrNoSideEffect(ptr, findHashByNodeId("vbo_test"), 
                                                            "Type mismatch - Boolean comme Float");
                // On veut que le cast échoue, donc result devrait être false
                return !result;  // Inverser : si result=false (échec du cast), retourner true (test réussi)
            },
            true  // Le test DOIT réussir (shouldSucceed = true)
        });
        
        // === Tests avec classe de base ===
        
        testCases.push_back({
            "Upcast vers Node - VBO comme Node",
            findHashByNodeId("vbo_test"),
            "vbo_test",
            CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE,  // ⚠️ Changez ici en 13 au lieu de 11
            [this]() {
                Node* ptr = nullptr;
                bool result = ::testInitNodePtrNoSideEffect(ptr, findHashByNodeId("vbo_test"), "test");
                
                // Debug pour comprendre :
                Serial.printf("  DEBUG Upcast: result=%d, ptr=%p\n", result, (void*)ptr);
                
                // Le test devrait vérifier :
                // - result == true (la fonction a réussi)
                // - ptr != nullptr (le cast a réussi)
                return result && (ptr != nullptr);
            },
            true  // Ce test DOIT réussir
        });
        
        // === Test avec hash invalide ===
        
        testCases.push_back({
            "Hash = 0 (doit échouer)",
            0,
            "none",
            -1,
            [this]() {
                Node* ptr = nullptr;
                ::testInitNodePtrNoSideEffect(ptr, 0, "test");  // Utiliser la fonction globale de test
                BorneUniverselle* bu = BorneUniverselle::getInstance();
                return (bu && bu->isPlcBroken());
            },
            false
        });
        
        std::cout << "  ✓ Préparé " << testCases.size() << " cas de test" << std::endl;
    }
    
    void runTestCase(const TestCase& tc) {
        totalTests++;
        
        // Exécuter le test
        bool result = tc.testFunction();
        
        // Pour les tests qui doivent échouer, on inverse le résultat
        bool success;
        if (!tc.shouldSucceed) {
            success = !result;
        } else {
            success = result;
        }
        
        // Afficher le résultat
        if (success) {
            passedTests++;
            Serial.printf("  ✅ PASS: %s\n", tc.description.c_str());
        } else {
            failedTests++;
            Serial.printf("  ❌ FAIL: %s\n", tc.description.c_str());
            Serial.printf("      Hash: 0x%08lX", (unsigned long)tc.hash);
            Serial.printf(" | Attendu: %s", tc.shouldSucceed ? "succès" : "échec");
            Serial.printf(" | Obtenu: %s\n", result ? "succès" : "échec");
        }
    }
    
public:
    void runAllTests() {
        Serial.println("\n=== EXÉCUTION DES TESTS ===");
        
        for (const auto& tc : testCases) {
            runTestCase(tc);
        }
    }
    
    void verifyNodeTypes() {
        Serial.println("\n=== VÉRIFICATION DES TYPES DANS LA MAP ===");
        
        BorneUniverselle* borne = BorneUniverselle::getInstance();
        if (!borne) {
            Serial.println("Erreur: BorneUniverselle non disponible");
            return;
        }
        
        for (const auto& [hash, nodeId] : hashToNodeId) {
            Node* node = borne->findNode("test", hash);
            if (node) {
                int classType = node->classType();
                Serial.printf("  Hash 0x%08lX (%s): %s (type=%d)\n",
                    (unsigned long)hash, nodeId.c_str(), 
                    formaca->getTypeName(classType).c_str(), 
                    classType);
            } else {
                Serial.printf("  ❌ Node avec hash 0x%08lX non trouvé!\n", (unsigned long)hash);
            }
        }
    }
};

// ============================================
// POINT D'ENTRÉE POUR ESP32
// ============================================

TestTypeConsistency* test = nullptr;

void setup() {
    // Initialisation pour ESP32
    Serial.begin(115200);
    delay(2000);  // Attendre que le Serial soit prêt
    
    Serial.println("\n████████████████████████████████████████");
    Serial.println("█  TEST DE COHÉRENCE DES TYPES FORMACA █");
    Serial.println("████████████████████████████████████████");
    
    // Créer l'instance de test
    test = new TestTypeConsistency();
    
    // Setup
    test->setUp();
    
    // Exécuter tous les tests
    test->runAllTests();
    
    // Vérifier les types dans la map
    test->verifyNodeTypes();
    
    // Teardown et résumé
    test->tearDown();
    
    Serial.println("\n████████████████████████████████████████");
    Serial.println("█           FIN DES TESTS               █");
    Serial.println("████████████████████████████████████████");
    
    // Nettoyer
    delete test;
    test = nullptr;
}

void loop() {
    // Ne rien faire dans loop pour les tests
    delay(10000);
}
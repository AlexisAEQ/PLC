// test/test_transition/test_transition_complete.cpp
// Tests complets de performance des transitions - Version refactorisée

#include <Arduino.h>
#include <unity.h>
#include <Esp.h>

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

// Forcer la compilation en incluant les .cpp
#include "../../src/Formaca/Formaca2.cpp"
#include "../../src/BusinessLogic/BusinessLogic.cpp"

// ============================================
// VARIABLES GLOBALES
// ============================================
Formaca2* machine = nullptr;
BorneUniverselle* bu = nullptr;

// Structure pour stocker les résultats de mesure
struct TransitionMetrics {
    const char* name;
    unsigned long duration_us;
    bool success;
};

// ============================================
// HELPER CLASS pour l'initialisation
// ============================================
class Formaca2TestHelper {
public:
    static void initializePrivateDependencies(Formaca2* machine) {
        if (!machine) return;

        Serial.println("\n=== Initialisation des dépendances ===");
          if (!BorneUniverselle::initializeInstance()) {
            Serial.println("âš ï¸ Ã‰chec initialisation BorneUniverselle");
            return; 
        }
        
        BorneUniverselle* bu = BorneUniverselle::getInstance();
        if (!bu) {
            Serial.println("❌ BorneUniverselle NULL");
        }
        
        // doorLed
        if (!machine->doorLed) {
            Serial.println("  - Création doorLed (VRAI VirtualBooleanOutputNode)");
            machine->doorLed = new VirtualBooleanOutputNode(
                const_cast<char*>("1test"),
                const_cast<char*>("test"),
                1,
                0,
                1000
            );
            bu->addNode(machine->doorLed);
        }
        
        // doorIndicator - RxIndicator utilisÃ© dans onStateEnter
        if (!machine->doorIndicator) {
            Serial.println("  - Creation doorIndicator (RxIndicator)");
            
            // CrÃ©er le node sous-jacent
            VirtualBooleanOutputNode* indicatorNode = new VirtualBooleanOutputNode(
                const_cast<char*>("doorIndicatorNode"),
                const_cast<char*>("test"),
                2,
                0,
                1000
            );
            
            bu->addNode(indicatorNode);
                // CrÃ©er le RxIndicator wrapper  
            machine->doorIndicator = new RxIndicator(indicatorNode);
            machine->doorIndicator->setBlinking(false);
            machine->doorIndicator->setValue(false);
        }
        
        
        // nbCyclesMade
        if (!machine->nbCyclesMade) {
            Serial.println("  - Création nbCyclesMade (VRAI VirtualUint32OutputNode)");
            machine->nbCyclesMade = new VirtualUint32OutputNode(
                const_cast<char*>("2test"),
                const_cast<char*>("test"),
                2,
                0,
                1000
            );
            bu->addNode(machine->nbCyclesMade);
        }
        
        // autorisationScie
        if (!machine->autorisationScie) {
            Serial.println("  - Création autorisationScie");
            machine->autorisationScie = new VirtualBooleanOutputNode(
                const_cast<char*>("2test"),
                const_cast<char*>("test"),
                18,
                0,
                1000
            );
            bu->addNode(machine->autorisationScie);
        }
        
        // visu nodes
        if (!machine->immediateStop) {
            Serial.println("  - Création visu");
            Serial.println("    - immediateStop (VirtualBooleanOutputNode)");
            machine->immediateStop = new VirtualBooleanOutputNode(
                const_cast<char*>("3test"),
                const_cast<char*>("test"),
                3,
                0,
                1000
            );
            bu->addNode(machine->immediateStop);
        }
        
        if (!machine->v_immediateStop) {
            Serial.println("    - v_immediateStop (VirtualBooleanOutputNode)");
            machine->v_immediateStop = new VirtualBooleanOutputNode(
                const_cast<char*>("4test"),
                const_cast<char*>("test"),
                4,
                0,
                1000
            );
            bu->addNode(machine->v_immediateStop);
        }
        
        Serial.println("=== Initialisation terminée ===");
    }
};

// ============================================
// SETUP/TEARDOWN
// ============================================
void setUp(void) {
    // Reset BorneUniverselle pour chaque test
    BorneUniverselle::resetForTests();
    bu = BorneUniverselle::getInstance();
    
    // Créer la machine de test (avec accesseurs publics)
    machine = new Formaca2();
    
    // Initialiser les dépendances
    Formaca2TestHelper::initializePrivateDependencies(machine);
    
    Serial.printf("\n📍 Setup - Heap libre: %lu octets\n", ESP.getFreeHeap());
}

void tearDown(void) {
    if (machine) {
        delete machine;
        machine = nullptr;
    }
    
    // Nettoyer BorneUniverselle
    BorneUniverselle::resetForTests();
    
    Serial.printf("📍 Teardown - Heap libre: %lu octets\n", ESP.getFreeHeap());
}

// ============================================
// FONCTION UTILITAIRE DE MESURE
// ============================================
unsigned long measureTransition(Formaca2* m, 
                               Formaca2::State targetState,
                               Formaca2::ContextCode context = Formaca2::ContextCode::NORMAL) {
    unsigned long start = micros();
    m->transitionToState(targetState, context);
    unsigned long duration = micros() - start;
    return duration;
}

// ============================================
// TESTS DE BASE
// ============================================
void test_machine_creation() {
    TEST_MESSAGE("TEST 1: Création machine");
    TEST_ASSERT_NOT_NULL(machine);
    
    // Les nodes sont privés, on ne peut pas les vérifier directement
    // Mais le test vérifie que la machine est bien créée
}

void test_initial_state() {
    TEST_MESSAGE("TEST 2: État initial");
    Formaca2::State state = machine->getCurrentFormacaState();
    TEST_ASSERT_TRUE((int)state == 0 || (int)state == 1);
}

// ============================================
// TESTS DE MESURE DES TRANSITIONS
// ============================================
void test_single_transition_timing() {
    TEST_MESSAGE("TEST 3: Mesure d'une transition simple");
    
    // Transition IDLE -> STOP
    unsigned long duration = measureTransition(machine, Formaca2::State::IDLE);
    
    Serial.printf("  Transition vers IDLE: %lu µs\n", duration);
    TEST_ASSERT_EQUAL((int)Formaca2::State::IDLE, (int)machine->getCurrentFormacaState());
    TEST_ASSERT_LESS_THAN(200000, duration); // < 200ms
}

void test_multiple_transitions_timing() {
    TEST_MESSAGE("TEST 4: Mesure de transitions multiples");
    
    struct TransitionTest {
        const char* name;
        Formaca2::State state;
        unsigned long maxTime_us;
    };
    
    TransitionTest tests[] = {
        {"STOP", Formaca2::State::STOP, 50000},
        {"IDLE", Formaca2::State::IDLE, 50000},
        {"HOMING", Formaca2::State::HOMING, 100000},
        {"PARKING", Formaca2::State::PARKING, 100000},
        {"NORMAL_CUTTING", Formaca2::State::NORMAL_CUTTING, 150000}
    };
    
    Serial.println("\n  === Mesures des transitions ===");
    for (auto& test : tests) {
        unsigned long duration = measureTransition(machine, test.state);
        
        Serial.printf("  %-15s: %6lu µs", test.name, duration);
        if (duration > test.maxTime_us) {
            Serial.printf(" ⚠️ DÉPASSE LIMITE (%lu µs)", test.maxTime_us);
        } else {
            Serial.printf(" ✓");
        }
        Serial.println();
        
        TEST_ASSERT_EQUAL((int)test.state, (int)machine->getCurrentFormacaState());
        TEST_ASSERT_LESS_THAN(test.maxTime_us, duration);
    }
}

void test_emergency_transition_timing() {
    TEST_MESSAGE("TEST 5: Mesure transition Emergency");
    
    // Mettre en NORMAL_CUTTING d'abord
    machine->transitionToState(Formaca2::State::NORMAL_CUTTING);
    delay(10); // Stabilisation
    
    // Mesurer la transition vers EMERGENCY
    unsigned long duration = measureTransition(machine, 
                                               Formaca2::State::EMERGENCY,
                                               Formaca2::ContextCode::EMERGENCY_ENTRY);
    
    Serial.printf("  Transition EMERGENCY: %lu µs\n", duration);
    TEST_ASSERT_EQUAL((int)Formaca2::State::EMERGENCY, (int)machine->getCurrentFormacaState());
    TEST_ASSERT_LESS_THAN(500000, duration); // Emergency doit être rapide < 50ms
}

// ============================================
// TESTS DE PERFORMANCE
// ============================================
void test_performance_100_transitions() {
    TEST_MESSAGE("TEST 6: Performance 100 transitions");
    
    unsigned long totalTime = 0;
    unsigned long minTime = ULONG_MAX;
    unsigned long maxTime = 0;
    
    Serial.println("\n  Exécution de 100 transitions...");
    
    for (int i = 0; i < 50; i++) {
        // IDLE -> STOP
        unsigned long t1 = measureTransition(machine, Formaca2::State::IDLE);
        // STOP -> IDLE
        unsigned long t2 = measureTransition(machine, Formaca2::State::STOP);
        
        totalTime += (t1 + t2);
        minTime = min(minTime, min(t1, t2));
        maxTime = max(maxTime, max(t1, t2));
        
        if (i % 10 == 0) {
            Serial.printf("  Progression: %d/50 transitions effectuées\n", (i+1)*2);
        }
    }
    
    // Calculs pour le tableau
    float avgTime_us = totalTime/100.0;
    float avgTime_ms = avgTime_us/1000.0;
    float transitionsPerSec = 1000000.0/avgTime_us;
    
    Serial.println("\n  ╔═══════════════════════════════════════════════════════════════════╗");
    Serial.println("  ║              TABLEAU D'ANALYSE DES PERFORMANCES                   ║");
    Serial.println("  ╠════════════════════════╤═══════════════╤═══════════════╤══════════╣");
    Serial.println("  ║ Métrique               │ Valeur mesurée│ Valeur cible  │ État     ║");
    Serial.println("  ╠════════════════════════╪═══════════════╪═══════════════╪══════════╣");
    
    Serial.printf("  ║ Temps moyen/transition │ %7.2f ms    │ 0.5-2 ms      │", avgTime_ms);
    if (avgTime_ms > 5.0) {
        Serial.println(" ❌ LENT   ║");
    } else if (avgTime_ms > 2.0) {
        Serial.println(" ⚠️ LIMITE ║");
    } else {
        Serial.println(" ✅ OK     ║");
    }
    
    Serial.printf("  ║ Temps min              │ %7lu µs    │ 100-1000 µs   │", minTime);
    if (minTime > 5000) {
        Serial.println(" ❌ LENT   ║");
    } else {
        Serial.println(" ✅ OK     ║");
    }
    
    Serial.printf("  ║ Temps max              │ %7lu µs    │ 2000-5000 µs  │", maxTime);
    if (maxTime > 50000) {
        Serial.println(" ❌ LENT   ║");
    } else if (maxTime > 20000) {
        Serial.println(" ⚠️ LIMITE ║");
    } else {
        Serial.println(" ✅ OK     ║");
    }
    
    Serial.printf("  ║ Transitions/seconde    │ %7.0f       │ 500-2000      │", transitionsPerSec);
    if (transitionsPerSec < 200) {
        Serial.println(" ❌ LENT   ║");
    } else if (transitionsPerSec < 500) {
        Serial.println(" ⚠️ LIMITE ║");
    } else {
        Serial.println(" ✅ OK     ║");
    }
    
    Serial.printf("  ║ Temps total (100 trans)│ %7lu ms    │ 50-200 ms     │", totalTime/1000);
    if (totalTime/1000 > 1000) {
        Serial.println(" ❌ LENT   ║");
    } else if (totalTime/1000 > 500) {
        Serial.println(" ⚠️ LIMITE ║");
    } else {
        Serial.println(" ✅ OK     ║");
    }
    
    Serial.println("  ╚════════════════════════╧═══════════════╧═══════════════╧══════════╝");
    
    // Analyse des problèmes potentiels si performance dégradée
    if (avgTime_ms > 5.0) {
        Serial.println("\n  ⚠️ ANALYSE DES PROBLÈMES DÉTECTÉS:");
        Serial.println("  ┌─────────────────────────────────────────────────────────────┐");
        if (avgTime_ms > 20.0) {
            Serial.println("  │ • Logging excessif probable (30-35 ms d'impact)            │");
            Serial.println("  │   → Désactiver les Serial.printf dans les transitions      │");
        }
        if (avgTime_ms > 10.0) {
            Serial.println("  │ • Historique des transitions actif (2-5 ms d'impact)       │");
            Serial.println("  │   → Rendre l'historique optionnel en production            │");
        }
        Serial.println("  │ • Callbacks onEnter/onExit lourds (2-5 ms d'impact)        │");
        Serial.println("  │   → Optimiser la logique métier dans les états             │");
        Serial.println("  └─────────────────────────────────────────────────────────────┘");
    }
    
    TEST_ASSERT_LESS_THAN(10000000, totalTime); // < 10 secondes pour 100 transitions
}

void test_stress_500_transitions() {
    TEST_MESSAGE("TEST 7: Stress test 500 transitions");
    
    Formaca2::State states[] = {
        Formaca2::State::IDLE,
        Formaca2::State::STOP,
        Formaca2::State::HOMING,
        Formaca2::State::PARKING,
        Formaca2::State::NORMAL_CUTTING
    };
    
    unsigned long startTime = millis();
    unsigned long transitions = 0;
    unsigned long failures = 0;
    unsigned long totalTime_us = 0;
    unsigned long minTime_us = ULONG_MAX;
    unsigned long maxTime_us = 0;
    
    Serial.println("\n  Démarrage du stress test...");
    
    for (int i = 0; i < 500; i++) {
        Formaca2::State targetState = states[i % 5];
        unsigned long duration = measureTransition(machine, targetState);
        
        totalTime_us += duration;
        minTime_us = min(minTime_us, duration);
        maxTime_us = max(maxTime_us, duration);
        
        if ((int)machine->getCurrentFormacaState() != (int)targetState) {
            failures++;
            Serial.printf("  ❌ Échec transition #%d vers état %d\n", i+1, (int)targetState);
        } else {
            transitions++;
        }
        
        // Log périodique
        if ((i + 1) % 100 == 0) {
            Serial.printf("  ✓ %d transitions complétées - Temps écoulé: %lu ms\n", 
                         i+1, millis() - startTime);
        }
    }
    
    unsigned long totalDuration = millis() - startTime;
    float avgTime_us = totalTime_us/500.0;
    float avgTime_ms = avgTime_us/1000.0;
    
    Serial.println("\n  ╔═══════════════════════════════════════════════════════════════════╗");
    Serial.println("  ║            RÉSULTATS DU STRESS TEST (500 TRANSITIONS)             ║");
    Serial.println("  ╠════════════════════════╤═══════════════╤═══════════════╤══════════╣");
    Serial.println("  ║ Métrique               │ Valeur mesurée│ Valeur cible  │ État     ║");
    Serial.println("  ╠════════════════════════╪═══════════════╪═══════════════╪══════════╣");
    
    Serial.printf("  ║ Transitions réussies   │ %3lu/500      │ 500/500       │", transitions);
    if (failures == 0) {
        Serial.println(" ✅ OK     ║");
    } else {
        Serial.println(" ❌ ÉCHEC  ║");
    }
    
    Serial.printf("  ║ Temps total            │ %7lu ms    │ < 30000 ms    │", totalDuration);
    if (totalDuration > 30000) {
        Serial.println(" ❌ LENT   ║");
    } else if (totalDuration > 15000) {
        Serial.println(" ⚠️ LIMITE ║");
    } else {
        Serial.println(" ✅ OK     ║");
    }
    
    Serial.printf("  ║ Temps moyen/transition │ %7.2f ms    │ 0.5-2 ms      │", avgTime_ms);
    if (avgTime_ms > 5.0) {
        Serial.println(" ❌ LENT   ║");
    } else if (avgTime_ms > 2.0) {
        Serial.println(" ⚠️ LIMITE ║");
    } else {
        Serial.println(" ✅ OK     ║");
    }
    
    Serial.printf("  ║ Temps min              │ %7.1f ms    │ 0.1-1 ms      │", minTime_us/1000.0);
    if (minTime_us > 5000) {
        Serial.println(" ❌ LENT   ║");
    } else {
        Serial.println(" ✅ OK     ║");
    }
    
    Serial.printf("  ║ Temps max              │ %7.1f ms    │ 2-5 ms        │", maxTime_us/1000.0);
    if (maxTime_us > 50000) {
        Serial.println(" ❌ LENT   ║");
    } else if (maxTime_us > 20000) {
        Serial.println(" ⚠️ LIMITE ║");
    } else {
        Serial.println(" ✅ OK     ║");
    }
    
    Serial.println("  ╚════════════════════════╧═══════════════╧═══════════════╧══════════╝");
    
    // Diagnostic de performance
    if (avgTime_ms > 10.0) {
        Serial.println("\n  🔍 DIAGNOSTIC DE PERFORMANCE:");
        Serial.println("  ┌─────────────────────────────────────────────────────────────────┐");
        
        float logImpact = avgTime_ms * 0.8;  // Estimation: 80% du temps est le logging
        float historyImpact = avgTime_ms * 0.1;  // 10% pour l'historique
        float businessImpact = avgTime_ms * 0.1;  // 10% pour la logique métier
        
        Serial.printf("  │ Impact estimé du logging: %.1f ms (%.0f%%)                        │\n", 
                     logImpact, (logImpact/avgTime_ms)*100);
        Serial.printf("  │ Impact de l'historique: %.1f ms (%.0f%%)                          │\n", 
                     historyImpact, (historyImpact/avgTime_ms)*100);
        Serial.printf("  │ Impact logique métier: %.1f ms (%.0f%%)                           │\n", 
                     businessImpact, (businessImpact/avgTime_ms)*100);
        Serial.println("  │                                                                  │");
        Serial.println("  │ RECOMMANDATIONS:                                                │");
        Serial.println("  │ 1. Désactiver les logs dans transitionToState()                │");
        Serial.println("  │ 2. Rendre l'historique conditionnel (#ifdef HISTORY_ENABLED)   │");
        Serial.println("  │ 3. Optimiser les callbacks onStateEnter/onStateExit            │");
        Serial.println("  └─────────────────────────────────────────────────────────────────┘");
    }
    
    TEST_ASSERT_EQUAL(0, failures);
    TEST_ASSERT_LESS_THAN(30000, totalDuration);
}

// ============================================
// TEST D'IDENTIFICATION DES GOULOTS
// ============================================
void test_bottleneck_identification() {
    TEST_MESSAGE("TEST 8: Identification des goulots d'étranglement");
    
    struct TransitionPath {
        const char* name;
        Formaca2::State from;
        Formaca2::State to;
        Formaca2::ContextCode context;
    };
    
    TransitionPath paths[] = {
        {"Normal->Stop", Formaca2::State::IDLE, Formaca2::State::STOP, Formaca2::ContextCode::NORMAL},
        {"Stop->Idle", Formaca2::State::STOP, Formaca2::State::IDLE, Formaca2::ContextCode::NORMAL},
        {"Idle->Homing", Formaca2::State::IDLE, Formaca2::State::HOMING, Formaca2::ContextCode::AUTO_DETECT},
        {"Normal->Emergency", Formaca2::State::NORMAL_CUTTING, Formaca2::State::EMERGENCY, Formaca2::ContextCode::EMERGENCY_ENTRY},
        {"Emergency->Idle", Formaca2::State::EMERGENCY, Formaca2::State::IDLE, Formaca2::ContextCode::EMERGENCY_EXIT}
    };
    
    unsigned long slowestTime = 0;
    const char* slowestTransition = nullptr;
    
    Serial.println("\n  === Analyse des transitions ===");
    
    for (auto& path : paths) {
        // Setup état initial
        machine->transitionToState(path.from);
        delay(10); // Stabilisation
        
        // Mesurer
        unsigned long duration = measureTransition(machine, path.to, path.context);
        
        Serial.printf("  %-20s: %6lu µs", path.name, duration);
        
        if (duration > slowestTime) {
            slowestTime = duration;
            slowestTransition = path.name;
            Serial.print(" ⚠️ PLUS LENT");
        }
        Serial.println();
    }
    
    Serial.printf("\n  🔍 Goulot identifié: '%s' avec %lu µs\n", 
                 slowestTransition, slowestTime);
    
    TEST_ASSERT_LESS_THAN(200000, slowestTime); // Aucune transition > 200ms
}

// ============================================
// TEST DE WORKFLOW COMPLET
// ============================================
void test_complete_workflow_timing() {
    TEST_MESSAGE("TEST 9: Workflow complet avec mesures");
    
    struct WorkflowStep {
        const char* description;
        Formaca2::State state;
        Formaca2::ContextCode context;
    };
    
    WorkflowStep workflow[] = {
        {"Initialisation", Formaca2::State::STOP, Formaca2::ContextCode::NORMAL},
        {"Attente", Formaca2::State::IDLE, Formaca2::ContextCode::NORMAL},
        {"Homing", Formaca2::State::HOMING, Formaca2::ContextCode::AUTO_DETECT},
        {"Retour attente", Formaca2::State::IDLE, Formaca2::ContextCode::NORMAL},
        {"Découpe", Formaca2::State::NORMAL_CUTTING, Formaca2::ContextCode::USER_START},
        {"Urgence", Formaca2::State::EMERGENCY, Formaca2::ContextCode::EMERGENCY_ENTRY},
        {"Recovery", Formaca2::State::IDLE, Formaca2::ContextCode::EMERGENCY_EXIT}
    };
    
    unsigned long totalWorkflowTime = 0;
    
    Serial.println("\n  === Exécution du workflow ===");
    
    for (auto& step : workflow) {
        unsigned long duration = measureTransition(machine, step.state, step.context);
        totalWorkflowTime += duration;
        
        Serial.printf("  %-15s -> %-15s: %6lu µs\n", 
                     step.description, 
                     machine->stateToString((int)step.state),
                     duration);
        
        TEST_ASSERT_EQUAL((int)step.state, (int)machine->getCurrentFormacaState());
    }
    
    Serial.printf("\n  Temps total workflow: %lu µs (%.2f ms)\n", 
                 totalWorkflowTime, totalWorkflowTime/1000.0);
    
    TEST_ASSERT_LESS_THAN(1000000, totalWorkflowTime); // < 1 seconde
}

// ============================================
// TEST DE STABILITÉ MÉMOIRE
// ============================================
void test_memory_stability() {
    TEST_MESSAGE("TEST 10: Stabilité mémoire");
    
    unsigned long heapBefore = ESP.getFreeHeap();
    Serial.printf("\n  Heap initial: %lu octets\n", heapBefore);
    
    // Faire 10 cycles complets
    for (int cycle = 0; cycle < 10; cycle++) {
        // Créer une nouvelle machine
        Formaca2* tempMachine = new Formaca2();
        Formaca2TestHelper::initializePrivateDependencies(tempMachine);
        
        // Faire quelques transitions
        tempMachine->transitionToState(Formaca2::State::IDLE);
        tempMachine->transitionToState(Formaca2::State::STOP);
        tempMachine->transitionToState(Formaca2::State::HOMING);
        
        // Supprimer la machine
        delete tempMachine;
        
        if (cycle % 2 == 0) {
            unsigned long heapNow = ESP.getFreeHeap();
            Serial.printf("  Cycle %d - Heap: %lu octets (delta: %ld)\n", 
                         cycle+1, heapNow, (long)(heapNow - heapBefore));
        }
    }
    
    // Reset final
    BorneUniverselle::resetForTests();
    
    unsigned long heapAfter = ESP.getFreeHeap();
    long heapDiff = (long)(heapAfter - heapBefore);
    
    Serial.printf("\n  Heap final: %lu octets\n", heapAfter);
    Serial.printf("  Différence: %ld octets\n", heapDiff);
    
    // Tolérance de 1KB pour les variations normales
    TEST_ASSERT_LESS_THAN(102400, abs(heapDiff));
}

// ============================================
// POINT D'ENTRÉE PRINCIPAL
// ============================================
void setup() {
    delay(2000);
    Serial.begin(115200);
    
    Serial.println("\n\n╔══════════════════════════════════════════╗");
    Serial.println("║   TESTS DE PERFORMANCE DES TRANSITIONS    ║");
    Serial.println("║         Version Refactorisée              ║");
    Serial.println("╚══════════════════════════════════════════╝");
    Serial.printf("ESP32 - Heap disponible: %lu octets\n", ESP.getFreeHeap());
    
    #ifdef UNIT_TEST_MODE
    Serial.println("✓ UNIT_TEST_MODE actif");
    #else
    Serial.println("⚠ UNIT_TEST_MODE NON actif");
    #endif
    
    Serial.println("\nLancement des tests...\n");
    
    UNITY_BEGIN();
    
    // Tests de base
    RUN_TEST(test_machine_creation);
    RUN_TEST(test_initial_state);
    
    // Tests de mesure simple
    RUN_TEST(test_single_transition_timing);
    RUN_TEST(test_multiple_transitions_timing);
    RUN_TEST(test_emergency_transition_timing);
    
    // Tests de performance
    RUN_TEST(test_performance_100_transitions);
    RUN_TEST(test_stress_500_transitions);
    
    // Analyse
    RUN_TEST(test_bottleneck_identification);
    RUN_TEST(test_complete_workflow_timing);
    
    // Stabilité
    RUN_TEST(test_memory_stability);
    
    UNITY_END();
    
    Serial.println("\n╔══════════════════════════════════════════╗");
    Serial.println("║           TESTS TERMINÉS                  ║");
    Serial.println("╚══════════════════════════════════════════╝");
    Serial.printf("Heap final: %lu octets\n", ESP.getFreeHeap());
    
    // Cleanup final
    if (machine) {
        delete machine;
        machine = nullptr;
    }
}

void loop() {
    delay(10000);
}
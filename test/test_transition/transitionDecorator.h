// test/test_transition/TransitionDecorator.h
// ÉTAPE 2 : Pattern Decorator pour mesurer les transitions sans modifier Formaca2
// Ce pattern permet d'AJOUTER des fonctionnalités sans MODIFIER le code existant

#ifndef TRANSITION_DECORATOR_H
#define TRANSITION_DECORATOR_H

#include <functional>
#include "../src/Formaca/Formaca2.h"

/**
 * @brief Decorator pour instrumenter les transitions d'état
 * 
 * Ce decorator enveloppe Formaca2 pour mesurer les performances
 * sans modifier le code de production.
 * 
 * DESIGN PATTERN: Decorator
 * - Ajoute des fonctionnalités (mesures) sans modifier la classe originale
 * - Respecte le principe Open/Closed (ouvert à l'extension, fermé à la modification)
 */
class InstrumentedFormaca2 {
private:
    Formaca2* wrapped;  // Instance encapsulée
    
    // Métriques détaillées
    struct DetailedMetrics {
        unsigned long preTransitionTime;   // Temps avant la transition
        unsigned long transitionTime;      // Temps de la transition elle-même
        unsigned long postTransitionTime;  // Temps après la transition
        unsigned long logOverhead;         // Surcharge des logs estimée
        
        void reset() {
            preTransitionTime = 0;
            transitionTime = 0;
            postTransitionTime = 0;
            logOverhead = 0;
        }
    };
    
    DetailedMetrics lastMetrics;
    bool measurementEnabled;
    bool logsEnabled;
    
    // Callback pour intercepter les logs
    static bool logInterceptorActive;
    static unsigned long logStartTime;
    static unsigned long totalLogTime;
    
public:
    InstrumentedFormaca2(Formaca2* instance) 
        : wrapped(instance), measurementEnabled(false), logsEnabled(true) {
        lastMetrics.reset();
    }
    
    /**
     * @brief Active les mesures de performance
     */
    void enableMeasurements() { measurementEnabled = true; }
    void disableMeasurements() { measurementEnabled = false; }
    
    /**
     * @brief Contrôle les logs pendant les mesures
     */
    void setLogsEnabled(bool enabled) { logsEnabled = enabled; }
    
    /**
     * @brief Transition instrumentée avec mesures
     */
    void transitionToState(Formaca2::State newState, 
                          Formaca2::ContextCode context = Formaca2::ContextCode::AUTO_DETECT) {
        if (!measurementEnabled) {
            // Comportement normal sans instrumentation
            wrapped->transitionToState(newState, context);
            return;
        }
        
        lastMetrics.reset();
        
        // Phase 1: Pré-transition (validation, vérifications)
        unsigned long phase1Start = micros();
        Formaca2::State currentState = wrapped->getCurrentFormacaState();
        bool canTransition = (currentState != newState);
        unsigned long phase1End = micros();
        lastMetrics.preTransitionTime = phase1End - phase1Start;
        
        if (!canTransition) {
            return; // Pas de transition nécessaire
        }
        
        // Phase 2: Transition effective
        unsigned long phase2Start = micros();
        
        // Désactiver temporairement les logs si demandé
        if (!logsEnabled) {
            disableAllLogs();
        }
        
        // Effectuer la transition
        wrapped->transitionToState(newState, context);
        
        unsigned long phase2End = micros();
        lastMetrics.transitionTime = phase2End - phase2Start;
        
        // Phase 3: Post-transition (callbacks, notifications)
        unsigned long phase3Start = micros();
        
        // Réactiver les logs
        if (!logsEnabled) {
            restoreLogSettings();
        }
        
        unsigned long phase3End = micros();
        lastMetrics.postTransitionTime = phase3End - phase3Start;
        
        // Calculer l'overhead estimé des logs
        if (logsEnabled && totalLogTime > 0) {
            lastMetrics.logOverhead = totalLogTime;
            totalLogTime = 0; // Reset pour la prochaine mesure
        }
    }
    
    /**
     * @brief Obtient les dernières métriques
     */
    DetailedMetrics getLastMetrics() const { return lastMetrics; }
    
    /**
     * @brief Affiche un rapport détaillé des mesures
     */
    void printDetailedReport() {
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║     ANALYSE DÉTAILLÉE DE TRANSITION    ║");
        Serial.println("╠════════════════════════════════════════╣");
        
        unsigned long totalTime = lastMetrics.preTransitionTime + 
                                 lastMetrics.transitionTime + 
                                 lastMetrics.postTransitionTime;
        
        Serial.printf("║ Temps total:        %6lu µs         ║\n", totalTime);
        Serial.println("╟────────────────────────────────────────╢");
        Serial.printf("║ Pré-transition:     %6lu µs (%3.0f%%) ║\n", 
                     lastMetrics.preTransitionTime,
                     (lastMetrics.preTransitionTime * 100.0) / totalTime);
        Serial.printf("║ Transition:         %6lu µs (%3.0f%%) ║\n", 
                     lastMetrics.transitionTime,
                     (lastMetrics.transitionTime * 100.0) / totalTime);
        Serial.printf("║ Post-transition:    %6lu µs (%3.0f%%) ║\n", 
                     lastMetrics.postTransitionTime,
                     (lastMetrics.postTransitionTime * 100.0) / totalTime);
        
        if (lastMetrics.logOverhead > 0) {
            Serial.println("╟────────────────────────────────────────╢");
            Serial.printf("║ Overhead des logs:  %6lu µs (%3.0f%%) ║\n", 
                         lastMetrics.logOverhead,
                         (lastMetrics.logOverhead * 100.0) / totalTime);
        }
        
        Serial.println("╚════════════════════════════════════════╝\n");
    }
    
    /**
     * @brief Effectue un benchmark comparatif
     */
    void runComparativeBenchmark(int iterations = 100) {
        Serial.println("\n============ BENCHMARK COMPARATIF ============");
        
        // Test 1: Avec logs
        setLogsEnabled(true);
        enableMeasurements();
        
        unsigned long withLogsStart = millis();
        for (int i = 0; i < iterations/2; i++) {
            transitionToState(Formaca2::State::IDLE);
            transitionToState(Formaca2::State::STOP);
        }
        unsigned long withLogsDuration = millis() - withLogsStart;
        
        // Test 2: Sans logs  
        setLogsEnabled(false);
        
        unsigned long withoutLogsStart = millis();
        for (int i = 0; i < iterations/2; i++) {
            transitionToState(Formaca2::State::IDLE);
            transitionToState(Formaca2::State::STOP);
        }
        unsigned long withoutLogsDuration = millis() - withoutLogsStart;
        
        // Analyse
        Serial.printf("Transitions avec logs:    %lu ms pour %d transitions\n", 
                     withLogsDuration, iterations);
        Serial.printf("Transitions sans logs:    %lu ms pour %d transitions\n", 
                     withoutLogsDuration, iterations);
        Serial.printf("Overhead des logs:        %lu ms (%.1f%%)\n", 
                     withLogsDuration - withoutLogsDuration,
                     ((withLogsDuration - withoutLogsDuration) * 100.0) / withLogsDuration);
        Serial.printf("Gain de performance:      x%.2f plus rapide sans logs\n",
                     (float)withLogsDuration / withoutLogsDuration);
        Serial.println("==============================================\n");
    }
    
    // Accès direct à l'instance encapsulée pour les autres méthodes
    Formaca2* operator->() { return wrapped; }
    Formaca2* getInstance() { return wrapped; }
    
private:
    
    void disableAllLogs() {
        BorneUniverselle* bu = BorneUniverselle::getInstance();
        bu->setShowHeartbeatMessages(false);
    }
    
    void restoreLogSettings() {
        BorneUniverselle* bu = BorneUniverselle::getInstance();
        bu->setShowHeartbeatMessages(true);
    }
};

// Variables statiques
bool InstrumentedFormaca2::logInterceptorActive = false;
unsigned long InstrumentedFormaca2::logStartTime = 0;
unsigned long InstrumentedFormaca2::totalLogTime = 0;

#endif // TRANSITION_DECORATOR_H
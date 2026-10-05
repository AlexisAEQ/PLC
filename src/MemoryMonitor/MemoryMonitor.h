#pragma once

#include "Arduino.h"
#include <WiFi.h>
#include <map>
#include <functional>

class MemoryMonitor {
public:
    // ========================================
    // FONCTIONS PRINCIPALES DE MONITORING (existantes)
    // ========================================
    static void printStats(const char* context = nullptr);
    static bool getChange();
    static void trackStats();
    
    // Nouvelle fonction pour l'analyse de tendance
    static void analyzeMemoryTrend();

    static void printFlashStats(const char* context = nullptr);

    // ========================================
    // FONCTIONS UTILITAIRES SUPPLÉMENTAIRES (existantes)
    // ========================================
    static uint32_t getLowestFreeHeap() { return lowestFreeHeap; }
    static uint32_t getLowestFreePsram() { return lowestFreePsram; }
    static uint32_t getLowestMaxBloc() { return lowestMaxBloc; }
    
    // Fonction pour réinitialiser les statistiques
    static void resetStats() {
        lowestFreeHeap = ESP.getFreeHeap();
        lowestFreePsram = ESP.getFreePsram();
        lowestMaxBloc = ESP.getMaxAllocHeap();
    }
    
    // Fonction pour vérifier l'état critique de la mémoire
    static bool isMemoryCritical(uint32_t threshold = 50000) {
        return ESP.getFreeHeap() < threshold;
    }

    // ========================================
    // NOUVEAU SYSTÈME DE NETTOYAGE AUTOMATIQUE (RAII)
    // ========================================
    
    // Types de callbacks pour intégration avec PLC_Tools
    using CleanupCallback = std::function<void()>;
    using GarbageCollectionCallback = std::function<void()>;
    
    /**
     * @brief Classe RAII pour nettoyage automatique des ressources
     * Usage: { MemoryMonitor::ScopeCleanup cleanup("my_scope"); ... } // auto-cleanup
     */
    class ScopeCleanup {
    private:
        bool registered;
        const char* scopeName;
        
    public:
        explicit ScopeCleanup(const char* name = "unnamed_scope") 
            : registered(true), scopeName(name) {
            MemoryMonitor::registerCleanupScope(name);
        }
        
        ~ScopeCleanup() {
            if (registered) {
                MemoryMonitor::cleanupScope(scopeName);
            }
        }
        
        // Empêcher la copie (RAII pattern)
        ScopeCleanup(const ScopeCleanup&) = delete;
        ScopeCleanup& operator=(const ScopeCleanup&) = delete;
        
        // Permettre le move (C++11)
        ScopeCleanup(ScopeCleanup&& other) noexcept 
            : registered(other.registered), scopeName(other.scopeName) {
            other.registered = false;
        }
    };
    
    /**
     * @brief Enregistre un scope pour surveillance mémoire
     */
    static void registerCleanupScope(const char* scopeName);
    
    /**
     * @brief Nettoie les ressources d'un scope et détecte les fuites
     */
    static void cleanupScope(const char* scopeName);
    
    // ========================================
    // SYSTÈME DE CALLBACKS POUR INTÉGRATION
    // ========================================
    
    /**
     * @brief Définit le callback de nettoyage des maps statiques
     */
    static void setCleanupCallback(CleanupCallback callback) {
        Serial.println("Memory monitor callback registration");
        if (!heap_caps_check_integrity_all(true)) {
            Serial.println("❌ HEAP CORROMPU DÉTECTÉ!");
        }
        cleanupCallback = callback;
    }
    
    /**
     * @brief Définit le callback de garbage collection
     */
    static void setGarbageCollectionCallback(GarbageCollectionCallback callback) {
        gcCallback = callback;
    }
    
    /**
     * @brief Nettoie les maps statiques via callback
     */
    static void cleanupStaticMaps();
    
    // ========================================
    // SURVEILLANCE PÉRIODIQUE
    // ========================================
    
    /**
     * @brief Démarre la surveillance périodique automatique
     * @param intervalMs Intervalle en millisecondes (défaut: 30 secondes)
     */
    static void startPeriodicWatching(uint32_t intervalMs = 30000);
    
    /**
     * @brief Arrête la surveillance périodique
     */
    static void stopPeriodicWatching();
    
    /**
     * @brief Vérifie si la surveillance périodique doit être exécutée
     * À appeler dans la boucle principale
     */
    static void checkPeriodicWatching();
    
    // ========================================
    // DÉTECTION AVANCÉE DE FUITES
    // ========================================
    
    /**
     * @brief Détecte une fuite mémoire par rapport à une baseline
     * @param checkpoint Nom du point de contrôle pour le log
     * @param thresholdBytes Seuil de perte en bytes (négatif)
     * @return true si une fuite est détectée
     */
    static bool detectMemoryLeak(const char* checkpoint, int32_t thresholdBytes = -10000);
    
    /**
     * @brief Force un garbage collection intelligent avec statistiques
     */
    static void forceSmartGarbageCollection();
    
    /**
     * @brief Définit une nouvelle baseline pour la détection de fuites
     * @param baseline Nouvelle baseline (0 = heap actuel)
     */
    static void setMemoryBaseline(size_t baseline = 0) {
        baselineHeap = (baseline == 0) ? ESP.getFreeHeap() : baseline;
    }
    
    /**
     * @brief Obtient la baseline actuelle
     */
    static size_t getMemoryBaseline() { return baselineHeap; }
    
    // ========================================
    // UTILITAIRES DE DIAGNOSTIC
    // ========================================
    
    /**
     * @brief Calcule le pourcentage d'utilisation de la mémoire
     */
    static float getMemoryUsagePercent() {
        // Estimation basée sur ESP32 avec ~320KB de heap
        const size_t ESTIMATED_TOTAL_HEAP = 320000;
        size_t used = ESTIMATED_TOTAL_HEAP - ESP.getFreeHeap();
        return (float)used * 100.0f / ESTIMATED_TOTAL_HEAP;
    }
    
    /**
     * @brief Vérifie si le système approche des limites critiques
     */
    static bool isApproachingMemoryLimit(float warningPercent = 80.0f) {
        return getMemoryUsagePercent() > warningPercent;
    }

private:
    // ========================================
    // VARIABLES STATIQUES EXISTANTES
    // ========================================
    static uint32_t lowestFreeHeap;
    static uint32_t lowestFreePsram;
    static uint32_t lowestMaxBloc;
    
    // ========================================
    // NOUVELLES VARIABLES POUR LE SYSTÈME AVANCÉ
    // ========================================
    
    // Surveillance périodique
    static bool periodicWatchingEnabled;
    static uint32_t lastPeriodicCheck;
    static uint32_t periodicInterval;
    
    // Gestion des scopes et baselines
    static size_t baselineHeap;
    static std::map<String, size_t> scopeBaselines;
    
    // Callbacks pour intégration avec PLC_Tools
    static CleanupCallback cleanupCallback;
    static GarbageCollectionCallback gcCallback;
};
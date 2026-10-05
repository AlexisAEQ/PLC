#include "MemoryMonitor/MemoryMonitor.h"

// Initialize static member variables
// Variables statiques existantes
uint32_t MemoryMonitor::lowestFreeHeap = ESP.getFreeHeap();
uint32_t MemoryMonitor::lowestFreePsram = ESP.getFreePsram();
uint32_t MemoryMonitor::lowestMaxBloc = ESP.getMaxAllocHeap();

// ✅ Nouvelles variables statiques
bool MemoryMonitor::periodicWatchingEnabled = false;
uint32_t MemoryMonitor::lastPeriodicCheck = 0;
uint32_t MemoryMonitor::periodicInterval = 30000;
size_t MemoryMonitor::baselineHeap = 0;
std::map<String, size_t> MemoryMonitor::scopeBaselines;

MemoryMonitor::CleanupCallback MemoryMonitor::cleanupCallback = nullptr;
MemoryMonitor::GarbageCollectionCallback MemoryMonitor::gcCallback = nullptr;

void MemoryMonitor::printFlashStats(const char* context) {
    uint32_t flashChipSize   = ESP.getFlashChipSize();   // physique, indépendant des partitions
    uint32_t sketchSize      = ESP.getSketchSize();      // dépend de la partition "app"
    uint32_t freeSketchSpace = ESP.getFreeSketchSpace();
    uint32_t appPartitionSize = sketchSize + freeSketchSpace;

    if (context) {
        Serial.printf("\n=== Flash Stats [%s] ===\n", context);
    } else {
        Serial.println("\n=== Flash Stats ===");
    }

    Serial.printf("Flash Chip Size (physique): %lu bytes (%.2f MB)\n",
                 (unsigned long)flashChipSize, flashChipSize / (1024.0f * 1024.0f));
    Serial.printf("Sketch Size: %lu bytes (%.2f MB)\n",
                 (unsigned long)sketchSize, sketchSize / (1024.0f * 1024.0f));
    Serial.printf("Free Sketch Space: %lu bytes (%.2f MB)\n",
                 (unsigned long)freeSketchSpace, freeSketchSpace / (1024.0f * 1024.0f));

    if (appPartitionSize > 0) {
        float usagePercent = 100.0f * (float)sketchSize / (float)appPartitionSize;
        Serial.printf("App Partition Usage: %.1f%% (partition app: %lu bytes)\n",
                     usagePercent, (unsigned long)appPartitionSize);
    }

    Serial.println("========================\n");
}

void MemoryMonitor::printStats(const char* context) {
    static uint32_t lastPrintCall = 0;
    uint32_t now = millis();
    
    // ✅ Éviter les appels trop fréquents
    if (now - lastPrintCall < 3000) {
        return;
    }
    lastPrintCall = now;
    
    try {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minFreeHeap = ESP.getMinFreeHeap();
        uint32_t freePsram = ESP.getFreePsram();
        uint32_t minFreePsram = ESP.getMinFreePsram();

        if (context) {
            Serial.printf("\n=== Memory Stats [%s] ===\n", context);
        } else {
            Serial.println("\n=== Memory Stats ===");
        }
        
        Serial.printf("Free Heap: %lu bytes (Min: %lu, Lowest: %lu)\n", 
                     (unsigned long)freeHeap, (unsigned long)minFreeHeap, 
                     (unsigned long)lowestFreeHeap);
        
        // ✅ ÉVITER getMaxAllocHeap() qui cause les crashes
        Serial.println("Max Alloc Heap: DISABLED (causes crashes in tlsf_walk_pool)");
        
        Serial.printf("Free PSRAM: %lu bytes (Min: %lu, Lowest: %lu)\n", 
                     (unsigned long)freePsram, (unsigned long)minFreePsram, 
                     (unsigned long)lowestFreePsram);
        
        // ✅ Estimation de fragmentation basée sur d'autres métriques
        float estimatedFragmentation = 100.0f * (1.0f - (float)minFreeHeap / (float)freeHeap);
        Serial.printf("Estimated Fragmentation: %.1f%%\n", estimatedFragmentation);
        
        Serial.println("========================\n");
        
    } catch (...) {
        Serial.println("Exception in printStats - memory system unstable");
    }
}

bool MemoryMonitor::getChange() {
    uint32_t currentFreeHeap = ESP.getFreeHeap();
    return currentFreeHeap < lowestFreeHeap;
}

void MemoryMonitor::trackStats() {
    static uint32_t lastSafeCall = 0;
    uint32_t now = millis();
    
    // ✅ THROTTLING obligatoire pour éviter les appels trop fréquents
    if (now - lastSafeCall < 2000) { // Max 1 fois toutes les 2 secondes
        return;
    }
    lastSafeCall = now;
    
    // ✅ Vérification de l'état système AVANT tout appel
    if (WiFi.getMode() != WIFI_OFF && WiFi.status() == WL_CONNECTED) {
        // En cas de WiFi actif, être encore plus prudent
        if (now - lastSafeCall < 5000) {
            return;
        }
    }
    
    try {
        // ✅ Appels ESP32 sécurisés seulement
        uint32_t currentFreeHeap = ESP.getFreeHeap();
        uint32_t currentFreePsram = ESP.getFreePsram();
        
        // ✅ Tracking SANS getMaxAllocHeap() qui cause le crash
        if (currentFreeHeap < lowestFreeHeap) {
            lowestFreeHeap = currentFreeHeap;
            Serial.printf("%lu::New lowest free heap: %lu bytes\n", 
                         (unsigned long)now, (unsigned long)lowestFreeHeap);
            
            // ✅ Analyser la tendance SEULEMENT si safe
            if (currentFreeHeap > 20000) {
                analyzeMemoryTrend();
            }
        }   

        if (currentFreePsram < lowestFreePsram) {
            lowestFreePsram = currentFreePsram;
            Serial.printf("%lu::New lowest free PSRAM: %lu bytes\n", 
                         (unsigned long)now, (unsigned long)lowestFreePsram);
        }
        
        // ✅ Log périodique simplifié (toutes les 10 fois)
        static uint32_t logCounter = 0;
        if (++logCounter % 10 == 0) {
            Serial.printf("Memory: Heap=%lu, PSRAM=%lu\n", 
                         (unsigned long)currentFreeHeap, (unsigned long)currentFreePsram);
        }
        
    } catch (const std::exception& e) {
        Serial.printf("Exception in trackStats: %s\n", e.what());
    } catch (...) {
        Serial.println("Unknown exception in trackStats - heap possibly corrupted");
    }
}

void MemoryMonitor::analyzeMemoryTrend() {
    static uint32_t lastHeapValue = 0;
    static uint32_t lastCheckTime = 0;
    static uint32_t totalLoss = 0;
    static uint32_t checkCount = 0;
    
    uint32_t currentTime = millis();
    uint32_t currentHeap = ESP.getFreeHeap();
    
    // Premier appel
    if (lastHeapValue == 0) {
        lastHeapValue = currentHeap;
        lastCheckTime = currentTime;
        return;
    }
    
    // Calculer la perte sur cette période
    if (currentHeap < lastHeapValue) {
        uint32_t loss = lastHeapValue - currentHeap;
        uint32_t timeElapsed = currentTime - lastCheckTime;
        
        totalLoss += loss;
        checkCount++;
        
        // Éviter la division par zéro
        if (timeElapsed > 0) {
            float avgLossPerSecond = (float)totalLoss * 1000.0f / (currentTime - lastCheckTime);
            Serial.printf("Memory loss trend: %lu bytes in %lu ms (%.2f bytes/sec average)\n", 
                         (long unsigned int)loss, (long unsigned)timeElapsed, avgLossPerSecond);
            
            // Alerte si la perte est trop rapide
            if (avgLossPerSecond > 100.0f) { // Plus de 100 bytes/sec
                Serial.printf("WARNING: High memory loss rate detected: %.2f bytes/sec\n", avgLossPerSecond);
            }
        }
    }
    
    lastHeapValue = currentHeap;
    lastCheckTime = currentTime;
}

void MemoryMonitor::registerCleanupScope(const char* scopeName) {
    String key(scopeName);
    scopeBaselines[key] = ESP.getFreeHeap();
    //Serial.printf("🔍 Memory scope registered: %s (baseline: %zu bytes)\n", scopeName, scopeBaselines[key]);
}

void MemoryMonitor::cleanupScope(const char* scopeName) {
    String key(scopeName);
    auto it = scopeBaselines.find(key);
    
    if (it != scopeBaselines.end()) {
        size_t currentHeap = ESP.getFreeHeap();
        int32_t diff = static_cast<int32_t>(currentHeap) - static_cast<int32_t>(it->second);
        
        //Serial.printf("🧹 Cleaning scope '%s': %s %lu bytes\n", scopeName, diff >= 0 ? "freed" : "leaked", abs(diff));
        
        if (diff < -5000) {
            Serial.printf("⚠️ Memory leak detected in scope '%s': %lu bytes\n", scopeName, -diff);
            forceSmartGarbageCollection();
        }
        
        scopeBaselines.erase(it);
    }
    
    // Nettoyer les maps statiques si nécessaire
    cleanupStaticMaps();
}

void MemoryMonitor::cleanupStaticMaps() {
    if (cleanupCallback) {
        cleanupCallback();
    } else {
        Serial.println("⚠️ No cleanup callback registered");
    }
}

void MemoryMonitor::startPeriodicWatching(uint32_t intervalMs) {
    periodicWatchingEnabled = true;
    periodicInterval = intervalMs;
    lastPeriodicCheck = millis();
    baselineHeap = ESP.getFreeHeap();
    
    Serial.printf("🔍 Periodic memory watching started: interval=%lu ms, baseline=%zu bytes\n", 
                 (unsigned long)intervalMs, baselineHeap);
}

void MemoryMonitor::stopPeriodicWatching() {
    periodicWatchingEnabled = false;
    Serial.println("🔍 Periodic memory watching stopped");
}

void MemoryMonitor::checkPeriodicWatching() {
    if (!periodicWatchingEnabled) return;
    
    uint32_t now = millis();
    if (now - lastPeriodicCheck >= periodicInterval) {
        trackStats();
        
        if (getChange()) {
            analyzeMemoryTrend();
        }
        
        // Vérifier les fuites globales
        if (detectMemoryLeak("periodic_check")) {
            Serial.println("🚨 Periodic leak detection triggered cleanup");
        }
        
        lastPeriodicCheck = now;
    }
}

bool MemoryMonitor::detectMemoryLeak(const char* checkpoint, int32_t thresholdBytes) {
    size_t currentHeap = ESP.getFreeHeap();
    int32_t diff = static_cast<int32_t>(currentHeap) - static_cast<int32_t>(baselineHeap);
    
    if (diff < thresholdBytes) {
        Serial.printf("🚨 Memory leak detected at %s: lost %lu bytes (threshold: %lu)\n", 
                     checkpoint, -diff, -thresholdBytes);
        printStats(checkpoint);
        return true;
    }
    
    return false;
}

void MemoryMonitor::forceSmartGarbageCollection() {
    Serial.println("🧹 Starting smart garbage collection...");
    
    size_t heapBefore = ESP.getFreeHeap();
    size_t psramBefore = ESP.getFreePsram();
    
    if (gcCallback) {
        gcCallback();
    } else {
        Serial.println("⚠️ No GC callback registered, using basic cleanup");
        // Nettoyage basique si pas de callback
        #ifdef ESP32
        // Force manual heap compaction
        void* test = malloc(1);
        free(test);
        #endif
    }
    
    size_t heapAfter = ESP.getFreeHeap();
    size_t psramAfter = ESP.getFreePsram();
    
    Serial.printf("🧹 GC Results: Heap %s %zu bytes, PSRAM %s %zu bytes\n",
                 heapAfter > heapBefore ? "gained" : "lost", 
                 heapAfter > heapBefore ? heapAfter - heapBefore : heapBefore - heapAfter,
                 psramAfter > psramBefore ? "gained" : "lost", 
                 psramAfter > psramBefore ? psramAfter - psramBefore : psramBefore - psramAfter);
}


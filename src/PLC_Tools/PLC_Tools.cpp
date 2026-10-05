#include "PLC_Tools/PLC_Tools.h"
#include "MutexGuard/MutexGuard.h" 
#include "PLC_Tools.h"

// Variables statiques pour JsonDisplayController
uint32_t JsonDisplayController::totalJsonsShown = 0;
uint32_t JsonDisplayController::maxJsonsToShow = 5;
bool JsonDisplayController::showAllJsons = false;
std::vector<String> JsonDisplayController::shownErrorTypes;

char* PLC_Tools::diagnosticBuffer = nullptr;
size_t PLC_Tools::diagnosticBufferUsed = 0;
size_t PLC_Tools::maxDiagnosticBufferSize = 0;
bool PLC_Tools::diagnosticBufferInitialized = false;
bool PLC_Tools::autoFlushEnabled = true;
SemaphoreHandle_t PLC_Tools::diagnosticMutex = nullptr;

// Mécanique pour sendPEriodiquMessage
std::map<uint32_t, PLC_Tools::PeriodicMessageState> PLC_Tools::periodicMessagesMap;
std::vector<String> PLC_Tools::filteredMessagePatterns;

// Structure pour le throttling des logs
struct LogThrottleEntry {
    uint32_t lastLogTime;
    uint32_t logCount;
    String lastMessage;
    
    LogThrottleEntry() : lastLogTime(0), logCount(0) {}
};

struct FileThrottleEntry {
    uint32_t lastLogTime;
    uint32_t suppressedCount;
    String lastMessage;
    
    FileThrottleEntry() : lastLogTime(0), suppressedCount(0) {}
};

// ✅ Variables globales (placez-les avant la structure d'initialisation)
std::map<String, LogThrottleEntry> logThrottleMap;
std::map<String, FileThrottleEntry> fileThrottleMap;

bool PLC_Tools::logReboot() {
    // Utiliser PLC_Persistence pour gérer l'accès au système de fichiers
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    const char* LOG_FILE = "/reboot_log.json";
    
    // Récupère le service RTC (singleton)
    RTC_Service* rtc = RTC_Service::getInstance();
    
    // Prépare les informations de temps
    char timestamp[32];
    uint32_t unixTime = 0;
    bool hasValidRTC = false;
    
    if (rtc->hasValidTime()) {
        // RTC disponible et configuré
        rtc->getTimestamp(timestamp, sizeof(timestamp));
        unixTime = rtc->getUnixTime();
        hasValidRTC = true;
    } else {
        // Fallback sur millis()
        uint32_t uptime = millis();
        snprintf(timestamp, sizeof(timestamp), "+%010lums", uptime);
    }
    
    // Si le fichier n'existe pas, le créer avec une entrée initiale
    if (!persistence.fileExists(LOG_FILE)) {
        Serial.println("[PLC_Tools] Création d'un nouveau journal de redémarrage");
        JsonDocument doc;
        doc["reboots"] = JsonArray();
        
        // Ajouter la première entrée de journal
        JsonArray reboots = doc["reboots"].to<JsonArray>();
        JsonObject entry = reboots.add<JsonObject>();
        
        if (hasValidRTC) {
            entry["timestamp"] = timestamp;      // "2024-12-21 14:32:45"
            entry["unix"] = unixTime;            // 1703170365
            entry["source"] = "rtc";
        } else {
            entry["timestamp"] = timestamp;      // "+0000012345ms"
            entry["millis"] = millis();
            entry["source"] = "system";
        }
        
        entry["heap"] = ESP.getFreeHeap();
        entry["psram"] = ESP.getFreePsram();
        entry["type"] = "initial";
        
        // Raison du reset ESP32
        esp_reset_reason_t reason = esp_reset_reason();
        entry["reset_reason"] = getResetReason(reason);
        
        return persistence.saveJsonToFile(LOG_FILE, doc);
    }
    
    // Lire le fichier existant
    SafeJsonDocument doc;
    if (!persistence.readJsonFromFile(LOG_FILE, doc)) {
        Serial.println("[PLC_Tools] Échec de lecture du journal, suppression et recréation");
        persistence.deleteFile(LOG_FILE);  // Supprimer le fichier corrompu
        doc.clear();
        doc["reboots"] = JsonArray();
    }
    
    // S'assurer que le format est correct
    if (doc["reboots"].isNull()) {
        doc["reboots"] = JsonArray();
    }
    
    // Ajouter une nouvelle entrée
    JsonArray reboots = doc["reboots"].as<JsonArray>();
    JsonObject entry = reboots.add<JsonObject>();
    
    // Informations temporelles
    if (hasValidRTC) {
        entry["timestamp"] = timestamp;      // "2024-12-21 14:32:45"
        entry["unix"] = unixTime;            // 1703170365
        entry["source"] = "rtc";
    } else {
        entry["timestamp"] = timestamp;      // "+0000012345ms"
        entry["millis"] = millis();
        entry["source"] = "system";
    }
    
    // Informations système
    entry["heap"] = ESP.getFreeHeap();
    entry["psram"] = ESP.getFreePsram();
    entry["type"] = "normal";
    
    // Raison du reset ESP32
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("=== Reset reason: %d ===\n", (int)reason);
    entry["reset_reason"] = getResetReason(reason);
    
    // Uptime depuis le dernier boot (en secondes)
    uint32_t uptimeSeconds = millis() / 1000;
    entry["uptime_sec"] = uptimeSeconds;
    
    // Limiter la taille du log (garder seulement les 20 derniers redémarrages)
    while (reboots.size() > 20) {
        // Supprimer le premier élément (le plus ancien)
        reboots.remove(0);
    }
    
    // Sauvegarder les modifications
    bool success = persistence.saveJsonToFile(LOG_FILE, doc);
    
    if (success) {
        Serial.printf("[PLC_Tools] ✓ Reboot logged: %s (%s)\n", 
                     timestamp, getResetReason(reason).c_str());
    } else {
        Serial.println("[PLC_Tools] ⚠️ Échec sauvegarde reboot log");
    }
    
    return success;
}

String PLC_Tools::getRebootLog() {
    // Utiliser PLC_Persistence pour gérer l'accès au système de fichiers
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    const char* LOG_FILE = "/reboot_log.json";
    
    if (!persistence.fileExists(LOG_FILE)) {
        return "Aucun journal de redémarrage disponible";
    }
    
    SafeJsonDocument doc;
    if (!persistence.readJsonFromFile(LOG_FILE, doc)) {
        return "Erreur lors de la lecture du journal de redémarrage";
    }
    
    if (doc["reboots"].isNull() || doc["reboots"].size() == 0) {
        return "Journal de redémarrage vide";
    }
    
    String result = "Journal des redémarrages :\n";
    result += "------------------------\n";
    
    int count = 1;
    for (JsonObject reboot : doc["reboots"].as<JsonArray>()) {
        result += "Redémarrage #" + String(count++) + ":\n";
        
        if (!reboot["time"].isNull()) {
            result += "  Temps: " + String((unsigned long)reboot["time"]) + " ms\n";
        }
        
        if (!reboot["heap"].isNull()) {
            result += "  Mémoire libre: " + String((unsigned long)reboot["heap"]) + " octets\n";
        }
        
        if (!reboot["psram"].isNull()) {
            result += "  PSRAM libre: " + String((unsigned long)reboot["psram"]) + " octets\n";
        }
        
        if (!reboot["type"].isNull()) {
            result += "  Type: " + String((const char*)reboot["type"]) + "\n";
        }
        
        // Ajouter d'autres champs si nécessaire
        
        result += "\n";
    }
    
    return result;
}

bool PLC_Tools::clearRebootLog() {
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    return persistence.deleteFile(REBOOT_LOG_FILE);
}

String PLC_Tools::getResetReason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:    return "Unknown";
        case ESP_RST_POWERON:    return "Power-on";
        case ESP_RST_EXT:        return "External reset/button";
        case ESP_RST_SW:         return "Software reset";
        case ESP_RST_PANIC:      return "Kernel panic";
        case ESP_RST_INT_WDT:    return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:   return "Task watchdog";
        case ESP_RST_WDT:        return "Other watchdog";
        case ESP_RST_DEEPSLEEP:  return "Deep sleep wake";
        case ESP_RST_BROWNOUT:   return "Brownout";
        case ESP_RST_SDIO:       return "SDIO";
        default:                 return "Unknown reason";
    }
}

void PLC_Tools::printBits(uint16_t nombre) {
    printf("Bits de %u (0x%04X): ", nombre, nombre);
    
    // Parcourir chaque bit, du plus significatif au moins significatif
    for (int i = 15; i >= 0; i--) {
        // Extraire le bit à la position i
        uint16_t bit = (nombre >> i) & 1;
        printf("%u", bit);
        
        // Ajouter un espace tous les 4 bits pour améliorer la lisibilité
        if (i % 4 == 0 && i > 0) {
            printf(" ");
        }
    }
    printf("\n");
}

void PLC_Tools::printJsonValue(const JsonVariant &value, int indent) {
    String indentStr = "";
    for (int i = 0; i < indent; i++) {
        indentStr += "  ";
    }

    if (value.is<JsonObject>()) {
        JsonObject obj = value.as<JsonObject>();
        Serial.println(indentStr + "{");
        for (JsonPair p : obj) {
            Serial.print(indentStr + "  \"" + p.key().c_str() + "\": ");
            printJsonValue(p.value(), indent + 1);
        }
        Serial.println(indentStr + "}");
    }
    else if (value.is<JsonArray>()) {
        JsonArray arr = value.as<JsonArray>();
        Serial.println(indentStr + "[");
        for (JsonVariant v : arr) {
            Serial.print(indentStr + "  ");
            printJsonValue(v, indent + 1);
        }
        Serial.println(indentStr + "]");
    }
    else if (value.is<const char*>()) {
        Serial.println("\"" + String(value.as<const char*>()) + "\"");
    }
    else if (value.is<bool>()) {
        Serial.println(value.as<bool>() ? "true" : "false");
    }
    else if (value.is<double>()) {
        // Vérifier si c'est un entier de grande taille
        double dValue = value.as<double>();
        if (dValue == floor(dValue) && dValue <= UINT32_MAX && dValue >= 0) {
            // C'est probablement un entier non signé
            Serial.println((uint32_t)dValue);
        } else {
            Serial.println(dValue);
        }
    }
    else if (value.is<float>()) {
        Serial.println(value.as<float>());
    }
    else if (value.is<int>()) {
        Serial.println(value.as<int>());
    }
    else if (value.isNull()) {
        Serial.println("null");
    }
    else {
        Serial.println("unknown type");
    }
}

// send a JsonDocument to the web socket, with optional context for debugging
bool PLC_Tools::sendJsonDocument(SafeJsonDocument& doc, const char* context) {
    // Vérifier que le document n'est pas vide
    if (doc.isNull()) {
        Serial.println("❌ PLC_Tools::sendJsonDocument: document NULL");
        return false;
    }
    
    // Mesurer la taille
    uint16_t size = measureJson(doc);
    if (size == 0) {
        Serial.println("❌ PLC_Tools::sendJsonDocument: taille JSON = 0");
        return false;
    }

    if (size > SOCKET_MESSAGE_MAX_SIZE) {
        Serial.printf("⚠️ PLC_Tools::sendJsonDocument: taille JSON (%u bytes) dépasse la limite de %u bytes\n", size, SOCKET_MESSAGE_MAX_SIZE);
        return false;
    }
    
    // Allouer la mémoire
    char *chain = (char *)allocateMemory(size + 10);
    if (!chain) {
        Serial.println("❌ PLC_Tools::sendJsonDocument: échec allocation mémoire");
        return false;
    }
    
    // Sérialiser
    size_t effectiveSize = serializeJson(doc, chain, size + 10);
    chain[effectiveSize] = 0;
    
    // Debug avec contexte
    if (context) {
        // Serial.printf("📦 sendJsonDocument [%s]: %u bytes\n", context, effectiveSize);
    } else {
        // Serial.printf("📦 sendJsonDocument: %u bytes\n", effectiveSize);
    }
    
    // Envoyer via BorneUniverselle
    bool success = BorneUniverselle::sendTextToClient(chain);
    if (!success) {
        if (context) {
            Serial.printf("❌ PLC_Tools::sendJsonDocument: échec envoi [%s]\n", context);
        } else {
            Serial.println("❌ PLC_Tools::sendJsonDocument: échec envoi");
        }
    }
    
    // Libérer
    freeMemory(chain);
    
    return success;
} // sendJsonDocument (to the web socket)

void PLC_Tools::logJsonInChunks(const String& jsonString, uint16_t recipeId) {
    size_t pos = 0;
    size_t chunkNum = 1;
    
    while (pos < jsonString.length()) {
        String chunk = jsonString.substring(pos, pos + MAX_JSON_CHUNK_SIZE);
        
        char diagnosticMsg[200];
        snprintf(diagnosticMsg, sizeof(diagnosticMsg), 
                "RECETTE_ERREUR_ID_%u_PARTIE_%zu: %s", 
                recipeId, chunkNum, chunk.c_str());
        
        logDiagnostic(diagnosticMsg);
        
        pos += MAX_JSON_CHUNK_SIZE;
        chunkNum++;
        
        // Limiter le nombre de chunks pour éviter le spam
        if (chunkNum > MAX_DIAGNOSTIC_ENTRIES_PER_RECIPE) {
            char truncatedMsg[200];
            snprintf(truncatedMsg, sizeof(truncatedMsg), 
                    "RECETTE_ERREUR_ID_%u: JSON tronqué après %zu parties", 
                    recipeId, chunkNum - 1);
            logDiagnostic(truncatedMsg);
            break;
        }
    }
}

void PLC_Tools::logRecipeErrorSummary(size_t totalErrors, size_t totalRecipes) {
    float errorRate = (float)totalErrors / (float)totalRecipes * 100.0f;
    
    char summaryMsg[200];
    snprintf(summaryMsg, sizeof(summaryMsg),
            "RECETTES_ERREURS_FINAL: %zu erreurs sur %zu recettes (%.1f%%)",
            totalErrors, totalRecipes, errorRate);
    logDiagnostic(summaryMsg);
    
    // Ajouter des recommandations basées sur le taux d'erreur
    const char* recommendation = nullptr;
    if (errorRate > 50.0f) {
        recommendation = "CRITIQUE: Vérifier le format du fichier";
    } else if (errorRate > 20.0f) {
        recommendation = "ATTENTION: Données possiblement corrompues";
    } else if (errorRate > 5.0f) {
        recommendation = "AVERTISSEMENT: Quelques entrées invalides";
    } else {
        recommendation = "INFO: Faible taux d'erreur";
    }
    
    char recommendationMsg[200];
    snprintf(recommendationMsg, sizeof(recommendationMsg),
            "RECETTES_RECOMMANDATION: %s", recommendation);
    logDiagnostic(recommendationMsg);
}

void PLC_Tools::cleanupDiagnosticFile() {
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    
    if (persistence.getFileSize(DIAGNOSTIC_FILE) > 16384) {
        String content;
        persistence.readFile(DIAGNOSTIC_FILE, content);
        
        // Garder seulement la fin
        if (content.length() > 3500) {
            content = content.substring(content.length() - 3500);
            int firstNewline = content.indexOf('\n');
            if (firstNewline >= 0) {
                content = content.substring(firstNewline + 1);
            }
        }
        
        persistence.saveToFile(DIAGNOSTIC_FILE, content);
        Serial.println("Diagnostic file cleaned up");
    }
}

bool PLC_Tools::isPSRAM_available() {
    static bool checked = false;
    static bool available = false;
    
    if (!checked) {
        checked = true;
        
        // ÉTAPE 1 : Vérifier si PSRAM est trouvé (plus fiable que psramInit)
        if (!psramFound()) {
            Serial.println("PSRAM check: Hardware not found");
            available = false;
            return available;
        }
        
        // ÉTAPE 2 : Vérifier la taille PSRAM
        size_t psramSize = ESP.getPsramSize();
        if (psramSize == 0) {
            Serial.println("PSRAM check: Found but size is 0");
            available = false;
            return available;
        }
        
        // ÉTAPE 3 : Essayer l'initialisation si pas déjà fait
        bool initResult = psramInit();
        if (!initResult) {
            Serial.printf("PSRAM check: Init failed but size detected: %zu bytes\n", psramSize);
            // Sur certains modules, psramInit() peut échouer même si PSRAM est disponible
            // On accepte si la taille est valide
        }
        
        // ÉTAPE 4 : Test d'allocation pour confirmer le fonctionnement
        void* testPtr = ps_malloc(1024);  // Test avec 1KB
        if (testPtr != nullptr) {
            free(testPtr);
            available = true;
            Serial.printf("✅ PSRAM check: Available and functional (Size: %zu bytes)\n", psramSize);
        } else {
            available = false;
            Serial.printf("❌ PSRAM check: Detected but allocation failed (Size: %zu bytes)\n", psramSize);
        }
    }
    
    return available;
}

void* PLC_Tools::allocateMemory(size_t size, MemoryPreference pref) {
    if (size == 0) return nullptr;
    if (size > 1024 * 1024) {
        Serial.printf("❌ allocateMemory: Size too large (%u bytes)\n", size);
        return nullptr;
    }
    
    void* ptr = nullptr;
    
    // ══════════════════════════════════════════════════════
    // STRATÉGIE SELON LA PRÉFÉRENCE
    // ══════════════════════════════════════════════════════
    
    bool tryPSRAM = false;
    
    switch (pref) {
        case MEMORY_PREFER_PSRAM:
            tryPSRAM = isPSRAM_available();
            break;
        case MEMORY_FORCE_HEAP:
            tryPSRAM = false;
            break;
        case MEMORY_AUTO:
        default:
            // Comportement actuel : PSRAM si >= 512 bytes
            tryPSRAM = isPSRAM_available() && (size >= 512);
            break;
    }
    
    // ════════════════════════════════════════════════
    // CALCUL DE LA MARGE PROPORTIONNELLE
    // ════════════════════════════════════════════════
    
    size_t safetyMargin;
    if (size < 1024) {
        safetyMargin = 5000;   // Petites allocations: 5KB
    } else if (size < 10240) {
        safetyMargin = 10000;  // Moyennes allocations: 10KB
    } else {
        safetyMargin = 20000;  // Grandes allocations: 20KB
    }
    
    // ════════════════════════════════════════════════
    // ALLOCATION PSRAM
    // ════════════════════════════════════════════════
    
    if (tryPSRAM) {
        size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (psramFree >= size + safetyMargin) {
            ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
            if (ptr) {
                memset(ptr, 0, size);
                return ptr;
            }
        }
    }
    
    // ════════════════════════════════════════════════
    // FALLBACK HEAP INTERNE
    // ════════════════════════════════════════════════
    
    size_t heapFree = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (heapFree >= size + safetyMargin) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
        if (ptr) {
            memset(ptr, 0, size);
            return ptr;
        }
    }
    
    Serial.printf("❌ allocateMemory FAILED: Size=%u\n", size);
    Serial.printf("   📊 Heap libre: %u bytes\n", heapFree);
    Serial.printf("   📊 Requis: %u bytes (allocation) + 20000 bytes (marge) = %u bytes\n", 
                  size, size + 20000);
    Serial.printf("   📊 Manque: %d bytes\n", (int)(size + 20000 - heapFree));
    return nullptr;
}

// ✅ NOUVELLE FONCTION DE VALIDATION SÉCURISÉE - À AJOUTER AU .h ET .cpp
bool PLC_Tools::validateAllocatedMemory(void* ptr, size_t expectedSize) {
    if (!ptr) {
        return false;
    }
    
    // ✅ Vérification 1: Le pointeur est-il dans une plage mémoire valide ?
    size_t actualSize = heap_caps_get_allocated_size(ptr);
    if (actualSize == 0) {
        Serial.printf("VALIDATION ERROR: Invalid pointer %p - not in heap\n", ptr);
        return false;
    }
    
    if (actualSize < expectedSize) {
        Serial.printf("VALIDATION ERROR: Allocated size %zu < expected %zu\n", actualSize, expectedSize);
        return false;
    }
    
    // ✅ Vérification 2: Test d'écriture/lecture sécurisé
    // Écrire un pattern au début et à la fin de la zone
    volatile uint8_t* testPtr = (volatile uint8_t*)ptr;
    uint8_t originalStart = testPtr[0];
    // Note: originalEnd pas utilisé car on va faire memset(0) après
    
    // Pattern de test
    const uint8_t testPattern1 = 0xAA;
    const uint8_t testPattern2 = 0x55;
    
    // Test écriture/lecture début
    testPtr[0] = testPattern1;
    if (testPtr[0] != testPattern1) {
        Serial.printf("VALIDATION ERROR: Memory write/read test failed at start of %p\n", ptr);
        return false;
    }
    
    // Test écriture/lecture fin
    testPtr[expectedSize - 1] = testPattern2;
    if (testPtr[expectedSize - 1] != testPattern2) {
        Serial.printf("VALIDATION ERROR: Memory write/read test failed at end of %p\n", ptr);
        // Restaurer le début avant de retourner
        testPtr[0] = originalStart;
        return false;
    }
    
    // ✅ Initialiser proprement la mémoire
    memset(ptr, 0, expectedSize);
    
    return true;
}

void PLC_Tools::freeMemory(void* ptr) {
    if (!ptr) {
        // ✅ PAS DE LOG pour éviter la récursion dans logReboot()
        return;
    }
    
    // ✅ LIBÉRATION DIRECTE ET SIMPLE - pas de vérification complexe
    // Le problème était heap_caps_get_allocated_size() qui causait la corruption
    heap_caps_free(ptr);
    
    // ✅ PAS DE TRACKING pendant l'initialisation pour éviter les problèmes
}

// ✅ NOUVELLES FONCTIONS DE VALIDATION ESP32-SAFE - À AJOUTER AU .h ET .cpp

bool PLC_Tools::validatePointerBeforeFree(void* ptr) {
    if (!ptr) {
        return false;
    }
    
    // ✅ Vérification 1: Le pointeur est-il dans une plage heap valide ?
    size_t blockSize = heap_caps_get_allocated_size(ptr);
    if (blockSize == 0) {
        Serial.printf("VALIDATION ERROR: Pointer %p not in heap or already freed\n", ptr);
        return false;
    }
    
    // ✅ Vérification 2: Test d'alignement mémoire
    uintptr_t addr = (uintptr_t)ptr;
    if ((addr % sizeof(void*)) != 0) {
        Serial.printf("VALIDATION ERROR: Pointer %p not properly aligned\n", ptr);
        return false;
    }
    
    // ✅ Vérification 3: Le pointeur est-il dans une plage d'adresse valide ?
    if (!isAddressInValidRange(ptr)) {
        Serial.printf("VALIDATION ERROR: Pointer %p outside valid memory ranges\n", ptr);
        return false;
    }
    
    return true;
}

bool PLC_Tools::markMemoryAsFreed(void* ptr, size_t size) {
    if (!ptr || size == 0) {
        return false;
    }
    
    // ✅ Marquer avec un pattern de debug pour détecter les use-after-free
    const uint8_t freedPattern = 0xDE; // "Dead" pattern
    
    volatile uint8_t* bytePtr = (volatile uint8_t*)ptr;
    
    // Marquer seulement les premiers et derniers bytes pour éviter la corruption
    // si jamais ce n'est pas un vrai bloc alloué
    if (size >= 8) {
        // Marquer les 4 premiers et 4 derniers bytes
        for (int i = 0; i < 4; i++) {
            bytePtr[i] = freedPattern;
            bytePtr[size - 1 - i] = freedPattern;
        }
    } else {
        // Petit bloc - marquer tout
        for (size_t i = 0; i < size; i++) {
            bytePtr[i] = freedPattern;
        }
    }
    
    return true;
}

bool PLC_Tools::validateMemoryAfterFree(void* ptr) {
    if (!ptr) {
        return true; // Pointeur null OK après free
    }
    
    // ✅ Vérification que le pointeur n'est plus dans le heap
    // Note: heap_caps_get_allocated_size devrait retourner 0 pour un pointeur libéré
    size_t remainingSize = heap_caps_get_allocated_size(ptr);
    
    if (remainingSize > 0) {
        Serial.printf("POST-FREE ERROR: Pointer %p still shows allocated size %zu\n", 
                     ptr, remainingSize);
        return false;
    }
    
    return true;
}

bool PLC_Tools::isAddressInValidRange(void* ptr) {
    if (!ptr) {
        return false;

    }
    
    uintptr_t addr = (uintptr_t)ptr;
    
    // ✅ Vérifier les plages mémoire ESP32 connues
    
    // DRAM (Data RAM) - plage principale pour heap
    if (addr >= 0x3FFB0000 && addr < 0x40000000) {
        return true;
    }
    
    // IRAM (Instruction RAM) - normalement pas pour heap mais vérification
    if (addr >= 0x40080000 && addr < 0x400A0000) {
        return true;
    }
    
    // PSRAM si disponible
    if (isPSRAM_available()) {
        #ifdef CONFIG_IDF_TARGET_ESP32
            // ESP32 original PSRAM range
            if (addr >= 0x3F800000 && addr < 0x3FC00000) {
                return true;
            }
        #elif defined(CONFIG_IDF_TARGET_ESP32S2)
            // ESP32-S2 PSRAM range
            if (addr >= 0x3F500000 && addr < 0x3FF00000) {
                return true;
            }
        #elif defined(CONFIG_IDF_TARGET_ESP32S3)
            // ESP32-S3 PSRAM range
            if (addr >= 0x3C000000 && addr < 0x3E000000) {
                return true;
            }
        #endif
    }
    
    Serial.printf("Address %p outside known valid ranges\n", ptr);
    return false;
}

size_t PLC_Tools::getHeapFreeSize() {
    return ESP.getFreeHeap();
}

bool PLC_Tools::isAddressInPSRAM(void* ptr) {
    if (!ptr || !isPSRAM_available()) {
        return false;
    }
    
    // ✅ VÉRIFICATION SIMPLE basée sur la plage d'adresses
    uintptr_t addr = (uintptr_t)ptr;
    
    #ifdef CONFIG_IDF_TARGET_ESP32
        // ESP32 original - plage PSRAM typique
        return (addr >= 0x3F800000 && addr < 0x3FC00000);
    #elif defined(CONFIG_IDF_TARGET_ESP32S3)
        // ESP32-S3
        return (addr >= 0x3C000000 && addr < 0x3E000000);
    #else
        // Détection générique conservative
        return (addr >= 0x3F800000 && addr < 0x3FC00000);
    #endif
}

size_t PLC_Tools::getPSRAMFreeSize() {
    return isPSRAM_available() ? ESP.getFreePsram() : 0;
}

void PLC_Tools::logMemoryAllocation(void* ptr, size_t size, const char* type) {
    #ifdef DEBUG_MEMORY_VERBOSE
    if (ptr && type) {
        // Log simple sans appels complexes
        Serial.printf("[%s] +%zu at %p\n", type, size, ptr);
    }
    #endif
}

void PLC_Tools::logMemoryDeallocation(void* ptr, const char* type) {
    #ifdef DEBUG_MEMORY_VERBOSE
    if (ptr && type) {
        // Log simple sans appels complexes
        Serial.printf("[%s] - at %p\n", type, ptr);
    }
    #endif
}

void PLC_Tools::printMemoryStats(const char* context) {
    static uint32_t lastCall = 0;
    uint32_t now = millis();
    
    // Protection contre les appels trop fréquents
    if (now - lastCall < 5000) {
        return;
    }
    lastCall = now;
    
    // Vérification mémoire critique
    size_t currentHeap = ESP.getFreeHeap();
    if (currentHeap < 30000) {
        return; // Pas de stats si mémoire critique
    }
    
    if (context) {
        Serial.printf("\n=== Memory Stats [%s] ===\n", context);
    } else {
        Serial.println("\n=== Memory Stats ===");
    }
    
    // ✅ APPELS ESP32 DE BASE SEULEMENT
    size_t freeHeap = ESP.getFreeHeap();
    size_t minFreeHeap = ESP.getMinFreeHeap();
    
    Serial.printf("Heap - Free: %zu, Min: %zu bytes\n", freeHeap, minFreeHeap);
    
    // PSRAM stats seulement si disponible
    if (isPSRAM_available()) {
        size_t freePSRAM = ESP.getFreePsram();
        size_t totalPSRAM = ESP.getPsramSize();
        
        if (totalPSRAM > 0) {
            Serial.printf("PSRAM - Free: %zu, Total: %zu bytes\n", freePSRAM, totalPSRAM);
        }
    }
    
    Serial.println("=====================\n");
}

float PLC_Tools::getHeapFragmentation() {
    size_t freeHeap = ESP.getFreeHeap();
    size_t maxAlloc = ESP.getMaxAllocHeap();
    
    if (freeHeap == 0) return 100.0f;
    return 100.0f * (1.0f - (float)maxAlloc / (float)freeHeap);
}

void PLC_Tools::forceGarbageCollection() {
    // Version simple sans heap_caps_malloc_trim qui peut être problématique
    Serial.println("=== Simple GC ===");
    
    size_t beforeHeap = ESP.getFreeHeap();
    
    // Petites allocations/désallocations pour forcer le compactage
    void* testAllocs[5];
    const size_t testSize = 512;
    
    for (int i = 0; i < 5; i++) {
        testAllocs[i] = malloc(testSize);
        if (!testAllocs[i]) break;
    }
    
    for (int i = 0; i < 5; i++) {
        if (testAllocs[i]) {
            free(testAllocs[i]);
        }
    }
    
    size_t afterHeap = ESP.getFreeHeap();
    Serial.printf("GC: %zu -> %zu bytes\n", beforeHeap, afterHeap);
    Serial.println("=================");
}

bool PLC_Tools::isValidNumericString(const String& str) {
    if (str.isEmpty()) return false;
    
    bool hasDigit = false;
    bool hasDot = false;
    
    for (int i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (isdigit(c)) {
            hasDigit = true;
        } else if (c == '.') {
            if (hasDot) return false; // Pas plus d'un point
            hasDot = true;
        } else {
            return false; // Caractère invalide
        }
    }
    
    return hasDigit; // Au moins un chiffre requis
}

std::vector<int> PLC_Tools::findAllDotPositions(const String& str) {
    std::vector<int> positions;
    for (int i = 0; i < str.length(); i++) {
        if (str.charAt(i) == '.') {
            positions.push_back(i);
        }
    }
    return positions;
}

int PLC_Tools::findBmPosition(const String& str) {
    // Ordre de priorité pour trouver les marqueurs
    const char* markers[] = {"BmBrHt", "BmHt", "BmBr", "Bm", "bm"};
    
    for (const char* marker : markers) {
        int pos = str.indexOf(marker);
        if (pos > 0) return pos;
    }
    return -1;
}

String PLC_Tools::extractNumericPrefix(const String& str) {
    String result = "";
    bool hasFoundDigit = false;
    bool hasDot = false;
    
    for (int i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (isdigit(c)) {
            result += c;
            hasFoundDigit = true;
        } else if (c == '.' && !hasDot && hasFoundDigit) {
            result += c;
            hasDot = true;
        } else if (hasFoundDigit) {
            break; // Arrêter à la première non-numérique après avoir trouvé des chiffres
        }
    }
    
    return result;
}

String PLC_Tools::extractAllDigits(const String& str) {
    String result = "";
    
    for (int i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (isdigit(c)) {
            result += c;
        }
    }
    
    return result;
}

// ========================================
// IMPLÉMENTATION PSRAMContainerManager
// ========================================

void PSRAMContainerManager::printContainerStats(const char* containerName, 
                                               size_t elementCount, 
                                               size_t elementSize) {
    size_t totalSize = elementCount * elementSize;
    Serial.printf("=== %s Container Stats ===\n", containerName);
    Serial.printf("Elements: %zu\n", elementCount);
    Serial.printf("Element size: %zu bytes\n", elementSize);
    Serial.printf("Total size: %zu bytes\n", totalSize);
    Serial.printf("Memory type: %s\n", 
                 totalSize >= 512 && PLC_Tools::isPSRAM_available() ? "PSRAM" : "RAM");
    Serial.println("============================");
}

bool PSRAMContainerManager::validateContainerMemory(void* containerPtr, 
                                                   size_t expectedSize) {
    if (!containerPtr) {
        Serial.println("ERROR: Container pointer is null");
        return false;
    }
    
    // Vérifier si la mémoire est accessible
    size_t actualSize = heap_caps_get_allocated_size(containerPtr);
    
    if (actualSize == 0) {
        Serial.println("ERROR: Cannot determine container memory size");
        return false;
    }
    
    if (actualSize < expectedSize) {
        Serial.printf("WARNING: Container memory smaller than expected (%zu < %zu)\n", 
                     actualSize, expectedSize);
        return false;
    }
    
    bool isPSRAMBlock = PLC_Tools::isAddressInPSRAM(containerPtr);
    Serial.printf("Container memory validation OK: %zu bytes in %s\n", 
                 actualSize, isPSRAMBlock ? "PSRAM" : "RAM");
    
    return true;
}

void PSRAMContainerManager::optimizeContainerMemory(void* containerPtr, 
                                                   const char* containerType) {
    if (!containerPtr) return;
    
    Serial.printf("Optimizing %s container memory...\n", containerType);
    
    bool isPSRAMBlock = PLC_Tools::isAddressInPSRAM(containerPtr);
    size_t blockSize = heap_caps_get_allocated_size(containerPtr);
    
    Serial.printf("Current allocation: %zu bytes in %s\n", 
                 blockSize, isPSRAMBlock ? "PSRAM" : "RAM");
    
    // Pour l'optimisation, on pourrait implémenter des stratégies spécifiques
    // comme la défragmentation ou la réallocation en PSRAM
    
    Serial.println("Memory optimization completed");
}

 bool PLC_Tools::isAllDigits(const String& str) {
    for (int i = 0; i < str.length(); i++) {
        if (!isDigit(str.charAt(i))) return false;
    }
    return true;
}

void PLC_Tools::displayKeyJsonFields(const JsonObject& jsonObj) {
    Serial.println("=== Champs clés ===");
    
    // Champs essentiels seulement
    const char* essentialFields[] = {"ItemId", "Produit", "Quantité", "Opérations"};
    
    for (size_t i = 0; i < 4; i++) {
        const char* key = essentialFields[i];
        if (!jsonObj[key].isNull()) {
            Serial.printf("%s: ", key);
            
            if (jsonObj[key].is<const char*>()) {
                const char* value = jsonObj[key].as<const char*>();
                Serial.printf("\"%s\"\n", value ? value : "null");
            } else {
                Serial.printf("%s\n", jsonObj[key].as<String>().c_str());
            }
        }
    }
    Serial.println("==================");
}

void PLC_Tools::logDiagnostic(const char* message, bool showTimestamp) {
#ifdef LOGGING_DISABLED
    return;
#endif

    // ========================================================================
    // 🛡️ PROTECTION AVEC MUTEXGUARD (RAII)
    // ========================================================================
    if (diagnosticMutex == nullptr) {
        // Si le mutex n'est pas initialisé, on log quand même mais en Serial uniquement
        Serial.printf("⚠️ logDiagnostic sans mutex: %s\n", message);
        return;
    }
    
    MutexGuard guard(diagnosticMutex, "diagnosticMutex", "logDiagnostic");
    
    if (!guard.isAcquired()) {
        // Un autre thread est en train de logger, abandon silencieux pour éviter blocage
        return;
    }
    
    // ✅ À partir d'ici, le mutex est acquis
    // ✅ Il sera AUTOMATIQUEMENT libéré à la fin de la fonction (ou à tout return)

    // ========================================================================
    // VALIDATION DU MESSAGE
    // ========================================================================
    if (!message || strlen(message) == 0) {
        return;  // ✅ Mutex libéré automatiquement
    }

    // ========================================================================
    // VÉRIFICATION CONFIGURATION
    // ========================================================================
    BorneUniverselle* instance = BorneUniverselle::getInstance();
    if (!instance || !instance->logOnDiagnosticFile) {
        return;  // ✅ Mutex libéré automatiquement - Logging désactivé
    }

    uint32_t now = millis();
    
    // ========================================================================
    // LIMITATION : throttle par hash — array fixe, zéro allocation dynamique
    // ========================================================================
    static const char* throttlePrefixes[] = {
        "Désactivation temporaire du nœud",
        "Réactivation du nœud",
        "WARNING: MyModbus::handleError",
        "WARNING: Refresh:: error on refresh node",
        "ERROR: Désactivation temporaire du nœud",
        nullptr
    };

    // Calculer la clé : préfixe connu → hash du préfixe, sinon hash du message entier
    const char* keyStr = message;
    for (int i = 0; throttlePrefixes[i] != nullptr; i++) {
        if (strncmp(message, throttlePrefixes[i], strlen(throttlePrefixes[i])) == 0) {
            keyStr = throttlePrefixes[i];
            break;
        }
    }
    uint32_t msgHash = (~crc32_le((uint32_t)~(0xffffffff), (const uint8_t*)keyStr, strlen(keyStr))) ^ 0xFFFFFFFF;

    // Table fixe — 16 slots, remplacement LRU (oldest)
    struct ThrottleSlot { uint32_t hash; uint32_t lastTime; };
    static ThrottleSlot throttleTable[16] = {};
    static uint8_t  throttleCount = 0;

    // Chercher le slot existant
    int  foundIdx = -1;
    int  oldestIdx = 0;
    uint32_t oldestTime = UINT32_MAX;
    for (int i = 0; i < throttleCount; i++) {
        if (throttleTable[i].hash == msgHash) { foundIdx = i; break; }
        if (throttleTable[i].lastTime < oldestTime) { oldestTime = throttleTable[i].lastTime; oldestIdx = i; }
    }

    if (foundIdx >= 0) {
        if (now - throttleTable[foundIdx].lastTime < 5000) return;
        throttleTable[foundIdx].lastTime = now;
    } else {
        // Nouveau hash : utiliser le prochain slot libre ou écraser le plus ancien
        int slot = (throttleCount < 16) ? throttleCount++ : oldestIdx;
        throttleTable[slot] = { msgHash, now };
    }

    bool systemReady = (instance->getIsetupComplete());
    if (!systemReady) {
        // Système pas encore prêt → log Serial uniquement
        Serial.printf("logDiagnostic (EARLY):: %lu: %s\n", now, message);
        return;  // ✅ Mutex libéré automatiquement - Sortie anticipée, pas d'accès fichier
    }
    
    // ========================================================================
    // PRÉPARER LE MESSAGE — char[] sur le stack, zéro allocation dynamique
    // ========================================================================
    char logMessage[512];
    size_t logLen = 0;

    if (showTimestamp) {
        char timestamp[24];
        RTC_Service* rtc = RTC_Service::getInstance();
        rtc->getTimestamp(timestamp, sizeof(timestamp));
        logLen = snprintf(logMessage, sizeof(logMessage) - 1, "%s: %s", timestamp, message);
    } else {
        logLen = snprintf(logMessage, sizeof(logMessage) - 1, "%s", message);
    }

    if (logLen > 0 && logLen < sizeof(logMessage) - 1 && logMessage[logLen - 1] != '\n') {
        logMessage[logLen++] = '\n';
        logMessage[logLen] = '\0';
    }

    // ========================================================================
    // AFFICHAGE SERIAL
    // ========================================================================
    Serial.print(logMessage);
    
    // ========================================================================
    // CAS 1: PAS DE BUFFER (pas de PSRAM) → FLUSH IMMÉDIAT
    // ========================================================================
    if (diagnosticBuffer == nullptr) {
        // Écrire directement dans le fichier sans buffer
        cleanupDiagnosticFileIfNeeded();
        if (!appendToDiagnosticFile(logMessage)) {
            Serial.println("ATTENTION: Échec écriture dans fichier diagnostic");
        }
        return;
    }

    // ========================================================================
    // CAS 2: AVEC BUFFER → AJOUTER AU BUFFER ET FLUSH SI NÉCESSAIRE
    // ========================================================================
    size_t messageLen = strlen(logMessage);
    if (diagnosticBufferUsed + messageLen < maxDiagnosticBufferSize) {
        memcpy(diagnosticBuffer + diagnosticBufferUsed, logMessage, messageLen);
        diagnosticBufferUsed += messageLen;
    } else {
        flushDiagnosticBuffer();
        if (messageLen < maxDiagnosticBufferSize) {
            memcpy(diagnosticBuffer + diagnosticBufferUsed, logMessage, messageLen);
            diagnosticBufferUsed += messageLen;
        } else {
            if (!appendToDiagnosticFile(logMessage)) {
                Serial.println("ATTENTION: Échec écriture message volumineux");
            }
        }
    }
    
    // ========================================================================
    // FLUSH PÉRIODIQUE (toutes les 30 secondes) — SEULEMENT si aucun axe ne bouge.
    // Chemin B : pendant un mouvement, on garde le diagnostic en RAM (buffer 16 Ko)
    // sans suspendre Modbus. On NE met PAS lastFlushTime à jour quand on diffère,
    // pour reflusher dès le retour en état idle (ou au prochain log une fois idle).
    // ========================================================================
    static uint32_t lastFlushTime = 0;
    if (now - lastFlushTime > 30000) {
        BusinessLogic* bl = instance->getBusinessLogic();
        if (!bl || bl->isIdle()) {
            flushDiagnosticBuffer();
            lastFlushTime = now;
        }
    }
    
    // ✅ Mutex libéré automatiquement à la fin de la fonction
}

bool PLC_Tools::isValidAsciiString(const char* str, size_t maxLength) {
    if (str == nullptr) {
        return false;
    }

    size_t length = 0;
    for (const char* p = str; *p != '\0'; p++) {
        length++;
        
        if (length > maxLength) {
            return false;
        }
        
        unsigned char c = (unsigned char)*p;
        
        // Accepter ASCII imprimables
        if (c >= 32 && c <= 126) {
            continue;
        }
        
        // Accepter les caractères de contrôle sûrs
        if (c == 9 || c == 10 || c == 13) {
            continue;
        }
        
        // Accepter spécifiquement les séquences UTF-8 pour É (C3 89)
        if (c == 0xC3 && *(p + 1) == 0x89) {
            p++; // Skip le caractère suivant
            length++;
            if (length > maxLength) {
                return false;
            }
            continue;
        }
        
        // Accepter quelques autres caractères accentués courants
        if (c == 0xC3 && (
            *(p + 1) == 0xA0 || // à
            *(p + 1) == 0xA8 || // è  
            *(p + 1) == 0xA9 || // é
            *(p + 1) == 0xB9 || // ù
            *(p + 1) == 0x87    // ç
        )) {
            p++; // Skip le caractère suivant
            length++;
            if (length > maxLength) {
                return false;
            }
            continue;
        }
        
        // Rejeter tout le reste
        return false;
    }
    
    return true;
}

bool PLC_Tools::printDiagnosticFile() { 
    // Obtenir l'instance de PLC_Persistence
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    
    // Vérifier si le fichier existe
    if (!persistence.fileExists(DIAGNOSTIC_FILE)) {
      Serial.println("Fichier de diagnostic non trouvé");
      return true;
    }
    
    // Lire le contenu du fichier
    String fileContent;
    bool success = persistence.readFile(DIAGNOSTIC_FILE, fileContent);
    
    if (!success) {
      Serial.println("Erreur lors de la lecture du fichier de diagnostic");
      Serial.printf("Détail: %s\n", persistence.getLastError());
      return false;
    }
    
    Serial.println();
    Serial.println("============ JOURNAL DE DIAGNOSTIC ============");
    // Afficher des statistiques sur le fichier
    //Serial.printf("Taille du fichier: %u octets\n", fileContent.length());
    
    // Compter le nombre d'entrées (lignes)
    int numEntries = 0;
    int index = 0;
    while ((index = fileContent.indexOf('\n', index) + 1) > 0) {
      numEntries++;
    }
    Serial.printf("Nombre d'entrées: %d\n\n", numEntries);
    
    // Imprimer le contenu du fichier
    Serial.println(fileContent);
    
    Serial.println("============= FIN DU JOURNAL DE DIAGNOSTIC =============");
    return true;
}

bool PLC_Tools::appendToDiagnosticFile(const char* message) {
#ifdef LOGGING_DISABLED_FILE_ONLY
    return true;
#endif

    if (!message) {
        Serial.println("ATTENTION: Message null dans appendToDiagnosticFile");
        return false;
    }
    
    size_t messageLen = strlen(message);
    if (messageLen == 0) {
        Serial.println("ATTENTION: Message vide dans appendToDiagnosticFile");
        return false;
    }
    
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    const char* diagnosticPath = DIAGNOSTIC_FILE;
    
    // ✅ PAS DE NETTOYAGE ICI - déjà fait par cleanupDiagnosticFileIfNeeded()
    //    qui est appelé JUSTE AVANT (ligne 1173 de logDiagnostic)
    
    // ✅ JUSTE APPEND ATOMIQUE
    //    - Mutex protège l'opération
    //    - PLC suspend pendant l'opération  
    //    - FILE_APPEND crée le fichier s'il n'existe pas
    return persistence.appendToFile(diagnosticPath, message, messageLen);
}

void PLC_Tools::cleanupThrottleMaps() {
    static uint32_t lastCleanup = 0;
    uint32_t now = millis();

    uint32_t psramFree = isPSRAM_available() ? ESP.getFreePsram() : UINT32_MAX;
    bool psramLow = (psramFree < 500000);  // < 500 KB → nettoyage d'urgence

    // Nettoyage périodique toutes les 30s OU urgence PSRAM basse
    if (!psramLow && now - lastCleanup < 30000) {
        return;
    }
    lastCleanup = now;

    if (psramLow) {
        Serial.printf("⚠️ PSRAM bas (%lu bytes) — purge des maps de messages\n", psramFree);
    }

    if (logThrottleMap.size() > 50 || psramLow) {
        logThrottleMap.clear();
    }

    if (fileThrottleMap.size() > 20 || psramLow) {
        fileThrottleMap.clear();
    }

    // Protection PSRAM : vider periodicMessagesMap si PSRAM basse ou map trop grande
    size_t periodicSize = periodicMessagesMap.size();
    if (periodicSize > 15 || psramLow) {
        Serial.printf("🧹 periodicMessagesMap vidée (%zu entrées, PSRAM=%lu)\n",
                      periodicSize, psramFree);
        clearPeriodicMessages();
    }
}

//====================================================================
// FONCTION DE NETTOYAGE AMÉLIORÉE - Modifier dans PLC_Tools.cpp
//====================================================================

void PLC_Tools::cleanupDiagnosticFileIfNeeded() {
    //Serial.println("🧹 Vérification nettoyage diagnostic.txt... DÉSACTIVÉ");
    //return; // Désactivé temporairement pour tests
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    const char* diagnosticPath = DIAGNOSTIC_FILE;
    
    size_t fileSize = persistence.getFileSize(diagnosticPath);
    
    // Nettoyer seulement si vraiment nécessaire
    if (fileSize < DIAGNOSTIC_MAX_SIZE_BYTES) {
        return; // Pas besoin de nettoyer
    }
    
    Serial.printf("Nettoyage diagnostic.txt nécessaire (%zu octets)\n", fileSize);
    
    // Lire le contenu
    String content;
    if (!persistence.readFile(diagnosticPath, content)) {
        Serial.println("ERREUR: Impossible de lire diagnostic.txt pour nettoyage");
        return;
    }
    
    // Garder seulement la fin du fichier
    if (content.length() > DIAGNOSTIC_KEEP_SIZE_BYTES) {
        // Chercher un point de coupure propre (début de ligne)
        size_t cutPosition = content.length() - DIAGNOSTIC_KEEP_SIZE_BYTES;
        int newlinePos = content.indexOf('\n', cutPosition);
        
        if (newlinePos > 0) {
            cutPosition = newlinePos + 1;
        }
        
        String truncatedContent = content.substring(cutPosition);
        
        // Ajouter un en-tête pour indiquer la troncature
        String cleanedContent = String("\n=== FICHIER TRONQUÉ LE ") + 
                               String(millis()) + 
                               String(" ms ===\n") + 
                               truncatedContent;
        
        // Sauvegarder atomiquement
        if (persistence.saveToFile(diagnosticPath, cleanedContent)) {
            Serial.printf("Diagnostic.txt nettoyé: %zu -> %zu octets\n", 
                         content.length(), cleanedContent.length());
        } else {
            Serial.println("ERREUR: Échec nettoyage diagnostic.txt");
        }
    }
}

void PLC_Tools::logThrottledToFile(const char* category, const char* message, uint32_t intervalMs) {
    String key = String(category);
    uint32_t now = millis();
    
    auto it = fileThrottleMap.find(key);
    if (it == fileThrottleMap.end()) {
        fileThrottleMap[key] = FileThrottleEntry();
        fileThrottleMap[key].lastLogTime = now;
        fileThrottleMap[key].lastMessage = String(message);
        
        // ✅ TOUJOURS afficher sur Serial
        Serial.printf("[%s] %s\n", category, message);
        
        // ✅ VÉRIFIER via BorneUniverselle avant d'écrire au fichier
        BorneUniverselle* instance = BorneUniverselle::getInstance();
        if (instance && instance->getLogOnDiagnosticFile()) {
            logDiagnostic(message);
        }
        return;
    }
    
    FileThrottleEntry& entry = it->second;
    
    if (now - entry.lastLogTime >= intervalMs) {
        if (entry.suppressedCount > 0) {
            char summaryMsg[400];
            snprintf(summaryMsg, sizeof(summaryMsg),
                "\n=== RESUME THROTTLING [%s] ===\n"
                "Messages supprimés: %lu\n"
                "=== FIN RESUME ===\n",
                category, entry.suppressedCount
            );
            Serial.print(summaryMsg);
            
            // ✅ VÉRIFIER via BorneUniverselle
            BorneUniverselle* instance = BorneUniverselle::getInstance();
            if (instance && instance->getLogOnDiagnosticFile()) {
                logDiagnostic(summaryMsg);
            }
        }
        
        Serial.printf("[%s] %s\n", category, message);
        
        // ✅ VÉRIFIER via BorneUniverselle
        BorneUniverselle* instance = BorneUniverselle::getInstance();
        if (instance && instance->getLogOnDiagnosticFile()) {
            logDiagnostic(message);
        }
        
        entry.lastLogTime = now;
        entry.suppressedCount = 0;
        entry.lastMessage = String(message);
    } else {
        entry.suppressedCount++;
        entry.lastMessage = String(message);
    }
}

void PLC_Tools::logDiagnosticMultipart(const char* part1, const char* part2, const char* part3, bool showTimeStamp ) {
    if (part1){
        logDiagnostic(part1, showTimeStamp);
    }

    if (part2) logDiagnostic(part2, false);
    if (part3) logDiagnostic(part3, false);
}

void PLC_Tools::logJsonPrettyToFile(const JsonObject& jsonObj, const char* context, bool printToSerial) {
    // ✅ Toujours loguer le contexte dans le fichier diagnostic
    if (context) {
        logDiagnostic((String("--- JSON ") + context + " ---").c_str(), false);
    }
    
    // ✅ Utiliser String pour éviter buffer stack
    String jsonBuffer;
    serializeJsonPretty(jsonObj, jsonBuffer);
    
    // ✅ Afficher dans Serial seulement si explicitement demandé
    if (printToSerial) {
        Serial.println(jsonBuffer);
    }
    
    // ✅ Toujours écrire dans le fichier diagnostic
    logDiagnostic(jsonBuffer.c_str(), false);
    
    if (context) {
        logDiagnostic("--- Fin JSON ---", false);
    }
}

// ✅ Fonction externe pour GC depuis MemoryMonitor
void PLC_Tools::performGarbageCollection() {
    PLC_Tools::forceGarbageCollection();
}

  // ========================================
// IMPLÉMENTATIONS JsonDisplayController
// ========================================

void JsonDisplayController::configure(uint32_t maxJsons, bool showAll) {
    maxJsonsToShow = maxJsons;
    showAllJsons = showAll;
    totalJsonsShown = 0;
    shownErrorTypes.clear();
}

bool JsonDisplayController::shouldShowJson(const String& errorType, uint32_t currentErrorCount) {
    // Mode debug : tout afficher
    if (showAllJsons) {
        totalJsonsShown++;
        return true;
    }
    
    // RÈGLE 1 : Montrer les nouveaux types d'erreurs (max 10 types différents)
    if (shownErrorTypes.size() < 10) {
        bool isNewType = true;
        for (const String& type : shownErrorTypes) {
            if (type == errorType) {
                isNewType = false;
                break;
            }
        }
        
        if (isNewType) {
            shownErrorTypes.push_back(errorType);
            totalJsonsShown++;
            return true;
        }
    }
    
    // RÈGLE 2 : Afficher les premiers JSONs jusqu'à la limite
    if (totalJsonsShown < maxJsonsToShow) {
        totalJsonsShown++;
        return true;
    }
    
    // RÈGLE 3 : Afficher chaque 10ème erreur pour éviter le spam
    if (currentErrorCount % 10 == 0) {
        totalJsonsShown++;
        return true;
    }
    
    // RÈGLE 4 : Afficher chaque 100ème erreur pour les très nombreuses erreurs
    if (currentErrorCount % 100 == 0) {
        totalJsonsShown++;
        return true;
    }
    
    return false;
}

void JsonDisplayController::printStats() {
    Serial.printf("Erreurs affichées: %lu, Types trackés: %zu\n", 
                 (unsigned long)totalJsonsShown, shownErrorTypes.size());
    
    // Optionnel: afficher les types d'erreurs rencontrés
    if (!shownErrorTypes.empty() && shownErrorTypes.size() <= 10) {
        Serial.print("Types: ");
        for (size_t i = 0; i < shownErrorTypes.size(); i++) {
            Serial.print(shownErrorTypes[i]);
            if (i < shownErrorTypes.size() - 1) Serial.print(", ");
        }
        Serial.println();
    }
}

void JsonDisplayController::reset() {
    totalJsonsShown = 0;
    shownErrorTypes.clear();
}

void PLC_Tools::setFilteredMessagePatterns(const std::vector<String>& patterns) {
    filteredMessagePatterns = patterns;
    Serial.printf("PLC_Tools: %u motif(s) de filtrage de messages charge(s)\r\n",
        (unsigned)filteredMessagePatterns.size());
}

bool PLC_Tools::isMessageFiltered(const char* message) {
    if (!message || filteredMessagePatterns.empty()) {
        return false;
    }
    for (const String& pattern : filteredMessagePatterns) {
        if (pattern.length() > 0 && strstr(message, pattern.c_str()) != nullptr) {
            return true;
        }
    }
    return false;
}

void PLC_Tools::sendPeriodicMessage(bool condition, uint8_t type, const char* message) {
    if (!message || strlen(message) == 0) {
        return;
    }
    
    uint32_t now = millis();
    uint32_t key = (~crc32_le((uint32_t)~(0xffffffff), (const uint8_t*)message, strlen(message))) ^ 0xffffffFF;
    
    // ========================================
    // GESTION DE LA LIMITE DE MÉMOIRE
    // ========================================
    size_t maxMessages = isPSRAM_available() ? 
        MAX_PERIODIC_MESSAGES : MAX_PERIODIC_MESSAGES_NO_PSRAM;
    
    // Si la map est pleine et que ce message n'existe pas encore
    if (periodicMessagesMap.size() >= maxMessages && 
        periodicMessagesMap.find(key) == periodicMessagesMap.end()) {
        
        // Trouver et supprimer le message le plus ancien (inactive depuis le plus longtemps)
        auto oldestIt = periodicMessagesMap.end();
        uint32_t oldestTime = now;
        
        for (auto it = periodicMessagesMap.begin(); it != periodicMessagesMap.end(); ++it) {
            if (!it->second.active && it->second.lastTime < oldestTime) {
                oldestTime = it->second.lastTime;
                oldestIt = it;
            }
        }
        
        if (oldestIt != periodicMessagesMap.end()) {
            periodicMessagesMap.erase(oldestIt);
        } else {
            // Tous les messages sont actifs : map saturée, message perdu
            static uint32_t lastMapFullWarning = 0;
            if (now - lastMapFullWarning >= 10000) {
                Serial.printf("WARNING: sendPeriodicMessage map pleine (%u/%u), message perdu: %.40s\r\n",
                    (unsigned)periodicMessagesMap.size(), (unsigned)maxMessages, message);
                lastMapFullWarning = now;
            }
            return;
        }
    }
    
    // ========================================
    // RÉCUPÉRER OU CRÉER L'ÉTAT POUR CE MESSAGE
    // ========================================
    PeriodicMessageState& state = periodicMessagesMap[key];
    state.type = type;

    if (condition) {
        // Condition vraie - envoyer si premier envoi ou si 10s écoulées
        if (!state.active || (now - state.lastTime >= PERIODIC_MESSAGE_INTERVAL)) {
            
            if (!state.firstLogged) {
                // Premier message → utiliser le type original
                BorneUniverselle::prepareMessage(type, message);
                state.firstLogged = true;
            } else {
                // Messages suivants → downgrade ERROR vers WARNING
                uint8_t displayType = (type == ERROR) ? WARNING : type;
                BorneUniverselle::prepareMessage(displayType, message);
            }
            
            state.active = true;
            state.lastTime = now;
        }
    } else {
        // Condition fausse - reset pour CE message uniquement
        state.active = false;
        state.firstLogged = false;
    }
}

void PLC_Tools::clearPeriodicMessages(bool preserveActiveErrors) {
    size_t countBefore = periodicMessagesMap.size();

    if (preserveActiveErrors) {
        // Purge selective : on garde les erreurs actives (notification en cours),
        // on efface le reste (throttling resolu ou non pertinent).
        for (auto it = periodicMessagesMap.begin(); it != periodicMessagesMap.end(); ) {
            if (it->second.active && it->second.type == ERROR) {
                ++it;
            } else {
                it = periodicMessagesMap.erase(it);
            }
        }
    } else {
        periodicMessagesMap.clear();
    }

    size_t freed = countBefore - periodicMessagesMap.size();
    if (freed > 0) {
        Serial.printf("clearPeriodicMessages: %u slots liberes (%u erreurs actives preservees)\r\n",
            (unsigned)freed, (unsigned)periodicMessagesMap.size());
    }
}

void PLC_Tools::resetPeriodicMessages() {
    periodicMessageActive = false;
    lastPeriodicMessageTime = 0;
}

bool PLC_Tools::isValidUtf8String(const char* str, size_t maxLength) {
    if (str == nullptr) {
        return false;
    }

    size_t length = 0;
    const unsigned char* p = (const unsigned char*)str;
    
    while (*p != '\0') {
        length++;
        
        if (length > maxLength) {
            return false;
        }
        
        unsigned char c = *p;
        
        // ASCII standard (0x00-0x7F)
        if (c <= 0x7F) {
            // Rejeter les caractères de contrôle sauf tab, newline, carriage return
            if (c < 32 && c != 9 && c != 10 && c != 13) {
                return false;
            }
            p++;
            continue;
        }
        
        // Séquence UTF-8 à 2 octets (0xC0-0xDF)
        if (c >= 0xC0 && c <= 0xDF) {
            if ((p[1] & 0xC0) != 0x80) return false; // Octet suivant invalide
            p += 2;
            length++;
            continue;
        }
        
        // Séquence UTF-8 à 3 octets (0xE0-0xEF)
        if (c >= 0xE0 && c <= 0xEF) {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
            p += 3;
            length += 2;
            continue;
        }
        
        // Séquence UTF-8 à 4 octets (0xF0-0xF7)
        if (c >= 0xF0 && c <= 0xF7) {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return false;
            p += 4;
            length += 3;
            continue;
        }
        
        // Octet invalide
        return false;
    }
    
    return true;
}
// ============================================================================
// LOGGING DES CHANGEMENTS DE PARAMÈTRES
// ============================================================================

void PLC_Tools::logParamChange(const char* paramName, float oldVal, float newVal) {
    if (oldVal != newVal) {
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "  [CHANGE] %s: %.4f -> %.4f", paramName, oldVal, newVal);
        logDiagnostic(logMsg, false);  // showTimestamp = false
    }
}

void PLC_Tools::logParamChange(const char* paramName, uint16_t oldVal, uint16_t newVal) {
    if (oldVal != newVal) {
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "  [CHANGE] %s: %u -> %u", paramName, oldVal, newVal);
        logDiagnostic(logMsg, false);  // showTimestamp = false
    }
}

void PLC_Tools::logParamChange(const char* paramName, uint32_t oldVal, uint32_t newVal) {
    if (oldVal != newVal) {
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "  [CHANGE] %s: %lu -> %lu", paramName, oldVal, newVal);
        logDiagnostic(logMsg, false);  // showTimestamp = false
    }
}

void PLC_Tools::logParamChange(const char* paramName, int oldVal, int newVal) {
    if (oldVal != newVal) {
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "  [CHANGE] %s: %d -> %d", paramName, oldVal, newVal);
        logDiagnostic(logMsg, false);  // showTimestamp = false
    }
}

void PLC_Tools::logParamChange(const char* paramName, bool oldVal, bool newVal) {
    if (oldVal != newVal) {
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "  [CHANGE] %s: %s -> %s", paramName, 
                 oldVal ? "true" : "false", 
                 newVal ? "true" : "false");
        logDiagnostic(logMsg, false);  // showTimestamp = false
    }
}

void PLC_Tools::logParamChange(const char* paramName, const String& oldVal, const String& newVal) {
    if (oldVal != newVal) {
        char logMsg[256];  // Plus grand buffer pour les strings
        snprintf(logMsg, sizeof(logMsg), "  [CHANGE] %s: '%s' -> '%s'", paramName, 
                 oldVal.c_str(), newVal.c_str());
        logDiagnostic(logMsg, false);  // showTimestamp = false
    }
}

void PLC_Tools::logParamChange(const char* paramName, const char* oldVal, const char* newVal) {
    if (oldVal && newVal && strcmp(oldVal, newVal) != 0) {
        char logMsg[256];  // Plus grand buffer pour les strings
        snprintf(logMsg, sizeof(logMsg), "  [CHANGE] %s: '%s' -> '%s'", paramName, oldVal, newVal);
        Serial.printf("%s\r\n", logMsg);
        logDiagnostic(logMsg, false);  // showTimestamp = false
    } else if ((oldVal == nullptr && newVal != nullptr) || (oldVal != nullptr && newVal == nullptr)) {
        char logMsg[256];
        snprintf(logMsg, sizeof(logMsg), "  [CHANGE] %s: '%s' -> '%s'", paramName, 
                 oldVal ? oldVal : "null", 
                 newVal ? newVal : "null");
        logDiagnostic(logMsg, false);  // showTimestamp = false
    }
}

void PLC_Tools::writeDiagnosticBufferToFile() {
    // Si pas de buffer (pas de PSRAM) → return
    if (diagnosticBuffer == nullptr) {
        return;
    }
    
    // Si buffer vide → return
    if (diagnosticBufferUsed == 0) {
        return;
    }

    // Garde heap : fopen() a besoin de ~200 bytes pour son mutex interne
    if (ESP.getFreeHeap() < 8000) {
        Serial.println("⚠️ writeDiagnosticBufferToFile: heap trop bas, flush ignoré");
        return;
    }

    cleanupDiagnosticFileIfNeeded();

    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    if (!persistence.appendToFile(DIAGNOSTIC_FILE, diagnosticBuffer, diagnosticBufferUsed)) {
        Serial.println("ATTENTION: Échec écriture dans fichier diagnostic");
    }

    diagnosticBufferUsed = 0;
    diagnosticBuffer[0] = '\0';
}

void PLC_Tools::flushDiagnosticBuffer() {
    // Solution propre: appeler directement la fonction d'écriture
    // Pas besoin de message vide ou de paramètre forceFlush
    writeDiagnosticBufferToFile();
}

// B-2 : point d'entrée protégé par le mutex récursif, appelé depuis logicExecutor.
// Vide le buffer diagnostic dès qu'on est idle. No-op si un axe bouge (sécurité)
// ou si le buffer est déjà vide (garde interne de writeDiagnosticBufferToFile).
void PLC_Tools::flushDiagnosticBufferIfIdle() {
    if (diagnosticMutex == nullptr) return;

    BorneUniverselle* instance = BorneUniverselle::getInstance();
    BusinessLogic* bl = instance ? instance->getBusinessLogic() : nullptr;
    if (bl && !bl->isIdle()) return;  // jamais de flush pendant un mouvement

    MutexGuard guard(diagnosticMutex, "diagnosticMutex", "flushDiagnosticBufferIfIdle");
    if (!guard.isAcquired()) return;

    writeDiagnosticBufferToFile();
}

void PLC_Tools::initializeDiagnosticBuffer() {
    if (diagnosticBufferInitialized) {
        return;  // Déjà initialisé
    }
    
    // ========================================================================
    // ✅ ÉTAPE 1 : INITIALISER LE MUTEX EN PREMIER
    // ========================================================================
    if (diagnosticMutex == nullptr) {
        diagnosticMutex = xSemaphoreCreateRecursiveMutex();
        if (diagnosticMutex == nullptr) {
            Serial.println("❌ ERREUR CRITIQUE: Impossible de créer diagnosticMutex");
            // ⚠️ Continuer quand même pour le buffer, mais logging sera moins sûr
        } else {
            Serial.println("✅ diagnosticMutex créé avec succès");
        }
    }
    
    // ========================================================================
    // ÉTAPE 2 : INITIALISER LE BUFFER (code existant)
    // ========================================================================
    bool isPSRAM = PLC_Tools::isPSRAM_available();
    
    // Déterminer la taille selon PSRAM: 16KB si PSRAM, 4KB sinon
    maxDiagnosticBufferSize = isPSRAM ? (16 * 1024) : (4 * 1024);
    
    // Allouer via allocateMemory (gère automatiquement PSRAM vs RAM)
    diagnosticBuffer = (char*)allocateMemory(maxDiagnosticBufferSize, MEMORY_PREFER_PSRAM);
    
    if (diagnosticBuffer) {
        diagnosticBuffer[0] = '\0';
        Serial.printf("📋 Buffer diagnostic: %zu KB (%s)\n", 
                     maxDiagnosticBufferSize / 1024,
                     isPSRAM ? "PSRAM" : "RAM");
    } else {
        Serial.printf("⚠️ Échec allocation buffer diagnostic (%zu KB), désactivé\n",
                     maxDiagnosticBufferSize / 1024);
        maxDiagnosticBufferSize = 0;
    }
    
    diagnosticBufferInitialized = true;
    
    // ========================================================================
    // ÉTAPE 3 : RÉSUMÉ DE L'INITIALISATION
    // ========================================================================
    Serial.println("========================================");
    Serial.println("📊 Système de diagnostic initialisé:");
    Serial.printf("  - Mutex: %s\n", diagnosticMutex ? "✅ OK" : "❌ ÉCHEC");
    Serial.printf("  - Buffer: %s (%zu KB)\n", 
                 diagnosticBuffer ? "✅ OK" : "❌ ÉCHEC",
                 maxDiagnosticBufferSize / 1024);
    Serial.println("========================================");
}

void PLC_Tools::initialize() {
    Serial.println("📋 PLC_Tools::initialize() - Initialisation des sous-systèmes");
    
    // Initialiser le buffer de diagnostic
    initializeDiagnosticBuffer();
    
    Serial.println("✅ PLC_Tools::initialize() - Terminé");
}

void PLC_Tools::setAutoFlush(bool enable) {
    autoFlushEnabled = enable;
}

/**
 * @brief Normalise un chemin de fichier
 * 
 * Méthode générique pour normaliser tous les chemins de fichiers du système.
 * Utilisée pour recetteFileName, resultatRecettesFileName, etc.
 */
bool PLC_Tools::normalizeFilePath(String& path, const char* extension) {
    bool modified = false;
    String original = path;
    
    // Étape 1: Ajouter '/' au début si manquant
    if (path.length() > 0 && path.charAt(0) != '/') {
        path = "/" + path;
        modified = true;
    }
    
    // Étape 2: Ajouter l'extension à la fin si manquante
    if (extension != nullptr && extension[0] != 0) {
        // Normaliser l'extension : s'assurer qu'elle commence par '.'
        String normalizedExt;
        if (extension[0] == '.') {
            normalizedExt = String(extension);  // Déjà avec le point
        } else {
            normalizedExt = String(".") + String(extension);  // Ajouter le point
        }
        
        // Vérifier si le path se termine déjà par cette extension
        if (!path.endsWith(normalizedExt)) {
            path += normalizedExt;
            modified = true;
        }
    }
    
    // Log si modification
    if (modified) {
        Serial.printf("⚠️ Chemin fichier normalisé: '%s' -> '%s'\n", original.c_str(), path.c_str());
    }
    
    return modified;
}

void PLC_Tools::checkHeapSafely(const char* location) {
    Serial.println("========================================");
    Serial.printf("DIAGNOSTIC HEAP à : %s\n", location);
    Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Min free heap: %lu bytes\n", ESP.getMinFreeHeap());
    Serial.printf("Largest block: %lu bytes\n", ESP.getMaxAllocHeap());
    Serial.printf("Free PSRAM: %lu bytes\n", ESP.getFreePsram());
    
    // ⚠️ NE PAS utiliser heap_caps_check_integrity_all(true) - ça crash!
    // À la place, on essaie un petit malloc de test
    void* testPtr = malloc(320);
    if (testPtr) {
        Serial.println("✅ malloc(320) OK");
        free(testPtr);
    } else {
        Serial.println("❌ malloc(32) FAILED");
    }
    
    Serial.println("========================================");
}

// ============================================================================
// EXTRACTION JSON SECURISEE (Safe Getters)
// ============================================================================
//
// Architecture: chaque type a une fonction template interne (safeGetXXImpl)
// qui contient la logique reelle. Les surcharges publiques (JsonDocument, JsonObject)
// deleguent a cette implementation interne.
// Cela evite la duplication tout en gardant des signatures claires pour l'appelant.
//

// --------------------------------------------------------
// HELPER INTERNE: gestion du flag ok
// --------------------------------------------------------
static inline void setOk(bool* ok, bool value) {
    if (ok) *ok = value;
}

// ============================================================================
// safeGetFloat
// ============================================================================

template<typename JsonSource>
static float safeGetFloatImpl(const JsonSource& src, const char* key, float defaultValue, bool* ok) {
    if (src[key].isNull()) {
        Serial.printf("ATTENTION: Cle '%s' manquante, utilisation valeur par defaut %.2f\n", key, defaultValue);
        setOk(ok, false);
        return defaultValue;
    }
    
    if (!src[key].template is<float>() && !src[key].template is<double>() && !src[key].template is<int>()) {
        Serial.printf("ATTENTION: Cle '%s' n'est pas un nombre, utilisation valeur par defaut %.2f\n", key, defaultValue);
        setOk(ok, false);
        return defaultValue;
    }
    
    setOk(ok, true);
    return src[key].template as<float>();
}

float PLC_Tools::safeGetFloat(const JsonDocument& doc, const char* key, float defaultValue, bool* ok) {
    return safeGetFloatImpl(doc, key, defaultValue, ok);
}

float PLC_Tools::safeGetFloat(const JsonObject& obj, const char* key, float defaultValue, bool* ok) {
    return safeGetFloatImpl(obj, key, defaultValue, ok);
}

// ============================================================================
// safeGetInt
// ============================================================================

template<typename JsonSource>
static int safeGetIntImpl(const JsonSource& src, const char* key, int defaultValue, bool* ok) {
    if (src[key].isNull()) {
        Serial.printf("ATTENTION: Cle '%s' manquante, utilisation valeur par defaut %d\n", key, defaultValue);
        setOk(ok, false);
        return defaultValue;
    }
    
    if (!src[key].template is<int>() && !src[key].template is<long>() && !src[key].template is<unsigned int>()) {
        Serial.printf("ATTENTION: Cle '%s' n'est pas un entier, utilisation valeur par defaut %d\n", key, defaultValue);
        setOk(ok, false);
        return defaultValue;
    }
    
    setOk(ok, true);
    return src[key].template as<int>();
}

int PLC_Tools::safeGetInt(const JsonDocument& doc, const char* key, int defaultValue, bool* ok) {
    return safeGetIntImpl(doc, key, defaultValue, ok);
}

int PLC_Tools::safeGetInt(const JsonObject& obj, const char* key, int defaultValue, bool* ok) {
    return safeGetIntImpl(obj, key, defaultValue, ok);
}

// ============================================================================
// safeGetUint32
// ============================================================================

template<typename JsonSource>
static uint32_t safeGetUint32Impl(const JsonSource& src, const char* key, uint32_t defaultValue, bool* ok) {
    if (src[key].isNull()) {
        Serial.printf("ATTENTION: Cle '%s' manquante, utilisation valeur par defaut %lu\n", key, (unsigned long)defaultValue);
        setOk(ok, false);
        return defaultValue;
    }
    
    if (!src[key].template is<int>() && !src[key].template is<long>() && !src[key].template is<unsigned int>()) {
        Serial.printf("ATTENTION: Cle '%s' n'est pas un entier, utilisation valeur par defaut %lu\n", key, (unsigned long)defaultValue);
        setOk(ok, false);
        return defaultValue;
    }
    
    setOk(ok, true);
    return src[key].template as<uint32_t>();
}

uint32_t PLC_Tools::safeGetUint32(const JsonDocument& doc, const char* key, uint32_t defaultValue, bool* ok) {
    return safeGetUint32Impl(doc, key, defaultValue, ok);
}

uint32_t PLC_Tools::safeGetUint32(const JsonObject& obj, const char* key, uint32_t defaultValue, bool* ok) {
    return safeGetUint32Impl(obj, key, defaultValue, ok);
}

// ============================================================================
// safeGetString
// ============================================================================

template<typename JsonSource>
static String safeGetStringImpl(const JsonSource& src, const char* key, const char* defaultValue, size_t maxLength, bool* ok) {
    if (src[key].isNull()) {
        Serial.printf("ATTENTION: Cle '%s' manquante, utilisation valeur par defaut '%s'\n", key, defaultValue);
        setOk(ok, false);
        return String(defaultValue);
    }
    
    if (!src[key].template is<const char*>() && !src[key].template is<String>()) {
        Serial.printf("ATTENTION: Cle '%s' n'est pas une chaine, utilisation valeur par defaut '%s'\n", key, defaultValue);
        setOk(ok, false);
        return String(defaultValue);
    }
    
    const char* value = src[key].template as<const char*>();
    if (!value) {
        Serial.printf("ATTENTION: Cle '%s' invalide, utilisation valeur par defaut '%s'\n", key, defaultValue);
        setOk(ok, false);
        return String(defaultValue);
    }
    
    if (!PLC_Tools::isValidAsciiString(value, maxLength)) {
        Serial.printf("ATTENTION: Valeur pour la cle '%s' contient des caracteres invalides, utilisation valeur par defaut '%s'\n", 
                     key, defaultValue);
        setOk(ok, false);
        return String(defaultValue);
    }
    
    setOk(ok, true);
    return String(value);
}

String PLC_Tools::safeGetString(const JsonDocument& doc, const char* key, const char* defaultValue, size_t maxLength, bool* ok) {
    return safeGetStringImpl(doc, key, defaultValue, maxLength, ok);
}

String PLC_Tools::safeGetString(const JsonObject& obj, const char* key, const char* defaultValue, size_t maxLength, bool* ok) {
    return safeGetStringImpl(obj, key, defaultValue, maxLength, ok);
}

// ============================================================================
// safeGetBool
// ============================================================================

template<typename JsonSource>
static bool safeGetBoolImpl(const JsonSource& src, const char* key, bool defaultValue, bool* ok) {
    if (src[key].isNull()) {
        Serial.printf("ATTENTION: Cle '%s' manquante, utilisation valeur par defaut %s\n", 
                     key, defaultValue ? "true" : "false");
        setOk(ok, false);
        return defaultValue;
    }
    
    // Type natif bool
    if (src[key].template is<bool>()) {
        setOk(ok, true);
        return src[key].template as<bool>();
    }
    
    // Chaines representant des booleens (flexibilite fichiers JSON)
    if (src[key].template is<const char*>()) {
        const char* strValue = src[key].template as<const char*>();
        if (strValue) {
            String lowerStr = String(strValue);
            lowerStr.toLowerCase();
            
            if (lowerStr == "true" || lowerStr == "1" || lowerStr == "yes" || lowerStr == "on") {
                setOk(ok, true);
                return true;
            } else if (lowerStr == "false" || lowerStr == "0" || lowerStr == "no" || lowerStr == "off") {
                setOk(ok, true);
                return false;
            }
        }
    }
    
    // Nombres representant des booleens (0/1)
    if (src[key].template is<int>()) {
        int intValue = src[key].template as<int>();
        setOk(ok, true);
        return (intValue != 0);
    }
    
    // Aucune conversion possible
    Serial.printf("ATTENTION: Cle '%s' n'est pas un booleen valide, utilisation valeur par defaut %s\n", 
                 key, defaultValue ? "true" : "false");
    setOk(ok, false);
    return defaultValue;
}

bool PLC_Tools::safeGetBool(const JsonDocument& doc, const char* key, bool defaultValue, bool* ok) {
    return safeGetBoolImpl(doc, key, defaultValue, ok);
}

bool PLC_Tools::safeGetBool(const JsonObject& obj, const char* key, bool defaultValue, bool* ok) {
    return safeGetBoolImpl(obj, key, defaultValue, ok);
}

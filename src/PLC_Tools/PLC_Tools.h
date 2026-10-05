#pragma once
#define ARDUINOJSON_ENABLE_COMMENTS 1
#include <esp_heap_caps.h>
#include <esp_heap_caps_init.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <rom/crc.h>

#include <vector>
#include <Arduino.h>
#include "ArduinoJson.h"
#include "PLC_Persistence/PLC_Persistence.h"
#include "BorneUniverselle/borneUniverselle.h"
#include "MyModbus/MyModbus.h"
#include "SafeJsonDocument/SafeJsonDocument.h"

// Json key
#define REASON  "reason"
#define REBOOT  "reboot_time"

#define DIAGNOSTIC_FILE          "/diagnostic.txt"  // Chemin du fichier de diagnostic
#define MEMORY_CHECK_INTERVAL   5000
#define MEMORY_THRESHOLD_LOW    100000
#define MEMORY_TRESHOLD_WITH_CLIENT 50000

#define DIAGNOSTIC_MAX_SIZE_BYTES    20480    // 20KB - taille max avant nettoyage
#define DIAGNOSTIC_KEEP_SIZE_BYTES   15360    // 15KB - taille conservée après nettoyage  
#define DIAGNOSTIC_SAFETY_MARGIN     5120     // 5KB - marge de sécurité

// pour le memory allocator...
enum MemoryPreference {
    MEMORY_AUTO = 0,      // Comportement actuel (PSRAM si >= 512 bytes)
    MEMORY_PREFER_PSRAM,  // Forcer PSRAM si disponible, sinon fallback RAM
    MEMORY_FORCE_HEAP     // Forcer la heap interne (jamais PSRAM)
};

#define SEND_PERIODIC_ERROR(condition, message) \
    PLC_Tools::sendPeriodicMessage(condition, ERROR, message)

#define SEND_PERIODIC_WARNING(condition, message) \
    PLC_Tools::sendPeriodicMessage(condition, WARNING, message)

#define SEND_PERIODIC_INFO(condition, message) \
    PLC_Tools::sendPeriodicMessage(condition, INFO, message)

#define SEND_USER_MESSAGE(type, message) \
    PLC_Tools::sendUserMessage(type, message)

#define SEND_SUCCESS(message) \
    PLC_Tools::sendUserMessage(SUCCESS, message)

#define SEND_ERROR(message) \
    PLC_Tools::sendUserMessage(ERROR, message)

#define SEND_WARNING(message) \
    PLC_Tools::sendUserMessage(WARNING, message)

#define SEND_INFO(message) \
    PLC_Tools::sendUserMessage(INFO, message)

using namespace ArduinoJson;

// ✅ Déclarations des fonctions pour MemoryMonitor
void cleanupThrottleMaps();
void performGarbageCollection();

class JsonDisplayController {
private:
    static uint32_t totalJsonsShown;
    static uint32_t maxJsonsToShow;
    static bool showAllJsons;
    static std::vector<String> shownErrorTypes;
    
public:
    /**
     * @brief Configure les paramètres d'affichage JSON
     * @param maxJsons Nombre maximum de JSONs complets à afficher
     * @param showAll Si true, affiche tous les JSONs (mode debug)
     */
    static void configure(uint32_t maxJsons = 5, bool showAll = false);
    
    /**
     * @brief Détermine si un JSON doit être affiché selon les règles de throttling
     * @param errorType Type d'erreur pour le regroupement
     * @param currentErrorCount Compteur actuel d'erreurs de ce type
     * @return true si le JSON doit être affiché
     */
    static bool shouldShowJson(const String& errorType, uint32_t currentErrorCount);
    
    /**
     * @brief Affiche les statistiques d'affichage
     */
    static void printStats();
    
    /**
     * @brief Remet à zéro les compteurs
     */
    static void reset();
};
;

struct DateTimeComponents {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool valid;
};


class PLC_Tools {

private:
    bool wifiDisconnectedForMemory;
    uint32_t wifiDisconnectionTime;
    uint32_t disconnectionCount;

    static void printJsonValue(const JsonVariant &value, int indent = 0);
    static void cleanupDiagnosticFile();
    static void cleanupDiagnosticFileIfNeeded();

public:
    static void initialize();
    bool logReboot();
    String getRebootLog();
    bool clearRebootLog();
    //static void setLastErrorMessage(const char* message);
    static void printBits(uint16_t value);
    static void logDiagnostic(const char* message, bool showTimestamp = true);
    static void flushDiagnosticBuffer();
    // B-2 : flush protégé par mutex, appelable depuis logicExecutor. Ne flushe QUE si idle (no-op pendant un mouvement).
    static void flushDiagnosticBufferIfIdle();

    static bool appendToDiagnosticFile(const char *message);

    // void manageWiFiBasedOnMemory();
    bool printDiagnosticFile();

    static bool isValidAsciiString(const char *str, size_t maxLength);

    // File management
    static std::vector<String> getFilteredFiles(const char* path, const char* pattern);
    static JsonDocument getFilteredFilesAsJson(const char* path, const char* pattern);

    //Json
    static void printJsonObject(const JsonObject &obj) {
        Serial.println("JSON Object Content:");
        Serial.println("-----------------");
        printJsonValue(obj);
        Serial.println("-----------------");
    }

    static bool sendJsonDocument(SafeJsonDocument& doc, const char* context = nullptr);

  // Méthodes de gestion mémoire PSRAM - DÉCLARATIONS SEULEMENT
    static void* allocateMemory(size_t size, MemoryPreference pref = MEMORY_AUTO);
    static void freeMemory(void* ptr);
    static bool isPSRAM_available();
    static size_t getPSRAMFreeSize();
    static size_t getHeapFreeSize();
    static void printMemoryStats(const char* context = nullptr);
    
    // Méthodes utilitaires pour les allocateurs
    static bool isAddressInPSRAM(void* ptr);
    static void logMemoryAllocation(void* ptr, size_t size, const char* type);
    static void logMemoryDeallocation(void* ptr, const char* type);
    
    // Méthodes de diagnostic mémoire
    static float getHeapFragmentation();
    static void forceGarbageCollection();
    static size_t getTotalAllocatedMemory();

    static bool isAllDigits(const String &str);

    //Méthodes pour le diagnostic des recettes
    static void logRecipeErrorSummary(size_t totalErrors, size_t totalRecipes);
    static void logDiagnosticThrottled(const char* category, const char* message, uint32_t intervalMs = 5000);
    static void displayKeyJsonFields(const JsonObject& jsonObj);
    static void logDetailedRecipeError(const JsonObject &recetteJson, const char *errorReason);
    static void logThrottledToFile(const char *category, const char *message, uint32_t intervalMs = 30000);
    static void logRecipeErrorSummaryToFile(size_t totalErrors, size_t totalRecipes);
    static void logDiagnosticMultipart(const char* part1, const char* part2 = nullptr, const char* part3 = nullptr, bool showTimestamp = true);
    static void logJsonPrettyToFile(const JsonObject& jsonObj, const char* context, bool printToSerial = false);
    static void logDiagnosticWithoutTimestamp(const char* message);

    static bool autoFlushEnabled;
    static void setAutoFlush(bool enable);

    static void cleanupThrottleMaps();

    // Methodes pour le parsing des recettes
    /**
     * @brief Valide qu'une chaîne représente un nombre valide (avec ou sans point décimal)
     * @param str Chaîne à valider
     * @return true si la chaîne est un nombre valide
     */
    static bool isValidNumericString(const String& str);

    /**
     * @brief Trouve toutes les positions des points dans une chaîne
     * @param str Chaîne à analyser
     * @return Vecteur contenant les positions de tous les points
     */
    static std::vector<int> findAllDotPositions(const String& str);

    /**
     * @brief Trouve la position du marqueur "Bm" et ses variants
     * @param str Chaîne à analyser
     * @return Position du marqueur ou -1 si non trouvé
     */
    static int findBmPosition(const String& str);

    /**
     * @brief Extrait le préfixe numérique d'une chaîne
     * @param str Chaîne à analyser
     * @return Partie numérique au début de la chaîne (peut inclure un point décimal)
     */
    static String extractNumericPrefix(const String& str);

    /**
     * @brief Extrait tous les chiffres d'une chaîne (ignore les autres caractères)
     * @param str Chaîne à analyser
     * @return Chaîne contenant seulement les chiffres
     */
    static String extractAllDigits(const String& str);

    // ========================================
    // GESTION DES MESSAGES UTILISATEUR
    // ========================================
    
    /**
     * @brief Envoie un message périodique si la condition est vraie
     * 
     * @param condition Si true, le message sera envoyé (avec throttling de 15s)
     * @param type Type de message (SUCCESS, INFO, WARNING, ERROR)
     * @param message Texte du message
     * @param logToDiagnostic Si true, log aussi dans le fichier diagnostic
     * 
     * Exemple d'usage:
     * ```cpp
     * PLC_Tools::sendPeriodicMessage(
     *     !doorCaptor->getValue(),
     *     ERROR,
     *     "Porte ouverte, impossible de passer à l'état IDLE"
     * );
     * ```
     */
    static void sendPeriodicMessage(bool condition, uint8_t type, const char* message);

    /**
     * @brief Vide la table de throttling des messages periodiques.
     * @param preserveActiveErrors Si true (defaut) : les entrees ERROR actuellement actives
     *        ne sont pas effacees, elles gardent leur historique de throttling/downgrade.
     *        Tout le reste (messages resolus, WARNING/INFO/SUCCESS) est purge.
     *        Si false : purge totale (ancien comportement de clearAllPeriodicMessages()).
     */
    static void clearPeriodicMessages(bool preserveActiveErrors = true);

    /**
     * @brief Définit la liste des motifs (sous-chaînes) de messages à ne pas notifier au client
     * @param patterns Liste de sous-chaînes ; un message contenant l'un de ces motifs sera filtré
     */
    static void setFilteredMessagePatterns(const std::vector<String>& patterns);

    /**
     * @brief Vérifie si un message doit être filtré (non notifié au client)
     * @param message Message complet à tester (déjà formaté, avec valeurs dynamiques incluses)
     * @return true si le message contient un des motifs filtrés
     */
    static bool isMessageFiltered(const char* message);

    
    /**
     * @brief Reset du système de messages périodiques (utile pour les changements d'état)
     */
    static void resetPeriodicMessages();

    static bool isValidUtf8String(const char *str, size_t maxLength);

    // ========================================================================
    // LOGGING DES CHANGEMENTS DE PARAMÈTRES
    // ========================================================================
    static void logParamChange(const char* paramName, float oldVal, float newVal);
    static void logParamChange(const char* paramName, uint16_t oldVal, uint16_t newVal);
    static void logParamChange(const char* paramName, uint32_t oldVal, uint32_t newVal);
    static void logParamChange(const char* paramName, int oldVal, int newVal);
    static void logParamChange(const char* paramName, bool oldVal, bool newVal);
    static void logParamChange(const char* paramName, const String& oldVal, const String& newVal);
    static void logParamChange(const char* paramName, const char* oldVal, const char* newVal);

    static bool normalizeFilePath(String& path, const char* extension = ".json");
    static void checkHeapSafely(const char *location);

    // ========================================
    // EXTRACTION JSON SÉCURISÉE (Safe Getters)
    // ========================================
    // 
    // Extraction typée avec validation, logging et valeur par défaut.
    // Deux politiques d'utilisation possibles via le paramètre optionnel 'ok':
    //
    //   - Mode TOLÉRANT (ok = nullptr, par défaut):
    //     Clé manquante → log warning + retourne defaultValue, on continue.
    //     Usage: extractMachineParameters (fichier de configuration machine)
    //
    //   - Mode STRICT (ok = &myBool):
    //     Clé manquante → *ok = false, l'appelant décide (rejeter la recette, etc.)
    //     Usage: parsing de recettes où chaque champ est obligatoire
    //
    // Surcharges JsonDocument et JsonObject pour supporter:
    //   - doc["key"] dans extractMachineParameters (JsonDocument)
    //   - r["key"]   dans processRecipes           (JsonObject)
    //

    // --- Float ---
    static float safeGetFloat(const JsonDocument& doc, const char* key, float defaultValue, bool* ok = nullptr);
    static float safeGetFloat(const JsonObject& obj, const char* key, float defaultValue, bool* ok = nullptr);

    // --- Int ---
    static int safeGetInt(const JsonDocument& doc, const char* key, int defaultValue, bool* ok = nullptr);
    static int safeGetInt(const JsonObject& obj, const char* key, int defaultValue, bool* ok = nullptr);

    // --- Uint32 ---
    static uint32_t safeGetUint32(const JsonDocument& doc, const char* key, uint32_t defaultValue, bool* ok = nullptr);
    static uint32_t safeGetUint32(const JsonObject& obj, const char* key, uint32_t defaultValue, bool* ok = nullptr);

    // --- String ---
    static String safeGetString(const JsonDocument& doc, const char* key, const char* defaultValue, size_t maxLength = 255, bool* ok = nullptr);
    static String safeGetString(const JsonObject& obj, const char* key, const char* defaultValue, size_t maxLength = 255, bool* ok = nullptr);

    // --- Bool ---
    static bool safeGetBool(const JsonDocument& doc, const char* key, bool defaultValue, bool* ok = nullptr);
    static bool safeGetBool(const JsonObject& obj, const char* key, bool defaultValue, bool* ok = nullptr);

private:
    // Mecanique pour sendPeriodicMessage
    struct PeriodicMessageState {
        bool active = false;
        uint32_t lastTime = 0;
        bool firstLogged = false;
        uint8_t type = 0;  // Type du message (SUCCESS/INFO/WARNING/ERROR), utilise par clearPeriodicMessages()
    };
    
    static std::map<uint32_t, PeriodicMessageState> periodicMessagesMap;
    static const size_t MAX_PERIODIC_MESSAGES = 20;  // Limite si PSRAM disponible
    static const size_t MAX_PERIODIC_MESSAGES_NO_PSRAM = 8;  // Limite sans PSRAM

    // Filtre de messages configurable (config.json: "filteredMessages")
    static std::vector<String> filteredMessagePatterns;

    // Variables statiques du buffer de diagnostic
    static char* diagnosticBuffer;                // Buffer alloué dynamiquement (nullptr si pas de PSRAM)
    static size_t diagnosticBufferUsed;           // Taille utilisée dans le buffer
    static size_t maxDiagnosticBufferSize;        // Taille max selon PSRAM (0 si pas de buffer)
    static bool diagnosticBufferInitialized;      // Flag d'initialisation

    String getResetReason(esp_reset_reason_t reason);
    const char* REBOOT_LOG_FILE = "/reboot_log.json";
    const size_t MAX_LOG_ENTRIES = 50;  // Maximum number of entries in the log file 
    
    static void logJsonInChunks(const String& jsonString, uint16_t recipeId);
    static void logRecipeBasicInfo(const JsonObject& recetteJson, uint16_t recipeId);
    static void performGarbageCollection();

    static bool validateAllocatedMemory(void* ptr, size_t expectedSize);
    static bool validatePointerBeforeFree(void* ptr);
    static bool markMemoryAsFreed(void* ptr, size_t size);
    static bool validateMemoryAfterFree(void* ptr);
    static bool isAddressInValidRange(void* ptr);
    
    // Constantes pour le diagnostic
    static constexpr size_t MAX_JSON_CHUNK_SIZE = 150;
    static constexpr size_t MAX_DIAGNOSTIC_ENTRIES_PER_RECIPE = 5;
    static constexpr size_t MAX_SINGLE_LOG_MESSAGE_SIZE = 1000; 

    // Variables pour l'envoi des messages périodiques
    static bool periodicMessageActive;
    static uint32_t lastPeriodicMessageTime;
    static const uint32_t PERIODIC_MESSAGE_INTERVAL = 10000; // 10 secondes

    static void writeDiagnosticBufferToFile();
    static void initializeDiagnosticBuffer();
    static bool firstPeriodicMessageLogged;

    static SemaphoreHandle_t diagnosticMutex;
};

// ========================================
// ALLOCATEUR PSRAM TEMPLATE - RESTE DANS LE HEADER
// ========================================

template<typename T>
class PSRAMAllocator {
public:
    // Types requis par std::allocator_traits
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    // Rebind pour les autres types
    template<typename U>
    struct rebind {
        using other = PSRAMAllocator<U>;
    };
    
    // Constructeurs
    PSRAMAllocator() noexcept = default;
    
    template<typename U>
    PSRAMAllocator(const PSRAMAllocator<U>&) noexcept {}
    
    // Destructeur
    ~PSRAMAllocator() = default;
    
    // Allocation - IMPLÉMENTATION INLINE DANS LE HEADER
    T* allocate(size_t n) {
        if (n == 0) return nullptr;
        
        size_t size = n * sizeof(T);
        void* ptr = PLC_Tools::allocateMemory(size);
        
        if (!ptr) {
            throw std::bad_alloc();
        }
        
        PLC_Tools::logMemoryAllocation(ptr, size, "PSRAMAllocator");
        return static_cast<T*>(ptr);
    }
    
    // Désallocation - IMPLÉMENTATION INLINE DANS LE HEADER
    void deallocate(T* ptr, size_t n) noexcept {
        if (ptr) {
            PLC_Tools::logMemoryDeallocation(ptr, "PSRAMAllocator");
            PLC_Tools::freeMemory(ptr);
        }
    }
    
    // Taille maximale
    size_t max_size() const noexcept {
        return std::numeric_limits<size_t>::max() / sizeof(T);
    }
    
    // Construction et destruction d'objets
    template<typename U, typename... Args>
    void construct(U* ptr, Args&&... args) {
        new(ptr) U(std::forward<Args>(args)...);
    }
    
    template<typename U>
    void destroy(U* ptr) noexcept {
        ptr->~U();
    }
    
    // Opérateurs de comparaison
    template<typename U>
    bool operator==(const PSRAMAllocator<U>&) const noexcept { 
        return true; 
    }
    
    template<typename U>
    bool operator!=(const PSRAMAllocator<U>&) const noexcept { 
        return false; 
    }

};

// ========================================
// TYPES UTILITAIRES - DANS LE HEADER
// ========================================

// Vecteur PSRAM générique
template<typename T>
using PSRAMVector = std::vector<T, PSRAMAllocator<T>>;

// String PSRAM
using PSRAMString = std::basic_string<char, std::char_traits<char>, PSRAMAllocator<char>>;

// Classe utilitaire pour la gestion des conteneurs PSRAM
class PSRAMContainerManager {
public:
    // Déclarations seulement - implémentation dans le .cpp
    static void printContainerStats(const char* containerName, size_t elementCount, size_t elementSize);
    static bool validateContainerMemory(void* containerPtr, size_t expectedSize);
    static void optimizeContainerMemory(void* containerPtr, const char* containerType);
    
    // Template inline dans le header
    template<typename Container>
    static size_t getContainerMemoryUsage(const Container& container) {
        return container.size() * sizeof(typename Container::value_type) + 
               container.capacity() * sizeof(typename Container::value_type);
    }
    
    template<typename Container>
    static float getContainerFragmentation(const Container& container) {
        if (container.capacity() == 0) return 0.0f;
        return (1.0f - static_cast<float>(container.size()) / container.capacity()) * 100.0f;
    }
};

#ifndef PLC_PERSISTENCE_H
#define PLC_PERSISTENCE_H

#include <Arduino.h>
#include <LittleFS.h>
#include <queue>
#include "ArduinoJson.h"
#include "PLC_CommonTypes/PLC_CommonTypes.h"
#include "PLC_Tools/PLC_Tools.h"
#include "MutexGuard/MutexGuard.h"
#include "AtomicFileWriter/AtomicFileWriter.h"
#include "SafeJsonDocument/SafeJsonDocument.h"

constexpr const char* PROTECTED_FILE_PATHS[] = {
    "/config.json",
    "/interface.json",
    "/index.html",
    "/index.json",
    "/diagnostic.txt",
    "/reboot_log.json"
};

static constexpr size_t PROTECTED_FILES_COUNT = sizeof(PROTECTED_FILE_PATHS) / sizeof(PROTECTED_FILE_PATHS[0]);

// Configuration
#define DEFAULT_PERSISTENCE_TIMEOUT     1000  // Timeout en millisecondes
#define DEFAULT_BUSY_WAIT_INTERVAL      50     // Intervalle d'attente en ms pour busy wait
#define DEFAULT_MAX_VERSIONS            10           // Nombre max de versions de backup par défaut
#define JSON_DOCUMENT_MAX_SIZE          65536      // Taille maximale d'un document JSON en octets

// Nouveaux paramètres pour le timeout dynamique
#define SMALL_FILE_SIZE_THRESHOLD       10240   // 10 KB - seuil pour considérer un fichier comme petit
#define SMALL_FILE_TIMEOUT              300            // 300 ms - timeout pour les petits fichiers
#define LARGE_FILE_TIMEOUT              1000           // 1000 ms - timeout pour les gros fichiers

#define MAX_FILE_SIZE                   1024 * 1024  // 1 MB - taille maximale de fichier
#define PATH_MAX_SIZE                   80          // Taille maximale du chemin d'accès


#define DEFERRED_SAVE_DELAY_MS          120000       // défini au bout de combien de temps (2 minutes) un fichier marqué modifié doit être enregistré

// Forward declaration pour éviter dépendance circulaire
class MyModbus;

class PLC_Persistence {
    public:
        // Accès au Singleton
        static PLC_Persistence& getInstance();
        
        // Gestion des états
        const char* getLastError() const;
        bool isPsramAvailable() const;
        
        // Lecture et écriture de fichiers
        bool saveToFile(const char* path, const char* data, size_t size);
        bool saveToFile(const char* path, const String& data);
        bool readFile(const char* path, String& outData);
        
        // Opérations JSON
        bool saveJsonToFile(const char* path, const JsonDocument& doc);
        bool saveJsonToFile(const char* path, const char* jsonString);
        bool readJsonFromFile(const char* path, SafeJsonDocument& outDoc);

        bool appendToFile(const char* path, const char* data, size_t size);
        bool appendToFile(const char* path, const String& data);
        
        // Utilitaires
        bool fileExists(const char* path, size_t* outFileSize = nullptr);
        bool deleteFile(const char* path, bool force = false);
        size_t getFileSize(const char* path);
        bool createBackup(const char* path, int maxVersions = DEFAULT_MAX_VERSIONS);
        
        // Gestion du système de fichiers
        bool checkFileSystemIntegrity();
        bool recoverFromBackup(const char* path);
        bool formatFileSystem();
        float getFileSystemUsage(); // Retourne le pourcentage d'utilisation du système de fichiers
        std::vector<String> getFilteredFiles(const char* path, const char* pattern);
        SafeJsonDocument getFilteredFilesAsJson(const char* path, const char* pattern);
        bool addRebootLogEntry();
        void logFileSystemStatus(bool cleanOrphans = true);

        typedef std::function<bool()> DeferredSaveCallback;

        // callback
        typedef std::function<void(const char* path, bool success)> FileSaveCallback;

        // ========================================================================
        // CALLBACK PRÉ-VALIDATION (Étape 1 - Infrastructure)
        // ========================================================================
        // Permet de valider un fichier AVANT sa sauvegarde
        // Retourne true si validation OK, false sinon (errorMsg contient le détail)
        typedef std::function<bool(const char* path, const JsonDocument& doc, String& errorMsg)> FilePreValidateCallback;
        void registerPreValidateCallback(FilePreValidateCallback callback);

        void registerSaveCallback(FileSaveCallback callback);

        bool isFileSystemReady() const {
            return fileSystemReady;
        }

        // Enregistrer le callback de sauvegarde (une fois au démarrage)
        void registerDeferredSave(const char* key, std::function<bool()> saveCallback);

        // Marquer qu'une sauvegarde est nécessaire (fin de cycle)
        void markDirty(const char* key);
        void flushAll(); 

        // Appeler dans doBusinessLogic() — décide seul si le moment est venu
        void process(uint32_t now);

    private:
            struct DeferredEntry {
                char key[32];
                DeferredSaveCallback callback;
                bool dirty = false;
                uint32_t lastMarkTime = 0;
        };
        std::vector<DeferredEntry> deferredEntries;

        // Singleton
        static PLC_Persistence* instance;
        
        // Constructeur privé (pattern Singleton)
        PLC_Persistence();
        
        // Mutex pour protéger l'accès aux ressources
        SemaphoreHandle_t persistenceMutex;
        
        // Variables d'état
        char lastError[128];
        unsigned long operationStartTime;
        bool psramAvailable;
        
        // Nouvelles variables pour la gestion du timeout
        size_t lastFileSize;           // Taille du dernier fichier traité
        unsigned long lastOperationTime; // Temps de la dernière opération
        
        // Fonctions privées
        void setLastError(const char* error);
        bool isPathValid(const char* path);

        // Corruption LittleFS - Étape 1 : restaure X depuis X.backup si X est absent
        void recoverInterruptedCommits();
        
        // Nouvelle fonction pour déterminer le timeout en fonction de la taille du fichier
        unsigned long getTimeoutForFileSize(size_t fileSize);

        // callback...
         // Store the callback
        FileSaveCallback saveCallback = nullptr;
        FilePreValidateCallback preValidateCallback = nullptr; 

        bool fileSystemReady = false;
};

#endif
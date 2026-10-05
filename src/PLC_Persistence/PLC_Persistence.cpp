#include "PLC_Persistence/PLC_Persistence.h"
#include <esp_heap_caps.h>
#include "PLC_Tools/PLC_Tools.h"
#include "MyModbus/MyModbus.h"
#include "PLC_Persistence.h"

// Initialisation du singleton
PLC_Persistence* PLC_Persistence::instance = nullptr;

PLC_Persistence::PLC_Persistence() : operationStartTime(0) {
    lastError[0] = '\0';
    
    // Création du mutex pour les opérations sur le système de fichiers
    persistenceMutex = xSemaphoreCreateRecursiveMutex();
    
    // Add diagnostic output to verify mutex creation
    if (persistenceMutex == NULL) {
        Serial.println("ERREUR CRITIQUE: Impossible de créer le mutex de persistance");
    } else { 
        // Test mutex acquisition using MutexGuard
        {
            MutexGuard guard(persistenceMutex, "persistenceMutex", "PLC_Persistence constructor");
            if (guard.isAcquired()) {
            } else {
                Serial.println("ERREUR: Test d'acquisition du mutex échoué");
            }
        } // MutexGuard goes out of scope here and the mutex is automatically released
    }

    // Vérification de la disponibilité de la PSRAM
    psramAvailable = PLC_Tools::isPSRAM_available();
    
    // Initialize filesystem here
    if (!LittleFS.begin(false)) {
        Serial.println("WARNING: Failed to mount LittleFS. Attempting to format...");
        if (!LittleFS.begin(true)) {
            Serial.println("ERROR: Failed to mount and format LittleFS!");
            BorneUniverselle::setPlcBroken("PLC_Persistence: LittleFS mount/format failed");
        } else {
            Serial.println("INFO: LittleFS formatted successfully");
        }
    } 

    fileSystemReady = true;
    logFileSystemStatus(true);  
}

PLC_Persistence& PLC_Persistence::getInstance() {
    if (instance == nullptr) {
        instance = new PLC_Persistence();
    }
    return *instance;
}

const char* PLC_Persistence::getLastError() const {
    return lastError;
}

void PLC_Persistence::setLastError(const char* error) {
    snprintf(lastError, sizeof(lastError), "PLC_Persistence error: %s\n", error);
    Serial.println(lastError);
    //PLC_Tools::logDiagnostic(lastError);
}

bool PLC_Persistence::isPathValid(const char* path) {
    if (!path || strlen(path) == 0) {
        setLastError("Chemin vide");
        return false;
    }
    
    // Vérifier si le chemin contient des séquences dangereuses
    if (strstr(path, "..") != nullptr) {
        setLastError("Le chemin contient des séquences invalides");
        return false;
    }
    
    // Vérifier que le chemin commence par un slash
    if (path[0] != '/') {
        setLastError("Le chemin doit commencer par '/'");
        return false;
    }
    
    return true;
}

unsigned long PLC_Persistence::getTimeoutForFileSize(size_t fileSize) {
    // Retourne un timeout adapté à la taille du fichier
    if (fileSize < SMALL_FILE_SIZE_THRESHOLD) {
        return SMALL_FILE_TIMEOUT;
    }
    return LARGE_FILE_TIMEOUT;
}

bool PLC_Persistence::saveToFile(const char* path, const char* data, size_t size) {
    if (!isPathValid(path)) {
        return false;
    }
    
    if (!data || size == 0) {
        setLastError("Données invalides pour sauvegarde");
        return false;
    }
    
    bool success = false;
    
    // ✅ SCOPE EXPLICITE pour le MutexGuard
    {
        MutexGuard guard(persistenceMutex, "persistenceMutex", "saveToFile_atomic");
        if (!guard.isAcquired()) {
            setLastError("Impossible d'acquérir le mutex pour sauvegarder le fichier");
            return false;
        }
        
        operationStartTime = millis();
        
        // Vérifier l'espace disque disponible
        size_t freeSpace = LittleFS.totalBytes() - LittleFS.usedBytes();
        if (size > freeSpace) {
            char errorMsg[150];
            snprintf(errorMsg, sizeof(errorMsg), 
                    "Espace disque insuffisant pour %s (%zu > %zu octets)", 
                    path, size, freeSpace);
            setLastError(errorMsg);
            return false;
        }
        
        // ÉCRITURE ATOMIQUE pour tous les fichiers texte
        String atomicError;
        BorneUniverselle* bu = BorneUniverselle::getInstance();
        bu->suspendForInternalOperation("Persistence: saveFile");
        {
            AtomicFileWriter writer{String(path)};
            
            if (writer.writeData(data, size)) {
                success = writer.commit();
                if (!success) {
                    atomicError = writer.getLastError();
                }
            } else {
                atomicError = writer.getLastError();
            }
            
            if (!success) {
                Serial.printf("PLC_Persistence: ✗ Échec sauvegarde atomique: %s\n", atomicError.c_str());
            }
            
        } // AtomicFileWriter détruit automatiquement (RAII)

        bu->resumeFromInternalOperation();

        // Gestion des erreurs
        if (!success) {
            char fullError[300];
            snprintf(fullError, sizeof(fullError), 
                    "Échec sauvegarde atomique fichier %s: %s", 
                    path, atomicError.c_str());
            setLastError(fullError);
        }
        
        lastFileSize = success ? size : 0;
        lastOperationTime = millis() - operationStartTime;
        
    } // ✅ MutexGuard détruit ICI - mutex libéré AVANT la callback
    
    // ✅ Callback synchrone APRÈS libération du mutex (aligné sur saveJsonToFile)
    if (saveCallback) {
        if (strcmp(path, "/diagnostic.txt") != 0) {
            saveCallback(path, success);
        }
    }
    
    return success;
}

bool PLC_Persistence::saveToFile(const char* path, const String& data) {
    return saveToFile(path, data.c_str(), data.length());
}

bool PLC_Persistence::readFile(const char* path, String& outData) {
    if (!isPathValid(path)) {
        setLastError("Chemin invalide pour la lecture du fichier");
        return false;
    }
    
    size_t fileSize = 0;
    if (!fileExists(path, &fileSize)) {
        char errorMsg[150];
        snprintf(errorMsg, sizeof(errorMsg), "Le fichier %s n'existe pas", path);
        setLastError(errorMsg);
        return false;
    }
    
    // Validation taille (protection contre fichiers corrompus/énormes)
    if (fileSize == 0) {
        setLastError("Fichier vide");
        return false;
    }
    
    if (fileSize > MAX_FILE_SIZE) {  // 1MB max
        setLastError("Fichier trop volumineux (> MAX_FILE_SIZE)");
        return false;
    }
    
    MutexGuard guard(persistenceMutex, "persistenceMutex", "readFile");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour lire le fichier");
        return false;
    }
    
    operationStartTime = millis();

    File file = LittleFS.open(path, FILE_READ);
    if (!file) {
        setLastError("Échec d'ouverture du fichier pour lecture");
        return false;
    }

    BorneUniverselle* bu = BorneUniverselle::getInstance();
    bu->suspendForInternalOperation("Persistence: read file");
    
    outData = file.readString();
    lastFileSize = fileSize;
    file.close();
    
    bu->resumeFromInternalOperation();
    
    lastOperationTime = millis() - operationStartTime;
    
    return true;
}

bool PLC_Persistence::saveJsonToFile(const char* path, const JsonDocument& doc) {
    static bool preventReentrance = false;
    
    // Protection contre la réentrance des callbacks
    if (preventReentrance) {
        Serial.println("ATTENTION: saveJsonToFile() appelé en réentrance - ignoré pour éviter corruption");
        return false;
    }
    
    bool success = false;
    BorneUniverselle* bu = BorneUniverselle::getInstance();
    bu->suspendForInternalOperation("Persistence: saveJsonToFile");
    
    // ✅ SCOPE EXPLICITE pour libérer le mutex AVANT la callback
    {
        MutexGuard guard(persistenceMutex, "persistenceMutex", "saveJsonToFile_atomic");
        if (!guard.isAcquired()) {
            setLastError("Impossible d'acquérir le mutex pour sauvegarde JSON atomique");
            bu->resumeFromInternalOperation();
            return false;
        }
        
        preventReentrance = true; // Bloquer la réentrance
        operationStartTime = millis();
        
        // Vérifier la taille du document AVANT allocation
        size_t estimatedSize = measureJson(doc);
        if (estimatedSize > JSON_DOCUMENT_MAX_SIZE) {
            char errorMsg[120];
            snprintf(errorMsg, sizeof(errorMsg), 
                    "Document JSON trop volumineux (%zu octets, max: %d)", 
                    estimatedSize, JSON_DOCUMENT_MAX_SIZE);
            setLastError(errorMsg);
            preventReentrance = false;
            bu->resumeFromInternalOperation();
            return false;
        }
        
        Serial.printf("PLC_Persistence: Sauvegarde JSON vers %s (%zu octets estimés)\n", 
                     path, estimatedSize);
        
        // Allouer buffer avec marge de sécurité augmentée
        size_t bufferSize = estimatedSize + 1024;
        char* jsonBuffer = (char*)PLC_Tools::allocateMemory(bufferSize, MEMORY_PREFER_PSRAM);
        
        if (!jsonBuffer) {
            setLastError("Échec d'allocation mémoire pour sérialisation JSON");
            preventReentrance = false;
            bu->resumeFromInternalOperation();
            return false;
        }
        
        // Sérialiser avec vérification renforcée
        size_t actualSize = serializeJson(doc, jsonBuffer, bufferSize - 1);
        if (actualSize == 0) {
            setLastError("Échec de sérialisation JSON - document vide");
            PLC_Tools::freeMemory(jsonBuffer);
            preventReentrance = false;
            bu->resumeFromInternalOperation();
            return false;
        }
        
        if (actualSize >= bufferSize - 1) {
            setLastError("Buffer JSON trop petit pour sérialisation complète");
            PLC_Tools::freeMemory(jsonBuffer);
            preventReentrance = false;
            bu->resumeFromInternalOperation();
            return false;
        }
        
        // Terminer la chaîne explicitement
        jsonBuffer[actualSize] = '\0';
        
        Serial.printf("PLC_Persistence: JSON sérialisé (%zu octets réels)\n", actualSize);
        
        // ÉCRITURE ATOMIQUE avec gestion d'erreur complète
        String atomicError;
        
        {
            AtomicFileWriter writer{String(path)};
            if (writer.writeData(jsonBuffer, actualSize)) {
                success = writer.commit();
                if (!success) {
                    atomicError = writer.getLastError();
                }
            } else {
                atomicError = writer.getLastError();
            }
            
            if (!success) {
                Serial.printf("PLC_Persistence: ✗ Échec sauvegarde atomique: %s\n", atomicError.c_str());
            }
            
        } // AtomicFileWriter se détruit ici automatiquement (RAII)
        
        // Libérer la mémoire
        PLC_Tools::freeMemory(jsonBuffer);
        
        // Construire le message d'erreur si échec
        if (!success) {
            char fullError[300];
            snprintf(fullError, sizeof(fullError), 
                    "Échec sauvegarde JSON atomique vers %s: %s", 
                    path, atomicError.c_str());
            setLastError(fullError);
        }
        
        // Débloquer la réentrance
        preventReentrance = false;
        lastOperationTime = millis() - operationStartTime;
        
    } // ✅ MutexGuard détruit ICI - mutex libéré AVANT la callback
    
    if (saveCallback) {
        if (strcmp(path, "/diagnostic.txt") != 0) {
            saveCallback(path, success);  // ← Synchrone
        }
    }
    bu->resumeFromInternalOperation();
    return success;
}

bool PLC_Persistence::saveJsonToFile(const char* path, const char* jsonString) {
    Serial.printf("%lu:: PLC_Persistence::saveJsonToFile (string): Validation JSON pour %s\r\n", 
                  millis(), path);
    
    // Guard Clause: Vérifier les paramètres
    if (!path || !jsonString) {
        setLastError("Chemin ou données JSON null");
        return false;
    }
    
    if (!isPathValid(path)) {
        return false;
    }

    BorneUniverselle* bu = BorneUniverselle::getInstance();
    bu->suspendForInternalOperation("Persistence: save json file");
    
    Serial.println("🧹 Nettoyage mémoire avant parsing JSON volumineux...");
    PLC_Tools::forceGarbageCollection();
    // =============================================================================
    // ÉTAPE 1: VALIDATION DU JSON
    // =============================================================================
    SafeJsonDocument validationDoc (65536);
    DeserializationError error = deserializeJson(validationDoc, jsonString);
    
    if (error) {
        char errorMsg[256];
        
        // Distinguer l'erreur de mémoire des autres erreurs
        if (error == DeserializationError::NoMemory) {
            // Erreur spécifique : mémoire insuffisante
            snprintf(errorMsg, sizeof(errorMsg), 
                    "Mémoire insuffisante pour parser %s (%u octets)", 
                    path, strlen(jsonString));
        } else {
            // Autres erreurs : JSON réellement invalide
            snprintf(errorMsg, sizeof(errorMsg), 
                    "JSON invalide pour %s: %s", 
                    path, error.c_str());
        }
    
        setLastError(errorMsg);
        
        // Logging diagnostic (premiers 300 caractères)
        Serial.println("\r\n=== FICHIER JSON CORROMPU ===");
        Serial.printf("Fichier: %s\r\n", path);
        Serial.printf("Erreur de désérialisation: %s\r\n", error.c_str());
        Serial.printf("Taille: %u octets\r\n", strlen(jsonString));
        Serial.println("Contenu (premiers 300 caractères):");
        Serial.println("---");
        
        size_t previewLen = min(strlen(jsonString), (size_t)300);
        char preview[301];
        strncpy(preview, jsonString, previewLen);
        preview[previewLen] = '\0';
        Serial.println(preview);
        
        Serial.println("---");
        Serial.println("=== FIN DU CONTENU ===\r\n");
        bu->resumeFromInternalOperation();
        return false; // NE PAS écrire de JSON corrompu
    }
    
    // JSON VALIDE
    if (preValidateCallback) {
        String errorMsg;
        if (!preValidateCallback(path, validationDoc, errorMsg)) {
            // ❌ La validation métier a échoué - rejeter AVANT la sauvegarde
            setLastError(errorMsg.c_str());
            Serial.printf("❌ %s\r\n", (errorMsg.c_str()));
            bu->resumeFromInternalOperation();
            return false;
        }
        Serial.printf("✅ Pre-validation passed for %s\r\n", path);
    }

    Serial.printf("PLC_Persistence:: JSON validé avec succès pour %s (%zu octets)\r\n", path, measureJson(validationDoc));
    bool success = saveJsonToFile(path, validationDoc);
    bu->resumeFromInternalOperation();
    return success;
}

bool PLC_Persistence::readJsonFromFile(const char* path, SafeJsonDocument& outDoc) {
    Serial.printf("%lu:: Lecture JSON depuis le fichier: %s\r\n", millis(), path);

    BorneUniverselle* bu = BorneUniverselle::getInstance();

    if (!isPathValid(path)) {
        setLastError("Le path est invalide");
        return false;
    }

    // Validation existence + récupération taille en un seul appel
    size_t fileSize = 0;
    Serial.println("readJsonFromFile, will call fileExists");

    if (!fileExists(path, &fileSize)) {
        char errorMsg[150];
        snprintf(errorMsg, sizeof(errorMsg), "Le fichier JSON %s n'existe pas", path);
        setLastError(errorMsg);
        Serial.println(errorMsg);
        return false;
    }

    // Validation taille
    if (fileSize == 0) {
        setLastError("Fichier JSON vide");
        return false;
    }

    if (fileSize > 1024 * 1024) {  // 1MB max
        setLastError("Fichier JSON trop volumineux (> 1MB)");
        return false;
    }

    MutexGuard guard(persistenceMutex, "persistenceMutex", "readJsonFromFile");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour lire le fichier JSON");
        return false;
    }

    bu->suspendForInternalOperation("Persistence: read json file");
    operationStartTime = millis();
    

    // Allouer de la mémoire pour lire le fichier entier
    size_t bufferSize = fileSize + 64; // Un peu plus pour la marge
    char* jsonBuffer = (char*)PLC_Tools::allocateMemory(bufferSize, MEMORY_PREFER_PSRAM);
   
    if (!jsonBuffer) {
        setLastError("Échec d'allocation mémoire pour deserialisation JSON");
        bu->resumeFromInternalOperation();
        return false;
    }
    
    // Lire le fichier en une seule fois
    File file = LittleFS.open(path, "r");
    if (!file) {
        PLC_Tools::freeMemory(jsonBuffer);
        bu->resumeFromInternalOperation();
        setLastError("Échec d'ouverture du fichier JSON pour lecture du contenu");
        return false;
    }
    
    size_t bytesRead = file.readBytes(jsonBuffer, fileSize);
    file.close();
    
    if (bytesRead != fileSize) {
        PLC_Tools::freeMemory(jsonBuffer);
        bu->resumeFromInternalOperation();
        setLastError("Échec de lecture complète du fichier JSON");
        return false;
    }
    
    // S'assurer que le buffer est terminé par un zéro
    jsonBuffer[bytesRead] = '\0';
    
    // Nettoyer le document avant désérialisation
    outDoc.clear();
    
    // Désérialiser directement depuis le buffer
    DeserializationError error = deserializeJson(outDoc, jsonBuffer);
    
    // TEST D'ERREUR AVANT LIBÉRATION DU BUFFER
    if (error) {
        // Afficher les 5000 premiers caractères pour diagnostic
        size_t previewLength = (bytesRead < 5000) ? bytesRead : 5000;
        Serial.printf("\n=== FICHIER JSON CORROMPU: %s ===\n", path);
        Serial.printf("Erreur de désérialisation: %s\n", error.c_str());
        Serial.printf("Taille du fichier: %zu octets\n", bytesRead);
        Serial.printf("Contenu (premiers %zu caractères):\n", previewLength);
        Serial.println("---");
        Serial.write((const uint8_t*)jsonBuffer, previewLength);
        Serial.println("\n---");
        Serial.println("=== FIN DU CONTENU ===\n");
        
        // Message d'erreur pour le système
        char errorMsg[150];
        snprintf(errorMsg, sizeof(errorMsg), 
                "Échec de désérialisation JSON pour %s: %s",
                path, error.c_str());
        setLastError(errorMsg);
        
        // Libérer la mémoire avant de retourner
        PLC_Tools::freeMemory(jsonBuffer);
        bu->resumeFromInternalOperation(); // La désérialisation prend du temps 
        return false;
    }

    // Libérer la mémoire (cas de succès)
    PLC_Tools::freeMemory(jsonBuffer);

    //serializeJsonPretty(outDoc, Serial);

    lastOperationTime = millis() - operationStartTime;
    bu->resumeFromInternalOperation(); // La désérialisation prend du temps 
    return true;
} // readJsonFromFile

bool PLC_Persistence::fileExists(const char* path, size_t* outFileSize) {
    if (!isPathValid(path)) {
        return false;
    }

    MutexGuard guard(persistenceMutex, "persistenceMutex", "fileExists");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour vérifier l'existence du fichier");
        return false;
    }

    BorneUniverselle* bu = BorneUniverselle::getInstance();
    bu->suspendForInternalOperation("Persistence: fileExist");

    // Si on veut juste vérifier l'existence (comportement original)
    bool success = LittleFS.exists(path);
    if (outFileSize == nullptr || !success) {
        bu->resumeFromInternalOperation();
         return success;
    }
    
    // Si on veut aussi la taille, on doit ouvrir le fichier
    File file = LittleFS.open(path, "r");
    if (!file) {
        *outFileSize = 0;
        bu->resumeFromInternalOperation(); 
        return false;
    }
    
    *outFileSize = file.size();
    file.close();
    bu->resumeFromInternalOperation(); 
    return true;
}

bool PLC_Persistence::deleteFile(const char* path, bool force) {
    // Vérifier si le fichier est protégé
    
    if (!force) {
        for (size_t i = 0; i < PROTECTED_FILES_COUNT; i++) {
            if (strcmp(path, PROTECTED_FILE_PATHS[i]) == 0) {
                char mess[256];
                snprintf(mess, sizeof(mess), "Tentative de suppression d'un fichier protégé: %s", path);
                setLastError(mess);
                return false;
            }
        }
    }

    if (!isPathValid(path)) {
        return false;
    }

    BorneUniverselle* bu = BorneUniverselle::getInstance();
    bu->suspendForInternalOperation("Persistence: deleteFile");
    
    MutexGuard guard(persistenceMutex, "persistenceMutex", "deleteFile");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour supprimer le fichier");
        bu->resumeFromInternalOperation();
        return false;
    }
    
    if (!LittleFS.exists(path)) {
        char errorMsg[150];
        snprintf(errorMsg, sizeof(errorMsg), "Le fichier %s n'existe pas", path);
        setLastError(errorMsg);
        bu->resumeFromInternalOperation();
        return false;
    }
    
    bool success = LittleFS.remove(path);
    
    if (!success) {
        setLastError("Échec de suppression du fichier");
    }
    bu->resumeFromInternalOperation();
    return success;
}

size_t PLC_Persistence::getFileSize(const char* path) {
    if (!isPathValid(path)) {
        return 0;
    }
    
    // Use a longer timeout and report attempt count
    const int maxAttempts = 3;
    BorneUniverselle* bu = BorneUniverselle::getInstance();
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        MutexGuard guard(persistenceMutex, "persistenceMutex", "getFileSize");
        if (guard.isAcquired()) {
            bu->suspendForInternalOperation("Persistence: getFileSize");
            //Serial.println("DEBUG: Successfully acquired persistenceMutex in getFileSize");

            size_t fileSize = 0;
            
            if (!LittleFS.exists(path)) {
                char errorMsg[150];
                snprintf(errorMsg, sizeof(errorMsg), "Le fichier %s n'existe pas", path);   
                setLastError(errorMsg);
                bu->resumeFromInternalOperation();
                return 0;
            }
            
            File file = LittleFS.open(path, FILE_READ);
            if (!file) {
                setLastError("Échec d'ouverture du fichier pour lecture");
                bu->resumeFromInternalOperation();
                return 0;
            }
            
            fileSize = file.size();
            file.close();
            bu->resumeFromInternalOperation();
            return fileSize;
        } else {
            delay(100 * attempt); // Increasing delay between attempts
        }
    }
    setLastError("Impossible d'acquérir le mutex pour obtenir la taille du fichier après plusieurs tentatives");
    return 0;
}

bool PLC_Persistence::createBackup(const char* path, int maxVersions) {
    if (!isPathValid(path)) {
        return false;
    }
    
    MutexGuard guard(persistenceMutex, "persistenceMutex", "createBackup");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour créer une sauvegarde");
        return false;
    }
    
    operationStartTime = millis();
    
    if (!LittleFS.exists(path)) {
        setLastError("Le fichier source n'existe pas");
        return false;
    }
    
    // Extraire le nom de base et l'extension
    String origPath(path);
    int lastSlash = origPath.lastIndexOf('/');
    int lastDot = origPath.lastIndexOf('.');
    
    String baseName, extension, dirPath;
    
    if (lastSlash >= 0) {
        dirPath = origPath.substring(0, lastSlash + 1);
    } else {
        dirPath = "/";
    }
    
    if (lastDot > lastSlash) {
        baseName = origPath.substring(lastSlash + 1, lastDot);
        extension = origPath.substring(lastDot);
    } else {
        baseName = origPath.substring(lastSlash + 1);
        extension = "";
    }
    
    // Collecter toutes les versions existantes
    std::vector<String> backupFiles;
    std::vector<int> versionNumbers;
    
    File root = LittleFS.open(dirPath.c_str());
    if (!root || !root.isDirectory()) {
        setLastError("Échec d'ouverture du répertoire");
        if (root) root.close();
        return false;
    }
    
    // Format de nom de backup attendu: baseNom.v1.extension, baseNom.v2.extension, etc.
    String versionPrefix = baseName + ".v";
    
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fileName = String(file.name());
            
            // Vérifier si c'est un fichier de backup pour ce fichier
            if (fileName.startsWith(dirPath + versionPrefix)) {
                String versionPart = fileName;
                versionPart.replace(dirPath + versionPrefix, "");
                versionPart.replace(extension, "");
                
                // Essayer de convertir en nombre
                int versionNum = versionPart.toInt();
                if (versionNum > 0) {
                    backupFiles.push_back(fileName);
                    versionNumbers.push_back(versionNum);
                }
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    
    // Trouver le numéro de version le plus élevé
    int highestVersion = 0;
    for (int version : versionNumbers) {
        if (version > highestVersion) {
            highestVersion = version;
        }
    }
    
    // Créer le nouveau nom de version
    int newVersion = highestVersion + 1;
    String backupPath = dirPath + baseName + ".v" + String(newVersion) + extension;
    
    // Obtenir la taille du fichier pour optimiser l'allocation
    size_t fileSize = getFileSize(path);
    
    // Utiliser un buffer adapté avec notre fonction allocateMemory
    const size_t bufferSize = (fileSize > 8192) ? 8192 : 512;
    uint8_t* buffer = (uint8_t*)PLC_Tools::allocateMemory(bufferSize, MEMORY_PREFER_PSRAM);
    
    if (!buffer) {
        setLastError("Échec d'allocation mémoire pour le buffer de sauvegarde");
        return false;
    }
    
    // Copier le contenu
    File sourceFile = LittleFS.open(path, FILE_READ);
    if (!sourceFile) {
        PLC_Tools::freeMemory(buffer);
        setLastError("Échec d'ouverture du fichier source");
        return false;
    }
    
    File backupFile = LittleFS.open(backupPath.c_str(), FILE_WRITE);
    if (!backupFile) {
        sourceFile.close();
        PLC_Tools::freeMemory(buffer);
        setLastError("Échec de création du fichier de sauvegarde");
        return false;
    }
    
    size_t bytesRead;
    while ((bytesRead = sourceFile.read(buffer, bufferSize)) > 0) {
        if (backupFile.write(buffer, bytesRead) != bytesRead) {
            sourceFile.close();
            backupFile.close();
            PLC_Tools::freeMemory(buffer);
            setLastError("Échec d'écriture dans le fichier de sauvegarde");
            return false;
        }
    }
    
    sourceFile.close();
    backupFile.close();
    PLC_Tools::freeMemory(buffer);
    
    // Si on dépasse le nombre max de versions, supprimer les plus anciennes
    if (backupFiles.size() >= maxVersions) {
        // Trier les versions
        struct VersionInfo {
            String filePath;
            int version;
        };
        
        std::vector<VersionInfo> sortedVersions;
        for (size_t i = 0; i < backupFiles.size(); i++) {
            sortedVersions.push_back({backupFiles[i], versionNumbers[i]});
        }
        
        // Trier par numéro de version (croissant)
        std::sort(sortedVersions.begin(), sortedVersions.end(), 
                 [](const VersionInfo& a, const VersionInfo& b) {
                     return a.version < b.version;
                 });
        
        // Supprimer les versions les plus anciennes jusqu'à ce qu'on soit en dessous de la limite
        int numToDelete = sortedVersions.size() + 1 - maxVersions; // +1 pour la nouvelle version créée
        for (int i = 0; i < numToDelete && i < (int)sortedVersions.size(); i++) {
            Serial.printf("Suppression de l'ancienne version de sauvegarde: %s\n", sortedVersions[i].filePath.c_str());
            LittleFS.remove(sortedVersions[i].filePath.c_str());
        }
    }
    
    Serial.printf("Sauvegarde créée: %s (version %d)\n", backupPath.c_str(), newVersion);
    lastOperationTime = millis() - operationStartTime;
    
    return true;
}

// =============================================================================
// VERSION AMÉLIORÉE SIMPLE DE appendToFile
// =============================================================================

bool PLC_Persistence::appendToFile(const char* path, const char* data, size_t size) {
    BorneUniverselle* bu = BorneUniverselle::getInstance();

    if (!isPathValid(path)) {
        return false;
    }

    // Vérification des données
    if (!data || size == 0) {
        setLastError("Données invalides pour append");
        return false;
    }

    // Utiliser le même mutex que les autres opérations
    MutexGuard guard(persistenceMutex, "persistenceMutex", "appendToFile");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour ajouter au fichier");
        return false;
    }

    bu->suspendForInternalOperation("Persistence: appendToFile");
    operationStartTime = millis();

    // Ouvrir en mode FILE_APPEND pour ajouter à la fin
    File file = LittleFS.open(path, FILE_APPEND);
    if (!file) {
        setLastError("Échec d'ouverture du fichier en mode append");
        bu->resumeFromInternalOperation();
        return false;
    }
    
    // Écriture des données
    size_t bytesWritten = file.write((const uint8_t*)data, size);
    
    // IMPORTANT: Forcer la synchronisation sur le disque
    file.flush();
    file.close();
    bu->resumeFromInternalOperation();
    bool success = (bytesWritten == size);
    
    if (!success) {
        char errorMsg[128];
        snprintf(errorMsg, sizeof(errorMsg), 
                "Échec d'écriture complète: %zu/%zu octets écrits", bytesWritten, size);
        setLastError(errorMsg);
    }

    // PAS DE CALLBACK pour appendToFile - évite la race condition
    // La callback sera seulement appelée pour saveToFile
    
    lastFileSize = size;
    lastOperationTime = millis() - operationStartTime;
    
    return success;
}

bool PLC_Persistence::appendToFile(const char* path, const String& data) {
    return appendToFile(path, data.c_str(), data.length()); 
}

bool PLC_Persistence::checkFileSystemIntegrity() {
    MutexGuard guard(persistenceMutex, "persistenceMutex", "checkFileSystemIntegrity");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour vérifier l'intégrité du système de fichiers");
        return false;
    }
    
    // Tentative de remontage du système de fichiers pour vérifier l'intégrité
    LittleFS.end();
    if (!LittleFS.begin(false)) {
        Serial.println("Vérification du système de fichiers: Échec de montage de LittleFS, tentative de récupération...");
        
        // Tenter de remonter avec l'option de formatage
        if (!LittleFS.begin(true)) {
            setLastError("Échec de vérification d'intégrité du système de fichiers: Impossible de récupérer le système de fichiers");
            Serial.println("Échec de vérification d'intégrité du système de fichiers: Impossible de récupérer le système de fichiers");
            BorneUniverselle::setPlcBroken("Échec de vérification d'intégrité du système de fichiers");
            return false;
        }
        fileSystemReady = true;
        Serial.println("Système de fichiers récupéré par formatage. Toutes les données ont été perdues!");
        return true;
    }
    
    Serial.println("Vérification d'intégrité du système de fichiers réussie");
    return true;
}

bool PLC_Persistence::recoverFromBackup(const char* path) {
    if (!isPathValid(path)) {
        return false;
    }
    
    MutexGuard guard(persistenceMutex, "persistenceMutex", "recoverFromBackup");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour récupérer depuis la sauvegarde");
        return false;
    }
    
    operationStartTime = millis();
    
    // Construire le schéma de nommage des fichiers de backup
    String origPath(path);
    int lastSlash = origPath.lastIndexOf('/');
    int lastDot = origPath.lastIndexOf('.');
    
    String baseName, extension, dirPath;
    
    if (lastSlash >= 0) {
        dirPath = origPath.substring(0, lastSlash + 1);
    } else {
        dirPath = "/";
    }
    
    if (lastDot > lastSlash) {
        baseName = origPath.substring(lastSlash + 1, lastDot);
        extension = origPath.substring(lastDot);
    } else {
        baseName = origPath.substring(lastSlash + 1);
        extension = "";
    }
    
    // Rechercher la version de backup la plus récente
    int highestVersion = 0;
    String latestBackup = "";
    
    File root = LittleFS.open(dirPath.c_str());
    if (!root || !root.isDirectory()) {
        setLastError("Échec d'ouverture du répertoire pour récupération de sauvegarde");
        if (root) root.close();
        return false;
    }
    
    String versionPrefix = baseName + ".v";
    
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fileName = String(file.name());
            
            if (fileName.startsWith(dirPath + versionPrefix) && fileName.endsWith(extension)) {
                String versionStr = fileName;
                versionStr.replace(dirPath + versionPrefix, "");
                versionStr.replace(extension, "");
                
                int version = versionStr.toInt();
                if (version > highestVersion) {
                    highestVersion = version;
                    latestBackup = fileName;
                }
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    
    if (highestVersion == 0) {
        setLastError("Aucune sauvegarde trouvée pour récupération");
        return false;
    }
    
    // Supprimer le fichier corrompu si existant
    if (LittleFS.exists(path)) {
        LittleFS.remove(path);
    }
    
    // Obtenir la taille du fichier de sauvegarde
    size_t backupSize = getFileSize(latestBackup.c_str());
    
    // Allouer un buffer adapté
    const size_t bufferSize = (backupSize > 8192) ? 8192 : 512;
    uint8_t* buffer = (uint8_t*)PLC_Tools::allocateMemory(bufferSize, MEMORY_PREFER_PSRAM);
    
    if (!buffer) {
        setLastError("Échec d'allocation mémoire pour le buffer de récupération");
        return false;
    }
    
    if (psramAvailable && backupSize > 8192) {
        Serial.printf("Utilisation de la PSRAM pour le buffer de récupération (%zu octets)\n", bufferSize);
    }
    
    // Copier le backup vers le fichier original
    File backupFile = LittleFS.open(latestBackup.c_str(), FILE_READ);
    if (!backupFile) {
        PLC_Tools::freeMemory(buffer);
        setLastError("Échec d'ouverture du fichier de sauvegarde");
        return false;
    }
    
    File restoredFile = LittleFS.open(path, FILE_WRITE);
    if (!restoredFile) {
        backupFile.close();
        PLC_Tools::freeMemory(buffer);
        setLastError("Échec de création du fichier récupéré");
        return false;
    }
    
    size_t bytesRead;
    while ((bytesRead = backupFile.read(buffer, bufferSize)) > 0) {
        if (restoredFile.write(buffer, bytesRead) != bytesRead) {
            backupFile.close();
            restoredFile.close();
            PLC_Tools::freeMemory(buffer);
            setLastError("Échec d'écriture dans le fichier récupéré");
            return false;
        }
    }
    
    backupFile.close();
    restoredFile.close();
    PLC_Tools::freeMemory(buffer);
    
    Serial.printf("Récupération réussie de %s depuis la sauvegarde version %d\n", path, highestVersion);
    lastOperationTime = millis() - operationStartTime;
    
    return true;
}

bool PLC_Persistence::formatFileSystem() {
    MutexGuard guard(persistenceMutex, "persistenceMutex", "formatFileSystem");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour formater le système de fichiers");
        return false;
    }
    
    // Démonter puis reformater le système de fichiers
    LittleFS.end();
    
    if (!LittleFS.begin(true)) { // true = formatter
        setLastError("Échec de formatage du système de fichiers");
        BorneUniverselle::setPlcBroken("Échec de formatage du système de fichiers");
        return false;
    }
    fileSystemReady = true;
    
    Serial.println("Système de fichiers formaté avec succès");
    return true;
}

float PLC_Persistence::getFileSystemUsage() {
    MutexGuard guard(persistenceMutex, "persistenceMutex", "getFileSystemUsage");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour obtenir l'utilisation du système de fichiers");
        return -1.0f;
    }
    
    #ifdef ESP32
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    #else
    FSInfo fs_info;
    LittleFS.info(fs_info);
    size_t totalBytes = fs_info.totalBytes;
    size_t usedBytes = fs_info.usedBytes;
    #endif
    
    float usagePercent = (float)usedBytes / totalBytes * 100.0f;
    
    return usagePercent;
}

std::vector<String> PLC_Persistence::getFilteredFiles(const char* path, const char* pattern) {
    std::vector<String> result;
    
    if (!isPathValid(path)) {
        return result;
    }
    
    MutexGuard guard(persistenceMutex, "persistenceMutex", "getFilteredFiles");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour obtenir les fichiers filtrés");
        return result;
    }
    
    File root = LittleFS.open(path);
    if (!root) {
        setLastError("Échec d'ouverture du répertoire");
        return result;
    }

    if (!root.isDirectory()) {
        setLastError("Pas un répertoire");
        root.close();
        return result;
    }

    // Vérifier si on veut tous les fichiers
    bool getAllFiles = (strcmp(pattern, "*") == 0) || (strcmp(pattern, "*.*") == 0);
    
    String patternStr = String(pattern);
    if (patternStr.startsWith("*")) {
        patternStr = patternStr.substring(1);
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fileName = String(file.name());
            if (getAllFiles || fileName.endsWith(patternStr)) {
                result.push_back(fileName);
            }
        }
        file.close();
        file = root.openNextFile();
    }

    root.close();
    
    return result;
}

SafeJsonDocument PLC_Persistence::getFilteredFilesAsJson(const char* path, const char* pattern) {
    SafeJsonDocument doc;
    
    // Obtenir la liste des fichiers
    std::vector<String> files = getFilteredFiles(path, pattern);
    
    // Créer le tableau JSON
    JsonArray directoryArray = doc["Directory"].to<JsonArray>();
    
    // Ajouter chaque fichier au tableau JSON
    for(const String& fileName : files) {
        directoryArray.add(fileName);
    }
    
    return doc;
}

bool PLC_Persistence::isPsramAvailable() const {
    return psramAvailable;
}

bool PLC_Persistence::addRebootLogEntry() {
    // Si le fichier n'existe pas, le créer avec une entrée initiale
    if (!fileExists("/reboot_log.json")) {
        JsonDocument doc;
        doc["reboots"] = JsonArray();
        
        // Utiliser add<JsonObject>() au lieu de createNestedObject()
        JsonArray reboots = doc["reboots"].as<JsonArray>();
        JsonObject entry = reboots.add<JsonObject>();
        entry["time"] = millis();
        entry["heap"] = ESP.getFreeHeap();
        
        return saveJsonToFile("/reboot_log.json", doc);
    }
    
    // Lire le fichier existant
    SafeJsonDocument doc;
    if (!readJsonFromFile("/reboot_log.json", doc)) {
        // En cas d'échec, ne pas bloquer le démarrage - simplement enregistrer l'erreur
        Serial.println("Échec de lecture du journal de redémarrage, création d'un nouveau journal");
        doc.clear();
        doc["reboots"] = JsonArray();
    }
    
    // Ajouter une nouvelle entrée
    if (doc["reboots"].isNull()) {
        doc["reboots"] = JsonArray();
    }
    
    // Utiliser add<JsonObject>() au lieu de createNestedObject()
    JsonArray reboots = doc["reboots"].as<JsonArray>();
    JsonObject entry = reboots.add<JsonObject>();
    entry["time"] = millis();
    entry["heap"] = ESP.getFreeHeap();
    
    // Limiter la taille du log (garder seulement les 20 derniers redémarrages)
    while (reboots.size() > 20) {
        // Supprimer le premier élément (le plus ancien)
        reboots.remove(0);
    }
    
    // Sauvegarder
    return saveJsonToFile("/reboot_log.json", doc);
}

void PLC_Persistence::registerPreValidateCallback(FilePreValidateCallback callback) {
    MutexGuard guard(persistenceMutex, "persistenceMutex", "registerPreValidateCallback");
    if (!guard.isAcquired()) {
        setLastError("Impossible d'acquérir le mutex pour enregistrer le callback de prévalidation");
        return;
    }
    
    preValidateCallback = callback;
    Serial.println("Pre-validate callback registered successfully");
}

void PLC_Persistence::registerSaveCallback(FileSaveCallback callback) {
    saveCallback = callback;
    Serial.println("PLC_Persistence: Save callback registered");
}

// ========================================================================
// SAUVEGARDE DIFFÉRÉE
// ========================================================================

void PLC_Persistence::registerDeferredSave(const char* key, DeferredSaveCallback callback) {
    if (!key || !callback) {
        Serial.println("PLC_Persistence::registerDeferredSave: key ou callback invalide");
        return;
    }

    // Vérifier si la clé est déjà enregistrée (protection contre double registration)
    for (auto& entry : deferredEntries) {
        if (strncmp(entry.key, key, sizeof(entry.key)) == 0) {
            Serial.printf("PLC_Persistence: [%s] deja enregistre - mise a jour du callback\n", key);
            entry.callback = callback;
            return;
        }
    }

    DeferredEntry entry;
    strncpy(entry.key, key, sizeof(entry.key) - 1);
    entry.key[sizeof(entry.key) - 1] = '\0';
    entry.callback    = callback;
    entry.dirty       = false;
    entry.lastMarkTime = 0;

    deferredEntries.push_back(entry);
    Serial.printf("PLC_Persistence: [%s] sauvegarde differee enregistree\n", key);
}

void PLC_Persistence::markDirty(const char* key) {
    if (!key) return;

    for (auto& entry : deferredEntries) {
        if (strncmp(entry.key, key, sizeof(entry.key)) == 0) {
            entry.dirty        = true;
            entry.lastMarkTime = millis();
            Serial.printf("dirty for ket: %s registred\r\n", key);
            return;
        }
    }

    // Clé non trouvée — probablement registerDeferredSave oublié
    Serial.printf("PLC_Persistence::markDirty: [%s] non enregistre - appeler registerDeferredSave() au demarrage\n", key);
}

void PLC_Persistence::process(uint32_t now) {
    for (auto& entry : deferredEntries) {
        if (!entry.dirty) continue;

        // Délai écoulé depuis le dernier markDirty ?
        if (now - entry.lastMarkTime < DEFERRED_SAVE_DELAY_MS){
            continue;
        } 

        Serial.printf("PLC_Persistence: [%s] flush differences -> sauvegarde\n", entry.key);

        bool success = entry.callback();
        entry.dirty  = false;

        if (!success) {
            Serial.printf("PLC_Persistence: [%s] echec sauvegarde differee\n", entry.key);
        }
    }
}

void PLC_Persistence::flushAll() {
    bool hasDirty = false;
    for (const auto& entry : deferredEntries) {
        if (entry.dirty) { hasDirty = true; break; }
    }
    if (!hasDirty) return;

    Serial.println("PLC_Persistence::flushAll() - Sauvegarde forcée de tous les fichiers dirty");

    for (auto& entry : deferredEntries) {
        if (!entry.dirty) continue;

        Serial.printf("PLC_Persistence::flushAll: [%s] sauvegarde forcée\n", entry.key);

        bool success = entry.callback();
        entry.dirty = false;

        if (!success) {
            Serial.printf("PLC_Persistence::flushAll: [%s] échec sauvegarde\n", entry.key);
        }
    }

    Serial.println("PLC_Persistence::flushAll() - Terminé");
}

// ============================================================================
// RÉCUPÉRATION DES COMMITS INTERROMPUS (Corruption LittleFS - Étape 1)
// ----------------------------------------------------------------------------
// AtomicFileWriter::commit() renomme X -> X.backup puis X.tmp -> X.
// Un reset entre les deux laisse X ABSENT et X.backup présent (dernière
// version committée, donc valide). On restaure X.backup -> X.
// Règle : si X existe, on ne touche à rien (cas normal).
// Appelé depuis logFileSystemStatus(true), donc au boot, avant tout accès métier.
// ============================================================================
void PLC_Persistence::recoverInterruptedCommits() {
    static const char* BACKUP_SUFFIX = ".backup";
    const size_t suffixLen = strlen(BACKUP_SUFFIX);

    // Passe 1 : collecter les noms (aucun rename pendant le parcours du répertoire)
    std::vector<String> backups;
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("recoverInterruptedCommits: impossible d'ouvrir la racine");
        if (root) root.close();
        return;
    }

    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            String name = String(f.name());
            if (name.length() > suffixLen && name.endsWith(BACKUP_SUFFIX)) {
                backups.push_back(name);
            }
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();

    // Passe 2 : restaurer X.backup -> X uniquement si X est absent
    for (const String& name : backups) {
        const char* prefix = name.startsWith("/") ? "" : "/";
        String backupPath   = String(prefix) + name;
        String originalPath = String(prefix) + name.substring(0, name.length() - suffixLen);

        if (LittleFS.exists(originalPath.c_str())) {
            continue;  // Cas normal : X présent, le .backup sera nettoyé comme orphelin
        }

        if (LittleFS.rename(backupPath.c_str(), originalPath.c_str())) {
            Serial.printf("  ⚠️ RECOVERY: %s absent -> restauré depuis %s\n",
                          originalPath.c_str(), backupPath.c_str());
        } else {
            Serial.printf("  ❌ RECOVERY: échec restauration de %s depuis %s\n",
                          originalPath.c_str(), backupPath.c_str());
        }
    }
}

void PLC_Persistence::logFileSystemStatus(bool cleanOrphans) {
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    size_t free  = total - used;

    Serial.println(F("=== LittleFS Status ==="));
    Serial.printf("  Total : %u bytes (%.1f KB)\n", total, total / 1024.0f);
    Serial.printf("  Used  : %u bytes (%.1f KB)\n", used,  used  / 1024.0f);
    Serial.printf("  Free  : %u bytes (%.1f KB)\n", free,  free  / 1024.0f);
    Serial.println(F("  --- Files ---"));

    // Corruption LittleFS - Étape 1 : restaurer les commits interrompus
    // AVANT que le nettoyage ne supprime les .backup / .tmp
    if (cleanOrphans) {
        recoverInterruptedCommits();
    }

    size_t orphanBytes = 0;
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    while (f) {
        const char* name = f.name();
        size_t      size = f.size();
        f.close();

        // Détecter les orphelins
        size_t nameLen = strlen(name);
        bool isTmp    = (nameLen > 4  && strcmp(name + nameLen - 4,  ".tmp")    == 0);
        bool isBackup = (nameLen > 7  && strcmp(name + nameLen - 7,  ".backup") == 0);
        bool isOrphan = isTmp || isBackup;

        Serial.printf("  [%s] %u bytes%s\n", name, size, isOrphan ? "  ← ORPHAN" : "");

        if (isOrphan) {
            orphanBytes += size;
            if (cleanOrphans) {
                char fullPath[PATH_MAX_SIZE];
                snprintf(fullPath, sizeof(fullPath), "/%s", name);
                if (LittleFS.remove(fullPath)) {
                    Serial.printf("  ✓ Supprimé: %s\n", fullPath);
                } else {
                    Serial.printf("  ✗ Échec suppression: %s\n", fullPath);
                }
            }
        }

        f = root.openNextFile();
    }
    root.close();

    if (cleanOrphans && orphanBytes > 0) {
        size_t freeAfter = LittleFS.totalBytes() - LittleFS.usedBytes();
        Serial.printf("  Orphelins nettoyés: %u bytes récupérés\n", orphanBytes);
        Serial.printf("  Free après nettoyage: %u bytes (%.1f KB)\n", 
                      freeAfter, freeAfter / 1024.0f);
    }

    // Seuil critique : moins de 50 KB libres après nettoyage
    size_t freeNow = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (freeNow < 50 * 1024) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "LittleFS critique: %u bytes libres (%.1f KB)",
                 freeNow, freeNow / 1024.0f);
        Serial.printf("  ⛔ %s\n", msg);
        BorneUniverselle::setPlcBroken(msg);
    }

    Serial.println(F("======================"));
}
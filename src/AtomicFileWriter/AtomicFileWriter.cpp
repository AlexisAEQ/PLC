#include "AtomicFileWriter.h"

AtomicFileWriter::AtomicFileWriter(const String& path) 
    : originalPath(path), committed(false) {
    
    // Générer les noms de fichiers
    tempPath = path + ".tmp";
    backupPath = path + ".backup";
    
    //Serial.printf("AtomicFileWriter: Préparation écriture de %s\n", path.c_str());
}

AtomicFileWriter::~AtomicFileWriter() {
    if (!committed) {
        Serial.println("AtomicFileWriter: Rollback automatique (pas de commit)");
        cleanup();
    }
}

bool AtomicFileWriter::writeData(const char* data, size_t size) {
    if (!data || size == 0) {
        lastError = "Données invalides";
        return false;
    }
    
    if (committed) {
        lastError = "Écriture déjà committée";
        return false;
    }
    
    // Nettoyer un éventuel fichier temporaire existant
    if (LittleFS.exists(tempPath.c_str())) {
        LittleFS.remove(tempPath.c_str());
    }
    
    // Ouvrir le fichier temporaire
    File tempFile = LittleFS.open(tempPath.c_str(), FILE_WRITE);
    if (!tempFile) {
        lastError = "Impossible d'ouvrir le fichier temporaire";
        Serial.printf("ERREUR: %s - %s\n", lastError.c_str(), tempPath.c_str());
        return false;
    }
    
    // Écrire les données
    size_t bytesWritten = tempFile.write((uint8_t*)data, size);
    
    // IMPORTANT: Forcer l'écriture sur le support physique
    tempFile.flush();
    
    // Fermer le fichier
    tempFile.close();
    
    // Vérifier que tout a été écrit
    if (bytesWritten != size) {
        lastError = "Écriture incomplète";
        Serial.printf("ERREUR: %s - %zu/%zu bytes\n", lastError.c_str(), bytesWritten, size);
        LittleFS.remove(tempPath.c_str());
        return false;
    }
    
    // Vérifier l'intégrité
    if (!verifyFileIntegrity(size)) {
        lastError = "Échec vérification intégrité";
        Serial.printf("ERREUR: %s\n", lastError.c_str());
        LittleFS.remove(tempPath.c_str());
        return false;
    }
    
    //Serial.printf("AtomicFileWriter: Données écrites avec succès (%zu bytes)\n", size);
    return true;
}

bool AtomicFileWriter::commit() {
    if (committed) {
        return true; // Déjà committé
    }
    
    if (!LittleFS.exists(tempPath.c_str())) {
        lastError = "Aucun fichier temporaire à committer";
        return false;
    }
    
    // Corruption LittleFS - Étape 2 : remplacement atomique en UN SEUL rename.
    // lfs_rename remplace la destination atomiquement : X existe à tout instant
    // (ancienne ou nouvelle version). Plus de fenêtre où X est absent.
    if (!LittleFS.rename(tempPath.c_str(), originalPath.c_str())) {
        // Repli : ancien comportement (backup puis rename) si la plateforme
        // refuse le rename direct. Journalisé pour le détecter au banc.
        Serial.printf("AtomicFileWriter: ⚠️ rename direct refusé pour %s, repli backup+rename\n",
                      originalPath.c_str());

        // Créer un backup de l'original si existant
        if (LittleFS.exists(originalPath.c_str())) {
            if (!createBackup()) {
                lastError = "Échec création backup";
                return false;
            }
        }

        // Opération atomique : renommer le fichier temporaire
        if (!LittleFS.rename(tempPath.c_str(), originalPath.c_str())) {
            lastError = "Échec commit atomique";
            Serial.printf("ERREUR: %s - %s -> %s\n",
                         lastError.c_str(), tempPath.c_str(), originalPath.c_str());

            // Tentative de restauration du backup
            restoreBackup();
            return false;
        }
    }
    
    committed = true;
    
    // Nettoyer le backup après succès
    if (LittleFS.exists(backupPath.c_str())) {
        LittleFS.remove(backupPath.c_str());
    }
    
    //Serial.printf("AtomicFileWriter: Commit réussi - %s\n", originalPath.c_str());
    return true;
}

bool AtomicFileWriter::verifyFileIntegrity(size_t expectedSize) {
    File verifyFile = LittleFS.open(tempPath.c_str(), FILE_READ);
    if (!verifyFile) {
        Serial.println("ERREUR: Impossible d'ouvrir fichier pour vérification");
        return false;
    }
    
    size_t actualSize = verifyFile.size();
    verifyFile.close();
    
    if (actualSize != expectedSize) {
        Serial.printf("ERREUR: Taille incorrecte %zu vs %zu bytes\n", actualSize, expectedSize);
        return false;
    }
    
    return true;
}

bool AtomicFileWriter::createBackup() {
    // Supprimer un ancien backup s'il existe
    if (LittleFS.exists(backupPath.c_str())) {
        if (!LittleFS.remove(backupPath.c_str())) {
            Serial.printf("ATTENTION: Impossible de supprimer ancien backup\n");
        }
    }
    
    // Créer le backup
    if (!LittleFS.rename(originalPath.c_str(), backupPath.c_str())) {
        Serial.printf("ERREUR: Impossible de créer backup %s\n", backupPath.c_str());
        return false;
    }
    
    //Serial.printf("Backup créé: %s\n", backupPath.c_str());
    return true;
}

bool AtomicFileWriter::restoreBackup() {
    if (!LittleFS.exists(backupPath.c_str())) {
        Serial.println("Pas de backup à restaurer");
        return false;
    }
    
    if (LittleFS.rename(backupPath.c_str(), originalPath.c_str())) {
        Serial.printf("Backup restauré: %s\n", originalPath.c_str());
        return true;
    }
    
    Serial.println("ERREUR: Échec restauration backup");
    return false;
}

void AtomicFileWriter::cleanup() {
    // Supprimer le fichier temporaire
    if (LittleFS.exists(tempPath.c_str())) {
        LittleFS.remove(tempPath.c_str());
        Serial.printf("Fichier temporaire supprimé: %s\n", tempPath.c_str());
    }
    
    // En cas d'échec, on garde le backup pour investigation manuelle
    if (!committed && LittleFS.exists(backupPath.c_str())) {
        Serial.printf("Backup conservé pour investigation: %s\n", backupPath.c_str());
    }
}
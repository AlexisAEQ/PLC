#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include "PLC_Tools/PLC_Tools.h"

/**
 * @brief Classe utilitaire pour l'écriture atomique de fichiers
 * 
 * Implémente le pattern RAII (Resource Acquisition Is Initialization)
 * pour garantir l'intégrité des opérations de fichier.
 * 
 * Usage:
 * {
 *     AtomicFileWriter writer("/config.json");
 *     if (writer.writeData(data, size)) {
 *         writer.commit(); // Atomique
 *     }
 *     // Rollback automatique si pas de commit()
 * }
 */
class AtomicFileWriter {
public:
    /**
     * @brief Constructeur - Prépare l'écriture atomique
     * @param path Chemin du fichier à écrire
     */
    explicit AtomicFileWriter(const String& path);
    
    /**
     * @brief Destructeur - Nettoyage automatique si pas de commit
     */
    ~AtomicFileWriter();
    
    /**
     * @brief Écrit les données dans le fichier temporaire
     * @param data Pointeur vers les données
     * @param size Taille des données
     * @return true si succès, false sinon
     */
    bool writeData(const char* data, size_t size);
    
    /**
     * @brief Valide l'écriture et remplace le fichier original
     * @return true si succès, false sinon
     */
    bool commit();
    
    /**
     * @brief Vérifie si l'écriture a été committée
     * @return true si committé, false sinon
     */
    bool isCommitted() const { return committed; }
    
    /**
     * @brief Obtient le dernier message d'erreur
     * @return Message d'erreur ou chaîne vide
     */
    const String& getLastError() const { return lastError; }

private:
    String originalPath;      ///< Chemin du fichier original
    String tempPath;          ///< Chemin du fichier temporaire
    String backupPath;        ///< Chemin du fichier de backup
    bool committed;           ///< État du commit
    String lastError;         ///< Dernier message d'erreur
    
    /**
     * @brief Vérifie l'intégrité du fichier temporaire
     * @param expectedSize Taille attendue
     * @return true si intègre, false sinon
     */
    bool verifyFileIntegrity(size_t expectedSize);
    
    /**
     * @brief Crée un backup du fichier original
     * @return true si succès, false sinon
     */
    bool createBackup();
    
    /**
     * @brief Restaure le backup en cas d'échec
     * @return true si succès, false sinon
     */
    bool restoreBackup();
    
    /**
     * @brief Nettoie les fichiers temporaires
     */
    void cleanup();
    
    // Empêcher la copie (RAII pattern)
    AtomicFileWriter(const AtomicFileWriter&) = delete;
    AtomicFileWriter& operator=(const AtomicFileWriter&) = delete;
};
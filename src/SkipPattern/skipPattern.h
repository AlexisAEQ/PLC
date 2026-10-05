// ========================================
// FICHIER: skipPattern.h (version simple)
// ========================================
#pragma once
#include <vector>
#include <Arduino.h>

/**
 * @brief Liste simple des patterns de produits à ignorer
 * 
 * Pas de singleton, pas d'état - juste une liste statique
 * et une fonction pour vérifier si un produit doit être ignoré.
 */
class SkipPattern {
public:
    // Liste des patterns à ignorer
    static const std::vector<String> PATTERNS;
    
    /**
     * @brief Vérifie si un numéro de produit doit être ignoré
     * @param productNumber Numéro de produit à vérifier
     * @return true si le produit doit être ignoré, false sinon
     */
    static bool shouldSkip(const String& productNumber) {
        // Si la liste est vide, ne rien ignorer
        if (PATTERNS.empty()) return false;
        
        // Recherche exacte dans la liste
        for (const String& pattern : PATTERNS) {
            if (productNumber == pattern) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Retourne le nombre de patterns configurés
     */
    static size_t getPatternCount() { 
        return PATTERNS.size(); 
    }
};



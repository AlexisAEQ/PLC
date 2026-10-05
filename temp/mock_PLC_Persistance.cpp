// test/test_transition/mock_PLC_Persistence.cpp
#include "../test/test_transition/mock_PLC_Persistance.h"

// Implémentation du singleton
PLC_Persistence& PLC_Persistence::getInstance() {
    static PLC_Persistence instance;
    return instance;
}

// Implémentations des méthodes mockées
bool PLC_Persistence::saveToFile(const char* filename, const String& content) {
    // Mock : simule une sauvegarde réussie
    #ifdef DEBUG_MOCK
    Serial.printf("[MOCK] saveToFile: %s\n", filename);
    #endif
    return true;
}

bool PLC_Persistence::saveJsonToFile(const char* filename, const JsonDocument& doc) {
    // Mock : simule une sauvegarde JSON réussie
    #ifdef DEBUG_MOCK
    Serial.printf("[MOCK] saveJsonToFile: %s\n", filename);
    #endif
    return true;
}

bool PLC_Persistence::readFile(const char* filename, String& content) {
    // Mock : retourne un contenu vide
    #ifdef DEBUG_MOCK
    Serial.printf("[MOCK] readFile: %s\n", filename);
    #endif
    content = "";
    return false;  // Simule qu'aucun fichier n'existe
}

bool PLC_Persistence::readJsonFromFile(const char* filename, SafeJsonDocument& doc) {
    // Mock : retourne un document JSON vide (surcharge pour SafeJsonDocument)
    #ifdef DEBUG_MOCK
    Serial.printf("[MOCK] readJsonFromFile (Safe): %s\n", filename);
    #endif
    doc.clear();
    return false;  // Simule qu'aucun fichier n'existe
}

bool PLC_Persistence::fileExists(const char* filename) {
    // Mock : aucun fichier n'existe
    #ifdef DEBUG_MOCK
    Serial.printf("[MOCK] fileExists: %s -> false\n", filename);
    #endif
    return false;
}

bool PLC_Persistence::deleteFile(const char* filename) {
    // Mock : simule une suppression réussie
    #ifdef DEBUG_MOCK
    Serial.printf("[MOCK] deleteFile: %s\n", filename);
    #endif
    return true;
}

bool PLC_Persistence::appendToFile(const char* filename, const String& content) {
    // Mock : simule un append réussi
    #ifdef DEBUG_MOCK
    Serial.printf("[MOCK] appendToFile: %s\n", filename);
    #endif
    return true;
}

size_t PLC_Persistence::getFileSize(const char* filename) {
    // Mock : retourne 0 (fichier inexistant)
    #ifdef DEBUG_MOCK
    Serial.printf("[MOCK] getFileSize: %s -> 0\n", filename);
    #endif
    return 0;
}

bool PLC_Persistence::createDirectory(const char* path) {
    // Mock : simule création réussie
    #ifdef DEBUG_MOCK
    Serial.printf("[MOCK] createDirectory: %s\n", path);
    #endif
    return true;
}

bool PLC_Persistence::directoryExists(const char* path) {
    // Mock : aucun répertoire n'existe
    #ifdef DEBUG_MOCK
    Serial.printf("[MOCK] directoryExists: %s -> false\n", path);
    #endif
    return false;
}

const  char *PLC_Persistence::getLastError() const{
    return "No error (mock)";  // Toujours pas d'erreur dans le mock
}
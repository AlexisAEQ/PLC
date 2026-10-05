// test/test_transition/mock_PLC_Persistence.h
#ifndef PLC_PERSISTENCE_H
#define PLC_PERSISTENCE_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Typedef pour compatibilité
typedef JsonDocument SafeJsonDocument;

class PLC_Persistence {
private:
    PLC_Persistence() {}
    
public:
    static PLC_Persistence& getInstance();
    
    // Méthodes principales de PLC_Persistence
    bool saveToFile(const char* filename, const String& content);
    bool saveJsonToFile(const char* filename, const JsonDocument& doc);
    bool readFile(const char* filename, String& content);
    bool readJsonFromFile(const char* path, SafeJsonDocument& outDoc);
    bool fileExists(const char* filename);
    bool deleteFile(const char* filename);
    bool appendToFile(const char* filename, const String& content);
    
    // Méthodes supplémentaires si nécessaires
    size_t getFileSize(const char* filename);
    bool createDirectory(const char* path);
    bool directoryExists(const char* path);
    const char* getLastError() const;
};

#endif // PLC_PERSISTENCE_H
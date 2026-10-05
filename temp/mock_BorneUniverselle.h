// test/test_transition/mock_BorneUniverselle_complete.h
#ifndef BORNE_UNIVERSELLE_LIB_H
#define BORNE_UNIVERSELLE_LIB_H  // Empêche l'inclusion du vrai header

#pragma message("Mock BorneUniverselle included!")

#include <Arduino.h>
#include <unity.h>

#define NOTIFY_STATES_CHANGED    "states"
#define DESCRIPTOR      "descriptor"
#define HASH            "hash"

// Constantes pour les niveaux de message
#define ERROR 0
#define WARNING 1
#define INFO 2

class Node;  // Forward declaration

// Mock complet qui remplace BorneUniverselle
class BorneUniverselle {
private:
    BorneUniverselle() {}
    
public:
    static BorneUniverselle* getInstance();
    
    // Méthodes mockées nécessaires pour la compilation
     bool sendTextToClientImpl(const char *text);

    static bool sendTextToClient(const char *text);

    void setPlcBroken(const char* message);
    bool isPlcBroken() ;
    
    Node* findNode(const char* context, uint32_t hash);
    
    static void prepareMessage(unsigned char type, const char* message);
    
    static void refresHardwareInputs();
    static void clearAllClientNotifications();

    bool getLogOnDiagnosticFile();

    void setInitialStateLoadedCallback(std::function<void()> callback);


    bool plcBroken = false;
    bool logOnDiagnosticFile = false;

    static void enableInterface(bool status);
};

#endif // BORNE_UNIVERSELLE_LIB_H
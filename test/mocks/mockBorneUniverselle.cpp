// test/mocks/MockBorneUniverselle.cpp
// Mock de BorneUniverselle pour les tests unitaires
// Ce fichier remplace BorneUniverselle dans l'environnement de test
/*
#include <cstddef>
#include <map>
#include <string>

// Mock minimal de Node
class Node {
public:
    virtual ~Node() = default;
};

// Mock de BorneUniverselle
namespace BorneUniverselle {
    
    // Variables statiques pour simuler l'état
    static bool plcBroken = false;
    static std::map<std::string, Node*> mockNodes;
    
    // Mock de getInstance - retourne un pointeur bidon
    void* getInstance() {
        static int dummyInstance;
        return &dummyInstance;
    }
    
    // Mock de setPlcBroken
    void setPlcBroken(const char* error) {
        plcBroken = true;
        // Pour debug si nécessaire
        // Serial.printf("PLC Broken: %s\n", error);
    }
    
    // Mock de prepareMessage
    void prepareMessage(unsigned char type, const char* message) {
        // Ne rien faire dans les tests
        // Ou logger si nécessaire pour debug
    }
    
    // Mock de refresHardwareInputs  
    void refresHardwareInputs() {
        // Ne rien faire dans les tests
    }
    
    // Mock de findNode
    Node* findNode(const char* context, const char* name) {
        // Retourner nullptr ou un mock selon les besoins
        return nullptr;
    }
    
    // Mock de isPlcBroken
    bool isPlcBroken() {
        return plcBroken;
    }
    
    // Fonction pour réinitialiser l'état dans les tests
    void resetMockState() {
        plcBroken = false;
        mockNodes.clear();
    }
    
    // Ajoutez ici d'autres méthodes si nécessaire
    // Par exemple si Formaca2 appelle d'autres méthodes :
    
    void notifyWebClient(bool force = false) {
        // Mock - ne rien faire
    }
    
    bool getWifiStatus() {
        return true; // Toujours connecté dans les tests
    }
}   
    
*/
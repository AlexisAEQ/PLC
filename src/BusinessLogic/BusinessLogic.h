#ifndef BUSINESS_LOGIC_H
#define BUSINESS_LOGIC_H

#include "Node/Node.h"
#include "PLC_Persistence/PLC_Persistence.h"
#include <vector>
#include <functional>

// FORWARD DECLARATIONS - évite l'inclusion circulaire
class BorneUniverselle;
class Node;

struct NodeInfo {
    uint32_t hash;
    void** ptr;
    int ClassType;          // Type configuré dans config.json
    int declaredClassType;  // Type déclaré dans Formaca.h
};

/**
 * Structure pour tracker la dernière sauvegarde
 */
struct LastSaveInfo {
    char path[128];              // Chemin du dernier fichier sauvegardé
    unsigned long timestamp;     // Timestamp de la sauvegarde (millis())
    
    LastSaveInfo() : timestamp(0) {
        path[0] = '\0';
    }
};

/**
 * @brief Classe de base pour la logique métier avec gestion d'état
 * 
 * Cette classe implémente le State Pattern et fournit une base commune
 * pour toutes les logiques métier avec gestion d'états.
 * 
 * Design Patterns utilisés:
 * - State Pattern: Gestion des états et transitions
 * - Template Method: printCurrentState utilise stateToString (virtuelle)
 * - Strategy Pattern: Les classes dérivées implémentent leur propre logique d'état
 */
class BusinessLogic {
public:
    /**
     * @brief États de base obligatoires pour toutes les machines
     * 
     * Ces états sont réservés et doivent être présents dans
     * toutes les machines d'état dérivées
     */
    enum class BaseState : int {
        UNDEFINED = 0,      // État initial/invalide
        INITIALIZING = 1,   // Phase d'initialisation
        STOP = 2,           // Machine arrêtée
        IDLE = 3,           // Machine en attente (état de repos)
        EMERGENCY = 4,      // Arrêt d'urgence
        JOGGING = 5,             // Mode manuel (optionnel, mais souvent utilisé)
        
        // Les valeurs >= 100 sont disponibles pour les états spécifiques
        CUSTOM_STATES_START = 100
    };

    /**
     * @brief Types d'états pour classification automatique
     * 
     * Permet de catégoriser les états selon leur nature et comportement.
     * Migré depuis Formaca2 pour une utilisation plus générale.
     */
    enum class StateType {
        STABLE,      // États stables du cycle principal
        UTILITY,     // États utilitaires temporaires
        EMERGENCY,   // États d'urgence
        TRANSIENT    // États de transition courte
    };

      /**
     * @brief Catégories de contextes de transition
     * 
     * Permet de classifier les raisons d'une transition d'état
     * pour améliorer le suivi et le diagnostic.
     */
    enum class ContextCategory : uint8_t {
        NORMAL = 0,      // Transitions normales du workflow
        UTILITY = 1,     // Opérations utilitaires (déplacements, calibration)
        EMERGENCY = 2,   // Situations d'urgence et erreurs critiques
        USER = 3,        // Actions déclenchées par l'utilisateur
        SYSTEM = 4,      // Événements système (timeouts, complétion)
        SAFETY = 5,      // Événements liés à la sécurité
        UNKNOWN = 255    // Contexte non classifié
    };



    /**
     * @brief Convertit un contexte en chaîne de caractères
     * @param context Le code contexte
     * @return Représentation textuelle du contexte
     */
    virtual const char* contextToString(uint32_t context) const;
    
    /**
     * @brief Classifie un contexte dans une catégorie
     * @param context Le code contexte
     * @return La catégorie du contexte
     */
    virtual ContextCategory classifyContext(uint32_t context) const;

    template<typename T>
    bool initNodePtr(T*& ptr, uint32_t hash, const char* context);
    static int getClassTypeFromTypeInfo(const std::type_info& typeInfo);

    /**
     * @brief Enregistre une callback appelée lors des transitions
     * @param callback Fonction appelée avec (nouvelÉtat, ancienÉtat)
     * 
     * Observer Pattern: Permet à d'autres composants d'être notifiés
     * des changements d'état sans couplage fort
     */
    void registerTransitionCallback(std::function<void(int, int)> callback) {
        transitionCallbacks.push_back(callback);
    }

    virtual const char* getContextName(uint32_t context) const;

    // ========================================
    // STRUCTURE D'HISTORIQUE GÉNÉRIQUE
    // ========================================
    
    /**
     * @brief Structure générique pour l'historique des états
     * 
     * Utilise des types génériques (int) pour permettre la réutilisation
     * par différentes classes dérivées avec leurs propres enums d'états
     */
    struct BaseStateHistoryEntry {
        int state;           // État générique (int pour flexibilité)
        StateType type;      // Type d'état (commun à toutes les machines)
        uint32_t timestamp;  // Timestamp en millisecondes
        uint32_t context;    // Contexte générique (uint32_t pour flexibilité)
        
       BaseStateHistoryEntry(int newState, StateType newType, uint32_t newTimestamp, uint32_t newContext = 0)
         : state(newState), type(newType), timestamp(newTimestamp), context(newContext) 
         {}
    };

    virtual ~BusinessLogic() = default;

    virtual bool logicExecutor(); // le nerf de la gueurre

    virtual bool doBusinessLogic() = 0;

    /**
    * @brief Appelé lorsque l'utilisateur change d'onglet dans l'interface
    * @param tabName Nom du nouvel onglet actif
    * 
    * Les classes dérivées peuvent override cette méthode pour réagir
    * aux changements d'onglet (ex: suspendre certaines opérations)
    */
    virtual void onTabChange(const char* tabName);
    
    // ========================================
    // STATE MANAGEMENT (Template Method Pattern)
    // ========================================
    
    /**
     * @brief Effectue une transition vers un nouvel état
     * @param newState Le nouvel état (type int pour flexibilité)
     * 
     * Template Method Pattern: La logique de base est dans BusinessLogic,
     * les spécificités sont dans les classes dérivées via onStateEnter/onStateExit
     */
    virtual void transition(int newState);
    
    /**
     * @brief Convertit un état en chaîne de caractères
     * @param state L'état à convertir
     * @return Représentation textuelle de l'état
     * 
     * Méthode virtuelle pure - chaque classe dérivée doit implémenter
     * sa propre représentation des états
     */
    virtual const char* stateToString(int state) const = 0; 

    virtual const char* getStateDescription(int state) const {
        return nullptr;  // Pas de description par défaut
    }
    
    /**
     * @brief Affiche l'état actuel (une seule fois par état)
     * 
     * Template Method Pattern: Utilise stateToString() pour l'affichage
     */
    virtual void printCurrentState();
    
    /**
     * @brief Réinitialise le flag d'impression d'état
     * Utile lors des transitions pour forcer l'affichage du nouvel état
     */
    void resetStatePrintFlag() { stateAlreadyPrinted = false; }
    
    // ========================================
    // GETTERS POUR L'ÉTAT
    // ========================================
    int getCurrentState() const {
         return currentState;
    }

    int getPreviousState() const {
        return previousState;
    }

    void requestRestart(uint32_t const timeBeforRestartMs = 5000, char const* reason = nullptr);

    uint32_t getStateStartTime();
    uint32_t getTimeInCurrentState();
    
    // ========================================
    // UTILITAIRES POUR LES TYPES DE NODES
    // ========================================
    static std::string getTypeName(int classType);

       // ========================================
    // UTILITAIRES DE GESTION D'ÉTAT
    // ===========================================
    
    /**
     * @brief Enregistre une transition d'état avec timestamp
     * @param reason Raison de la transition
     * 
     * Observer Pattern: Peut être étendu pour notifier des observateurs
     */
    void recordTransition(const char* reason = nullptr) {
        logStateTransition(previousState, currentState, reason);
        
        // Possibilité d'ajouter un historique des transitions
        // transitionHistory.push_back({previousState, currentState, millis(), reason});
    }

    /**
     * @brief Vérifie si la machine est en IDLE
     * Implémentation concrète - toutes les machines utilisent BaseState::IDLE
     */
    bool isIdle() const;
    
    /**
     * @brief Vérifie si la machine est en EMERGENCY
     */
    bool isInEmergency() const {
        return currentState == static_cast<int>(BaseState::EMERGENCY);
    }
    
    /**
     * @brief Vérifie si la machine est en STOP
     */
    bool isStopped() const {
        return currentState == static_cast<int>(BaseState::STOP);
    }
    
    /**
     * @brief Vérifie si la machine est en INITIALIZING
     */
    bool isInitializing() const {
        return currentState == static_cast<int>(BaseState::INITIALIZING);
    }

     // ========================================
    // CALLBACK TYPE POUR SUSPENSION
    // ========================================
    
    /**
     * @brief Type de callback appelé lors d'un changement d'état de suspension
     * @param suspend true pour suspendre, false pour reprendre
     * @param accepted true si demande acceptée, false si refusée
     * @param reason Raison du refus (nullptr si accepté)
     */
    using SuspendCallback = std::function<void(bool suspend, bool accepted, const char* reason)>;
    /**
     * @brief Enregistre le callback pour les événements de suspension
     */
    void setSuspendCallback(SuspendCallback callback) {
        suspendCallback = callback;
    }

    /**
     * @brief Demande la suspension/reprise de la machine
     * @param suspend true pour suspendre, false pour reprendre
     * 
     * Cette méthode vérifie les conditions et notifie via callback.
     * Elle NE GÈRE PAS l'état - c'est BorneUniverselle qui le fait.
     */
    void handleSuspendRequest(bool suspend);

    void handleIsPLCIdleRequest() const;

    virtual void onFileSaved(const char* path, bool success);

    virtual bool validateConfigFile(const char* path, const JsonDocument& doc, String& errorMsg);

    virtual bool canDeleteFile(const char* path) = 0;

protected:

     // ========================================
    // GESTION DE L'HISTORIQUE DES ÉTATS
    // ========================================
    std::vector<BaseStateHistoryEntry> baseStateHistory;
    
    /**
     * @brief Taille maximale de l'historique
     */
    static constexpr size_t DEFAULT_MAX_HISTORY_SIZE = 10;
    
    /**
     * @brief Ajoute une entrée à l'historique
     * @param state L'état à enregistrer
     * @param type Le type de l'état
     * @param context Contexte optionnel de la transition
     */
    void addToBaseHistory(int state, StateType type, uint32_t context = 0);
    
    /**
     * @brief Trouve le dernier état d'un type donné dans l'historique
     * @param targetType Le type d'état recherché
     * @return L'état trouvé ou -1 si non trouvé
     */
    int findLastStateOfTypeInHistory(StateType targetType) const;
    
    /**
     * @brief Vide l'historique
     */
    void clearStateHistory();
    
    /**
     * @brief Obtient la taille actuelle de l'historique
     * @return Nombre d'entrées dans l'historique
     */
    size_t getHistorySize() const;
    
    /**
     * @brief Affiche l'historique (peut être override par les classes dérivées)
     */
    virtual void printBaseStateHistory() const;

    /**
     * @brief Convertit un StateType en chaîne de caractères
     * @param type Le type d'état à convertir
     * @return Représentation textuelle du type
     */
    static const char* stateTypeToString(StateType type);

    void handleExecutorFailure();
    // ========================================
    // VARIABLES D'ÉTAT PROTÉGÉES
    // ========================================
    int currentState = 0;           // État actuel (int pour flexibilité)
    int previousState = 0;          // État précédent
    uint32_t stateStartTime = 0;    // Timestamp du début de l'état actuel
    bool stateAlreadyPrinted = false; // Flag pour éviter l'affichage répétitif

    bool isStateTimeoutExceeded(uint32_t timeoutMs) const;
    void logStateTransition(int fromState, int toState, const char* reason = nullptr);
    bool hasBeenInStateFor(uint32_t durationMs) const {
        return stateStartTime > 0 && (millis() - stateStartTime >= durationMs);
    }
    bool transitionWithTimeout(int newState, uint32_t currentTimeoutMs = 0, const char* reason = nullptr);
    
    // ========================================
    // HOOKS VIRTUELS POUR LES CLASSES DÉRIVÉES (Strategy Pattern)
    // ========================================
    
    /**
     * @brief Hook appelé lors de l'entrée dans un nouvel état
     * @param newState Le nouvel état
     * @param oldState L'ancien état
     * 
     * Les classes dérivées peuvent override cette méthode pour
     * effectuer des actions spécifiques lors des transitions
     */
    virtual void onStateEnter(int newState, int oldState) {}
    
    /**
     * @brief Hook appelé lors de la sortie d'un état
     * @param oldState L'état que l'on quitte
     * @param newState Le nouvel état
     */
    virtual void onStateExit(int oldState, int newState) {}
    
    /**
     * @brief Hook appelé après qu'une transition soit complète
     * @param newState Le nouvel état (maintenant actuel)
     * @param oldState L'ancien état
     */
    virtual void onTransitionComplete(int newState, int oldState) {}

    /**
     * @brief Détecte automatiquement le contexte d'une transition
     * @param oldState État de départ
     * @param newState État d'arrivée
     * @param oldType Type de l'état de départ
     * @param newType Type de l'état d'arrivée
     * @return Contexte détecté (uint32_t pour flexibilité)
     */
    virtual uint32_t detectTransitionContext(
        int oldState, 
        int newState,
        StateType oldType,
        StateType newType) const;
    
    /**
     * @brief Effectue une transition avec contexte
     * @param newState Nouvel état
     * @param context Contexte de transition (0 = auto-detect)
     */
    void transitionWithContext(int newState, uint32_t context = 0);

    /**
     * @brief Classifie un état selon son type
     * @param state État à classifier
     * @return Type de l'état
     * 
     * Méthode virtuelle pure - chaque machine d'état
     * doit implémenter sa propre classification
     */
    virtual StateType classifyState(int state) const = 0;
    
    Node* getNodePointerForHash(uint32_t hash, const char* context);
    
    template<typename ExpectedType>
    ExpectedType* getTypedPointerForHash(uint32_t hash, const char* context);

    /**
     * @brief Hook appelé lors d'une suspension acceptée
     * Les classes dérivées peuvent override pour actions spécifiques
     */
    virtual void onSuspendRequested() {
        // Implémentation par défaut : rien
    }
    
    /**
     * @brief Hook appelé lors d'une reprise
     */
    virtual void onResumeRequested() {
        // Implémentation par défaut : rien
    } 
    
    /**
     * @brief Hook appelé avant le redémarrage
     * Les classes dérivées peuvent override pour cleanup spécifique
     */
    virtual void onBeforeRestart() {}

     // Méthode de filtrage
    bool shouldProcessFileCallback(const char* path);  // permet de filtrer les traitements dans on FileSaved de la classe métier. Si c'est la classe métier l'origine de l'enregistrement du fichier on ne veux pas que la callback onFileSaved traite le fichier une seconde fois.

    void markInternalSave(const char *path);

    /**
     * Met à jour l'horloge uniquement dans les états appropriés
     * @param clockNode Le Node d'affichage de l'horloge
     */
    void updateClockNode(VirtualTextOutputNode* clockNode);
    
    /**
     * Hook method : détermine dans quels états l'horloge doit être affichée
     * Par défaut : IDLE, EMERGENCY, STOP
     * Les classes dérivées peuvent override si logique différente
     */
    virtual bool shouldShowClock() const = 0;  // Méthode pure virtuelle

private:
    // ========================================
    // CALLBACKS DE TRANSITION (Observer Pattern)
    // ========================================
    std::vector<std::function<void(int, int)>> transitionCallbacks;
    
    // MÉTHODE PRIVÉE POUR L'IMPLÉMENTATION DU TEMPLATE
    //void* getTypedPointerForHashImpl(uint32_t hash, const char* context, const std::type_info& typeInfo);
    

    /**
     * @brief Notifie tous les observateurs d'une transition
     */
    void notifyTransitionObservers(int newState, int oldState);

    SuspendCallback suspendCallback = nullptr;

    bool restartPending = false;
    uint32_t restartRequestedAt = 0;
    uint32_t restartDelayMs = 0;
    static constexpr uint32_t RESTART_GRACE_PERIOD_MS = 2000; // 2 secondes pour shutdown propre

    LastSaveInfo lastSave;

    // Délai de 20 secondes pour le debounce
    static constexpr unsigned long SAVE_CALLBACK_DEBOUNCE_MS = 5000;

    VirtualTextOutputNode* clockNode = nullptr;
    void clearClock();
};

#endif



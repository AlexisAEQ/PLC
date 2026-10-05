#pragma once

// ========================================
// INCLUDES ET DÉPENDANCES
// ========================================
#include "ArduinoJson.h"
#include <memory>
#include "Node/Node.h"
#include "PLC_Persistence/PLC_Persistence.h"
#include "RenderTools/RenderTools.h"
#include "PLC_CommonTypes/PLC_CommonTypes.h"
#include "BusinessLogic/BusinessLogic.h"
#include "PLC_Tools/PLC_Tools.h"
#include "BusinessLogic/BusinessLogic.inl"
#include "SureServo/SureServo.h"

// ========================================
// UTILITAIRES DE COMPILATION
// ========================================
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// ========================================
// CONFIGURATION ET DÉLAIS
// ========================================
#define DELAY_INIT                  100     // 100ms
#define CONSTR_RESSORT_ROYAL2      "Constructeur class RessortRoyal2"
#define CONFIG_FILE_NAME           "/RessortRoyal2.json"

// ========================================
// IDENTIFIANTS DE NŒUDS (HASH CODES)
// ========================================

// Nœuds de contrôle principal
#define BUZZER                     1711197005
#define ESTOPA                     721063196
#define START_CYCLE                2693195903

// Nœuds de sortie Kincony
#define SERVOON                    1927398708
#define TRIGGER_PR                 4235117271
#define IMMEDIATE_STOP             973196112
#define ALARMS_RESET               4138453966
#define FREE_RUN                   2015677485
#define GO_AND_BACK                1078009035


// Nœuds JOG
#define JOG                        627952477
#define FWD                        936928435
#define RWD                        2405696470
#define FULL_TORQUE                313736047

// Nœuds de status servo
#define SERVOREADY                 1578545169
#define SERVOACTIVATED             1878960780
#define ZEROSPEED                  3381240120
#define TARGETSPEEDRATED           204382134
#define ATTARGET                   2857997314
#define SERVOALARM                 1036420395
#define ABSOLUTE_POS_LOST          1845100150
#define BATTERYALARM               1229514833
#define MULTIPLETURNSOVERFLOW      4013941733
#define PUUOVERFLOW                3738696056
#define ABSOLUTECOORDONATENOTSET   2023813836
#define HOMEDONE                   3171404866
#define MODBUSERROR                460376054
#define DRIVEINITIALISED           714661227

// Nœuds d'entrée utilisateur (paramètres)
#define USER_TARGET                645363011
#define RETURN_POS                 396324830
#define VJOG_SPEED                 2984797290
#define VCYCLE_SPEED               1951357668
#define NUMBER_OF_CYCLES_TO_DO     3526906192
#define NUMBER_OF_CYCLES_MADE      3822078925
#define NB_CYCLES_CLEAR            3294789918
#define V_TORQUE                   3018797200

// Nœuds Modbus read holding register
#define STATUS_DRIVE               4137233651
#define ABSOLUTE_COORDONATE_SYSTEM_STATUS 4098304980
#define ALARMS                     4126332617

// Nœuds Modbus write holding register
#define AUX_FUNCTION               2308213873
#define TRIGGER                    748480285
#define JOG_SPEED                  3187488644
#define MAX_TORQUE                 3740987040

// Nœuds Modbus read double holding register
#define POSITION                   4045003206

// Nœuds Modbus write double holding register
// Nœuds de cibles et vitesses PR
#define TARGET_A                   715974120
#define TARGET_B                   1732568463
#define TARGET_C                   1022630084
#define PR1_SPEED                  3939244141
#define PR2_SPEED                  4237616449
#define PR3_SPEED                  1897666723
#define JOGSPEED                   3187488644
#define AUX_FUNCTIONS              2308213873

#define V_ALARMS_RESET             1726752376
#define V_IMMEDIAT_STOP            2701507160
#define V_POSITION                 1168588921
#define V_SERVO_ON                 623257485

// Nœuds d'interface et affichage
#define STATES_MACHINE             1218981442

#define V_SET_PLC_CLOCK           59448638
#define V_CLOCK                   4007601654

// ========================================
// CLÉS JSON POUR LA CONFIGURATION
// ========================================
#define MACHINE_NAME_KEY           "machine name"
#define DEFAULT_MACHINE_NAME       "RessortRoyal2"
#define USER_TARGET_KEY            "user target"
#define RETURN_POS_KEY             "return position"
#define GO_AND_BACK_KEY           "go and back"
#define JOG_SPEED_KEY              "jog speed"
#define CYCLE_SPEED_KEY            "cycle speed"
#define NUMBER_OF_CYCLES_KEY       "number of cycles to do"
#define NUMBER_OF_CYCLES_MADE_KEY  "number of cycles made"      
#define NB_TOTAL_CYCLES_KEY        "total cycles"            
#define FULL_TORQUE_KEY            "full torque"
#define INITIAL_TORQUE_KEY         "initial torque [%]"

// ========================================
// STRUCTURE PERSISTANT_PARAMETERS
// ========================================
struct PERSISTANT_PARAMETERS {
    // Paramètres machine de base
    char machineName[80] ; // Nom de la machine
    
    // Paramètres de mouvement
    uint32_t userTarget;        // Position cible définie par l'utilisateur
    uint32_t returnPos;         // Position de retour
    bool goAndBack;             // Indique si le mode go and back est activé
    uint16_t jogSpeed;          // Vitesse en mode JOG
    uint16_t cycleSpeed;        // Vitesse en mode cycle
    
    // Paramètres de cycle
    uint32_t numberOfCyclesToDo;   // Nombre de cycles à effectuer
    uint32_t numberOfCyclesMade;   // Nombre de cycles effectués
    uint32_t nbTotalCycles;         // nombre total de cycles touts job confondu
    
    // Paramètres de contrôle
    bool fullTorque;            // Mode couple maximal
    uint16_t initialTorque;     // Couple initial en %
};

/**
 * @brief Classe métier pour RessortRoyal2
 * 
 * Système de test de ressorts avec servo moteur.
 * Fonctionnalités principales :
 * - Mode JOG manuel (avant/arrière)
 * - Mode cycle automatique (aller-retour)
 * - Mode free run
 * - Compteur de cycles
 * - Gestion des alarmes et arrêts d'urgence
 * 
 * Design Patterns utilisés:
 * - State Pattern (hérité de BusinessLogic)
 * - Template Method (initNodePtr)
 * - Observer Pattern (callbacks de transition)
 * - Factory Pattern (création des nœuds)
 */
class RessortRoyal2 : public BusinessLogic {

public:
    // ========================================
    // ÉNUMÉRATION DES ÉTATS
    // ========================================
    
    /**
     * @brief États de la machine RessortRoyal2
     * 
     * Utilise enum class pour un typage fort et éviter les collisions de noms
     */
    enum class State : int {
        // Les états de base hérités de BusinessLogic
        UNDEFINED = static_cast<int>(BaseState::UNDEFINED),
        INITIALIZING = static_cast<int>(BaseState::INITIALIZING),
        STOP = static_cast<int>(BaseState::STOP),
        IDLE = static_cast<int>(BaseState::IDLE),
        EMERGENCY = static_cast<int>(BaseState::EMERGENCY),
        JOGGING = static_cast<int>(BaseState::JOGGING),  // Hérité de BaseState (valeur 5)

        // États spécifiques RessortRoyal2
        HOMING      = 101,
        GOING       = 102,          // un start pour le go et un start pour le back
        CYCLING     = 103           // En cycle automatique (aller-retour)
    };

    // ========================================
    // X MACRO définition de la table des états
    // ========================================
    #define RESSORT_ROYAL2_STATE_TABLE \
        X(UNDEFINED,        TRANSIENT,  "État non défini") \
        X(INITIALIZING,     TRANSIENT,  "Initialisation en cours") \
        X(STOP,             STABLE,     "Machine arrêtée") \
        X(IDLE,             STABLE,     "Attente de commande") \
        X(EMERGENCY,        EMERGENCY,  "Arrêt d'urgence") \
        X(HOMING,           UTILITY,    "Recherche origine") \
        X(JOGGING,          UTILITY,    "Mode JOG manuel") \
        X(GOING,            STABLE,     "Going to target or back") \
        X(CYCLING,          STABLE,     "Go and back automatique")

    /**
     * @brief Obtient le nom d'un état depuis la X-Macro
     */
    static const char* getStateNameFromTable(State state);
    static StateType getStateTypeFromTable(State state);
    
    /**
     * @brief Obtient la catégorie d'un état (TRANSIENT, STABLE, EMERGENCY, UTILITY)
     */
    static const char* getStateCategoryFromTable(State state);
    
    /**
     * @brief Obtient la description d'un état
     */
    static const char* getStateDescriptionFromTable(State state);

    // ========================================
    // CODES DE CONTEXTE POUR LES TRANSITIONS
    // ========================================
    enum class ContextCode : uint8_t {
        // Contextes normaux
        NORMAL = 0,
        AUTO_DETECT = 1,
        BEFORE_UTILITY = 2,
        MOVEMENT_SUCCESS = 3,
        POSITION_REACHED = 4,
        CYCLE_COMPLETE = 5,
        JOG_MODE = 6,
        STOP_MODE = 7,
        
        // Contextes d'urgence
        EMERGENCY_STOP = 10,
        SERVO_ALARM = 11,
        SERVO_NOT_READY = 12,
        EMERGENCY_SERVO_FAULT = 13,      // Echec démarrage mouvement servo)
        
        // Contextes utilisateur
        USER_REQUEST = 20,
        BUTTON_PRESSED = 21,
        
        // Contextes d'erreur
        TIMEOUT_ERROR = 30,
        INIT_FAILED = 31,
        SYSTEM_ERROR = 32,               // Erreur système générique)
        INVALID_PARAMETER = 33           // Paramètre invalide: position=0, etc.)
    };

#define RESSORT_ROYAL2_CONTEXT_TABLE \
    X(NORMAL, NORMAL, "Normal") \
    X(AUTO_DETECT, NORMAL, "Détection automatique") \
    X(BEFORE_UTILITY, UTILITY, "Avant utilitaire") \
    X(MOVEMENT_SUCCESS, UTILITY, "Mouvement réussi") \
    X(POSITION_REACHED, UTILITY, "Position atteinte") \
    X(CYCLE_COMPLETE, SYSTEM, "Cycle complet") \
    X(JOG_MODE, USER, "Mode JOG") \
    X(STOP_MODE, USER, "Mode STOP") \
    X(EMERGENCY_STOP, EMERGENCY, "Arrêt d'urgence") \
    X(SERVO_ALARM, EMERGENCY, "Alarme servo") \
    X(SERVO_NOT_READY, EMERGENCY, "Servo non prêt") \
    X(EMERGENCY_SERVO_FAULT, EMERGENCY, "Défaut servo") \
    X(USER_REQUEST, USER, "Demande utilisateur") \
    X(BUTTON_PRESSED, USER, "Bouton pressé") \
    X(TIMEOUT_ERROR, SYSTEM, "Timeout") \
    X(INIT_FAILED, SYSTEM, "Échec initialisation") \
    X(SYSTEM_ERROR, SYSTEM, "Erreur système") \
    X(INVALID_PARAMETER, SYSTEM, "Paramètre invalide")


    // ========================================
    // CONSTRUCTEUR / DESTRUCTEUR
    // ========================================
    RessortRoyal2();
    virtual ~RessortRoyal2();

    // ========================================
    // MÉTHODES PUBLIQUES
    // ========================================
    
    /**
     * @brief Initialise les pointeurs vers les nœuds
     * @param context Contexte de l'appel (pour debug)
     * @return true si succès
     */
    bool initializePointers(const char* context);



    /**
     * @brief Démarre la machine d'états
     * @return true si succès
     */
    bool startStateMachine();
    
    /**
     * @brief Exécute la logique métier principale (boucle)
     * @return true si succès
     */
    bool doBusinessLogic();

    // ========================================
    // MÉTHODES VIRTUELLES DE BusinessLogic
    // ========================================
    
    /**
     * @brief Convertit un état en chaîne de caractères
     */
    const char* stateToString(int state) const override;  // AVEC const ET override

    /**
     * @brief Handler appelé après le chargement de l'état initial
     */
    void initialStateLoadedHandler();

    /**
     * @brief Valide un fichier de configuration avant sauvegarde
     */
    bool validateConfigFile(const char* path, const JsonDocument& doc, String& errorMsg) override;

    /**
     * @brief Vérifie si un fichier peut être supprimé
     */
    bool canDeleteFile(const char* path);

    void printPersistance();

#ifdef PERF_MONITOR
    bool isPmActive()              const { return sureServo && sureServo->isPmActive(); }
    void pmAccumulateRefresh(uint32_t ms) { if (sureServo) sureServo->pmAccumulateRefresh(ms); }
#endif

protected:
    // ========================================
    // OVERRIDES DE BUSINESSLOGIC
    // ========================================
    
    /**
     * @brief Hook appelé lors de l'entrée dans un nouvel état
     */
    void onStateEnter(int newState, int oldState) override;
    
    /**
     * @brief Hook appelé lors de la sortie d'un état
     */
    void onStateExit(int oldState, int newState) override;
    
    /**
     * @brief Hook appelé après qu'une transition soit complète
     */
    void onTransitionComplete(int newState, int oldState) override;
    
    /**
     * @brief Classifie un état selon son type
     */
    StateType classifyState(int state) const override;
    
    // ========================================
    // MÉTHODES DE CONFIGURATION
    // ========================================
    
    /**
     * @brief Lit le fichier de configuration dédié
     * @return true si succès
     */
    bool readDedicacedConfigFile();
    
    /**
     * @brief Extrait les paramètres machine depuis le JSON
     * @param doc Document JSON contenant la configuration
     * @return true si succès
     */
    bool extractMachineParameters(const JsonDocument& doc);

    /**
     * @brief Sauvegarde tous les paramètres machine dans RessortRoyal2.json
     * @return true si succès
     */
    bool saveMachineParameters();
    
    /**
     * @brief Méthode interne pour sauvegarder la configuration
     * @param doc Document JSON à sauvegarder
     * @return true si succès
     */
    bool saveRessortRoyalConfigInternal(const JsonDocument& doc);
    
    // ========================================
    // CALLBACKS
    // ========================================
    
    /**
     * @brief Callback appelé quand un fichier est sauvegardé
     */
    void onFileSaved(const char* path, bool success) override;

    virtual bool shouldShowClock() const override; // détéermin si on affiche l'heure ou pas

private:
    // ========================================
    // POINTEURS VERS LES NŒUDS
    // ========================================
    
    // === NŒUDS DE CONTRÔLE PRINCIPAL ===
    BooleanOutputNode* buzzer = nullptr;
    BooleanInputNode* eStopA = nullptr;
    BooleanInputNode* startCycle = nullptr;


    // === NŒUDS DE SORTIE KINCONY ===
    BooleanOutputNode* servoOn = nullptr;
    BooleanOutputNode* triggerPR = nullptr;
    BooleanOutputNode* immediateStop = nullptr;
    BooleanOutputNode* alarmsReset = nullptr;
    BooleanOutputNode* freeRun = nullptr;

    // === NŒUDS JOG ===
    VirtualBooleanInputNode* jog = nullptr;
    BooleanInputNode* fwd = nullptr;
    BooleanInputNode* rwd = nullptr;
    VirtualBooleanInputNode* fullTorque = nullptr;

    // === NŒUDS DE STATUS SERVO ===
    BooleanOutputNode* servoReady = nullptr;
    BooleanOutputNode* servoActivated = nullptr;
    BooleanOutputNode* zeroSpeed = nullptr;
    BooleanOutputNode* targetSpeedRated = nullptr;
    BooleanOutputNode* targetPositionReached = nullptr;
    BooleanOutputNode* servoAlarm = nullptr;
    BooleanOutputNode* absolutePositionLost = nullptr;
    BooleanOutputNode* batteryAlarm = nullptr;
    BooleanOutputNode* multipleTurnsOverflow = nullptr;
    BooleanOutputNode* puuOverflow = nullptr;
    BooleanOutputNode* absoluteCoordonateNotSet = nullptr;
    BooleanOutputNode* homeDone = nullptr;
    BooleanOutputNode* modbusError = nullptr;
    BooleanOutputNode* driveInitialised = nullptr;

     // === NŒUDS DE STATUS ET MONITORING ===
    Uint16InputNode* status = nullptr;
    Uint16InputNode* absoluteCoordonateSystemStatus = nullptr;
    Uint16InputNode* alarms = nullptr;
    Uint16OutputNode* auxFunctions = nullptr;
    Uint16OutputNode* trigger = nullptr;

    // === NŒUDS D'ENTRÉE UTILISATEUR ===
    VirtualUint32OutputNode* userTarget = nullptr;
    VirtualUint32OutputNode* returnPos = nullptr;
    VirtualUint32OutputNode* v_jogSpeed = nullptr;
    VirtualUint32OutputNode* v_cycleSpeed = nullptr;
    VirtualUint32OutputNode* v_position = nullptr;
    VirtualUint32OutputNode* numberOfCyclesToDo = nullptr;
    VirtualUint32OutputNode* numberOfCyclesMade = nullptr;
    VirtualUint32OutputNode* v_torque = nullptr;
    VirtualBooleanInputNode* nbCyclesClear = nullptr;
    VirtualBooleanInputNode* v_servoOn = nullptr;

    // === NŒUDS MODBUS WRITE HOLDING REGISTER ===
    Uint16OutputNode* jogSpeed = nullptr;
    Uint16OutputNode* maxTorque = nullptr;

    // === NŒUDS MODBUS READ DOUBLE HOLDING REGISTER ===
    Uint32InputNode* position = nullptr;
    Uint32InputNode* torque = nullptr;

    // === NŒUDS MODBUS WRITE DOUBLE HOLDING REGISTER ===
    Uint32OutputNode* targetGo = nullptr;
    Uint32OutputNode* targetBack = nullptr;
    Uint32OutputNode* pr1Speed = nullptr;
    Uint32OutputNode* pr2Speed = nullptr;
    Uint32OutputNode* pr3Speed = nullptr;

    Uint32OutputNode* target_A = nullptr;
    Uint32OutputNode* target_B = nullptr;
    Uint32OutputNode* target_C = nullptr;

    VirtualBooleanInputNode* v_immediateStop = nullptr;
    BooleanInputNode* v_alarmsReset = nullptr;
    VirtualBooleanInputNode* goAndBack = nullptr;
    

    VirtualTextOutputNode* statesMachine = nullptr;

    VirtualBooleanInputNode* v_setPLC_Clock = nullptr;
    EnhancedButton* v_setPLC_Clock_enhenced = nullptr;
    VirtualTextOutputNode* v_clock = nullptr;

    VisualIndicator* visu = nullptr;

    // === SERVO DRIVE ===
    std::unique_ptr<SureServo> sureServo = nullptr;
    SureServo* getSureServo() const { return sureServo.get(); }
    bool driveInitStarted = false;
    bool resetDrive(const char* context);
    bool resetInProgress = false;

    bool initialised = false;

    // ========================================
    // HANDLERS D'ÉTATS - À IMPLÉMENTER
    // ========================================
    bool handleUndefinedState(uint32_t now);
    bool handleInitializingState(uint32_t now);
    bool handleStopState(uint32_t now);
    bool handleIdleState(uint32_t now);
    bool handleEmergencyState(uint32_t now);
    bool handleHomingState(uint32_t now);
    bool handleJoggingState(uint32_t now);
    bool handleFreeRunningState(uint32_t now);
    bool handleCyclingState(uint32_t now);
    bool handleGoingState(uint32_t now);
    bool handleMovementState(uint32_t now);

    void setEmergencyMode(bool status);
    bool canExitEmergency() const;

    // ========================================
    // MÉTHODES UTILITAIRES
    // ========================================

    void saveDriveParameters();

    void jogTreatment();

    /**
     * @brief Gère les conditions d'urgence
     */
    bool checkEmergencyConditions(uint32_t now);

    /**
     * @brief Gère les interruptions
     */
    void handleInterrupts();

    /**
     * @brief Met à jour les sorties
     */
    void updateOutputs();

    /**
     * @brief Traitement de l'interface
     */
    void interfaceTreatment();

    /**
     * @brief Affiche les alarmes et le status
     */
    void displayAlarmsAndStatus();

    /**
     * @brief Obtient l'état courant
     */
    State getCurrentState() const {
        return static_cast<State>(BusinessLogic::getCurrentState());
    }
    
    /**
     * @brief Transition vers un nouvel état avec contexte
     */
    void transitionToState(State newState, ContextCode contextCode = ContextCode::NORMAL);

    /**
     * @brief Configure la vitesse JOG
     */
    bool setJogSpeed(uint32_t speed);

    /**
     * @brief Configure la vitesse de cycle
     */
    void cycleSpeedChange();

    // ========================================
    // GESTION DES FICHIERS DE CONFIGURATION
    // ========================================
    
    /**
     * @brief Gère le rechargement automatique du fichier RessortRoyal2.json
     */
    void handleRessortRoyal2ConfigFileChanged();
    
    /**
     * @brief Applique les paramètres persistants aux nodes
     */
    void applyParametersToNodes();

    // ========================================
    // VARIABLES MEMBRES
    // ========================================
    
    // Objets persistants
    PERSISTANT_PARAMETERS parameters;

    // Variables d'état
    uint32_t cycleCounter = 0;
    bool inCycle = false;
    bool goingPhase = true;  // true = aller, false = retour
    
    // Variables pour emergency
    uint8_t emergencyPhase = 0;
    uint8_t emergencyPhaseCache = 99;
    uint32_t lastEmergencyCheck = 0;
    State emergencyTargetState = State::STOP;
    
    // Variables pour les mouvements
    bool goingMovementStarted = false;
    bool returningMovementStarted = false;
    uint32_t currentTargetPosition = 0;

    static const char* getContextNameFromTable(ContextCode code);
    static const char* getContextDescriptionFromTable(ContextCode code);
    static BusinessLogic::ContextCategory getContextCategoryFromTable(ContextCode code);
    const char* contextToString(uint32_t context) const override;
};
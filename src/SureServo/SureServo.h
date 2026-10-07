#ifndef SURE_SERVO_H
#define SURE_SERVO_H

// ========================================
// OUTILS DE DIAGNOSTIC — commenter pour désactiver
// ========================================
// Macros préfixées SURESERVO_ pour ne pas entrer en collision avec les autres drives
// (LichuanServo, StepperMotor) inclus dans la même unité de compilation.
// ⚠️ Ces macros ne changent PAS la disposition mémoire de la classe SureServo (les membres
// pm_* et les méthodes pm*() existent toujours) : aucune incohérence ODR possible entre
// unités de compilation, quel que soit l'ordre des #include.
#define SURESERVO_PERF_MONITOR          // instrumentation des cycles (pmStartCycle/pmPrintReport)
#define SURESERVO_TRIGGER_SMART_DELAY   // trigger PR dès que l'écriture du target est confirmée

// Compatibilité : RessortRoyal2, main.cpp et le code généré par PLC Studio testent
// PERF_MONITOR pour appeler isPmActive()/pmAccumulateRefresh(). Définie (sans redéfinition)
// tant que l'instrumentation SureServo est active.
#if defined(SURESERVO_PERF_MONITOR) && !defined(PERF_MONITOR)
#define PERF_MONITOR
#endif

#include "Node/Node.h"
#include "ServoDrive/ServoDrive.h"
#include "BorneUniverselle/borneUniverselle.h"

// ========================================
// CONSTANTES SPÉCIFIQUES SURESERVO
// ========================================
// Toutes les constantes du drive sont regroupées dans le namespace sureservo (anciennement
// macros / constantes globales) pour éviter les collisions de symboles avec les en-têtes des
// autres drives. Le code applicatif peut les utiliser qualifiées : sureservo::TOLERANCE_PUU.
//
// Note : les anciennes macros DRIVE_INIT_TIMEOUT et DELAY_INIT (inutilisées par SureServo)
// ont été supprimées ; RessortRoyal2.h définit son propre DELAY_INIT.
namespace sureservo {

// Timeout homing par défaut (ex-macro HOMING_TIMEOUT_MS)
constexpr uint32_t HOMING_TIMEOUT_MS = 100000;   // 100 secondes

// Valeurs de commande JOG (registre jogSpeed)
// Câblage standard : FWD = 4998, RWD = 4999.
// Pour Formaca c'est inversé.... → le sens est désormais choisi par instance
// (paramètre invertJogDirection du constructeur, true par défaut = FWD 4999 / RWD 4998).
constexpr uint16_t JOG_FWD_VALUE_STANDARD = 4998;
constexpr uint16_t JOG_RWD_VALUE_STANDARD = 4999;

constexpr uint16_t STOP_VALUE = 0;

// Paramètres de mouvement
constexpr uint32_t TOLERANCE_PUU = 500;  // modifié le 17 aout 2026
// Marge de sécurité sur la butée physique (maxRangePuu) pour absorber les écarts
// d'arrondi entre les différentes conversions inch->PUU (ex: convToPUU() côté métier)
constexpr uint32_t PHYSICAL_LIMIT_MARGIN_PUU = 100;
constexpr uint32_t MOVE_TIMEOUT_MS = 60000;
constexpr uint32_t RESET_TIMEOUT_MS = 5000;
constexpr uint32_t INIT_TIMEOUT_MS = 15000;
constexpr uint32_t TRIGGER_DELAY_MS = 150; // 50ms est trop court.
constexpr uint32_t RESET_PULSE_MS = 200;
constexpr uint32_t RESET_STABILIZE_MS = 500;
constexpr uint32_t HOME_SIGNAL_DURATION_MS = 100;

// Types de commande PR (bits 31-28 du registre config)
constexpr uint32_t PR_TYPE_SINGLE = 0x2;  // Stop après ce PR
constexpr uint32_t PR_TYPE_AUTO   = 0x3;  // Enchaîne sur nextPR

// Valeurs PR (Program Register)
constexpr uint8_t PR1_START = 1;
constexpr uint8_t PR2_START = 2;
constexpr uint8_t PR3_START = 3;
constexpr uint16_t PR2_DONE = 20002;

// ========================================
// MASQUES DE BITS POUR DÉCODAGE STATUS
// ========================================

// Registre Status principal
constexpr uint16_t STATUS_SERVO_READY          = 0x0001;  // Bit 0 - Servo prêt
constexpr uint16_t STATUS_SERVO_ACTIVATED      = 0x0002;  // Bit 1 - Servo activé
constexpr uint16_t STATUS_ZERO_SPEED           = 0x0004;  // Bit 2 - Vitesse zéro
constexpr uint16_t STATUS_TARGET_SPEED_RATED   = 0x0008;  // Bit 3 - Vitesse cible atteinte
constexpr uint16_t STATUS_TARGET_REACHED       = 0x0010;  // Bit 4 - Position cible atteinte
constexpr uint16_t STATUS_SERVO_ALARM          = 0x0040;  // Bit 6 - Alarme servo
constexpr uint16_t STATUS_HOME_DONE            = 0x0100;  // Bit 8 - Homing terminé

// Registre Système de Coordonnées Absolues
constexpr uint16_t COORD_ABS_POSITION_LOST     = 0x0001;  // Bit 0 - Position absolue perdue
constexpr uint16_t COORD_BATTERY_ALARM         = 0x0002;  // Bit 1 - Alarme batterie encodeur
constexpr uint16_t COORD_MULTI_TURNS_OVERFLOW  = 0x0004;  // Bit 2 - Dépassement multi-tours
constexpr uint16_t COORD_PUU_OVERFLOW          = 0x0008;  // Bit 3 - Dépassement unités d'impulsion
constexpr uint16_t COORD_ABS_NOT_SET           = 0x0010;  // Bit 4 - Coordonnées absolues non définies

// Constantes pour gestion EEPROM
// (le délai de sauvegarde effectif est SureServo::EEPROM_SAVE_DELAY_MS = 1000 ms ; l'ancienne
// constante globale de 200 ms était masquée par ce membre de classe et n'a jamais servi)
constexpr uint16_t EEPROM_AUTO_SAVE_DISABLED = 5;     // Désactive sauvegarde auto
constexpr uint16_t EEPROM_MANUAL_SAVE_TRIGGER = 8;    // Déclenche sauvegarde manuelle

// ========================================
// CODES D'ALARME SURESERVO 2
// ========================================

// Erreurs d'alimentation
constexpr uint16_t ALARM_OVERVOLTAGE           = 0x2110;  // Surtension
constexpr uint16_t ALARM_UNDERVOLTAGE          = 0x2120;  // Sous-tension
constexpr uint16_t ALARM_OVERVOLTAGE_MAIN      = 0x2130;  // Surtension circuit principal

// Erreurs thermiques
constexpr uint16_t ALARM_POWER_MODULE_ABNORMAL = 0x2210;  // Module de puissance anormal
constexpr uint16_t ALARM_HEATSINK_OVERHEAT     = 0x2220;  // Surchauffe dissipateur
constexpr uint16_t ALARM_DISCHARGE_RESISTOR_OH = 0x2230;  // Surchauffe résistance de décharge
constexpr uint16_t ALARM_MOTOR_OVERHEAT        = 0x2320;  // Surchauffe moteur

// Erreurs de charge
constexpr uint16_t ALARM_OVERLOAD              = 0x2310;  // Surcharge
constexpr uint16_t ALARM_OVERCURRENT           = 0x2340;  // Surintensité
constexpr uint16_t ALARM_TORQUE_OVERLOAD       = 0x2350;  // Surcharge de couple

// Erreurs de contrôle
constexpr uint16_t ALARM_OVERSPEED             = 0x3110;  // Survitesse
constexpr uint16_t ALARM_EXCESSIVE_DEVIATION   = 0x3120;  // Déviation excessive
constexpr uint16_t ALARM_PULSE_CONTROL_ERROR   = 0x3130;  // Erreur contrôle impulsion
constexpr uint16_t ALARM_EXCESSIVE_ERROR_PULSE = 0x3210;  // Impulsion d'erreur excessive
constexpr uint16_t ALARM_ELECTRONIC_GEAR_ERROR = 0x3220;  // Erreur engrenage électronique

// Erreurs encodeur
constexpr uint16_t ALARM_ENCODER_ERROR         = 0x4110;  // Erreur encodeur
constexpr uint16_t ALARM_ENCODER_OUTPUT_ERROR  = 0x4220;  // Erreur sortie encodeur
constexpr uint16_t ALARM_ENCODER_COUNTER_ERROR = 0x4230;  // Erreur compteur encodeur

// Erreurs moteur
constexpr uint16_t ALARM_UVW_CONNECTION_ERROR  = 0x4310;  // Erreur connexion UVW
constexpr uint16_t ALARM_MOTOR_MATCH_ERROR     = 0x4410;  // Erreur correspondance moteur

// Erreurs mémoire
constexpr uint16_t ALARM_EEPROM_ERROR          = 0x5110;  // Erreur EEPROM
constexpr uint16_t ALARM_EEPROM_WRITE_ERROR    = 0x5120;  // Erreur écriture EEPROM

// Erreurs sécurité
constexpr uint16_t ALARM_EMERGENCY_STOP        = 0x6010;  // Arrêt d'urgence
constexpr uint16_t ALARM_LIMIT_SWITCH_POS      = 0x6020;  // Interrupteur de fin de course +
constexpr uint16_t ALARM_LIMIT_SWITCH_NEG      = 0x6030;  // Interrupteur de fin de course -

// Avertissements
constexpr uint16_t ALARM_HOMING_INCOMPLETE     = 0x7110;  // Retour origine incomplet
constexpr uint16_t ALARM_JOG_IN_POSITION_MODE  = 0x7120;  // JOG en mode position
constexpr uint16_t ALARM_ELECTRONIC_GEAR_WARN  = 0x7210;  // Avertissement engrenage électronique
constexpr uint16_t ALARM_FAN_WARNING           = 0x7220;  // Avertissement ventilateur
constexpr uint16_t ALARM_BATTERY_WARNING       = 0x7230;  // Avertissement batterie
constexpr uint16_t ALARM_ABSOLUTE_SYSTEM_DOWN  = 0x7240;  // Système absolu défaillant

// ========================================
// STRUCTURE POUR DÉCODAGE DES ALARMES
// ========================================

struct AlarmInfo {
    uint16_t code;
    const char* description;
    const char* action;
};

// ========================================
// TABLE D'ALARMES SURESERVO COMPLÈTE
// ========================================
// inline constexpr (C++17) : une seule instance de la table pour tout le programme
// (l'ancien "static const" en créait une copie par unité de compilation).

inline constexpr AlarmInfo ALARMS[] = {
    {0x0009, "Deviation excessive de la commande de position", "Vérifier les paramètres"},
    {0x0011, "Erreur avec l'encodeur", "Vérifier le câble de l'encodeur"},
    {0x0013, "Arrêt du drive actif", "Assurez vous qu'il n'y a plus d'arrêt d'urgence"},
    {0x0017, "Erreur mémoire EEPROM (AL017, à confirmer)", "Réinitialiser les paramètres / contacter le support Delta"},
    {0x0213, "Déviation de position excessive (AL531)", "Réduire vitesse/accélération ou augmenter P2-35"},
    {0x0030, "Blocage mécanique", "Vérifier qu'il n'y a pas de blocage"},
    {0x0083, "Erreur avec le câblage du moteur", "vérifier le câblage du moteur"},  
    // Erreurs d'alimentation
    {0x2110, "Surtension", "Vérifier tension d'alimentation"},
    {0x2120, "Sous-tension", "Vérifier tension d'alimentation"},
    {0x2130, "Surtension circuit principal", "Vérifier alimentation principale"},
    
    // Erreurs thermiques
    {0x2210, "Module de puissance anormal", "Vérifier module de puissance"},
    {0x2220, "Surchauffe dissipateur", "Améliorer ventilation/réduire charge"},
    {0x2230, "Surchauffe résistance décharge", "Réduire cycles décélération"},
    {0x2320, "Surchauffe moteur", "Réduire charge/améliorer refroidissement"},
    
    // Erreurs de charge
    {0x2310, "Surcharge", "Réduire charge/vérifier mécanique"},
    {0x2340, "Surintensité", "Vérifier court-circuit/surcharge"},
    {0x2350, "Surcharge de couple", "Réduire couple/vérifier blocage"},
    
    // Erreurs de contrôle
    {0x3110, "Survitesse", "Réduire vitesse/vérifier paramètres"},
    {0x3120, "Déviation position excessive", "Ajuster gains/réduire accélération"},
    {0x3130, "Erreur contrôle impulsions", "Vérifier câblage signaux"},
    {0x3210, "Impulsions d'erreur excessives", "Vérifier paramètres/charge"},
    {0x3220, "Erreur rapport électronique", "Vérifier paramètres engrenage"},
    
    // Erreurs encodeur
    {0x4110, "Erreur encodeur", "Vérifier connexion encodeur"},
    {0x4220, "Erreur sortie encodeur", "Vérifier câblage encodeur"},
    {0x4230, "Erreur compteur encodeur", "Remplacer encodeur"},
    
    // Erreurs moteur
    {0x4310, "Erreur connexion UVW", "Vérifier câblage moteur"},
    {0x4410, "Erreur correspondance moteur", "Vérifier paramètres moteur"},
    
    // Erreurs mémoire
    {0x5110, "Erreur EEPROM", "Réinitialiser paramètres"},
    {0x5120, "Erreur écriture EEPROM", "Contacter support"},
    
    // Erreurs sécurité
    {0x6010, "Arrêt d'urgence activé", "Désactiver arrêt d'urgence"},
    {0x6020, "Fin de course positif", "Éloigner de la butée +"},
    {0x6030, "Fin de course négatif", "Éloigner de la butée -"},
    
    // Avertissements
    {0x7110, "Retour origine incomplet", "Refaire procédure homing"},
    {0x7120, "JOG en mode position", "Changer mode ou désactiver JOG"},
    {0x7210, "Avertissement engrenage", "Vérifier rapport engrenage"},
    {0x7220, "Avertissement ventilateur", "Vérifier/nettoyer ventilateur"},
    {0x7230, "Batterie faible", "Remplacer batterie encodeur"},
    {0x7240, "Système absolu défaillant", "Refaire origine absolue"}
};

inline constexpr size_t ALARMS_COUNT = sizeof(ALARMS) / sizeof(ALARMS[0]);

} // namespace sureservo

// Alias de compatibilité (nom unique, sans risque de collision)
using SureServoAlarm = sureservo::AlarmInfo;

// ========================================
// ÉNUMÉRATIONS PUBLIQUES (portée globale conservée)
// ========================================
// Laissées hors namespace : le code applicatif (RessortRoyal2, code généré par PLC Studio)
// les utilise sans qualification, ex. HomePositionPolicy::NEGATIVE_INVALIDATES_HOME.

// Énumération pour mode EEPROM
enum class EEPROMMode {
    AUTO_SAVE_ENABLED = 0,      // Sauvegarde automatique (défaut drive)
    AUTO_SAVE_DISABLED = 1,     // Sauvegarde automatique désactivée (recommandé)
    MANUAL_SAVE_ONLY = 2        // Seulement sauvegarde manuelle
};

// Politique de tolérance d'une position négative au repos/à l'init
enum class HomePositionPolicy {
    NEGATIVE_TOLERATED,           // négatif normal (dérive drive), ne rien invalider
    NEGATIVE_INVALIDATES_HOME     // négatif significatif + home fait -> invalider home, re-homing requis
};

// ========================================
// STRUCTURE POUR REGROUPER LES NODES
// ========================================

struct ServoNodes {
    // === NODES OBLIGATOIRES ===
    
    // Contrôle de base
    BooleanOutputNode* servoOn = nullptr;
    BooleanOutputNode* immediateStop = nullptr;
    BooleanOutputNode* alarmsReset = nullptr;
    
    // Position et mouvement
    Uint32InputNode* position = nullptr;
    Uint32OutputNode* targetA = nullptr;
    Uint32OutputNode* targetB = nullptr;
    Uint32OutputNode* targetC = nullptr;
    Uint16OutputNode* trigger = nullptr;

    BooleanOutputNode* targetPositionReached = nullptr;
    
    // Vitesse et contrôle
    Uint16OutputNode* jogSpeed = nullptr;
    Uint16OutputNode* maxTorque = nullptr;
    Uint32OutputNode* pr1Speed = nullptr;
    Uint32OutputNode* pr2Speed = nullptr;
    Uint32OutputNode* pr3Speed = nullptr;
    
    // Configuration
    Uint16OutputNode* auxFunctions = nullptr;
    
    // État et alarmes (INPUT)
    Uint16InputNode* status = nullptr;
    Uint16InputNode* alarms = nullptr;
    Uint16InputNode* absoluteCoordonateSystemStatus = nullptr;
    
    // === NODES OPTIONNELS (peuvent être nullptr) ===
    
    // États décodés (OUTPUT pour interface)
    BooleanOutputNode* servoReady = nullptr;
    BooleanOutputNode* servoActivated = nullptr;
    BooleanOutputNode* zeroSpeed = nullptr;
    BooleanOutputNode* targetSpeedRated = nullptr;
    BooleanOutputNode* servoAlarm = nullptr;
    BooleanOutputNode* homeDone = nullptr;
    BooleanOutputNode* modbusError = nullptr;
    BooleanOutputNode* driveInitialised = nullptr;
    
    // Système de coordonnées absolues
    BooleanOutputNode* absolutePositionLost = nullptr;
    BooleanOutputNode* batteryAlarm = nullptr;
    BooleanOutputNode* multipleTurnsOverflow = nullptr;
    BooleanOutputNode* puuOverflow = nullptr;
    BooleanOutputNode* absoluteCoordonateNotSet = nullptr;

    Uint16InputNode*  homingModeRead  = nullptr;  // lecture mode homing actuel (registre servo)
    Uint16OutputNode* homingModeWrite = nullptr;  // écriture mode homing (commande servo)

    Uint16OutputNode* clearPuu = nullptr; // clear pulses utilisateur

    // ========================================
    // MÉTHODES DE VALIDATION
    // ========================================
    
    /**
     * @brief Valide que tous les nodes obligatoires sont présents
     * @return true si tous les nodes obligatoires sont non-null
     */
    bool validateRequired() const {
        return servoOn != nullptr &&
               immediateStop != nullptr &&
               alarmsReset != nullptr &&
               position != nullptr &&
               targetA != nullptr &&
               targetB != nullptr &&
               targetC != nullptr &&
               trigger != nullptr &&
               targetPositionReached != nullptr &&
               jogSpeed != nullptr &&
               maxTorque != nullptr &&
               pr1Speed != nullptr &&
               pr2Speed != nullptr &&
               pr3Speed != nullptr &&
               auxFunctions != nullptr &&
               status != nullptr &&
               alarms != nullptr &&
               absoluteCoordonateSystemStatus != nullptr;
    }
    
    /**
     * @brief Compte le nombre de nodes optionnels configurés
     * @return Nombre de nodes optionnels non-null
     */
    size_t countOptional() const {
        size_t count = 0;
        if (servoReady) count++;
        if (servoActivated) count++;
        if (zeroSpeed) count++;
        if (targetSpeedRated) count++;
        if (servoAlarm) count++;
        if (homeDone) count++;
        if (modbusError) count++;
        if (driveInitialised) count++;
        if (absolutePositionLost) count++;
        if (batteryAlarm) count++;
        if (multipleTurnsOverflow) count++;
        if (puuOverflow) count++;
        if (absoluteCoordonateNotSet) count++;
        if (homingModeRead) count++;
        if (homingModeWrite) count++;
        if (clearPuu) count++;
        return count;
    }
};

// ========================================
// CLASSE SURESERVO
// ========================================

/**
 * @brief Implémentation non-bloquante pour SureServo 2
 * 
 * Encapsule toute la logique complexe des servo drives SureServo:
 * - Gestion d'état non-bloquante
 * - Décodage automatique des registres
 * - Gestion des alarmes avec descriptions
 * - Interface simplifiée pour les classes métier
 * 
 * Design Patterns utilisés:
 * - State Pattern (machines à états internes) 
 * - Strategy Pattern (décodage des registres)
 * - Template Method (process() avec hooks)
 * - Observer Pattern (mise à jour des nodes)
 * - Facade Pattern (interface simplifiée)
 */
class SureServo : public ServoDrive {
public:
    // ========================================
    // CONSTRUCTEUR
    // ========================================
    
    /**
     * @brief Constructeur avec validation des nodes
     * @param pulsesPerUnit Nombre d'impulsions par unité de mesure (ex: 10000 ppu/inch)
     * @param nodes Structure contenant tous les pointeurs vers les nodes
     * @param servoId Identifiant du drive, préfixe de tous ses messages ("[servoId] ...")
     *        — indispensable pour distinguer plusieurs SureServo dans une même application
     * @param invertJogDirection true (défaut, câblage Formaca) : FWD = 4999 / RWD = 4998 ;
     *        false : câblage standard FWD = 4998 / RWD = 4999
     * @throw std::invalid_argument si les nodes obligatoires manquent
     */
   SureServo(double pulsesPerUnit,
              const ServoNodes& nodes,
              HomePositionPolicy homePositionPolicy,
              float maxRangeInches = 0.0f,
              const char* servoId = "SURE_SERVO",
              bool invertJogDirection = true);

    // ========================================
    // IMPLÉMENTATIONS DE L'INTERFACE SERVODRIVE
    // ========================================
    
    // Méthode principale (appelée dans logicExecutor)
    void process() override;
    
    // Démarrage d'opérations (non-bloquant)
    bool startInitialize() override;
    bool startReset() override;
    bool startGoToPosition(int32_t positionPuu, bool waitForSync = false, uint32_t timeout = 60000) override;
    bool startHoming(uint32_t timeoutMs = 0) override;
    bool stopMovement() override;
    uint32_t moveStartTime = 0;
    
    // État des opérations
    OperationState getInitializeState() const override;
    OperationState getResetState() const override;
    OperationState getMoveState() const override;
    OperationState getHomingState() const override;
    
    // Contrôles directs (synchrones)
    bool jogForward(int32_t limitPuu = INT32_MAX, uint32_t slowZonePuu = 0, uint16_t slowSpeed = 10) override;
    bool jogReverse(int32_t limitPuu = INT32_MIN, uint32_t slowZonePuu = 0, uint16_t slowSpeed = 10) override;
    bool jogStop() override;
    bool setJogSpeed(uint32_t speed) override;
    uint32_t getJogSpeed() const override;
    bool setServoEnabled(bool enable) override;
    bool setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) override;
    bool setMaxTorque(uint8_t torquePercent) override;
    uint8_t getMaxTorque() const;

    /**
     * @brief Recalcule à chaud la limite physique (maxRangePuu) à partir
     *        d'une nouvelle valeur en pouces (ex: rechargement de config).
     * @param maxRangeInches Nouvelle butée physique en pouces (0 = désactivée)
     */
    void setMaxRangeInches(float maxRangeInches);

    // Configuration avancée des PR (pré-chargement sans déclenchement)
    // speedIndex : index table vitesse 0-15 (P5.060–P5.075)
    // nextPR     : PR à enchaîner automatiquement (0 = stop après ce PR
    bool configurePR(uint8_t prNumber, int32_t targetPuu, uint8_t speedIndex, bool chainToNext = false);
    bool clearPRConfig(uint8_t prNumber);
    bool clearPRConfig();  // Restaure tous les PR

    bool emergencyStop() override;
    bool saveParameters() override;
    void clearMovementLock() override;
    
    // Getters d'état général
    int32_t getPosition() const override;
    bool getIsReady() const override;
    bool getHasAlarms() const override;
    uint16_t getAlarmCode() const override;
    bool getIsInitialized() const override;
    const char* getLastError() const override;

    // ========================================
    // AXES MULTIPLES (interface ServoDrive étendue)
    // ========================================

    /**
     * @brief Position mesurée lue sur le drive (node position, signée), sans trace Serial
     *        (contrairement à getPosition() qui journalise à chaque appel)
     */
    int32_t getActualPosition() const override {
        return nodes.position ? nodes.position->getValueAsInt32() : 0;
    }

    /**
     * @brief Vitesse des prochains déplacements : index 0-15 de la table de vitesses
     *        du drive (P5.060–P5.075), rampe index 0. Valeur bornée à 0..15.
     */
    bool setMoveSpeed(int32_t speed) override {
        if (speed < 0) speed = 0;
        if (speed > 15) speed = 15;
        return setSpeedAndRamp((uint8_t)speed, 0);
    }
    
    // ========================================
    // MÉTHODES SPÉCIFIQUES SURESERVO (CONCRÈTES)
    // ========================================
    
    // État détaillé du servo
    bool getServoReady() const;
    bool getServoActivated() const;
    bool getTargetSpeedRated() const;
    bool getTargetPositionReached() const;
    bool getHomeDone() const override;
    bool isHomingFailed() const;
    bool isHomingInProgress() const;
    bool isHomingCompleted() const;
    
    // État système coordonnées absolues
    bool getAbsolutePositionLost() const;
    bool getBatteryAlarm() const;
    bool getMultipleTurnsOverflow() const;
    bool getPuuOverflow() const;
    bool getAbsoluteCoordonateNotSet() const;

    void clearUserPulses();
    void pmDeactivate() { pm_active = false; }
    
    // Description des alarmes
    const char* getAlarmDescription() const override;
    const char* getAlarmAction() const;
    bool clearError();

    void detectDriveRestart(uint32_t now);

    /**
     * @brief Indique qu'un défaut drive (redémarrage ou perte communication Modbus)
     * vient d'être détecté. Sert aux classes métier qui ne s'abonnent pas à
     * setOnAlarmCallback() et font du polling dans leur checkEmergencyConditions()
     * (ex: Formaca2). Les classes abonnées au callback (Woodzco via EmbeddedSureServo,
     * RotaryIndexingTable) n'ont pas besoin de consulter ce flag.
     * @return true si un défaut est en attente d'acquittement
     */
    bool hasPendingDriveFault() const { return driveFaultPending; }

    /**
     * @brief Acquitte le défaut détecté (à appeler une fois la transition EMERGENCY
     * effectuée côté métier, pour éviter de re-signaler le même événement)
     */
    void acknowledgeDriveFault() { driveFaultPending = false; }

    // Débogage et diagnostics
    void printStatus() const;
    void printAlarms() const;
    void printConfiguration() const;

    /*
     * @brief Configure le mode de sauvegarde EEPROM
     * @param mode Mode de sauvegarde désiré
     * @return true si la configuration a réussi
     */
    bool setEEPROMMode(EEPROMMode mode);

    /**
     * @brief Force une sauvegarde manuelle des paramètres en EEPROM
     * @return true si la sauvegarde a été déclenchée avec succès
     */
    bool triggerManualEEPROMSave();

    /**
     * @brief Obtient le mode EEPROM actuel
     * @return Mode EEPROM configuré
     */

     /**
     * @brief Obtient le mode EEPROM actuel
     * @return Mode EEPROM configuré
     */
    EEPROMMode getEEPROMMode() const;

    bool getHasReliablePosition() const;

    // Implémentation des handler pour la machine d'état (dans la classe métier). Ces meyhodes sont déclarées virtuelles pures dans ServoDrive
    bool handleInitializing(OperationStatus& status) override;
    bool handleHoming(OperationStatus& status, uint32_t homingSpeed = 0, uint32_t timeoutMs = 0) override;
    bool handleJogging(OperationStatus& status) override;
    bool handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync = true, uint32_t timeoutMs = 60000) override;
    bool isImmediateStopActive() const override {
        return pendingReleaseImmediateStop ||
            (nodes.immediateStop && nodes.immediateStop->getValue());
    }

    bool isChainedPR1Complete() const { return chainedPR1Complete; }
    void setChainedPRMode(bool enabled);

    // PERF MONITOR — toujours déclarés (disposition mémoire indépendante des macros) ;
    // la collecte n'est active que si SURESERVO_PERF_MONITOR est défini et après pmStartCycle().
    void     pmStartCycle();
    void     pmPrintReport(uint32_t cycleTotalMs) const;
    bool     isPmActive() const { return pm_active; }
    void     pmAccumulateRefresh(uint32_t ms);
    void     pmNotifyCompleted();
    uint32_t pmGetCycleDurationMs() const { return pm_active ? millis() - pm_cycleStartTime : 0; }
    uint32_t pmGetCycleStartTime() const { return pm_cycleStartTime; }
    uint32_t pm_refreshCumul = 0;  // public pour accès main.cpp
    bool isPostResetStabilizing() const;

    bool setHomeAtCurrentPosition();

    bool homeAtCurrentPositionDone = false;

private:
    // ========================================
    // PARAMÈTRES DE CONFIGURATION
    // ========================================
    bool clearUserPulsesPending = false;
    uint16_t clearPuu = false;

    const double pulsesPerUnit;
    const HomePositionPolicy _homePositionPolicy;

    // Valeurs de commande JOG propres à cette instance (voir invertJogDirection du constructeur)
    const uint16_t fwdJogValue;   // ex-FWD_VALUE global
    const uint16_t rwdJogValue;   // ex-RWD_VALUE global
    /**
     * @brief Position interne fiable (mise à jour dans case 3)
     */
    int32_t reliablePosition = 0;

    bool hasReliablePosition = false; // Indique si on a une position fiable

    bool homeDoneInvalidated = false; // true = homeDone du servo n'est plus fiable (position incohérente détectée), re-homing requis

    /**
     * @brief Indique si on a une position fiable
     * true = mouvement réussi, false = utiliser nodes.position
     */
    
    const ServoNodes nodes;

    /**
     * @brief Position précédente pour détecter la stabilisation
     */
    uint32_t previousHomingPosition = 0;
    uint32_t previousMovePosition = 0;
    
    /**
     * @brief Timestamp de la dernière lecture de position pendant homing
     */
    uint32_t lastHomingPositionReadTime = 0;
    
    /**
     * @brief Nombre de lectures consécutives stables pendant homing
     */
    uint8_t homingStableReadingsCount = 0;
    uint8_t moveStableReadingsCount = 0;

    /**
     * @brief Position précédente pour détecter l'immobilité
     */
    uint32_t previousIdlePosition = 0;
    
    /**
     * @brief Timestamp de la dernière lecture de position en idle
     */
    uint32_t lastIdlePositionReadTime = 0;
    
    /**
     * @brief Nombre de lectures consécutives stables en idle
     */
    uint8_t idleStableReadingsCount = 0;
    
    /**
     * @brief Indique si la re-synchronisation est en cours
     */
    bool resyncInProgress = false;
    
    /**
     * @brief Timestamp du début de re-synchronisation
     */
    uint32_t resyncStartTime = 0;

    // Grace period après une correction "position aberrante forcée à 0" en fin de homing
    // (voir processHoming() case 1) : évite que le contrôle de cohérence de process() ne
    // réinvalide hasReliablePosition dans la foulée, sur la base de la même lecture brute.
    uint32_t homingCorrectionTime = 0;
    static constexpr uint32_t HOMING_CORRECTION_GRACE_MS = 500;

    /**
     * @brief Active le polling rapide (refreshInterval=0) du node position.
     *        Réentrant : JOG et re-sync peuvent tous les deux le demander en même
     *        temps sans s'écraser mutuellement (voir _fastPollRefCount).
     */
    void acquireFastPositionPolling();

    /**
     * @brief Relâche une demande de polling rapide. L'intervalle d'origine
     *        n'est restauré que lorsque plus aucun demandeur n'est actif.
     */
    void releaseFastPositionPolling();

    uint32_t _positionBaselineRefreshInterval = 0; // Valeur réelle avant tout polling rapide
    uint8_t  _fastPollRefCount = 0;                // Nombre de demandeurs actifs (JOG, resync)

    /**
     * @brief Active/relâche le polling rapide (refreshInterval=0) du node status.
     *        Utilisé pendant movePhase 2 (attente targetReached/zeroSpeed) pour ne pas
     *        laisser le bouton startCycle obligatoire jusqu'à 200ms de plus que nécessaire
     *        (voir shouldCheckStartButton côté RessortRoyal2). Usage mono-appelant : pas
     *        de ref-count, un simple flag suffit.
     */
    void acquireFastStatusPolling();
    void releaseFastStatusPolling();

    uint32_t _statusBaselineRefreshInterval = 0;
    bool _statusFastPollActive = false;

    // ========================================
    // ÉTAT PAR INSTANCE (ex-variables "static" locales aux fonctions)
    // ========================================
    // Ces variables étaient partagées entre toutes les instances de SureServo : avec plusieurs
    // drives, l'un pouvait masquer la perte de communication de l'autre, consommer son front
    // montant ready/mouvement ou valider sa fenêtre de stabilité de reset.

    // Surveillance de la communication Modbus (process())
    bool     commWatchStarted = false;        // lastValidCommunication initialisé au 1er process()
    uint32_t lastValidCommunication = 0;
    uint32_t consecutiveTimeouts = 0;

    // Détection de fronts (process())
    bool wasReady = false;                    // front montant ready → endPositionResync()
    bool wasMoving = false;                   // début de mouvement → position non fiable

    // Fenêtre de stabilité du reset (processReset(), 200 ms) — remise à 0 dans startReset()
    // et lorsque le reset échoue (audit MI-20)
    uint32_t resetSuccessStartTime = 0;

    // Dernier état "position fiable" journalisé par attemptPositionResync()
    bool resyncLastHadReliablePosition = true;

    /**
     * @brief Horodatages de limitation des logs (un par message périodique)
     */
    struct LogThrottle {
        uint32_t commErrorMessage = 0;      // process() : message IHM perte communication (10 s)
        uint32_t diagnostic = 0;            // process() : diagnostic périodique (30 s)
        uint32_t initWaitDrive = 0;         // processInitialize() : drive injoignable (5 s)
        uint32_t initAutoRecovery = 0;      // processInitialize() : reset automatique en cours (2 s)
        uint32_t initWaitPosition = 0;      // processInitialize() : attente lecture position (500 ms)
        uint32_t initWaitReady = 0;         // processInitialize() : phase 2 attente servoReady (2 s)
        uint32_t resetStability = 0;        // processReset() : période de stabilité (50 ms)
        uint32_t resetWait = 0;             // processReset() : attente ready/alarme (500 ms)
        uint32_t moveWaitStart = 0;         // processMove() : attente début mouvement (2 s)
        uint32_t moveWaitReached = 0;       // processMove() : attente target reached (2 s)
        uint32_t homingWaitHomeDone = 0;    // processHoming() : attente HomeDone (2 s)
        uint32_t homingStabilization = 0;   // processHoming() : attente stabilisation (1 s)
        uint32_t idleResync = 0;            // isIdlePositionStable() (5 s)
        uint32_t resyncConditions = 0;      // attemptPositionResync() (10 s)
        uint32_t handleHomingProgress = 0;  // handleHoming() (5 s)
        uint32_t handleMoveProgress = 0;    // handleMovingToPosition() (1 s)
    } logThrottle;

    // ========================================
    // JOURNALISATION IDENTIFIÉE PAR DRIVE
    // ========================================
    // Tous les messages émis par SureServo (Serial et IHM) sont préfixés par "[servoId] "
    // afin que plusieurs drives produisent des textes distincts
    // (PLC_Tools::sendPeriodicMessage déduplique par CRC du texte).

    /** @brief Serial.printf préfixé par "[servoId] " (écriture en un seul bloc) */
    void logPrintf(const char* format, ...) const __attribute__((format(printf, 2, 3)));
    /** @brief Serial.println préfixé par "[servoId] " */
    void logPrintln(const char* text) const;
    void logPrintln(const String& text) const { logPrintln(text.c_str()); }
    /** @brief BorneUniverselle::prepareMessage préfixé par "[servoId] " */
    void notifyUser(uint8_t type, const char* text) const;
    /** @brief PLC_Tools::sendPeriodicMessage préfixé par "[servoId] " */
    void notifyUserPeriodic(bool condition, uint8_t type, const char* text) const;

    // ========================================
    // ÉTATS INTERNES DES MACHINES À ÉTATS
    // ========================================
    
    // États principaux des opérations
    OperationState initState = OperationState::DRIVE_IDLE;
    OperationState resetState = OperationState::DRIVE_IDLE;
    OperationState moveState = OperationState::DRIVE_IDLE;
    OperationState homingState = OperationState::DRIVE_IDLE;
    
    // Phases internes pour chaque opération
    uint8_t initPhase = 0;
    uint8_t resetPhase = 0;
    uint8_t movePhase = 0;
    uint8_t homingPhase = 0;

    bool lastTargetPositionReached = false; // Pour détecter le changement d'état (debug)
    bool signalValidated = false;

    uint16_t lastJogSpeedValue = 0;  // Pour détecter les transitions JOG
    bool jogToStopDetected = false; // Flag pour forcer re-sync
    bool pendingResyncAfterJogStop = false; // NOUVEAU: Immédiat après jogStop()
    uint32_t jogStopTimestamp = 0; // NOUVEAU: Moment du jogStop pour délai
    
    // Timers pour les opérations
    uint32_t operationStartTime = 0;
    uint32_t phaseStartTime = 0;

    // Timeouts effectifs (fixés par startGoToPosition()/startHoming(), fallback sur macros par défaut)
    uint32_t currentMoveTimeoutMs = sureservo::MOVE_TIMEOUT_MS;
    uint32_t currentHomingTimeoutMs = sureservo::HOMING_TIMEOUT_MS;

        // Timeouts et surveillance
    static constexpr uint32_t MODBUS_TIMEOUT_MS = 5000;      // 5 sec sans communication = problème
    static constexpr uint32_t EEPROM_SAVE_DELAY_MS = 1000;   // Délai sauvegarde EEPROM
    static constexpr uint32_t DRIVE_RESTART_THRESHOLD = 3;   // Seuil détection redémarrage
    
    // ========================================
    // ÉTAT DÉCODÉ DU SERVO
    // ========================================
    
    struct DecodedStatus {
        // === DONNÉES BRUTES DES REGISTRES ===
        uint16_t statusRegister = 0;
        uint16_t alarmsRegister = 0;
        uint16_t coordSystemRegister = 0;
        
        // === ÉTATS DÉCODÉS STATUS ===
        bool servoReady = false;
        bool servoActivated = false;
        bool zeroSpeed = false;
        bool targetSpeedRated = false;
        bool servoAlarm = false;
        bool homeDone = false;
        bool targetPositionReached = false;
        
        // === SYSTÈME DE COORDONNÉES ABSOLUES ===
        bool absolutePositionLost = false;
        bool batteryAlarm = false;
        bool multipleTurnsOverflow = false;
        bool puuOverflow = false;
        bool absoluteCoordonateNotSet = false;
        
        // === MÉTHODES DE MISE À JOUR ===
        
        /**
         * @brief Met à jour depuis les nodes et détecte les changements
         * @param nodes Référence vers les nodes
         * @return true si des changements ont été détectés
         */
        bool updateFromNodes(const ServoNodes& nodes);
        
        /**
         * @brief Décode le registre de status principal
         */
        void decodeStatusRegister();
        
       /**
         * @brief Décode le registre d'alarmes
         * @note Le registre ALARMS (0x9E) contient DIRECTEMENT le code d'alarme complet.
         *       Contrairement aux registres STATUS et COORD_SYSTEM qui nécessitent
         *       d'extraire des bits individuels, alarmsRegister est déjà le code final.
         *       Cette fonction reste donc vide par design - c'est correct.
         *       Exemples: 0x0013 = "Arrêt du drive actif", 0x0030 = "Blocage mécanique"
         */
        void decodeAlarmsRegister();
        
        /**
         * @brief Décode le registre du système de coordonnées
         */
        void decodeCoordSystemRegister();
        
        /**
         * @brief Réinitialise tous les états décodés
         */
        void reset();
        
    } status;
    
    // ========================================
    // VARIABLES D'ÉTAT
    // ========================================
    
    bool initialized = false;
    uint8_t currentPr = 1;  // Programme Register actuel (1, 2, ou 3)
    int32_t targetPosition = 0;
    uint32_t currentJogSpeed = 1000;
    uint8_t currentPrSpeed = 8;  // Vitesse actuelle pour les PR (0-15, valeur servo drive)
    uint8_t currentRampIndex = 0;  // Rampe accel/décel actuelle pour les PR (0-13, valeur clampée)
    String lastError = "";

    int lastLoggedStatusValue  = -1;
    uint32_t lastStatusLogTime = 0;
    static constexpr uint32_t MIN_LOG_INTERVAL_MS = 500; // 500ms minimum entre logs identiques

   
    
    // ========================================
    // MÉTHODES PRIVÉES - MACHINES À ÉTATS
    // ========================================
    
    bool isPlcSuspended() const;
    void processInitialize(uint32_t now);
    void processReset(uint32_t now);
    bool isCurrentlyHealthy() const;
    void processMove(uint32_t now);

    void processHoming(uint32_t now);
    void processHomeAtCurrentPosition();

    void printHomingStatus() const;

    // ========================================
    // MÉTHODES PRIVÉES - UTILITAIRES
    // ========================================
    
    // Gestion des mouvements
    bool isAtPosition(int32_t targetPuu) const;
    void sendTargetToPR(int32_t targetPuu);
    void advancePR();  // Passer au PR suivant (1->2->3->1)

    void setReliablePosition(int32_t newPosition);

    uint32_t getHomingPositionReadInterval() const;

    bool isHomingPositionStabilized(uint32_t currentPosition, uint32_t now);
    bool isMovePositionStabilized(uint32_t currentPosition, uint32_t now);

    // Validation et diagnostics
    bool checkDriveHealth() const;
    bool areNodesValid() const;
    void setError(const String& error);
    void updateOptionalOutputNodes();
    
    // Décodage des alarmes
    const sureservo::AlarmInfo* findAlarmInfo(uint16_t alarmCode) const;
    
    // Logging et débogage
    void logStatusChange(const char* context = nullptr) const;
    void logAlarmChange(const char* context = nullptr) const;
    void logOperationStateChange(const char* operation, OperationState oldState, OperationState newState) const;

     /**
     * @brief Retourne l'adresse Modbus du servo drive
     * Lue depuis nodes.status via dynamic_cast vers ModbusNode.
     * Independant de l'adresse configuree dans la classe metier.
     * @return adresse esclave Modbus, ou 0 si non disponible
     */
    uint16_t getSlaveAddress() const;

    // Variables d'état EEPROM
    EEPROMMode currentEEPROMMode = EEPROMMode::AUTO_SAVE_DISABLED;
    uint32_t lastEEPROMSaveTime = 0;
    bool eepromSaveInProgress = false;
    uint32_t maxRangePuu = 0;  // Limite physique en PUU

    // Méthodes privées EEPROM
    void initializeEEPROMConfiguration();
    bool setAuxiliaryFunctions(uint16_t value);

     /**
     * @brief Tente une re-synchronisation "nice to have" de la position
     * Ne bloque rien si ça échoue, juste un bonus si le moteur est immobile
     * @param now Timestamp actuel
     */
    void attemptPositionResync(uint32_t now);

    /**
     * @brief Démarre la re-synchronisation : sauvegarde le refreshInterval du node
     *        position et le bascule en polling rapide (0) pour raccourcir le temps
     *        de validation (3 lectures stables au lieu d'attendre le refresh lent)
     * @param now Timestamp actuel
     */
    void beginPositionResync(uint32_t now);

    /**
     * @brief Termine la re-synchronisation (succès ou abandon) : restaure le
     *        refreshInterval d'origine du node position et réinitialise l'état
     * @param success true si la position a été validée, false si abandon
     */
    void endPositionResync(bool success);
    
    /**
     * @brief Vérifie si la position est stable en mode idle (même algo que homing)
     * @param currentPosition Position actuelle
     * @param now Timestamp actuel
     * @return true si la position est stable
     */
    bool isIdlePositionStable(uint32_t currentPosition, uint32_t now);
    
    /**
     * @brief Invalide la position interne (appelée lors du JOG)
     */
    void invalidateReliablePosition();
    uint32_t lastMovePositionReadTime = 0;
    bool driveInitStarted = false;
    bool initFailureReported = false;
    bool driveHomingStarted = false;
    bool waitForSyncRequested = false;  

    uint16_t lastLoggedAlarmCode = 0; // Dernier code d'alarme loggé
    uint32_t waitModbusStartTime = 0; // Timestamp du début de l'attente Modbus vivant (pour timeouts)

    // État mémorisé pour detectDriveRestart() (remplace les static locaux dupliqués
    // qui existaient à la fois dans process() et dans detectDriveRestart())
    bool restartDetect_wasInitialized = false;
    bool restartDetect_hadHomeDone = false;

    // Flag sticky consommé par polling (Formaca2) — voir hasPendingDriveFault()
    bool driveFaultPending = false;

    /**
     * @brief Notifie la classe métier d'un défaut drive (redémarrage ou perte Modbus),
     * quel que soit l'état courant (idle, move, homing). Unifie les deux canaux de
     * notification existants : callback (Woodzco/RotaryIndexingTable) et polling
     * (Formaca2 via hasPendingDriveFault()).
     * @param reason Message d'erreur, mémorisé dans lastError et transmis au callback
     */
    void notifyBusinessLogicFault(const char* reason);

    bool pendingReleaseImmediateStop = false;
    uint32_t immediateStopTimestamp = 0;
    static constexpr uint32_t IMMEDIATE_STOP_DURATION_MS = 500;  // Durée minimale pour activer le relais
    uint32_t postResetStabilizationTime = 0;  // ← ajouter
    static constexpr uint32_t POST_RESET_STABILIZATION_MS = 3000;
    bool movementCancelled = false;

    bool jogForwardActive = false;
    bool jogReverseActive = false;
    uint32_t _savedJogSpeed = 0;
    bool _jogSpeedRestored = false;
    unsigned long alarmPendingTime = 0;

    // Logique d'Enchainement des PR
    bool chainedPRMode = false;  // true = attendre fin PR2 après fin PR1
    bool getChainedPRMode() const { return chainedPRMode; }
    bool chainedPR1Complete = false;

    uint8_t  chainedMovingAway     = 0;       // 0=inconnu, 1=monte, 2=descend
    int32_t  chainedPrevPosition   = 0;
    int32_t chainedPR1Target       = 0;

    bool homeAtCurrentPositionInProgress = false;
    uint32_t homeAtCurrentPositionEndTime = 0;
    static constexpr uint32_t HOME_AT_POS_GUARD_MS = 1000;
    bool softwareHomeDone = false;
    uint8_t  homeAtCurrentPositionPhase = 0;
    uint16_t homeAtCurrentPositionSavedMode = 0;

    // Compteurs PERF MONITOR (toujours présents, voir SURESERVO_PERF_MONITOR)
    mutable bool pm_active        = false;
    uint32_t pm_cycleStartTime    = 0;
    uint32_t pm_processCallCount  = 0;
    uint32_t pm_processTimeMax    = 0;
    uint32_t pm_processTimeCumul  = 0;
    uint32_t pm_writeTargetCount  = 0;
    uint32_t pm_writeTriggerCount = 0;
    uint32_t pm_driveLatencyMax   = 0;  // T1 : trigger → signalValidated
    uint32_t pm_driveLatencyCumul = 0;
    uint32_t pm_moveTimeMax       = 0;  // T2 : signalValidated → targetReached
    uint32_t pm_moveTimeCumul     = 0;
    uint32_t pm_moveCount         = 0;  // nb de mouvements complétés
    uint32_t pm_triggerSentTime   = 0;  // horodatage interne trigger
    uint32_t pm_signalValidatedTime = 0;// horodatage interne signalValidated
    uint32_t pm_interMoveStart    = 0;
    uint32_t pm_interMoveCount    = 0;
    uint32_t pm_interMoveCumul    = 0;
    uint32_t pm_interMoveMax      = 0;
    uint32_t pm_targetSentTime    = 0;
    uint32_t pm_targetToTrigCount = 0;
    uint32_t pm_targetToTrigCumul = 0;
    uint32_t pm_targetToTrigMax   = 0;
    uint32_t pm_smartDelayCount    = 0;  // trigger confirmé par lastRefresh
    uint32_t pm_safetyTimeoutCount = 0;  // trigger par filet de sécurité
};

#endif // SURE_SERVO_H
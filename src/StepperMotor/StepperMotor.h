/**
 * @file StepperMotor.h
 * @brief Driver pour moteur pas à pas Step/Dir avec rampe d'accélération
 * 
 * Wrapper autour de FastAccelStepper pour intégration dans l'architecture
 * BorneUniverselle / ServoDrive / BusinessLogic.
 * 
 * Caractéristiques :
 * - Utilise FastAccelStepper (MCPWM + PCNT hardware ESP32)
 * - Rampe d'accélération/décélération trapézoïdale intégrée
 * - Interface compatible ServoDrive (comme SureServo)
 * - Intégration avec système de Nodes
 * - Récupération automatique des pins via getPinNumber()
 * - Non-bloquant avec machine à états
 * 
 * @author Thierry
 * @date 2025
 */

#ifndef STEPPER_MOTOR_H
#define STEPPER_MOTOR_H

#include "ServoDrive/ServoDrive.h"
#include "Node/Node.h"
#include <FastAccelStepper.h>
#include <memory>

#define MAX_JOG_SPEED       1500  // Vitesse jog max en steps/sec
#define MAX_SERVO_SPEED     15   
#define MAX_ACCELERATION     10000 // Accélération max en steps/sec²

#define STEPPER_ALARM_NONE              0
#define STEPPER_ALARM_HARDWARE          1   // hardwareStepperAlarm = true
#define STEPPER_ALARM_OPERATION_FAILED  2   // moveState/homingState/resetState = DRIVE_FAILED
#define STEPPER_ALARM_LIMIT_ERROR       3   // limitError = true

struct StepperAlarm {
    uint16_t    code;
    const char* description;
};

static constexpr StepperAlarm STEPPER_ALARMS[] = {
    { STEPPER_ALARM_HARDWARE,         "Alarme hardware stepper" },
    { STEPPER_ALARM_OPERATION_FAILED, "Échec opération stepper" },
    { STEPPER_ALARM_LIMIT_ERROR,      "Erreur fin de course"    }
};

// Forward declaration
class BusinessLogic;

// ========================================
// STRUCTURE DES NODES POUR STEPPERMOTOR
// ========================================

/**
 * @brief Structure contenant tous les pointeurs vers les nodes
 * 
 * Architecture identique à ServoNodes de SureServo.
 * Les nodes sont créés par BorneUniverselle via config.json,
 * puis initialisés via initNodePtr() dans BusinessLogic.
 */
struct StepperNodes {
    // ========================================
    // NODES OBLIGATOIRES - Contrôle de base
    // ========================================
    
    VirtualBooleanInputNode* servoOn = nullptr;          // Active/désactive le moteur
    BooleanOutputNode* immediateStop = nullptr;    // Arrêt immédiat
    BooleanOutputNode* alarmsReset = nullptr;      // Reset des alarmes
    
    // ========================================
    // NODES OBLIGATOIRES - Hardware Pins
    // ========================================
    
    /**
     * Pins de contrôle moteur
     * TYPE REQUIS : HardwareBooleanOutputNode
     * 
     * Ces nodes permettent de récupérer les numéros de pins GPIO
     * via getPinNumber() pour configuration de FastAccelStepper
     */
    HardwareBooleanOutputNode* stepPin = nullptr;          // PIN STEP (généré par MCPWM)
    HardwareBooleanOutputNode* dirPin = nullptr;           // PIN DIR (direction)
    BooleanOutputNode* enable = nullptr;        // PIN ENABLE (activation moteur)
    
    // ========================================
    // NODES OBLIGATOIRES - Position et mouvement
    // ========================================
    
    Uint32OutputNode* position = nullptr;           // Position actuelle (steps) - output !
    Uint32InputNode* targetPosition = nullptr;    // Position cible (steps)
    BooleanOutputNode* targetPositionReached = nullptr;  // Flag: position atteinte
    
    // ========================================
    // NODES OBLIGATOIRES - Vitesse et contrôle
    // ========================================
    
    VirtualUint32InputNode* maxSpeed = nullptr;          // Vitesse max (steps/sec)
    VirtualUint32InputNode* jogSpeed = nullptr;          // Vitesse jog (steps/sec)
    
    // ========================================
    // NODES OBLIGATOIRES - État et alarmes (OUTPUT)
    // ========================================
    
    // ========================================
    // NODES OPTIONNELS - Rampe d'accélération
    // ========================================
    
    Uint32InputNode* acceleration = nullptr;       // Accélération (steps/sec²)
    
    // ========================================
    // NODES OPTIONNELS - Limit switches & sécurité
    // ========================================
    
    BooleanInputNode* limitSwitchPositive = nullptr;  // Fin de course +
    BooleanInputNode* limitSwitchNegative = nullptr;  // Fin de course -
    BooleanInputNode* homeSwitch = nullptr;           // Capteur origine
    BooleanInputNode* hardwareStepperAlarm = nullptr;
    
    // ========================================
    // NODES OPTIONNELS - États décodés (OUTPUT pour interface)
    // ========================================
    
    BooleanOutputNode* stepperReady = nullptr;     // Stepper prêt
    BooleanOutputNode* stepperActivated = nullptr; // Stepper activé
    BooleanOutputNode* homeDone = nullptr;          // Homing effectué
    BooleanOutputNode* limitError = nullptr;       // Erreur fin de course
    BooleanOutputNode* stepperAlarm = nullptr;     // Alarme générale
    BooleanOutputNode* zeroSpeed = nullptr;
    BooleanOutputNode* targetSpeedRated = nullptr;
    BooleanOutputNode* driveInitialised = nullptr;
    
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
               stepPin != nullptr &&
               dirPin != nullptr &&
               enable != nullptr &&
               position != nullptr &&
               targetPosition != nullptr &&
               targetPositionReached != nullptr &&
               maxSpeed != nullptr &&
               jogSpeed != nullptr;
    }
    
    /**
     * @brief Compte le nombre de nodes optionnels configurés
     * @return Nombre de nodes optionnels non-null
     */
    size_t countOptional() const {
        size_t count = 0;
        if (acceleration) count++;
        if (limitSwitchPositive) count++;
        if (limitSwitchNegative) count++;
        if (homeSwitch) count++;
        if (hardwareStepperAlarm) count++;
        if (stepperReady) count++;
        if (stepperActivated) count++;
        if (homeDone) count++;
        if (limitError) count++;
        if (stepperAlarm) count++;
        return count;
    }
};

// ========================================
// CLASSE STEPPERMOTOR
// ========================================

class StepperMotor : public ServoDrive {
public:
    StepperMotor(uint32_t stepsPerUnit,
                 const StepperNodes& nodes,
                 uint32_t maxRangeSteps = 0,
                 const char* servoId = "STEPPER");
    
    ~StepperMotor();
    
    // Interface ServoDrive
    void process() override;
    bool startInitialize() override;
    bool startReset() override;
    bool startGoToPosition(int32_t positionPuu, bool waitForSync = false, uint32_t timeout = 60000);
    // bool startGoToPosition(uint32_t positionPuu, bool waitForSync = false, uint32_t timeout = 60000 ) override;
    bool startHoming(uint32_t timeoutMs = 0) override;
    bool stopMovement() override;
    
    OperationState getInitializeState() const override;
    OperationState getResetState() const override;
    OperationState getMoveState() const override;
    OperationState getHomingState() const override;

    bool getIsReady() const override;
    bool getIsInitialized() const override;

    bool getHomeDone() const;
    bool getServoActivated() const;
    bool getTargetPositionReached() const;
    
    bool jogForward(int32_t limitPuu = INT32_MAX, uint32_t slowZonePuu = 0, uint16_t slowSpeed = 10) override;
    bool jogReverse(int32_t limitPuu = INT32_MIN, uint32_t slowZonePuu = 0, uint16_t slowSpeed = 10) override;
    bool jogStop() override;
    bool setJogSpeed(uint32_t speed) override;
    uint32_t getJogSpeed() const override;
    bool setServoEnabled(bool enable) override;
    bool setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) override;
    bool setMaxTorque(uint8_t torquePercent) override;
    bool emergencyStop() override;
    bool saveParameters() override;
    bool getHasAlarms() const override {
        return nodes.stepperAlarm && nodes.stepperAlarm->getValue();
    };

    uint16_t getAlarmCode() const override;
    const char *getAlarmDescription() const;
    
    int32_t getPosition() const override;
    const char* getLastError() const override;
    
    bool handleInitializing(OperationStatus& status) override;
    bool handleHoming(OperationStatus& status, uint32_t homingSpeed = 0, uint32_t timeoutMs = 0) override;
    bool handleJogging(OperationStatus& status) override;
    bool handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync = true, uint32_t timeoutMs = 60000) override;
    
    bool setAccelerationProfile(uint32_t maxSpeedStepsPerSec, uint32_t accelStepsPerSec2);
    
    uint32_t getCurrentSpeed() const;
    bool isAccelerationEnabled() const { return accelerationEnabled; }
    void printConfiguration() const;

    void setHomingTimeout(uint32_t timeoutMs) {
        homingTimeoutMs = timeoutMs;
        Serial.printf("[%s] Homing timeout configuré: %lu ms\n", motorId.c_str(), timeoutMs);
    }


    void setInitTimeout(uint32_t timeoutMs) { initTimeoutMs = timeoutMs; }
    void setResetTimeout(uint32_t timeoutMs) { resetTimeoutMs = timeoutMs; }
    void setMoveTimeout(uint32_t timeoutMs) { moveTimeoutMs = timeoutMs; }

    uint32_t getHomingTimeout() const { return homingTimeoutMs; }

private:
    /**
     * @brief Structure contenant tous les indicateurs de statut calculés
     * 
     * Similaire à DecodedStatus de SureServo, mais adaptée pour StepperMotor.
     * Les valeurs sont calculées depuis FastAccelStepper plutôt que depuis
     * des registres Modbus.
     */
    struct StepperStatus {
        // === ÉTATS CALCULÉS DEPUIS FASTACCELSTEPPER ===
        bool stepperReady = false;           // Initialisé et pas d'alarme
        bool stepperActivated = false;       // ServoOn actif
        bool zeroSpeed = false;              // Vitesse = 0 (moteur arrêté)
        bool targetPositionReached = false;  // Position cible atteinte
        bool stepperAlarm = false;           // Alarme active
        bool homeDone = false;               // Homing terminé
        bool limitError = false;             // Erreur fin de course
        
        /**
         * @brief Met à jour tous les indicateurs depuis FastAccelStepper
         * @param motor Pointeur vers le moteur FastAccelStepper
         * @param stepperMotor Référence vers le StepperMotor parent
         */
        void updateFromMotor(FastAccelStepper* motor, const StepperMotor& stepperMotor);
        
        /**
         * @brief Réinitialise tous les états
         */
        void reset() {
            stepperReady = false;
            stepperActivated = false;
            zeroSpeed = false;
            targetPositionReached = false;
            stepperAlarm = false;
            homeDone = false;
            limitError = false;
        }
    };
    
    // === INSTANCE DE LA STRUCTURE DE STATUT ===
    StepperStatus status;

    const uint32_t stepsPerUnit;
    const uint32_t maxRangeSteps;
    const StepperNodes nodes;
    
    // ✅ FastAccelStepper APRÈS (initialisés dans le corps)
    FastAccelStepperEngine engine;
    FastAccelStepper* fasMotor;
    
    uint8_t stepPinNumber;
    uint8_t dirPinNumber;
    uint8_t enablePinNumber;
    
    String motorId = "STEPPER";
    
    OperationState initState;
    OperationState resetState;
    OperationState moveState;
    OperationState homingState;
    
    uint8_t initPhase;
    uint8_t resetPhase;
    uint8_t movePhase;
    uint8_t homingPhase;
    
    uint32_t operationStartTime;
    uint32_t phaseStartTime;

    uint32_t initTimeoutMs;
    uint32_t resetTimeoutMs;
    uint32_t moveTimeoutMs;
    uint32_t homingTimeoutMs;
    
    int32_t targetPositionInternal;
    uint32_t currentJogSpeed;
    uint32_t currentHomingSpeed = 0;  // Vitesse homing (0 = utilise jogSpeed)
    bool accelerationEnabled;
    bool initialized;
    bool homingDone;
   
    
    String lastError;
    
    void setError(const char* error) {
        lastError = String(error);
        Serial.printf("[%s] ERROR: %s\n", motorId.c_str(), error);
        
        // ✅ AJOUT
        if (nodes.stepperAlarm) {
            nodes.stepperAlarm->setValue(true);
        }
    }
    
    // Méthodes de traitement des états (comme SureServo)
    void processInitialize(uint32_t now);
    void processReset(uint32_t now);
    void processMove(uint32_t now);
    void processHoming(uint32_t now);
    
    bool extractPinNumber(HardwareBooleanOutputNode* node, const char* pinName, uint8_t& pinNumber);
    bool checkAndHandleLimitSwitches();
    void updateStatusNodes();
    void syncParametersFromNodes();
    bool isMovementComplete() const;

    bool stepperHomingStarted = false;
    bool stepperInitStarted = false;
    bool stepperResetStarted = false;
    bool stepperMoveStarted = false;
};

#endif // STEPPER_MOTOR_H
#pragma once

#include <Arduino.h>
#include <functional>
#include "Node/Node.h"

// ========================================
// INTERFACE ABSTRAITE SERVO DRIVE
// ========================================

/**
 * @brief Interface abstraite pour tous les servo drives
 * 
 * Design minimaliste pour faciliter l'intégration dans les classes métier
 */
class ServoDrive {
public:
    // ========================================
    // ÉNUMÉRATIONS
    // ========================================
    // Les classe qui héritent de ServoDrive ont leur propre machine d'États
    enum class OperationState {
        DRIVE_IDLE = 0,           // ✅ Prêt OU Terminé avec succès
        DRIVE_IN_PROGRESS = 1,    // En cours
        DRIVE_FAILED = 2,         // Échec
        DRIVE_TIMEOUT = 3         // Timeout
    };

    enum class OperationStatus {
        COMPLETED,      // Opération terminée avec succès
        IN_PROGRESS,    // Opération en cours
        SERVO_DRIVE_ERROR,          // Erreur récupérable
        SERVO_DRIVE_FATAL_ERROR,    // Erreur fatale (nécessite intervention)
        ABORTED,         // Opération annulée
        SERVO_TIMEOUT        // Opération timeout
    };


    // ========================================
    // CONSTRUCTEUR / DESTRUCTEUR
    // ========================================
    
    //ServoDrive() = default;
    ServoDrive(const char* id = "SERVO") : servoId(id) {}
    
    const char* getServoId() const { return servoId.c_str(); }
    
    virtual ~ServoDrive() = default;

    // ========================================
    // MÉTHODES ABSTRAITES - CONTRÔLE NON-BLOQUANT
    // ========================================
    
    // Traitement principal (appelée dans logicExecutor)
    virtual void process() = 0;
    
    // Démarrageprivate d'opérations (non-bloquant)
    virtual bool startInitialize() = 0;
    virtual bool startReset() = 0;
    virtual bool startGoToPosition(int32_t positionPuu, bool waitForSync = false, uint32_t timeout = 60000) = 0;

    // Surcharge float — implémentation par défaut (délègue à la version int32_t) pour ne pas
    // forcer les autres ServoDrive à l'implémenter ; seul VFD la redéfinit pour la précision sous-po.
    virtual bool startGoToPosition(float positionInch, bool waitForSync = false, uint32_t timeout = 60000) {
        return startGoToPosition((int32_t)roundf(positionInch), waitForSync, timeout);
    }

    virtual bool startHoming(uint32_t timeoutMs = 0) = 0;
    virtual bool stopMovement() = 0; 
    
    // État des opérations
    virtual OperationState getInitializeState() const = 0;
    virtual OperationState getResetState() const = 0;
    virtual OperationState getMoveState() const = 0;
    virtual OperationState getHomingState() const = 0;
    
    // Contrôles directs (synchrones)
    virtual bool jogForward(int32_t limitPuu = INT32_MAX, uint32_t slowZonePuu = 0, uint16_t slowSpeed = 10) = 0;

    virtual bool jogReverse(int32_t limitPuu = INT32_MIN, uint32_t slowZonePuu = 0, uint16_t slowSpeed = 10) = 0;
    virtual bool jogStop() = 0;
    virtual bool setJogSpeed(uint32_t speed) = 0;
    virtual uint32_t getJogSpeed() const = 0;
    virtual bool setServoEnabled(bool enable) = 0;
    virtual bool setSpeedAndRamp(uint8_t speed, uint8_t rampIndex) = 0;
    virtual bool setMaxTorque(uint8_t torquePercent) = 0;
    virtual bool emergencyStop() = 0;
    virtual bool saveParameters() = 0;
    virtual void clearMovementLock() {}
    
    // État général
    virtual int32_t getPosition() const = 0;
    virtual bool getIsReady() const = 0;
    virtual bool getHasAlarms() const = 0;
    virtual uint16_t getAlarmCode() const = 0;
    virtual bool getIsInitialized() const = 0;
    virtual const char* getLastError() const = 0;
    virtual bool getHomeDone() const = 0;

    // ========================================
    // AXES MULTIPLES (AxisController, code généré par PLC Studio)
    // Valeurs par défaut compatibles avec les drives existants.
    // ========================================

    // Position mesurée (lue sur le variateur ou le compteur d'impulsions), sans trace Serial.
    virtual int32_t getActualPosition() const { return getPosition(); }

    // Vitesse des prochains déplacements, en unités natives du drive :
    // SureServo = index 0-15 de la table de vitesses, Lichuan = tr/min, pas-à-pas = pas/s.
    virtual bool setMoveSpeed(int32_t speed) {
        if (speed < 0) speed = 0;
        if (speed > 255) speed = 255;
        return setSpeedAndRamp((uint8_t)speed, 0);
    }

    // Vrai pendant la fenêtre où un nouveau déplacement serait refusé après un arrêt.
    virtual bool isImmediateStopActive() const { return false; }

    // Libellé de l'alarme courante (vide si inconnue).
    virtual const char* getAlarmDescription() const { return ""; }

    // handler pour la machine d'état (dans la classe métier)
    virtual bool handleInitializing(OperationStatus& status) = 0;
    virtual bool handleHoming(OperationStatus& status, uint32_t homingSpeed = 0, uint32_t timeoutMs = 0) = 0;
    virtual bool handleJogging(OperationStatus& status) = 0;
    virtual bool handleMovingToPosition(int32_t pos, OperationStatus& status, bool waitForSync = true, uint32_t timeoutMs = 60000) = 0;

    static const char* getOperationStateText(OperationState state);

    /**
     * @brief Callback déclenché sur front montant d'une alarme servo
     * Permet à la classe métier de réagir (ex: transitionToState EMERGENCY)
     * sans que ServoDrive connaisse la machine à états du projet
     */
    void setOnAlarmCallback(std::function<void(uint16_t code, const char* desc)> cb) {
        onAlarmCallback = cb;
    }

    void setOnWarningCallback(std::function<void(bool active, const char* msg)> cb) {
        onWarningCallback = cb;
    }

    protected:
        String servoId;
        std::function<void(uint16_t, const char*)> onAlarmCallback = nullptr;
        std::function<void(bool active, const char* msg)> onWarningCallback = nullptr;
};
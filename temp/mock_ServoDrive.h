// test/test_transition/mock_ServoDrive.h
#ifndef SERVO_DRIVE_H
#define SERVO_DRIVE_H

#include <cstdint>

// Classe de base ServoDrive
class ServoDrive {
protected:
    bool hasReliablePosition;
    bool isInitialized;
    float currentPosition;
    float targetPosition;
    bool isMoving;
    
public:
     enum class OperationState {
        DRIVE_IDLE = 0,           // ✅ Prêt OU Terminé avec succès
        DRIVE_IN_PROGRESS = 1,    // En cours
        DRIVE_FAILED = 2,         // Échec
        DRIVE_TIMEOUT = 3         // Timeout
    };

    ServoDrive();
    virtual ~ServoDrive();
    
    // Méthodes virtuelles de base
    virtual bool initialize() = 0;
    virtual bool getHasReliablePosition() const;
    virtual void setHasReliablePosition(bool reliable);
    virtual bool isReady() const;
    virtual float getPosition() const;
    virtual void setPosition(float position);
    virtual bool moveTo(float position) = 0;
    virtual bool stop() = 0;
    virtual bool home() = 0;
    virtual bool getIsMoving() const;
    virtual const char* getLastError() const;
    virtual OperationState getResetState() const = 0;
    
    // Méthodes communes
    virtual int getErrorCode() const;
    virtual const char* getErrorMessage() const;
    virtual bool hasError() const;
    virtual void process() = 0;
    virtual bool setMaxTorque(uint8_t torquePercent) = 0;

      // Contrôles directs (synchrones)
    virtual bool jogForward() = 0;
    virtual bool jogReverse() = 0;
    virtual bool jogStop() = 0;
    virtual bool setJogSpeed(uint32_t speed) = 0;
    virtual bool setCycleSpeed(uint8_t speed) = 0;
};

#endif // SERVO_DRIVE_H
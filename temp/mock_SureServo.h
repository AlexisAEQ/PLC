#ifndef SURE_SERVO_H
#define SURE_SERVO_H

#include "../test/test_transition/mock_ServoDrive.h"
class SureServo : public ServoDrive {
private:
    // Membres spécifiques à SureServo si nécessaire
    float speed;
    float acceleration;
    OperationState resetState = OperationState::DRIVE_IDLE;
    
public:
    SureServo();
    virtual ~SureServo();
    
    // Implémentation des méthodes virtuelles pures
    bool initialize() override;
    bool moveTo(float position) override;
    bool stop() override;
    bool home() override;
    
    // Méthodes spécifiques à SureServo
    void setSpeed(float speed);
    float getSpeed() const;
    void setAcceleration(float accel);
    float getAcceleration() const;
    bool isHoming() const;
    float getTargetPosition() const;
    void setTargetPosition(float target);
    void clearError();
    bool getHasReliablePosition() const;
    void process() override;
    const char* getLastError() const override;
    ServoDrive::OperationState getResetState() const;
    bool setMaxTorque(uint8_t torquePercent) override;

      // Contrôles directs (synchrones)
    bool jogForward() override;
    bool jogReverse()override;
    bool jogStop() override;
    bool setJogSpeed(uint32_t speed) override;
    bool setCycleSpeed(uint8_t speed) override;
    
};
#endif // SURE_SERVO_H
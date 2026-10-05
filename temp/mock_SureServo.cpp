// test/test_transition/mock_ServoDrive.cpp
#include "mock_SureServo.h"
#include <cstdio>
# pragma message("Mock SureServo included!")
// ========== ServoDrive (classe de base) ==========

ServoDrive::ServoDrive() 
    : hasReliablePosition(false),
      isInitialized(false),
      currentPosition(0.0f),
      targetPosition(0.0f),
      isMoving(false) {
    #ifdef DEBUG_MOCK
    printf("[MOCK] ServoDrive constructor\n");
    #endif
}

ServoDrive::~ServoDrive() {
    #ifdef DEBUG_MOCK
    printf("[MOCK] ServoDrive destructor\n");
    #endif
}

bool ServoDrive::getHasReliablePosition() const {
    #ifdef DEBUG_MOCK
    printf("[MOCK] ServoDrive::getHasReliablePosition() -> %s\n", 
           hasReliablePosition ? "true" : "false");
    #endif
    return hasReliablePosition;
}

void ServoDrive::setHasReliablePosition(bool reliable) {
    hasReliablePosition = reliable;
}

bool ServoDrive::isReady() const {
    return isInitialized && !isMoving;
}

float ServoDrive::getPosition() const {
    return currentPosition;
}

void ServoDrive::setPosition(float position) {
    currentPosition = position;
}

bool ServoDrive::getIsMoving() const {
    return isMoving;
}

int ServoDrive::getErrorCode() const {
    return 0;  // Mock : pas d'erreur
}

const char* ServoDrive::getErrorMessage() const {
    return "No error";
}

bool ServoDrive::hasError() const {
    return false;
}

// ========== SureServo (classe dérivée) ==========

SureServo::SureServo() 
    : ServoDrive(),
      speed(100.0f),
      acceleration(50.0f) {
    #ifdef DEBUG_MOCK
    printf("[MOCK] SureServo constructor\n");
    #endif
}

SureServo::~SureServo() {
    #ifdef DEBUG_MOCK
    printf("[MOCK] SureServo destructor\n");
    #endif
}

bool SureServo::initialize() {
    #ifdef DEBUG_MOCK
    printf("[MOCK] SureServo::initialize()\n");
    #endif
    isInitialized = true;
    hasReliablePosition = true;
    return true;
}

bool SureServo::moveTo(float position) {
    #ifdef DEBUG_MOCK
    printf("[MOCK] SureServo::moveTo(%.2f)\n", position);
    #endif
    targetPosition = position;
    isMoving = true;
    // Mock : mouvement instantané
    currentPosition = position;
    isMoving = false;
    return true;
}

bool SureServo::stop() {
    #ifdef DEBUG_MOCK
    printf("[MOCK] SureServo::stop()\n");
    #endif
    isMoving = false;
    return true;
}

bool SureServo::home() {
    #ifdef DEBUG_MOCK
    printf("[MOCK] SureServo::home()\n");
    #endif
    isMoving = true;
    currentPosition = 0.0f;
    targetPosition = 0.0f;
    isMoving = false;
    hasReliablePosition = true;
    return true;
}

void SureServo::setSpeed(float speed) {
    this->speed = speed;
}

float SureServo::getSpeed() const {
    return speed;
}

void SureServo::setAcceleration(float accel) {
    this->acceleration = accel;
}

float SureServo::getAcceleration() const {
    return acceleration;
}

bool SureServo::isHoming() const {
    return false;  // Mock : jamais en homing
}

float SureServo::getTargetPosition() const {
    return targetPosition;
}

void SureServo::setTargetPosition(float target) {
    targetPosition = target;
}

void SureServo::clearError() {
    #ifdef DEBUG_MOCK
    printf("[MOCK] SureServo::clearError()\n");
    #endif
}

void SureServo::process() {}

const char* SureServo::getLastError() const {
    return "No error";
}

ServoDrive::OperationState SureServo::getResetState() const {
    return resetState;
}

bool SureServo::setMaxTorque(uint8_t torquePercent) {
    #ifdef DEBUG_MOCK
    printf("[MOCK] SureServo::setMaxTorque(%d)\n", torquePercent);
    #endif
    return true;
}

bool SureServo::jogForward() {
    return true;
}

bool SureServo::jogReverse() {
    return true;
}

bool SureServo::jogStop() {
    return true;
}

bool SureServo::setJogSpeed(uint32_t speed) {
    return true;
}

bool  SureServo::setCycleSpeed(uint8_t speed) {
    return true;
}
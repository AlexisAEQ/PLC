#include "ServoDrive.h"

const char* ServoDrive::getOperationStateText(OperationState state) {
    switch (state) {
        case OperationState::DRIVE_IDLE: 
            return "inactif";
        case OperationState::DRIVE_IN_PROGRESS: 
            return "en cours";
        case OperationState::DRIVE_FAILED: 
            return "échec";
        case OperationState::DRIVE_TIMEOUT: 
            return "timeout";
        default: 
            return "inconnu";
    }
}
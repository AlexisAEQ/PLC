#include "AxisController/AxisController.h"
#include "BorneUniverselle/borneUniverselle.h"
#include "PLC_CommonTypes/PLC_CommonTypes.h"

using OpState = ServoDrive::OperationState;
using OpStatus = ServoDrive::OperationStatus;

AxisController::AxisController(const char* name, const Config& config)
    : _name(name ? name : "Axe"), _cfg(config) {}

void AxisController::report(uint8_t level, const char* what, const char* detail) const {
    char msg[192];
    if (detail && *detail) {
        snprintf(msg, sizeof(msg), "%s : %s : %s", _name, what, detail);
    } else {
        snprintf(msg, sizeof(msg), "%s : %s", _name, what);
    }
    BorneUniverselle::prepareMessage(level, msg);
}

// -----------------------------------------------------------------------------
// Déplacements et prise d'origine demandés par le grafcet
// -----------------------------------------------------------------------------
bool AxisController::service() {
    if (!_drive) return true;

    if (_movePending) {
        if (_pendingSpeed != _appliedSpeed) {
            // La vitesse est écrite d'abord ; le déplacement part au cycle suivant.
            _drive->setMoveSpeed(_pendingSpeed);
            _appliedSpeed = _pendingSpeed;
            return true;
        }
        // Après un arrêt, le variateur peut refuser les déplacements quelques instants : on patiente.
        if (_drive->isImmediateStopActive()) return true;
        _movePending = false;
        if (_drive->startGoToPosition(_pendingTarget, false)) {
            _moveActive = true;
        } else {
            report(ERROR, "déplacement refusé", _drive->getLastError());
            return false;
        }
    }

    if (_moveActive) {
        switch (_drive->getMoveState()) {
            case OpState::DRIVE_IDLE:
                _moveActive = false;
                _moveDone = true;
                break;
            case OpState::DRIVE_IN_PROGRESS:
                break;
            default:
                _moveActive = false;
                report(ERROR, "déplacement en échec", _drive->getLastError());
                return false;
        }
    }

    if (_homingActive) {
        OpStatus st;
        bool ok = _drive->handleHoming(st);
        _homingRunning = ok && st == OpStatus::IN_PROGRESS;
        if (!ok || (st != OpStatus::IN_PROGRESS && st != OpStatus::COMPLETED)) {
            _homingActive = false;
            report(ERROR, "prise d'origine en échec", _drive->getLastError());
            return false;
        }
        if (st == OpStatus::COMPLETED) _homingActive = false;
    }
    return true;
}

bool AxisController::moveTo(int32_t target, int32_t speed) {
    // Butée logicielle appliquée à toutes les cibles.
    if (target < _cfg.minPosition || (_cfg.maxPosition != 0 && target > _cfg.maxPosition)) {
        char detail[48];
        snprintf(detail, sizeof(detail), "%ld", (long)target);
        report(ERROR, "cible hors course", detail);
        return false;
    }
    if (speed < _cfg.minSpeed) speed = _cfg.minSpeed;
    if (speed > _cfg.maxSpeed) speed = _cfg.maxSpeed;
    _moveDone = false;
    _movePending = true;
    _pendingTarget = target;
    _pendingSpeed = speed;
    return true;
}

void AxisController::startHoming() {
    _moveDone = false;
    _homingActive = true;
}

void AxisController::halt(bool homingMode) {
    if (!_drive) return;
    if (_jogForwardHeld || _jogReverseHeld) _drive->jogStop();
    _jogForwardHeld = _jogReverseHeld = _jogBlocked = false;
    bool homing = _homingRunning && (_homingActive || _homingArmed || homingMode);
    if (_moveActive || homing) _drive->stopMovement();
    if (homing) {
        // stopMovement() ne réarme pas toujours la prise d'origine interne du drive : un appel à
        // handleHoming() consomme cet état, sinon la prochaine prise d'origine finirait aussitôt.
        OpStatus st;
        _drive->handleHoming(st);
    }
    _homingRunning = false;
    _movePending = _moveActive = _homingActive = _homingArmed = false;
    _moveDone = false;
}

void AxisController::setTorque(int percent) {
    if (!_drive) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent != _lastTorque) {
        _drive->setMaxTorque((uint8_t)percent);
        _lastTorque = percent;
    }
}

// -----------------------------------------------------------------------------
// Modes de la machine
// -----------------------------------------------------------------------------
AxisController::Progress AxisController::initStep() {
    if (!_drive) return Progress::FAILED;
    if (!_initRequested) {
        OpState is = _drive->getInitializeState();
        // Après un réarmement, le drive peut relancer lui-même l'initialisation : on attend sa fin.
        if (is == OpState::DRIVE_IN_PROGRESS) return Progress::RUNNING;
        if (is == OpState::DRIVE_IDLE && _drive->getIsInitialized()) return Progress::DONE;
    }
    OpStatus st;
    if (!_drive->handleInitializing(st)) {
        _initRequested = false;
        report(ERROR, "initialisation impossible", _drive->getLastError());
        return Progress::FAILED;
    }
    _initRequested = true;
    switch (st) {
        case OpStatus::COMPLETED:
            _initRequested = false;
            _appliedSpeed = -1;
            _lastTorque = -1;
            return Progress::DONE;
        case OpStatus::IN_PROGRESS:
            return Progress::RUNNING;
        default:
            _initRequested = false;
            report(ERROR, "initialisation en échec", _drive->getLastError());
            return Progress::FAILED;
    }
}

void AxisController::armHoming() {
    if (_drive && !_drive->getHomeDone()) _homingArmed = true;
}

AxisController::Progress AxisController::homingStep() {
    if (!_drive || !_homingArmed) return Progress::DONE;
    OpStatus st;
    if (!_drive->handleHoming(st)) {
        _homingRunning = _homingArmed = false;
        report(ERROR, "prise d'origine impossible", _drive->getLastError());
        return Progress::FAILED;
    }
    _homingRunning = (st == OpStatus::IN_PROGRESS);
    if (st == OpStatus::IN_PROGRESS) return Progress::RUNNING;
    _homingArmed = false;
    if (st == OpStatus::COMPLETED) return Progress::DONE;
    report(ERROR, "prise d'origine en échec", _drive->getLastError());
    return Progress::FAILED;
}

bool AxisController::startReset() {
    if (!_drive) return false;
    if (_resetRequested) return true;
    _resetRequested = _drive->startReset();
    return _resetRequested;
}

AxisController::Progress AxisController::resetStep() {
    if (!_drive) return Progress::FAILED;
    if (!_resetRequested) return Progress::DONE;
    switch (_drive->getResetState()) {
        case OpState::DRIVE_IN_PROGRESS:
            return Progress::RUNNING;
        case OpState::DRIVE_IDLE:
            _resetRequested = false;
            return Progress::DONE;
        default:
            _resetRequested = false;
            report(ERROR, "échec du réarmement", _drive->getLastError());
            return Progress::FAILED;
    }
}

bool AxisController::needsInitialization() const {
    // Le réarmement relance l'initialisation du variateur si elle avait été interrompue.
    return !_drive || !_drive->getIsInitialized() || _drive->getInitializeState() != OpState::DRIVE_IDLE;
}

bool AxisController::jogStep(bool plus, bool minus, int32_t speed) {
    if (!_drive) return false;
    OpStatus st;
    _drive->handleJogging(st);
    if (st == OpStatus::SERVO_DRIVE_ERROR) {
        report(ERROR, "défaut pendant le jog", _drive->getLastError());
        return false;
    }
    if (speed < _cfg.jogMinSpeed) speed = _cfg.jogMinSpeed;
    if (speed > _cfg.jogMaxSpeed) speed = _cfg.jogMaxSpeed;
    if (speed != _lastJogSpeed && !_jogForwardHeld && !_jogReverseHeld) {
        _drive->setJogSpeed((uint32_t)speed);
        _lastJogSpeed = speed;
    }
    // Le drive attend un appel à chaque cycle tant que le bouton est maintenu (butée, zone lente).
    if (plus && !minus) {
        if (_jogReverseHeld) { _drive->jogStop(); _jogReverseHeld = false; return true; }  // arrêt avant d'inverser
        if (!_jogBlocked) {
            _jogForwardHeld = _drive->jogForward(_cfg.maxPosition != 0 ? _cfg.maxPosition : INT32_MAX, 0, 10);
            _jogBlocked = !_jogForwardHeld;
        }
    } else if (minus && !plus) {
        if (_jogForwardHeld) { _drive->jogStop(); _jogForwardHeld = false; return true; }
        if (!_jogBlocked) {
            int32_t limit = (_cfg.jogReverseStopsAtZero && homeDone()) ? _cfg.minPosition : INT32_MIN;
            _jogReverseHeld = _drive->jogReverse(limit, 0, 10);
            _jogBlocked = !_jogReverseHeld;
        }
    } else {
        if (_jogForwardHeld || _jogReverseHeld) _drive->jogStop();
        _jogForwardHeld = _jogReverseHeld = false;
        _jogBlocked = false;
    }
    return true;
}

void AxisController::onEmergency() {
    if (_drive) {
        if (_jogForwardHeld || _jogReverseHeld) _drive->jogStop();  // remet à zéro le jog interne
        _drive->emergencyStop();
    }
    _movePending = _moveActive = _homingActive = _homingArmed = _homingRunning = false;
    _jogForwardHeld = _jogReverseHeld = _jogBlocked = false;
    _initRequested = false;
    _resetRequested = false;
}

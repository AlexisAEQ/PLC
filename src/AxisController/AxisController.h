#pragma once

#include <Arduino.h>
#include "ServoDrive/ServoDrive.h"

/**
 * @brief Pilotage d'un axe (SureServo, Lichuan, moteur pas-à-pas...) par le code généré par PLC Studio
 *
 * Une instance par axe. Reprend la logique qui était générée pour l'unique SureServo
 * (déplacement différé d'un cycle après l'écriture de la vitesse, attente de la fenêtre
 * d'arrêt immédiat, suivi du déplacement et de la prise d'origine, jog maintenu, réarmement),
 * pour un nombre quelconque d'axes de types différents.
 *
 * Les positions et les vitesses sont en unités natives du drive :
 * - SureServo : impulsions (PUU) et index 0-15 de la table de vitesses
 * - Lichuan   : impulsions et tr/min
 * - pas-à-pas : pas et pas/s
 *
 * La classe ne change jamais l'état de la machine : une méthode qui renvoie false
 * (ou Progress::FAILED) a déjà affiché le message, l'appelant passe en URGENCE.
 */
class AxisController {
public:
    enum class Progress { RUNNING, DONE, FAILED };

    struct Config {
        int32_t minPosition = 0;          // butée logicielle basse (cibles des déplacements)
        int32_t maxPosition = 0;          // butée logicielle haute ; 0 = pas de butée haute
        int32_t minSpeed = 0;             // bornes de la vitesse des déplacements
        int32_t maxSpeed = 15;
        int32_t jogMinSpeed = 1;          // bornes de la vitesse du jog
        int32_t jogMaxSpeed = 3000;
        bool jogReverseStopsAtZero = true;  // jog arrière limité à 0 une fois l'origine faite
    };

    AxisController(const char* name, const Config& config);

    // Le drive est créé par le code généré (initializePointers) puis confié à l'axe.
    void attach(ServoDrive* drive) { _drive = drive; }
    ServoDrive* drive() const { return _drive; }
    const char* name() const { return _name; }

    // ---- À chaque cycle
    void process() { if (_drive) _drive->process(); }

    // Déplacements et prise d'origine demandés par le grafcet (états REPOS / CYCLE).
    // false : défaut (message affiché).
    bool service();

    // ---- Actions du grafcet
    bool moveTo(int32_t target, int32_t speed);   // false : cible hors course (message affiché)
    void startHoming();
    void halt(bool homingMode = false);           // arrête déplacement, prise d'origine et jog
    void setTorque(int percent);                  // couple maximal (drives qui le gèrent)

    // ---- Modes de la machine
    Progress initStep();                          // INITIALISATION
    void armHoming();                             // avant d'entrer en PRISE D'ORIGINE
    Progress homingStep();                        // PRISE D'ORIGINE (DONE si l'axe n'est pas concerné)
    bool startReset();                            // sortie d'URGENCE : true quand le réarmement est lancé
    Progress resetStep();                         // RÉARMEMENT
    bool needsInitialization() const;             // après réarmement : réinitialiser le variateur ?
    bool jogStep(bool plus, bool minus, int32_t speed);  // MANUEL ; false : défaut du variateur
    void onEmergency();                           // entrée en URGENCE

    // ---- État (expressions des réceptivités : Axe.pret, Axe.enPosition...)
    bool isReady() const { return _drive && _drive->getIsReady(); }
    bool inPosition() const { return _moveDone; }
    bool isMoving() const { return _moveActive || _movePending || _homingActive; }
    bool homeDone() const { return _drive && _drive->getHomeDone(); }
    bool hasAlarm() const { return _drive && _drive->getHasAlarms(); }
    int32_t position() const { return _drive ? _drive->getActualPosition() : 0; }
    uint16_t alarmCode() const { return _drive ? _drive->getAlarmCode() : 0; }
    const char* alarmText() const { return _drive ? _drive->getAlarmDescription() : ""; }

private:
    void report(uint8_t level, const char* what, const char* detail = nullptr) const;

    const char* _name;
    Config _cfg;
    ServoDrive* _drive = nullptr;

    bool _initRequested = false;     // initialisation lancée par cet axe
    bool _moveActive = false;        // déplacement lancé, en attente de fin
    bool _moveDone = false;          // dernier déplacement terminé
    bool _movePending = false;       // déplacement demandé, pas encore lancé
    int32_t _pendingTarget = 0;
    int32_t _pendingSpeed = 0;
    int32_t _appliedSpeed = -1;
    bool _homingActive = false;      // prise d'origine demandée par le grafcet
    bool _homingArmed = false;       // prise d'origine du mode PRISE D'ORIGINE
    bool _homingRunning = false;     // handleHoming() a démarré une prise d'origine
    bool _resetRequested = false;
    bool _jogForwardHeld = false;
    bool _jogReverseHeld = false;
    bool _jogBlocked = false;        // butée atteinte : attendre le relâchement
    int32_t _lastJogSpeed = -1;
    int _lastTorque = -1;
};

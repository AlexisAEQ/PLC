// Simulateur : exécute le projet comme la classe C++ générée (shared/cpp.js).
//
// Même représentation intermédiaire (compileLogic), même ordre de cycle, mêmes modes de
// marche. Chaque méthode de Simulator correspond à la méthode C++ de même nom. Le servo
// est remplacé par ServoModel, qui reproduit les états internes de SureServo utiles à la
// logique (initialisation, déplacement, prise d'origine, jog, arrêt, urgence, réarmement).

import { compileLogic } from './grafcet.js';
import { evaluate, edgeKey } from './expr.js';
import { CATALOG } from './catalog.js';

export const MODES = {
  UNDEFINED: 'INDÉFINI',
  INITIALIZING: 'INITIALISATION',
  STOP: 'ARRÊT',
  IDLE: 'REPOS',
  EMERGENCY: 'URGENCE',
  JOGGING: 'MANUEL',
  HOMING: "PRISE D'ORIGINE",
  RUNNING: 'CYCLE',
  RESETTING: 'RÉARMEMENT',
};

const DEFAULTS = { bool: false, int: 0, float: 0, text: '' };
const MAX_EVOLUTIONS = 16;

// ---------------------------------------------------------------------------
// Modèle simplifié du SureServo (états calqués sur src/SureServo/SureServo.cpp)
export class ServoModel {
  constructor({ maxRange = 0, initDelay = 800, puuPerSecondPerSpeed = 8000 } = {}) {
    this.maxRange = maxRange;
    this.initDelay = initDelay;
    this.rate = puuPerSecondPerSpeed;
    this.now = 0;
    this.position = 0;
    this.target = 0;
    this.initialized = false;
    this.homeDone = false;
    this.alarm = false;
    this.alarmCode = 0;
    this.servoOn = false;
    this.initState = 'IDLE';
    this.moveState = 'IDLE';
    this.homingState = 'IDLE';
    this.resetState = 'IDLE';
    this.initStarted = false; // driveInitStarted
    this.homingStarted = false; // driveHomingStarted
    this.speedIdx = 0;
    this.jogSpeed = 500;
    this.jogDir = 0;
    this.torque = 100;
    this.timer = 0;
    this.resetTimer = 0;
    this.stopUntil = 0; // arrêt immédiat maintenu 500 ms après stopMovement / emergencyStop
    this.lastError = '';
  }
  get ready() {
    return this.initialized && !this.alarm && this.servoOn;
  }
  isImmediateStopActive() {
    return this.now < this.stopUntil;
  }
  raiseAlarm(code = 0x0201) {
    this.alarm = true;
    this.alarmCode = code;
  }
  fail(msg) {
    this.lastError = msg;
    return false;
  }
  startInitialize() {
    if (this.initState !== 'IDLE') return this.fail('Initialisation déjà en cours ou terminée');
    this.initState = 'IN_PROGRESS';
    this.timer = 0;
    return true;
  }
  // { ok, status } comme handleInitializing(status)
  handleInitializing() {
    if (!this.initStarted) {
      if (!this.startInitialize()) return { ok: false, status: 'SERVO_DRIVE_FATAL_ERROR' };
      this.initStarted = true;
      return { ok: true, status: 'IN_PROGRESS' };
    }
    if (this.initState === 'IDLE') {
      this.initStarted = false;
      return { ok: true, status: 'COMPLETED' };
    }
    if (this.initState === 'FAILED') return { ok: true, status: 'SERVO_DRIVE_ERROR' };
    return { ok: true, status: 'IN_PROGRESS' };
  }
  handleHoming() {
    if (!this.homingStarted) {
      if (!this.ready) return { ok: false, status: 'SERVO_DRIVE_FATAL_ERROR' };
      this.homingStarted = true;
      this.homingState = 'IN_PROGRESS';
      return { ok: true, status: 'IN_PROGRESS' };
    }
    if (this.homingState === 'IDLE') {
      this.homingStarted = false;
      return { ok: true, status: 'COMPLETED' };
    }
    if (this.homingState === 'FAILED') {
      this.homingStarted = false;
      return { ok: true, status: 'SERVO_DRIVE_ERROR' };
    }
    return { ok: true, status: 'IN_PROGRESS' };
  }
  setSpeedAndRamp(speed) {
    this.speedIdx = Math.max(0, Math.min(15, speed));
  }
  startGoToPosition(pos) {
    if (this.isImmediateStopActive()) return this.fail('Mouvement bloqué - annulation récente');
    if (this.moveState === 'IN_PROGRESS') return this.fail('Mouvement déjà en cours');
    if (this.moveState !== 'IDLE') return this.fail('Erreur précédente - utilisez RESET d’abord');
    if (!this.ready) return this.fail('SureServo pas prêt pour mouvement');
    if (this.alarm) return this.fail('Servo en alarme - mouvement impossible');
    if (this.maxRange > 0 && pos > this.maxRange + 100) return this.fail('Position cible au-delà de la butée physique');
    this.target = pos;
    this.moveState = 'IN_PROGRESS';
    return true;
  }
  // Comme SureServo::stopMovement : la prise d'origine interne (driveHomingStarted) n'est PAS réarmée.
  stopMovement() {
    if (this.moveState === 'IN_PROGRESS') this.moveState = 'IDLE';
    if (this.homingState === 'IN_PROGRESS') this.homingState = 'IDLE';
    this.jogDir = 0;
    this.target = this.position;
    this.stopUntil = this.now + 500;
  }
  emergencyStop() {
    this.initState = this.resetState = this.moveState = this.homingState = 'FAILED';
    this.servoOn = false;
    this.jogDir = 0;
    this.target = this.position;
    this.stopUntil = this.now + 500;
  }
  startReset() {
    if (this.resetState === 'IN_PROGRESS') return this.fail('Reset déjà en cours');
    this.resetState = 'IN_PROGRESS';
    this.resetTimer = 0;
    return true;
  }
  // Comme SureServo::clearMovementLock (appelé en fin de réarmement réussi).
  clearMovementLock() {
    this.stopUntil = 0;
    if (this.moveState === 'FAILED') this.moveState = 'IDLE';
    this.servoOn = true;
    if (this.initState === 'FAILED') {
      this.initState = 'IDLE';
      this.initStarted = false;
      this.startInitialize();
    }
    if (this.homingState !== 'IDLE') {
      this.homingState = 'IDLE';
      this.homingStarted = false;
    }
  }
  jog(dir) {
    if (!this.initialized) return this.fail('Servo non initialisé - jog impossible');
    if (this.alarm) return this.fail('Servo en alarme - jog impossible');
    if (dir > 0 && this.homeDone && this.maxRange > 0 && this.position >= this.maxRange) {
      this.jogDir = 0;
      return false;
    }
    if (dir < 0 && this.homeDone && this.position <= 0) {
      this.jogDir = 0;
      return false;
    }
    this.jogDir = dir;
    return true;
  }
  jogStop() {
    this.jogDir = 0;
  }
  process(dt) {
    this.now += dt;
    if (this.initState === 'IN_PROGRESS') {
      this.timer += dt;
      if (this.alarm) this.initState = 'FAILED';
      else if (this.timer >= this.initDelay) {
        this.initState = 'IDLE';
        this.initialized = true;
        this.servoOn = true;
      }
    }
    if (this.resetState === 'IN_PROGRESS') {
      this.resetTimer += dt;
      if (this.resetTimer >= 300) {
        this.resetState = 'IDLE';
        this.alarm = false;
        this.alarmCode = 0;
        this.clearMovementLock();
      }
    }
    const stepTowards = (goal, rate) => {
      const d = goal - this.position;
      const max = (rate * dt) / 1000;
      if (Math.abs(d) <= max) {
        this.position = goal;
        return true;
      }
      this.position += Math.sign(d) * max;
      return false;
    };
    if (this.moveState === 'IN_PROGRESS') {
      if (this.alarm) this.moveState = 'FAILED';
      else if (stepTowards(this.target, this.rate * (this.speedIdx + 1))) this.moveState = 'IDLE';
    }
    if (this.homingState === 'IN_PROGRESS') {
      if (this.alarm) this.homingState = 'FAILED';
      else if (stepTowards(0, this.rate * 4)) {
        this.homingState = 'IDLE';
        this.homeDone = true;
      }
    }
    if (this.jogDir) {
      this.position += (this.jogDir * this.jogSpeed * 20 * dt) / 1000;
      if (this.homeDone && this.maxRange > 0 && this.position > this.maxRange) this.position = this.maxRange;
      if (this.homeDone && this.position < 0) this.position = 0;
    }
    this.position = Math.round(this.position);
  }
}

// ---------------------------------------------------------------------------
export class Simulator {
  constructor(project, { scanMs = 10 } = {}) {
    this.project = project;
    this.scanMs = scanMs;
    const { ir, issues } = compileLogic(project);
    this.ir = ir;
    this.issues = issues;
    this.vars = new Map(project.variables.map((v) => [v.symbol, v]));
    this.servoEq = project.equipment.find((e) => CATALOG[e.type]?.role === 'servo') || null;
    this.hasServo = !!this.servoEq && ir.servo;
    this.drivenSet = new Map(ir.driven.map((d) => [d.sym, d]));
    this.reset();
  }

  reset() {
    this.now = 0;
    this.values = new Map();
    for (const v of this.project.variables) {
      let init = DEFAULTS[v.dataType];
      if (v.kind === 'parameter' && v.initial !== undefined) {
        init = v.dataType === 'bool' ? v.initial === true || v.initial === 'true' : v.dataType === 'text' ? String(v.initial) : Number(v.initial) || 0;
        if (v.dataType === 'int') init = Math.trunc(init);
      }
      this.values.set(v.symbol, init);
    }
    const n = this.ir.steps.length;
    this.X = new Array(n).fill(false);
    this.Xnew = new Array(n).fill(false);
    this.Xstart = new Array(n).fill(0);
    this.S = new Map(); // opérandes des fronts capturés en début de cycle
    this.E = new Map(); // mêmes opérandes au cycle précédent
    this.latch = new Map();
    this.mode = 'UNDEFINED';
    this.modeSince = 0;
    this.emergencyClearSince = 0;
    this.unstableReported = false;
    this.messages = [];
    this.journal = [];
    this.fired = [];
    this.maxRangePuu = this.hasServo ? Math.max(0, Math.round((Number(this.servoEq.options?.maxRange) || 0) * (Number(this.servoEq.options?.pulsesPerUnit) || 1))) : 0;
    this.servo = this.hasServo ? new ServoModel({ maxRange: this.maxRangePuu }) : null;
    this.servoInitRequested = false;
    this.servoMoveActive = false;
    this.servoMoveDone = false;
    this.servoMovePending = false;
    this.servoPendingTarget = 0;
    this.servoPendingSpeed = 0;
    this.servoAppliedSpeed = -1;
    this.servoHomingActive = false;
    this.servoHomingRunning = false;
    this.jogForwardHeld = false;
    this.jogReverseHeld = false;
    this.jogBlocked = false;
    this.lastJogSpeed = -1;
    this.lastTorque = -1;
    this.go('INITIALIZING');
  }

  // ---- accès
  get(sym) {
    return this.values.get(sym);
  }
  set(sym, value) {
    const v = this.vars.get(sym);
    if (!v) return;
    if (v.dataType === 'int') value = Math.trunc(Number(value) || 0) | 0;
    else if (v.dataType === 'float') value = Math.fround(Number(value) || 0);
    else if (v.dataType === 'bool') value = !!value;
    else value = String(value ?? '');
    this.values.set(sym, value);
  }
  stepIndex(num) {
    return this.ir.steps.findIndex((s) => s.num === num);
  }
  env() {
    return {
      get: (s) => this.values.get(s),
      step: (num) => this.X[this.stepIndex(num)],
      stepTime: (num) => {
        const i = this.stepIndex(num);
        return this.X[i] ? (this.now - this.Xstart[i]) | 0 : 0;
      },
      servo: (prop) => this.servoProp(prop),
      cur: (key) => this.S.get(key) || false,
      prev: (key) => this.E.get(key) || false,
    };
  }
  ev(ast) {
    return evaluate(ast, this.env());
  }
  // Valeur directe (sans la capture des fronts), comme toCpp(ast, baseNames).
  raw(ast) {
    const env = this.env();
    delete env.cur;
    return evaluate(ast, env);
  }
  servoProp(prop) {
    const s = this.servo;
    if (!s) return prop === 'position' ? 0 : false;
    switch (prop) {
      case 'pret':
        return s.ready;
      case 'enPosition':
        return this.servoMoveDone;
      case 'enMouvement':
        return this.servoIsMoving();
      case 'origineFaite':
        return s.homeDone;
      case 'alarme':
        return s.alarm;
      case 'position':
        return s.position | 0;
    }
    return false;
  }
  servoIsMoving() {
    return this.servoMoveActive || this.servoMovePending || this.servoHomingActive;
  }

  log(kind, text) {
    this.journal.push({ t: this.now, kind, text });
    if (this.journal.length > 500) this.journal.shift();
  }
  message(level, text) {
    this.messages.push({ t: this.now, level, text });
    if (this.messages.length > 200) this.messages.shift();
    this.log('message', text);
  }

  go(mode) {
    if (this.mode === mode) return;
    const old = this.mode;
    this.mode = mode;
    this.modeSince = this.now;
    this.log('mode', `${MODES[old] || old} → ${MODES[mode]}`);
    this.onStateEnter(mode, old);
  }

  onStateEnter(mode, old) {
    if (mode === 'EMERGENCY') {
      if (this.servo) {
        if (this.jogForwardHeld || this.jogReverseHeld) this.servo.jogStop();
        this.servo.emergencyStop();
      }
      this.servoMovePending = this.servoMoveActive = this.servoHomingActive = this.servoHomingRunning = false;
      this.jogForwardHeld = this.jogReverseHeld = this.jogBlocked = false;
      this.servoInitRequested = false;
      this.emergencyClearSince = 0;
      this.grafcetClear();
    } else if (mode === 'STOP') {
      if (this.hasServo) this.servoHalt(old === 'HOMING');
      this.grafcetClear();
    }
  }

  // ---- un cycle d'automate (doBusinessLogic)
  scan(dt = this.scanMs) {
    this.now += dt;
    this.fired = [];
    if (this.servo) this.servo.process(dt);
    this.captureEdges();
    const b = this.ir.blocks;

    const m = this.mode;
    if (m !== 'EMERGENCY' && m !== 'UNDEFINED') {
      if (this.emergencyCondition()) {
        this.message('error', `Arrêt d'urgence : ${this.project.logic?.blocks?.emergency?.condition || ''}`);
        this.go('EMERGENCY');
      } else if (this.hasServo && m !== 'RESETTING' && this.servo.alarm) {
        this.message('error', `Alarme servo 0x${this.servo.alarmCode.toString(16).padStart(4, '0').toUpperCase()}`);
        this.go('EMERGENCY');
      }
    }
    if (b.stop && ['IDLE', 'RUNNING', 'HOMING', 'JOGGING'].includes(this.mode) && this.ev(b.stop)) {
      this.message('info', 'Arrêt demandé');
      this.go('STOP');
    }

    switch (this.mode) {
      case 'UNDEFINED':
        this.go('INITIALIZING');
        break;
      case 'INITIALIZING':
        this.handleInitializing();
        break;
      case 'STOP':
        this.handleStop();
        break;
      case 'HOMING':
        this.handleHoming();
        break;
      case 'IDLE':
      case 'RUNNING':
        this.handleRun();
        break;
      case 'JOGGING':
        this.handleJogging();
        break;
      case 'EMERGENCY':
        this.handleEmergency();
        break;
      case 'RESETTING':
        this.handleResetting();
        break;
      default:
        this.go('EMERGENCY');
    }
    this.computeOutputs();
    this.checkParams();
    this.updateEdges();
  }

  emergencyCondition() {
    const b = this.ir.blocks;
    return !!(b.emergency && this.ev(b.emergency));
  }

  handleInitializing() {
    if (!this.hasServo) return this.go('STOP');
    const s = this.servo;
    if (!this.servoInitRequested) {
      if (s.initState === 'IN_PROGRESS') return; // relancée par le réarmement
      if (s.initState === 'IDLE' && s.initialized) return this.go('STOP');
    }
    const r = s.handleInitializing();
    if (!r.ok) {
      this.servoInitRequested = false;
      return this.go('EMERGENCY');
    }
    this.servoInitRequested = true;
    if (r.status === 'COMPLETED') {
      this.servoInitRequested = false;
      this.servoAppliedSpeed = -1;
      this.lastTorque = -1;
      this.go('STOP');
    } else if (r.status !== 'IN_PROGRESS') {
      this.servoInitRequested = false;
      this.message('error', 'Initialisation du servo en échec');
      this.go('EMERGENCY');
    }
  }

  handleStop() {
    const b = this.ir.blocks;
    if (b.stop && this.ev(b.stop)) return;
    const jog = this.hasServo && b.servo?.jog;
    if (jog && this.ev(jog.mode)) return this.go('JOGGING');
    if (!(b.start ? this.ev(b.start) : true)) return;
    if (this.hasServo && b.servo.homing !== 'none' && !this.servo.homeDone) {
      const trigger = b.servo.homing === 'condition' ? this.ev(b.servo.homingCondition) : true;
      if (trigger) this.go('HOMING');
      return;
    }
    this.grafcetReset();
    this.go('IDLE');
  }

  handleHoming() {
    if (!this.hasServo) return this.go('STOP');
    const r = this.servo.handleHoming();
    if (!r.ok) {
      this.servoHomingRunning = false;
      return this.go('EMERGENCY');
    }
    this.servoHomingRunning = r.status === 'IN_PROGRESS';
    if (r.status === 'COMPLETED') {
      this.message('success', "Prise d'origine terminée");
      this.grafcetReset();
      this.go('IDLE');
    } else if (r.status !== 'IN_PROGRESS') {
      this.go('EMERGENCY');
    }
  }

  handleRun() {
    const jog = this.hasServo && this.ir.blocks.servo?.jog;
    if (jog && this.ev(jog.mode) && this.grafcetInInitialSituation() && !this.servoIsMoving()) return this.go('JOGGING');
    if (this.hasServo) {
      this.servoService();
      if (this.mode === 'EMERGENCY') return;
    }
    this.grafcetActivationActions();
    if (this.mode === 'EMERGENCY') return;
    for (let iter = 0; iter < MAX_EVOLUTIONS; iter++) {
      if (!this.grafcetEvolve()) break;
      if (iter === 0) for (const [k, v] of this.S) this.E.set(k, v); // un front ne vaut qu'une fois
      this.grafcetActivationActions();
      if (this.mode === 'EMERGENCY') return;
      if (iter === MAX_EVOLUTIONS - 1 && !this.unstableReported) {
        this.message('warning', 'Grafcet instable : boucle de franchissements sans fin');
        this.unstableReported = true;
      }
    }
    this.Xnew.fill(false);
    this.go(this.grafcetInInitialSituation() && !this.servoIsMoving() ? 'IDLE' : 'RUNNING');
  }

  handleJogging() {
    const jog = this.hasServo && this.ir.blocks.servo?.jog;
    if (!jog) return this.go('STOP');
    const s = this.servo;
    if (s.alarm) {
      this.message('error', 'Alarme détectée en JOG');
      return this.go('EMERGENCY');
    }
    if (!this.ev(jog.mode)) return this.go('STOP');
    let speed = Math.trunc(this.ev(jog.speed));
    speed = Math.max(1, Math.min(3000, speed));
    if (speed !== this.lastJogSpeed && !this.jogForwardHeld && !this.jogReverseHeld) {
      s.jogSpeed = speed;
      this.lastJogSpeed = speed;
    }
    const plus = this.ev(jog.plus);
    const minus = this.ev(jog.minus);
    if (plus && !minus) {
      if (this.jogReverseHeld) {
        s.jogStop();
        this.jogReverseHeld = false;
        return;
      }
      if (!this.jogBlocked) {
        this.jogForwardHeld = s.jog(1);
        this.jogBlocked = !this.jogForwardHeld;
      }
    } else if (minus && !plus) {
      if (this.jogForwardHeld) {
        s.jogStop();
        this.jogForwardHeld = false;
        return;
      }
      if (!this.jogBlocked) {
        this.jogReverseHeld = s.jog(-1);
        this.jogBlocked = !this.jogReverseHeld;
      }
    } else {
      if (this.jogForwardHeld || this.jogReverseHeld) s.jogStop();
      this.jogForwardHeld = this.jogReverseHeld = false;
      this.jogBlocked = false;
    }
  }

  handleEmergency() {
    if (this.emergencyCondition()) {
      this.emergencyClearSince = 0;
      return;
    }
    if (this.emergencyClearSince === 0) this.emergencyClearSince = this.now || 1;
    const reset = this.ir.blocks.emergencyReset;
    if (reset) {
      if (!this.ev(reset)) return;
    } else if (this.now - this.emergencyClearSince < 1000) return;
    if (this.hasServo) {
      if (this.servo.startReset()) {
        this.message('info', 'Réarmement du servo…');
        this.go('RESETTING');
      }
    } else {
      this.message('success', "Arrêt d'urgence acquitté");
      this.go('STOP');
    }
  }

  handleResetting() {
    if (!this.hasServo) return this.go('STOP');
    const s = this.servo;
    if (s.resetState === 'IN_PROGRESS') return;
    if (s.resetState === 'IDLE') {
      this.message('success', "Arrêt d'urgence acquitté");
      this.go(s.initialized && s.initState === 'IDLE' ? 'STOP' : 'INITIALIZING');
      return;
    }
    this.message('error', 'Échec du réarmement du servo');
    this.go('EMERGENCY');
  }

  // ---- servo
  servoService() {
    const s = this.servo;
    const sv = this.ir.blocks.servo;
    if (sv?.torque) {
      const t = Math.max(0, Math.min(100, Math.trunc(this.ev(sv.torque))));
      if (t !== this.lastTorque) {
        s.torque = t;
        this.lastTorque = t;
      }
    }
    if (this.servoMovePending) {
      if (this.servoPendingSpeed !== this.servoAppliedSpeed) {
        s.setSpeedAndRamp(this.servoPendingSpeed);
        this.servoAppliedSpeed = this.servoPendingSpeed;
        return;
      }
      if (s.isImmediateStopActive()) return;
      this.servoMovePending = false;
      if (s.startGoToPosition(this.servoPendingTarget)) this.servoMoveActive = true;
      else {
        this.message('error', `Déplacement servo refusé : ${s.lastError}`);
        this.go('EMERGENCY');
        return;
      }
    }
    if (this.servoMoveActive) {
      if (s.moveState === 'IDLE') {
        this.servoMoveActive = false;
        this.servoMoveDone = true;
      } else if (s.moveState !== 'IN_PROGRESS') {
        this.message('error', `Déplacement servo en échec : ${s.lastError}`);
        this.servoMoveActive = false;
        this.go('EMERGENCY');
        return;
      }
    }
    if (this.servoHomingActive) {
      const r = s.handleHoming();
      this.servoHomingRunning = r.ok && r.status === 'IN_PROGRESS';
      if (!r.ok || (r.status !== 'IN_PROGRESS' && r.status !== 'COMPLETED')) {
        this.servoHomingActive = false;
        this.go('EMERGENCY');
        return;
      }
      if (r.status === 'COMPLETED') this.servoHomingActive = false;
    }
  }
  servoMoveTo(target, speed) {
    target = Math.trunc(target);
    if (target < 0 || (this.maxRangePuu && target > this.maxRangePuu)) {
      this.message('error', `Cible servo hors course : ${target}`);
      this.go('EMERGENCY');
      return;
    }
    this.servoMoveDone = false;
    this.servoMovePending = true;
    this.servoPendingTarget = target;
    this.servoPendingSpeed = Math.max(0, Math.min(15, Math.trunc(speed)));
  }
  servoStartHoming() {
    this.servoMoveDone = false;
    this.servoHomingActive = true;
  }
  servoHalt(homingMode = false) {
    const s = this.servo;
    if (!s) return;
    if (this.jogForwardHeld || this.jogReverseHeld) s.jogStop();
    this.jogForwardHeld = this.jogReverseHeld = this.jogBlocked = false;
    const homing = this.servoHomingRunning && (this.servoHomingActive || homingMode);
    if (this.servoMoveActive || homing) s.stopMovement();
    if (homing) s.handleHoming(); // consomme l'état interne (voir le C++)
    this.servoHomingRunning = false;
    this.servoMovePending = this.servoMoveActive = this.servoHomingActive = false;
    this.servoMoveDone = false;
  }

  // ---- grafcet
  grafcetClear() {
    this.X.fill(false);
    this.Xnew.fill(false);
    this.Xstart.fill(0);
    this.latch.clear();
    this.unstableReported = false;
  }
  grafcetReset() {
    this.grafcetClear();
    this.ir.steps.forEach((s, i) => s.initial && this.activateStep(i));
  }
  activateStep(i) {
    this.X[i] = true;
    this.Xnew[i] = true;
    this.Xstart[i] = this.now;
  }
  grafcetInInitialSituation() {
    return this.ir.steps.every((s, i) => this.X[i] === !!s.initial);
  }
  grafcetActivationActions() {
    this.ir.steps.forEach((s, i) => {
      if (!this.Xnew[i]) return;
      for (const a of s.actions) {
        switch (a.type) {
          case 'S':
            this.latch.set(a.target, true);
            break;
          case 'R':
            this.latch.set(a.target, false);
            break;
          case 'SET':
            if (!this.drivenSet.has(a.target)) this.set(a.target, this.ev(a.value));
            break;
          case 'INC':
            if (!this.drivenSet.has(a.target)) this.set(a.target, this.get(a.target) + 1);
            break;
          case 'DEC':
            if (!this.drivenSet.has(a.target)) this.set(a.target, this.get(a.target) - 1);
            break;
          case 'SERVO_MOVE':
            this.servoMoveTo(this.ev(a.position), this.ev(a.speed));
            break;
          case 'SERVO_HOME':
            this.servoStartHoming();
            break;
          case 'SERVO_STOP':
            this.servoHalt();
            break;
          case 'MSG':
            this.message(a.level, a.text);
            break;
        }
      }
    });
    this.Xnew.fill(false);
  }
  grafcetEvolve() {
    const fire = this.ir.transitions.map((t) => t.from.length > 0 && t.from.every((k) => this.X[k]) && !!t.cond && !!this.ev(t.cond));
    if (!fire.some(Boolean)) return false;
    this.ir.transitions.forEach((t, i) => fire[i] && t.from.forEach((k) => (this.X[k] = false)));
    this.ir.transitions.forEach((t, i) => {
      if (!fire[i]) return;
      t.to.forEach((k) => this.activateStep(k));
      this.fired.push(t.id);
      this.log('transition', `T${t.num} franchie`);
    });
    return true;
  }
  // Transitions validées et réceptivité vraie, pour l'affichage.
  transitionStates() {
    return this.ir.transitions.map((t) => {
      const enabled = t.from.length > 0 && t.from.every((k) => this.X[k]);
      let cond = false;
      try {
        cond = !!t.cond && !!this.ev(t.cond);
      } catch {
        cond = false;
      }
      return { id: t.id, enabled, cond };
    });
  }

  computeOutputs() {
    const run = this.mode === 'IDLE' || this.mode === 'RUNNING';
    const emergency = this.mode === 'EMERGENCY' || this.mode === 'RESETTING';
    for (const d of this.ir.driven) {
      let v = false;
      if (run) {
        this.ir.steps.forEach((s, i) => {
          if (!this.X[i]) return;
          for (const a of s.actions) if (a.type === 'N' && a.target === d.sym && (!a.condition || this.ev(a.condition))) v = true;
        });
        if (d.latch && this.latch.get(d.sym)) v = true;
      } else if (emergency) {
        const variable = this.vars.get(d.sym);
        v = variable?.kind === 'output' ? (variable.fallback === 'on' ? true : variable.fallback === 'hold' ? this.get(d.sym) : false) : false;
      }
      this.set(d.sym, v);
    }
    if (run) {
      this.ir.steps.forEach((s, i) => {
        if (!this.X[i]) return;
        for (const a of s.actions) if (a.type === 'NSET' && !this.drivenSet.has(a.target)) this.set(a.target, this.ev(a.value));
      });
    }
    for (const v of this.project.variables) {
      if (v.kind !== 'output' || v.system || this.drivenSet.has(v.symbol)) continue;
      if (emergency) {
        if (v.fallback !== 'hold') this.set(v.symbol, v.fallback === 'on');
      } else if (!run) this.set(v.symbol, false);
    }
  }

  checkParams() {
    for (const v of this.project.variables) {
      if (v.kind !== 'parameter' || (v.dataType !== 'int' && v.dataType !== 'float')) continue;
      const has = (x) => x !== undefined && x !== null && x !== '' && Number.isFinite(Number(x));
      const val = this.get(v.symbol);
      if (has(v.min) && val < Number(v.min)) this.set(v.symbol, Number(v.min));
      if (has(v.max) && val > Number(v.max)) this.set(v.symbol, Number(v.max));
    }
  }

  captureEdges() {
    for (const e of this.ir.edges) this.S.set(e.key, !!this.raw(e.ast));
  }
  updateEdges() {
    for (const [k, v] of this.S) this.E.set(k, v);
  }

  activeSteps() {
    return this.ir.steps.filter((_, i) => this.X[i]).map((s) => s.num);
  }

  run(ms) {
    const n = Math.max(1, Math.round(ms / this.scanMs));
    for (let i = 0; i < n; i++) this.scan();
  }
}

export { edgeKey };

// Simulateur : exécute le projet comme la classe C++ générée (shared/cpp.js).
//
// Même représentation intermédiaire (compileLogic), même ordre de cycle, mêmes modes de
// marche. Chaque méthode de Simulator correspond à la méthode C++ de même nom. Chaque axe
// est piloté par AxisSim (calqué sur src/AxisController) et son variateur est remplacé par
// ServoModel, qui reproduit les états internes utiles à la logique (initialisation,
// déplacement, prise d'origine, jog, arrêt, urgence, réarmement).

import { compileLogic } from './grafcet.js';
import { evaluate, edgeKey } from './expr.js';
import { axisList } from './axes.js';

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
// Modèle simplifié d'un variateur (états calqués sur src/SureServo/SureServo.cpp, que
// LichuanServo et StepperMotor reproduisent). speedRate(v) : unités/s pour la vitesse v
// passée à setMoveSpeed (SureServo : index 0-15, Lichuan : tr/min, pas-à-pas : pas/s).
export class ServoModel {
  constructor({ maxRange = 0, initDelay = 800, puuPerSecondPerSpeed = 8000, speedRate = null, jogRate = 20 } = {}) {
    this.maxRange = maxRange;
    this.initDelay = initDelay;
    this.rate = puuPerSecondPerSpeed;
    this.speedRate = speedRate || ((v) => this.rate * (v + 1));
    this.jogRate = jogRate;
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
  // Comme ServoDrive::setMoveSpeed (unités natives, bornées par AxisController).
  setMoveSpeed(speed) {
    this.speedIdx = speed;
  }
  // Comme handleJogging(status) : erreur si le variateur est en alarme.
  handleJogging() {
    return this.alarm ? 'SERVO_DRIVE_ERROR' : 'IN_PROGRESS';
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
      else if (stepTowards(this.target, this.speedRate(this.speedIdx))) this.moveState = 'IDLE';
    }
    if (this.homingState === 'IN_PROGRESS') {
      if (this.alarm) this.homingState = 'FAILED';
      else if (stepTowards(0, this.rate * 4)) {
        this.homingState = 'IDLE';
        this.homeDone = true;
      }
    }
    if (this.jogDir) {
      this.position += (this.jogDir * this.jogSpeed * this.jogRate * dt) / 1000;
      if (this.homeDone && this.maxRange > 0 && this.position > this.maxRange) this.position = this.maxRange;
      if (this.homeDone && this.position < 0) this.position = 0;
    }
    this.position = Math.round(this.position);
  }
}

// ---------------------------------------------------------------------------
// Pilotage d'un axe : copie de src/AxisController/AxisController.cpp (mêmes états, mêmes
// messages). Les méthodes qui renvoient false / 'FAILED' ont déjà affiché le message.
const SPEED_MODELS = {
  sureservo: { speedRate: (v) => 8000 * (v + 1), jogRate: 20 },
  lichuan: { speedRate: (rpm) => Math.max(1, rpm) * 200, jogRate: 200 / 60 },
  stepper: { speedRate: (sps) => Math.max(1, sps), jogRate: 1 },
  // StepperOnline RS : tr/min, 10 000 pas par tour (Pr0.00 par défaut)
  'stepperonline-rs': { speedRate: (rpm) => (Math.max(1, rpm) * 10000) / 60, jogRate: 10000 / 60 },
  'stepperonline-servo': { speedRate: (rpm) => (Math.max(1, rpm) * 10000) / 60, jogRate: 10000 / 60 },
};

export class AxisSim {
  constructor(axis, block, sim) {
    const d = axis.entry.axis || {};
    const factor = Number(axis.eq.options?.pulsesPerUnit) || Number(axis.eq.options?.stepsPerUnit) || 1;
    this.uid = axis.uid;
    this.symbol = axis.symbol;
    this.label = axis.label;
    this.block = block || { homing: 'auto', homingOrder: 1 };
    this.sim = sim;
    this.cfg = {
      minPosition: 0,
      maxPosition: Math.max(0, Math.round((Number(axis.eq.options?.maxRange) || 0) * factor)),
      minSpeed: d.speedMin ?? 0,
      maxSpeed: d.speedMax ?? 15,
      jogMinSpeed: d.jogSpeedMin ?? 1,
      jogMaxSpeed: d.jogSpeedMax ?? 3000,
    };
    this.drive = new ServoModel({ maxRange: this.cfg.maxPosition, ...(SPEED_MODELS[d.kind] || SPEED_MODELS.sureservo) });
    this.initRequested = false;
    this.moveActive = false;
    this.moveDone = false;
    this.movePending = false;
    this.pendingTarget = 0;
    this.pendingSpeed = 0;
    this.appliedSpeed = -1;
    this.homingActive = false;
    this.homingArmed = false;
    this.homingRunning = false;
    this.resetRequested = false;
    this.jogForwardHeld = false;
    this.jogReverseHeld = false;
    this.jogBlocked = false;
    this.lastJogSpeed = -1;
    this.lastTorque = -1;
  }
  report(what, detail) {
    this.sim.message('error', detail ? `${this.label} : ${what} : ${detail}` : `${this.label} : ${what}`);
  }
  // ---- état
  isReady() {
    return this.drive.ready;
  }
  inPosition() {
    return this.moveDone;
  }
  isMoving() {
    return this.moveActive || this.movePending || this.homingActive;
  }
  homeDone() {
    return this.drive.homeDone;
  }
  hasAlarm() {
    return this.drive.alarm;
  }
  position() {
    return this.drive.position | 0;
  }
  prop(name) {
    switch (name) {
      case 'pret':
        return this.isReady();
      case 'enPosition':
        return this.inPosition();
      case 'enMouvement':
        return this.isMoving();
      case 'origineFaite':
        return this.homeDone();
      case 'alarme':
        return this.hasAlarm();
      case 'position':
        return this.position();
    }
    return false;
  }
  // ---- cycle
  service() {
    const s = this.drive;
    if (this.movePending) {
      if (this.pendingSpeed !== this.appliedSpeed) {
        s.setMoveSpeed(this.pendingSpeed);
        this.appliedSpeed = this.pendingSpeed;
        return true;
      }
      if (s.isImmediateStopActive()) return true;
      this.movePending = false;
      if (s.startGoToPosition(this.pendingTarget)) this.moveActive = true;
      else {
        this.report('déplacement refusé', s.lastError);
        return false;
      }
    }
    if (this.moveActive) {
      if (s.moveState === 'IDLE') {
        this.moveActive = false;
        this.moveDone = true;
      } else if (s.moveState !== 'IN_PROGRESS') {
        this.moveActive = false;
        this.report('déplacement en échec', s.lastError);
        return false;
      }
    }
    if (this.homingActive) {
      const r = s.handleHoming();
      this.homingRunning = r.ok && r.status === 'IN_PROGRESS';
      if (!r.ok || (r.status !== 'IN_PROGRESS' && r.status !== 'COMPLETED')) {
        this.homingActive = false;
        this.report("prise d'origine en échec", s.lastError);
        return false;
      }
      if (r.status === 'COMPLETED') this.homingActive = false;
    }
    return true;
  }
  moveTo(target, speed) {
    target = Math.trunc(target);
    if (target < this.cfg.minPosition || (this.cfg.maxPosition !== 0 && target > this.cfg.maxPosition)) {
      this.report('cible hors course', String(target));
      return false;
    }
    speed = Math.max(this.cfg.minSpeed, Math.min(this.cfg.maxSpeed, Math.trunc(speed)));
    this.moveDone = false;
    this.movePending = true;
    this.pendingTarget = target;
    this.pendingSpeed = speed;
    return true;
  }
  startHoming() {
    this.moveDone = false;
    this.homingActive = true;
  }
  halt(homingMode = false) {
    const s = this.drive;
    if (this.jogForwardHeld || this.jogReverseHeld) s.jogStop();
    this.jogForwardHeld = this.jogReverseHeld = this.jogBlocked = false;
    const homing = this.homingRunning && (this.homingActive || this.homingArmed || homingMode);
    if (this.moveActive || homing) s.stopMovement();
    if (homing) s.handleHoming(); // consomme l'état interne (voir le C++)
    this.homingRunning = false;
    this.movePending = this.moveActive = this.homingActive = this.homingArmed = false;
    this.moveDone = false;
  }
  setTorque(percent) {
    const t = Math.max(0, Math.min(100, Math.trunc(percent)));
    if (t !== this.lastTorque) {
      this.drive.torque = t;
      this.lastTorque = t;
    }
  }
  // ---- modes
  initStep() {
    const s = this.drive;
    if (!this.initRequested) {
      if (s.initState === 'IN_PROGRESS') return 'RUNNING'; // relancée par le réarmement
      if (s.initState === 'IDLE' && s.initialized) return 'DONE';
    }
    const r = s.handleInitializing();
    if (!r.ok) {
      this.initRequested = false;
      this.report('initialisation impossible', s.lastError);
      return 'FAILED';
    }
    this.initRequested = true;
    if (r.status === 'COMPLETED') {
      this.initRequested = false;
      this.appliedSpeed = -1;
      this.lastTorque = -1;
      return 'DONE';
    }
    if (r.status === 'IN_PROGRESS') return 'RUNNING';
    this.initRequested = false;
    this.report('initialisation en échec', s.lastError);
    return 'FAILED';
  }
  armHoming() {
    if (!this.drive.homeDone) this.homingArmed = true;
  }
  homingStep() {
    if (!this.homingArmed) return 'DONE';
    const r = this.drive.handleHoming();
    if (!r.ok) {
      this.homingRunning = this.homingArmed = false;
      this.report("prise d'origine impossible", this.drive.lastError);
      return 'FAILED';
    }
    this.homingRunning = r.status === 'IN_PROGRESS';
    if (r.status === 'IN_PROGRESS') return 'RUNNING';
    this.homingArmed = false;
    if (r.status === 'COMPLETED') return 'DONE';
    this.report("prise d'origine en échec", this.drive.lastError);
    return 'FAILED';
  }
  startReset() {
    if (this.resetRequested) return true;
    this.resetRequested = this.drive.startReset();
    return this.resetRequested;
  }
  resetStep() {
    if (!this.resetRequested) return 'DONE';
    const st = this.drive.resetState;
    if (st === 'IN_PROGRESS') return 'RUNNING';
    this.resetRequested = false;
    if (st === 'IDLE') return 'DONE';
    this.report('échec du réarmement', this.drive.lastError);
    return 'FAILED';
  }
  needsInitialization() {
    return !this.drive.initialized || this.drive.initState !== 'IDLE';
  }
  jogStep(plus, minus, speed) {
    const s = this.drive;
    if (s.handleJogging() === 'SERVO_DRIVE_ERROR') {
      this.report('défaut pendant le jog', s.lastError);
      return false;
    }
    speed = Math.max(this.cfg.jogMinSpeed, Math.min(this.cfg.jogMaxSpeed, Math.trunc(speed)));
    if (speed !== this.lastJogSpeed && !this.jogForwardHeld && !this.jogReverseHeld) {
      s.jogSpeed = speed;
      this.lastJogSpeed = speed;
    }
    if (plus && !minus) {
      if (this.jogReverseHeld) {
        s.jogStop();
        this.jogReverseHeld = false;
        return true;
      }
      if (!this.jogBlocked) {
        this.jogForwardHeld = s.jog(1);
        this.jogBlocked = !this.jogForwardHeld;
      }
    } else if (minus && !plus) {
      if (this.jogForwardHeld) {
        s.jogStop();
        this.jogForwardHeld = false;
        return true;
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
    return true;
  }
  onEmergency() {
    if (this.jogForwardHeld || this.jogReverseHeld) this.drive.jogStop();
    this.drive.emergencyStop();
    this.movePending = this.moveActive = this.homingActive = this.homingArmed = this.homingRunning = false;
    this.jogForwardHeld = this.jogReverseHeld = this.jogBlocked = false;
    this.initRequested = false;
    this.resetRequested = false;
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
    this.axisDefs = axisList(project);
    this.hasServo = this.axisDefs.length > 0; // ancien nom : au moins un axe
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
    this.axes = this.axisDefs.map((a) => new AxisSim(a, (this.ir.axes || []).find((b) => b.uid === a.uid), this));
    this.axisByUid = new Map(this.axes.map((a) => [a.uid, a]));
    this.go('INITIALIZING');
  }

  // ---- accès
  get(sym) {
    return this.values.get(sym);
  }
  set(sym, value) {
    const v = this.vars.get(sym);
    if (!v) return;
    if (v.dataType === 'int') {
      value = Math.trunc(Number(value) || 0) | 0;
      // Registre Modbus écrit (Uint16OutputNode) : 0 à 65535 comme dans le firmware.
      if (v.kind === 'output') value &= 0xffff;
    }
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
      axis: (uid, prop) => this.axisByUid.get(uid)?.prop(prop) ?? (prop === 'position' ? 0 : false),
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
  // Axe par nom (symbole ou libellé, sans casse) ; sans nom : l'axe unique / le premier.
  axis(name) {
    if (name === undefined || name === null || name === '' || /^servo$/i.test(String(name))) {
      const byName = this.axes.find((a) => /^servo$/i.test(a.symbol));
      return byName || this.axes[0] || null;
    }
    const n = String(name).toLowerCase();
    return this.axes.find((a) => a.symbol.toLowerCase() === n || a.label.toLowerCase() === n) || null;
  }
  // Premier axe (ancien nom : le servo unique du projet).
  get servo() {
    return this.axes[0]?.drive || null;
  }
  // Propriété d'un axe ; « Servo » ou rien : l'axe unique (anciens projets).
  axisProp(name, prop) {
    const a = this.axis(name);
    return a ? a.prop(prop) : prop === 'position' ? 0 : false;
  }
  servoProp(prop) {
    return this.axisProp(null, prop);
  }
  axesMoving() {
    return this.axes.some((a) => a.isMoving());
  }
  servoIsMoving() {
    return this.axesMoving();
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
      for (const a of this.axes) a.onEmergency();
      this.emergencyClearSince = 0;
      this.grafcetClear();
    } else if (mode === 'STOP') {
      for (const a of this.axes) a.halt(old === 'HOMING');
      this.grafcetClear();
    }
  }

  // ---- un cycle d'automate (doBusinessLogic)
  scan(dt = this.scanMs) {
    this.now += dt;
    this.fired = [];
    for (const a of this.axes) a.drive.process(dt);
    this.captureEdges();
    const b = this.ir.blocks;

    const m = this.mode;
    if (m !== 'EMERGENCY' && m !== 'UNDEFINED') {
      if (this.emergencyCondition()) {
        this.message('error', `Arrêt d'urgence : ${this.project.logic?.blocks?.emergency?.condition || ''}`);
        this.go('EMERGENCY');
      } else if (m !== 'RESETTING') {
        const a = this.axes.find((x) => x.hasAlarm());
        if (a) {
          this.message('error', `Alarme ${a.label} 0x${a.drive.alarmCode.toString(16).padStart(4, '0').toUpperCase()}`);
          this.go('EMERGENCY');
        }
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
    if (!this.axes.length) return this.go('STOP');
    // Tous les variateurs s'initialisent en parallèle ; ARRÊT quand ils ont tous fini.
    let done = true;
    let failed = false;
    for (const a of this.axes) {
      const p = a.initStep();
      if (p === 'RUNNING') done = false;
      else if (p === 'FAILED') failed = true;
    }
    if (failed) return this.go('EMERGENCY');
    if (done) this.go('STOP');
  }

  homingAxes() {
    return this.axes.filter((a) => a.block.homing !== 'none');
  }
  jogAxes() {
    return this.axes.filter((a) => a.block.jog);
  }
  jogModeActive() {
    return this.jogAxes().some((a) => this.ev(a.block.jog.mode));
  }
  homingNeeded() {
    return this.homingAxes().some((a) => !a.homeDone());
  }

  handleStop() {
    const b = this.ir.blocks;
    if (b.stop && this.ev(b.stop)) return;
    if (this.jogAxes().length && this.jogModeActive()) return this.go('JOGGING');
    if (!(b.start ? this.ev(b.start) : true)) return;
    if (this.homingNeeded()) {
      let armed = false;
      for (const a of this.homingAxes()) {
        const trigger = a.block.homing === 'condition' ? this.ev(a.block.homingCondition) : true;
        if (!a.homeDone() && trigger) {
          a.armHoming();
          armed = true;
        }
      }
      if (armed) this.go('HOMING');
      return;
    }
    this.grafcetReset();
    this.go('IDLE');
  }

  handleHoming() {
    const axes = this.homingAxes();
    if (!axes.length) return this.go('STOP');
    // Prises d'origine par ordre croissant ; les axes d'un même ordre en parallèle.
    const orders = [...new Set(axes.map((a) => a.block.homingOrder || 1))].sort((x, y) => x - y);
    for (const order of orders) {
      const results = axes.filter((a) => (a.block.homingOrder || 1) === order).map((a) => a.homingStep());
      if (results.includes('FAILED')) return this.go('EMERGENCY');
      if (results.some((r) => r !== 'DONE')) return;
    }
    this.message('success', "Prise d'origine terminée");
    if (this.homingNeeded()) return this.go('STOP'); // axe à condition non déclenchée
    this.grafcetReset();
    this.go('IDLE');
  }

  handleRun() {
    if (this.jogAxes().length && this.jogModeActive() && this.grafcetInInitialSituation() && !this.axesMoving()) return this.go('JOGGING');
    if (this.axes.length && !this.axesService()) return this.go('EMERGENCY');
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
    this.go(this.grafcetInInitialSituation() && !this.axesMoving() ? 'IDLE' : 'RUNNING');
  }

  handleJogging() {
    const axes = this.jogAxes();
    if (!axes.length) return this.go('STOP');
    let anyMode = false;
    for (const a of axes) {
      const j = a.block.jog;
      const mode = !!this.ev(j.mode);
      anyMode = anyMode || mode;
      if (!a.jogStep(mode && !!this.ev(j.plus), mode && !!this.ev(j.minus), this.ev(j.speed))) return this.go('EMERGENCY');
    }
    if (!anyMode) this.go('STOP'); // le jog est arrêté en entrant dans ARRÊT
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
    if (this.axes.length) {
      let started = true;
      for (const a of this.axes) started = a.startReset() && started;
      if (started) {
        this.message('info', this.axes.length === 1 ? `Réarmement de ${this.axes[0].label}…` : 'Réarmement des axes…');
        this.go('RESETTING');
      }
    } else {
      this.message('success', "Arrêt d'urgence acquitté");
      this.go('STOP');
    }
  }

  handleResetting() {
    if (!this.axes.length) return this.go('STOP');
    let done = true;
    for (const a of this.axes) {
      const p = a.resetStep();
      if (p === 'RUNNING') done = false;
      else if (p === 'FAILED') return this.go('EMERGENCY');
    }
    if (!done) return;
    this.message('success', "Arrêt d'urgence acquitté");
    this.go(this.axes.some((a) => a.needsInitialization()) ? 'INITIALIZING' : 'STOP');
  }

  // ---- axes : déplacements et prises d'origine du grafcet, couple maximal
  axesService() {
    for (const a of this.axes) {
      if (a.block.torque) a.setTorque(this.ev(a.block.torque));
      if (!a.service()) return false;
    }
    return true;
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
            // Cible hors course : message affiché par l'axe, passage en URGENCE.
            if (!this.axisByUid.get(a.axis).moveTo(this.ev(a.position), this.ev(a.speed))) this.go('EMERGENCY');
            break;
          case 'SERVO_HOME':
            this.axisByUid.get(a.axis).startHoming();
            break;
          case 'SERVO_STOP':
            this.axisByUid.get(a.axis).halt();
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

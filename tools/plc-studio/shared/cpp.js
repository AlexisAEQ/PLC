// Génération de la classe C++ d'application (dérivée de BusinessLogic).

import { toCpp, SERVO_PROPS } from './expr.js';
import { CATALOG } from './catalog.js';
import { paramsFileName } from './model.js';

const CPP_TYPE = { bool: 'bool', int: 'int32_t', float: 'float', text: 'const char*' };
const DEFAULT_VALUE = { bool: 'false', int: '0', float: '0.0f', text: '""' };

function cstr(s) {
  return JSON.stringify(String(s ?? '')).replace(/\\u00([0-9a-f]{2})/gi, '\\x$1');
}

function cppLiteral(value, type) {
  if (type === 'bool') return value === true || value === 'true' ? 'true' : 'false';
  if (type === 'int') return String(Math.trunc(Number(value) || 0));
  if (type === 'float') {
    const n = Number(value) || 0;
    return `${Number.isInteger(n) ? n.toFixed(1) : n}f`;
  }
  return cstr(value);
}

const ident = (s) => String(s).replace(/[^A-Za-z0-9_]/g, '_');

// ---------------------------------------------------------------------------
// Accès typés aux variables (lecture v_X(), écriture set_X(x)).
function variableAccess(project, layout) {
  const out = new Map(); // symbole -> { members, init, getter, setter, type, node }
  const banks = new Map(); // ref banc -> { name, cls, hash }
  for (const v of project.variables) {
    const node = layout.nodes.get(`var:${v.uid}`);
    const type = v.dataType;
    const t = CPP_TYPE[type];
    const acc = { type, cppType: t, variable: v, node, members: [], init: [] };
    if (!node) {
      // Mémoire interne pure : simple membre.
      acc.members.push(`${t} m_${v.symbol} = ${type === 'text' ? '""' : DEFAULT_VALUE[type]};`);
      acc.get = `m_${v.symbol}`;
      acc.set = (x) => `m_${v.symbol} = ${x};`;
      out.set(v.symbol, acc);
      continue;
    }
    if (node.bankRef) {
      let bank = banks.get(node.bankRef);
      if (!bank) {
        bank = { name: `bank_${banks.size}`, cls: node.cppClass, hashName: `BANK_${banks.size}`, hash: node.hash, section: node.section };
        banks.set(node.bankRef, bank);
      }
      if (node.bank.dir === 'in') {
        acc.get = `bankBit(${bank.name}, ${node.bit})`;
      } else {
        acc.get = `(${bank.name} ? ${bank.name}->getValue(${node.bit}) : false)`;
        acc.set = (x) => `if (${bank.name}) ${bank.name}->setValue(${x}, ${node.bit});`;
      }
      acc.hashName = null;
      out.set(v.symbol, acc);
      continue;
    }
    const ptr = `n_${v.symbol}`;
    acc.members.push(`${node.cppClass}* ${ptr} = nullptr;`);
    acc.hashName = v.symbol;
    acc.init.push(`initNodePtr(${ptr}, ${project.name}Nodes::${v.symbol}, context);`);
    switch (node.cppClass) {
      case 'BooleanInputNode':
        acc.get = `${ptr}->getValue()`;
        break;
      case 'VirtualBooleanInputNode':
        acc.get = `${ptr}->getValue()`;
        acc.set = (x) => `{ bool _v = ${x}; if (${ptr}->getValue() != _v) ${ptr}->setValue(_v); }`;
        break;
      case 'BooleanOutputNode':
        acc.get = `${ptr}->getValue()`;
        acc.set = (x) => `${ptr}->setValue(${x});`;
        break;
      case 'Uint16InputNode':
        acc.get = `(int32_t)${ptr}->getValue()`;
        break;
      case 'VirtualUint32InputNode':
        acc.get = `${ptr}->getValueAsInt32()`;
        acc.set = (x) => `{ int32_t _v = ${x}; if (${ptr}->getValueAsInt32() != _v) ${ptr}->setValue((uint32_t)_v); }`;
        break;
      case 'Uint32OutputNode':
        acc.get = `${ptr}->getValueAsInt32()`;
        acc.set = (x) => `{ int32_t _v = ${x}; if (${ptr}->getValueAsInt32() != _v) ${ptr}->setValueFromInt32(_v); }`;
        break;
      case 'VirtualFloatInputNode':
      case 'FloatOutputNode':
        acc.get = `${ptr}->getValue()`;
        acc.set = (x) => `{ float _v = ${x}; if (${ptr}->getValue() != _v) ${ptr}->setValue(_v); }`;
        break;
      case 'VirtualTextOutputNode':
      case 'VirtualTextInputNode':
        acc.get = `(const char*)${ptr}->getValue()`;
        acc.set = (x) => `{ const char* _v = ${x}; if (strcmp(${ptr}->getValue(), _v) != 0) ${ptr}->setValue(_v); }`;
        break;
      default:
        acc.get = `${ptr}->getValue()`;
    }
    out.set(v.symbol, acc);
  }
  return { access: out, banks };
}

// ---------------------------------------------------------------------------
export function generateCpp(project, layout, ir) {
  const N = project.name;
  const servoEq = project.equipment.find((e) => CATALOG[e.type]?.role === 'servo');
  const hasServo = !!servoEq && ir.servo;
  // Course maximale en impulsions (PUU) = course (unités) x impulsions par unité.
  const maxRangePuu = hasServo ? Math.max(0, Math.round((Number(servoEq.options?.maxRange) || 0) * (Number(servoEq.options?.pulsesPerUnit) || 1))) : 0;
  const { access, banks } = variableAccess(project, layout);
  const stepIndex = new Map(ir.steps.map((s, i) => [s.num, i]));
  const edgeIndex = new Map(ir.edges.map((e, i) => [e.key, i]));
  const stateNode = layout.nodes.get('sys:state');
  const clockNode = layout.nodes.get('sys:clock');
  const outputs = project.variables.filter((v) => v.kind === 'output' && !v.system);
  const params = project.variables.filter((v) => v.kind === 'parameter');
  const drivenSet = new Map(ir.driven.map((d) => [d.sym, d]));

  const names = {
    var: (sym) => `v_${sym}()`,
    step: (num) => `X[${stepIndex.get(num)}]`,
    stepTime: (num) => `stepTime(${stepIndex.get(num)})`,
    servo: (prop) => SERVO_PROPS[prop].cpp,
    prev: (key) => `E[${edgeIndex.get(key)}]`,
  };
  const cx = (ast) => toCpp(ast, names);
  const setCall = (sym, valueCpp) => `set_${sym}(${valueCpp});`;

  // ---------------- En-tête ----------------
  const H = [];
  H.push(
    `// =============================================================================`,
    `// ${N}.h — généré par PLC Studio (tools/plc-studio)`,
    `// NE PAS MODIFIER À LA MAIN : ce fichier est régénéré depuis projets/${N}.plc.json`,
    `// =============================================================================`,
    `#pragma once`,
    ``,
    `#include "ArduinoJson.h"`,
    `#include <memory>`,
    `#include <string.h>`,
    `#include "Node/Node.h"`,
    `#include "PLC_Persistence/PLC_Persistence.h"`,
    `#include "PLC_CommonTypes/PLC_CommonTypes.h"`,
    `#include "BusinessLogic/BusinessLogic.h"`,
    `#include "PLC_Tools/PLC_Tools.h"`,
    `#include "BusinessLogic/BusinessLogic.inl"`
  );
  if (hasServo) H.push(`#include "SureServo/SureServo.h"`);
  H.push(``, `// Identifiants des nœuds (CRC-32 de "<id><section>", voir data/config.json).`, `namespace ${N}Nodes {`);
  for (const [sym, acc] of access) {
    if (!acc.hashName) continue;
    H.push(`    constexpr uint32_t ${sym} = ${acc.node.hash}u;  // ${acc.node.section} / id ${acc.node.id}`);
  }
  for (const b of banks.values()) H.push(`    constexpr uint32_t ${b.hashName} = ${b.hash}u;  // ${b.section}`);
  if (stateNode) H.push(`    constexpr uint32_t SYS_STATE = ${stateNode.hash}u;`);
  if (clockNode) H.push(`    constexpr uint32_t SYS_CLOCK = ${clockNode.hash}u;`);
  if (hasServo) {
    for (const [ref, node] of layout.nodes) {
      if (ref.startsWith('servo:')) H.push(`    constexpr uint32_t SERVO_${ident(node.field)} = ${node.hash}u;  // ${node.section} / id ${node.id}`);
    }
  }
  H.push(`}`, ``);

  const stepCount = Math.max(1, ir.steps.length);
  const transCount = Math.max(1, ir.transitions.length);
  const edgeCount = Math.max(1, ir.edges.length);

  H.push(
    `class ${N} : public BusinessLogic {`,
    `public:`,
    `    enum class State : int {`,
    `        UNDEFINED    = static_cast<int>(BaseState::UNDEFINED),`,
    `        INITIALIZING = static_cast<int>(BaseState::INITIALIZING),`,
    `        STOP         = static_cast<int>(BaseState::STOP),`,
    `        IDLE         = static_cast<int>(BaseState::IDLE),       // grafcet en situation initiale`,
    `        EMERGENCY    = static_cast<int>(BaseState::EMERGENCY),`,
    `        JOGGING      = static_cast<int>(BaseState::JOGGING),`,
    `        HOMING       = 101,`,
    `        RUNNING      = 102,                                      // grafcet en cycle`,
    `        RESETTING    = 103`,
    `    };`,
    ``,
    `    static constexpr const char* PARAMS_FILE = "/${paramsFileName(project)}";`,
    ``,
    `    ${N}();`,
    `    virtual ~${N}();`,
    ``,
    `    // Appelées par main.cpp`,
    `    bool initializePointers(const char* context);`,
    `    bool startStateMachine();`,
    `    void printPersistance();`,
    `#ifdef PERF_MONITOR`,
    hasServo ? `    bool isPmActive() const { return sureServo && sureServo->isPmActive(); }` : `    bool isPmActive() const { return false; }`,
    hasServo ? `    void pmAccumulateRefresh(uint32_t ms) { if (sureServo) sureServo->pmAccumulateRefresh(ms); }` : `    void pmAccumulateRefresh(uint32_t) {}`,
    `#endif`,
    ``,
    `    // BusinessLogic`,
    `    bool doBusinessLogic() override;`,
    `    const char* stateToString(int state) const override;`,
    `    bool canDeleteFile(const char* path) override;`,
    `    bool validateConfigFile(const char* path, const JsonDocument& doc, String& errorMsg) override;`,
    `    void initialStateLoadedHandler();`,
    ``,
    `protected:`,
    `    StateType classifyState(int state) const override;`,
    `    bool shouldShowClock() const override;`,
    `    void onStateEnter(int newState, int oldState) override;`,
    `    void onStateExit(int oldState, int newState) override;`,
    `    void onFileSaved(const char* path, bool success) override;`,
    ``,
    `private:`,
    `    // ---- Nœuds`
  );
  for (const acc of access.values()) for (const m of acc.members) H.push(`    ${m}`);
  for (const b of banks.values()) H.push(`    ${b.cls}* ${b.name} = nullptr;`);
  if (stateNode) H.push(`    VirtualTextOutputNode* n_sys_state = nullptr;`);
  if (clockNode) H.push(`    VirtualTextOutputNode* n_sys_clock = nullptr;`);

  H.push(
    ``,
    `    // ---- Grafcet (${ir.steps.length} étapes, ${ir.transitions.length} transitions)`,
    `    static constexpr int STEP_COUNT = ${stepCount};`,
    `    static constexpr int TRANSITION_COUNT = ${transCount};`,
    `    static constexpr int EDGE_COUNT = ${edgeCount};`,
    `    bool X[STEP_COUNT] = {};          // étapes actives`,
    `    bool Xnew[STEP_COUNT] = {};       // étapes activées au cycle précédent (actions à l'activation)`,
    `    uint32_t Xstart[STEP_COUNT] = {}; // instant d'activation`,
    `    bool E[EDGE_COUNT] = {};          // valeurs précédentes pour les fronts`,
    `    uint32_t now = 0;`,
    `    uint32_t emergencyClearSince = 0;`,
    `    bool initialised = false;`
  );
  for (const d of ir.driven) if (d.latch) H.push(`    bool latch_${d.sym} = false;`);
  if (hasServo) {
    H.push(
      ``,
      `    // ---- Servo`,
      `    std::unique_ptr<SureServo> sureServo;`,
      `    ServoNodes servoNodes;`,
      `    bool servoMoveActive = false;   // déplacement lancé, en attente de fin`,
      `    bool servoMoveDone = false;     // dernier déplacement terminé (Servo.enPosition)`,
      `    bool servoMovePending = false;  // déplacement demandé, pas encore lancé`,
      `    int32_t servoPendingTarget = 0;`,
      `    int servoPendingSpeed = 0;`,
      `    int servoAppliedSpeed = -1;`,
      `    bool servoHomingActive = false;`,
      `    bool jogForwardHeld = false;`,
      `    bool jogReverseHeld = false;`,
      `    int lastJogSpeed = -1;`,
      `    int lastTorque = -1;`
    );
  }
  H.push(``, `    // ---- Paramètres sauvegardés dans ${'/' + paramsFileName(project)}`);
  for (const p of params) {
    if (p.dataType === 'text') H.push(`    String init_${p.symbol} = ${cstr(p.initial ?? '')};`, `    String saved_${p.symbol};`);
    else H.push(`    ${CPP_TYPE[p.dataType]} init_${p.symbol} = ${cppLiteral(p.initial, p.dataType)};`, `    ${CPP_TYPE[p.dataType]} saved_${p.symbol} = ${cppLiteral(p.initial, p.dataType)};`);
  }

  H.push(``, `    // ---- Accès aux variables`);
  for (const [sym, acc] of access) {
    H.push(`    inline ${acc.cppType} v_${sym}() { return ${acc.get}; }`);
    if (acc.set) H.push(`    inline void set_${sym}(${acc.cppType} x) { ${acc.set('x')} }`);
  }
  H.push(
    ``,
    `    // ---- Fonctions internes`,
    `    State cur() const { return static_cast<State>(BusinessLogic::getCurrentState()); }`,
    `    void go(State s) { if (cur() != s) transitionWithContext(static_cast<int>(s), 0); }`,
    `    uint32_t stepTime(int i) const { return X[i] ? now - Xstart[i] : 0; }`,
    `    void activateStep(int i) { X[i] = true; Xnew[i] = true; Xstart[i] = now; }`,
    `    static bool bankBit(ModbusReadMultipleInputsRegistersNode* b, uint8_t bit) {`,
    `        if (!b) return false;`,
    `        auto* x = b->getBit(bit);`,
    `        return x ? x->getValue() : false;`,
    `    }`,
    `    static int32_t plcDivI(int32_t a, int32_t b) { return b == 0 ? 0 : a / b; }`,
    `    static float plcDivF(float a, float b) { return b == 0.0f ? 0.0f : a / b; }`,
    `    static int32_t plcMod(int32_t a, int32_t b) { return b == 0 ? 0 : a % b; }`,
    `    static int32_t plcMinI(int32_t a, int32_t b) { return a < b ? a : b; }`,
    `    static int32_t plcMaxI(int32_t a, int32_t b) { return a > b ? a : b; }`,
    `    static float plcMinF(float a, float b) { return a < b ? a : b; }`,
    `    static float plcMaxF(float a, float b) { return a > b ? a : b; }`,
    `    static int32_t plcAbsI(int32_t a) { return a < 0 ? -a : a; }`,
    `    static float plcAbsF(float a) { return a < 0 ? -a : a; }`,
    ``,
    `    void grafcetReset();`,
    `    void grafcetClear();`,
    `    bool grafcetInInitialSituation() const;`,
    `    void grafcetActivationActions();`,
    `    void grafcetEvolve();`,
    `    void computeOutputs();`,
    `    void updateEdges();`,
    `    bool emergencyRequested(char* reason, size_t len);`,
    `    void handleInitializing();`,
    `    void handleStop();`,
    `    void handleRun();`,
    `    void handleEmergency();`,
    `    void handleResetting();`,
    `    void handleHoming();`,
    `    void handleJogging();`,
    `    void updateStateDisplay();`,
    `    bool readParams();`,
    `    bool saveParams();`,
    `    void checkParams();`,
    `    void applyParamsToNodes();`
  );
  if (hasServo) {
    H.push(
      `    bool createServo(const char* context);`,
      `    void servoService();`,
      `    void servoMoveTo(int32_t target, int32_t speed);`,
      `    void servoStartHoming();`,
      `    void servoStop();`,
      `    bool servoIsReady() const { return sureServo && sureServo->getIsReady(); }`,
      `    bool servoInPosition() const { return servoMoveDone; }`,
      `    bool servoIsMoving() const { return servoMoveActive || servoMovePending || servoHomingActive; }`,
      `    bool servoHomeDone() const { return sureServo && sureServo->getHomeDone(); }`,
      `    bool servoHasAlarm() const { return sureServo && sureServo->getHasAlarms(); }`,
      `    int32_t servoPosition() const { return sureServo ? sureServo->getPosition() : 0; }`
    );
  } else {
    H.push(`    bool servoIsMoving() const { return false; }`);
  }
  H.push(`};`, ``);

  // ---------------- Implémentation ----------------
  const C = [];
  const L = (...lines) => C.push(...lines);
  L(
    `// =============================================================================`,
    `// ${N}.cpp — généré par PLC Studio (tools/plc-studio)`,
    `// NE PAS MODIFIER À LA MAIN : ce fichier est régénéré depuis projets/${N}.plc.json`,
    `// =============================================================================`,
    `#include "${N}.h"`,
    clockNode ? `#include "DS3231_RTC/ClockDisplay.h"` : null,
    ``,
    `${N}::${N}() : BusinessLogic() {`,
    `    currentState = static_cast<int>(State::UNDEFINED);`,
    `    previousState = static_cast<int>(State::UNDEFINED);`,
    `    stateStartTime = millis();`,
    `    BorneUniverselle::getInstance()->setInitialStateLoadedCallback([this]() { this->initialStateLoadedHandler(); });`,
    `    PLC_Persistence::getInstance().registerDeferredSave(PARAMS_FILE, [this]() { return this->saveParams(); });`,
    `    readParams();`,
    `}`,
    ``,
    `${N}::~${N}() {}`,
    ``
  );

  // initializePointers
  L(`bool ${N}::initializePointers(const char* context) {`);
  for (const acc of access.values()) for (const line of acc.init) L(`    ${line}`);
  for (const b of banks.values()) L(`    initNodePtr(${b.name}, ${N}Nodes::${b.hashName}, context);`);
  if (stateNode) L(`    initNodePtr(n_sys_state, ${N}Nodes::SYS_STATE, context);`);
  if (clockNode) L(`    initNodePtr(n_sys_clock, ${N}Nodes::SYS_CLOCK, context);`, `    ClockDisplay::attach(n_sys_clock);`);
  L(`    if (BorneUniverselle::getInstance()->isPlcBroken()) {`, `        Serial.println("${N}: erreur pendant l'initialisation des pointeurs");`, `        return false;`, `    }`);
  if (hasServo) L(`    if (!createServo(context)) return false;`);
  L(`    applyParamsToNodes();`, `    return true;`, `}`, ``);

  if (hasServo) {
    const opts = servoEq.options || {};
    L(`bool ${N}::createServo(const char* context) {`);
    for (const [ref, node] of layout.nodes) {
      if (!ref.startsWith('servo:')) continue;
      L(`    initNodePtr(servoNodes.${node.field}, ${N}Nodes::SERVO_${ident(node.field)}, context);`);
    }
    for (const v of project.variables.filter((x) => x.system?.eq === servoEq.uid)) {
      L(`    servoNodes.${v.system.field} = n_${v.symbol};`);
    }
    L(
      `    if (BorneUniverselle::getInstance()->isPlcBroken()) return false;`,
      `    if (!servoNodes.validateRequired()) {`,
      `        BorneUniverselle::setPlcBroken("${N}: nœuds obligatoires du servo manquants");`,
      `        return false;`,
      `    }`,
      `    sureServo = std::unique_ptr<SureServo>(new SureServo(${Number(opts.pulsesPerUnit) || 1}, servoNodes, HomePositionPolicy::${opts.homePolicy === 'NEGATIVE_TOLERATED' ? 'NEGATIVE_TOLERATED' : 'NEGATIVE_INVALIDATES_HOME'}, ${Math.max(0, Number(opts.maxRange) || 0)}.0f));`,
      `    const char* err = sureServo->getLastError();`,
      `    if (err && strlen(err) > 0 && strcmp(err, "Pas d'erreur") != 0) {`,
      `        char msg[160];`,
      `        snprintf(msg, sizeof(msg), "${N}: création du servo impossible : %s", err);`,
      `        BorneUniverselle::setPlcBroken(msg);`,
      `        return false;`,
      `    }`,
      `    return true;`,
      `}`,
      ``
    );
  }

  L(
    `bool ${N}::startStateMachine() {`,
    `    go(State::INITIALIZING);`,
    `    return true;`,
    `}`,
    ``,
    `void ${N}::initialStateLoadedHandler() {`,
    `    if (cur() == State::EMERGENCY) BorneUniverselle::prepareMessage(ERROR, "Machine en arrêt d'urgence");`,
    `    updateStateDisplay();`,
    `    BorneUniverselle::enableInterface(true);`,
    `    BorneUniverselle::getInstance()->clearAllClientNotifications();`,
    `    initialised = true;`,
    `}`,
    ``
  );

  // doBusinessLogic
  L(
    `bool ${N}::doBusinessLogic() {`,
    `    now = millis();`,
    hasServo ? `    if (sureServo) sureServo->process();` : null,
    `    if (BorneUniverselle::getInstance()->isPlcBroken()) return false;`,
    ``,
    `    // Arrêt d'urgence : prioritaire sur tous les modes.`,
    `    State s = cur();`,
    `    if (s != State::EMERGENCY && s != State::RESETTING && s != State::UNDEFINED) {`,
    `        char reason[160];`,
    `        if (emergencyRequested(reason, sizeof(reason))) {`,
    `            BorneUniverselle::prepareMessage(ERROR, reason);`,
    `            PLC_Tools::logDiagnostic(reason);`,
    `            go(State::EMERGENCY);`,
    `        }`,
    `    }`
  );
  if (ir.blocks.stop) {
    L(
      `    // Arrêt demandé : retour en STOP (grafcet remis à zéro).`,
      `    s = cur();`,
      `    if ((s == State::IDLE || s == State::RUNNING || s == State::HOMING || s == State::JOGGING) && ${cx(ir.blocks.stop)}) {`,
      hasServo ? `        servoStop();` : null,
      `        BorneUniverselle::prepareMessage(INFO, "Arrêt demandé");`,
      `        go(State::STOP);`,
      `    }`
    );
  }
  L(
    ``,
    `    switch (cur()) {`,
    `        case State::UNDEFINED:    go(State::INITIALIZING); break;`,
    `        case State::INITIALIZING: handleInitializing(); break;`,
    `        case State::STOP:         handleStop(); break;`,
    `        case State::HOMING:       handleHoming(); break;`,
    `        case State::IDLE:`,
    `        case State::RUNNING:      handleRun(); break;`,
    `        case State::JOGGING:      handleJogging(); break;`,
    `        case State::EMERGENCY:    handleEmergency(); break;`,
    `        case State::RESETTING:    handleResetting(); break;`,
    `        default:                  go(State::EMERGENCY); break;`,
    `    }`,
    ``,
    `    computeOutputs();`,
    `    checkParams();`,
    `    updateEdges();`,
    clockNode ? `    if (shouldShowClock()) updateClockNode(n_sys_clock);` : null,
    `    return true;`,
    `}`,
    ``
  );

  // emergencyRequested
  L(`bool ${N}::emergencyRequested(char* reason, size_t len) {`);
  if (ir.blocks.emergency) {
    L(
      `    if (${cx(ir.blocks.emergency)}) {`,
      `        snprintf(reason, len, "Arrêt d'urgence : %s", ${cstr(project.logic?.blocks?.emergency?.condition || '')});`,
      `        return true;`,
      `    }`
    );
  }
  if (hasServo) {
    L(
      `    if (sureServo && sureServo->getHasAlarms()) {`,
      `        snprintf(reason, len, "Alarme servo 0x%04X : %s", sureServo->getAlarmCode(), sureServo->getAlarmDescription());`,
      `        return true;`,
      `    }`
    );
  }
  L(`    (void)reason; (void)len;`, `    return false;`, `}`, ``);

  // Modes
  L(`void ${N}::handleInitializing() {`);
  if (hasServo) {
    L(
      `    ServoDrive::OperationStatus st;`,
      `    if (!sureServo->handleInitializing(st)) { go(State::EMERGENCY); return; }`,
      `    switch (st) {`,
      `        case ServoDrive::OperationStatus::COMPLETED:`,
      `            servoAppliedSpeed = -1;`,
      `            lastTorque = -1;`,
      `            go(State::STOP);`,
      `            break;`,
      `        case ServoDrive::OperationStatus::IN_PROGRESS:`,
      `            break;`,
      `        default:`,
      `            BorneUniverselle::prepareMessage(ERROR, "Initialisation du servo en échec");`,
      `            go(State::EMERGENCY);`,
      `            break;`,
      `    }`
    );
  } else {
    L(`    go(State::STOP);`);
  }
  L(`}`, ``);

  const startExpr = ir.blocks.start ? cx(ir.blocks.start) : 'true';
  const stopExpr = ir.blocks.stop ? cx(ir.blocks.stop) : 'false';
  const jog = hasServo && ir.blocks.servo?.jog;
  L(`void ${N}::handleStop() {`);
  if (jog) L(`    if (${cx(jog.mode)}) { go(State::JOGGING); return; }`);
  L(`    if (!(${startExpr}) || ${stopExpr}) return;`);
  if (hasServo && ir.blocks.servo.homing !== 'none') {
    const homingTrigger = ir.blocks.servo.homing === 'condition' ? cx(ir.blocks.servo.homingCondition) : 'true';
    L(
      `    if (!servoHomeDone()) {`,
      `        if (${homingTrigger}) go(State::HOMING);`,
      `        return;`,
      `    }`
    );
  }
  L(`    grafcetReset();`, `    go(State::IDLE);`, `}`, ``);

  L(`void ${N}::handleHoming() {`);
  if (hasServo) {
    L(
      `    ServoDrive::OperationStatus st;`,
      `    if (!sureServo->handleHoming(st)) { go(State::EMERGENCY); return; }`,
      `    if (st == ServoDrive::OperationStatus::COMPLETED) {`,
      `        BorneUniverselle::prepareMessage(SUCCESS, "Prise d'origine terminée");`,
      `        grafcetReset();`,
      `        go(State::IDLE);`,
      `    } else if (st != ServoDrive::OperationStatus::IN_PROGRESS) {`,
      `        go(State::EMERGENCY);`,
      `    }`
    );
  } else L(`    go(State::STOP);`);
  L(`}`, ``);

  L(`void ${N}::handleRun() {`);
  if (jog) L(`    if (${cx(jog.mode)} && grafcetInInitialSituation() && !servoIsMoving()) { go(State::JOGGING); return; }`);
  if (hasServo) L(`    servoService();`, `    if (cur() == State::EMERGENCY) return;`);
  L(
    `    grafcetActivationActions();`,
    `    if (cur() == State::EMERGENCY) return;  // déclenchée par une action (ex. cible hors course)`,
    `    grafcetEvolve();`,
    `    go(grafcetInInitialSituation() && !servoIsMoving() ? State::IDLE : State::RUNNING);`,
    `    updateStateDisplay();`,
    `}`,
    ``
  );

  L(`void ${N}::handleJogging() {`);
  if (jog) {
    L(
      `    ServoDrive::OperationStatus st;`,
      `    sureServo->handleJogging(st);`,
      `    if (st == ServoDrive::OperationStatus::SERVO_DRIVE_ERROR) { go(State::EMERGENCY); return; }`,
      `    if (!(${cx(jog.mode)})) {`,
      `        if (jogForwardHeld || jogReverseHeld) sureServo->jogStop();`,
      `        jogForwardHeld = jogReverseHeld = false;`,
      `        go(State::STOP);`,
      `        return;`,
      `    }`,
      `    int speed = (int)(${cx(jog.speed)});`,
      `    if (speed < 1) speed = 1;`,
      `    if (speed > 3000) speed = 3000;`,
      `    if (speed != lastJogSpeed && !jogForwardHeld && !jogReverseHeld) { sureServo->setJogSpeed(speed); lastJogSpeed = speed; }`,
      `    bool plus = ${cx(jog.plus)};`,
      `    bool minus = ${cx(jog.minus)};`,
      `    // SureServo attend des appels à chaque cycle tant que le bouton est maintenu (butée, zone lente).`,
      `    if (plus && !minus) {`,
      `        if (jogReverseHeld) { sureServo->jogStop(); jogReverseHeld = false; }`,
      `        jogForwardHeld = sureServo->jogForward(${maxRangePuu || 'INT32_MAX'}, 0, 10);`,
      `    } else if (minus && !plus) {`,
      `        if (jogForwardHeld) { sureServo->jogStop(); jogForwardHeld = false; }`,
      `        jogReverseHeld = sureServo->jogReverse(servoHomeDone() ? 0 : INT32_MIN, 0, 10);`,
      `    } else if (jogForwardHeld || jogReverseHeld) {`,
      `        sureServo->jogStop();`,
      `        jogForwardHeld = jogReverseHeld = false;`,
      `    }`
    );
  } else L(`    go(State::STOP);`);
  L(`}`, ``);

  L(
    `void ${N}::handleEmergency() {`,
    `    // Seule la condition d'urgence bloque la sortie : une alarme servo est justement`,
    `    // effacée par le réarmement (startReset). Si elle persiste, l'urgence se redéclenche.`,
    `    if (${ir.blocks.emergency ? cx(ir.blocks.emergency) : 'false'}) { emergencyClearSince = 0; return; }`,
    `    if (emergencyClearSince == 0) emergencyClearSince = now ? now : 1;`
  );
  if (ir.blocks.emergencyReset) L(`    if (!(${cx(ir.blocks.emergencyReset)})) return;   // acquittement`);
  else L(`    if (now - emergencyClearSince < 1000) return;   // sortie automatique après 1 s`);
  if (hasServo) {
    L(
      `    if (sureServo->startReset()) {`,
      `        BorneUniverselle::prepareMessage(INFO, "Réarmement du servo…");`,
      `        go(State::RESETTING);`,
      `    }`
    );
  } else L(`    BorneUniverselle::prepareMessage(SUCCESS, "Arrêt d'urgence acquitté");`, `    go(State::STOP);`);
  L(`}`, ``);

  L(`void ${N}::handleResetting() {`);
  if (hasServo) {
    L(
      `    switch (sureServo->getResetState()) {`,
      `        case ServoDrive::OperationState::DRIVE_IN_PROGRESS:`,
      `            return;`,
      `        case ServoDrive::OperationState::DRIVE_IDLE:`,
      `            BorneUniverselle::prepareMessage(SUCCESS, "Arrêt d'urgence acquitté");`,
      `            go(sureServo->getIsInitialized() ? State::STOP : State::INITIALIZING);`,
      `            return;`,
      `        default:`,
      `            BorneUniverselle::prepareMessage(ERROR, "Échec du réarmement du servo");`,
      `            go(State::EMERGENCY);`,
      `            return;`,
      `    }`
    );
  } else L(`    go(State::STOP);`);
  L(`}`, ``);

  // Servo service
  if (hasServo) {
    const maxRange = maxRangePuu;
    L(
      `void ${N}::servoService() {`,
      `    if (!sureServo) return;`
    );
    if (ir.blocks.servo?.torque) {
      L(
        `    int torque = (int)(${cx(ir.blocks.servo.torque)});`,
        `    if (torque < 0) torque = 0;`,
        `    if (torque > 100) torque = 100;`,
        `    if (torque != lastTorque) { sureServo->setMaxTorque((uint8_t)torque); lastTorque = torque; }`
      );
    }
    L(
      `    if (servoMovePending) {`,
      `        if (servoPendingSpeed != servoAppliedSpeed) {`,
      `            // La vitesse est écrite d'abord ; le déplacement part au cycle suivant.`,
      `            sureServo->setSpeedAndRamp((uint8_t)servoPendingSpeed, 0);`,
      `            servoAppliedSpeed = servoPendingSpeed;`,
      `            return;`,
      `        }`,
      `        servoMovePending = false;`,
      `        if (sureServo->startGoToPosition(servoPendingTarget, false)) {`,
      `            servoMoveActive = true;`,
      `        } else {`,
      `            char msg[160];`,
      `            snprintf(msg, sizeof(msg), "Déplacement servo refusé : %s", sureServo->getLastError());`,
      `            BorneUniverselle::prepareMessage(ERROR, msg);`,
      `            go(State::EMERGENCY);`,
      `            return;`,
      `        }`,
      `    }`,
      `    if (servoMoveActive) {`,
      `        switch (sureServo->getMoveState()) {`,
      `            case ServoDrive::OperationState::DRIVE_IDLE:`,
      `                servoMoveActive = false;`,
      `                servoMoveDone = true;`,
      `                break;`,
      `            case ServoDrive::OperationState::DRIVE_IN_PROGRESS:`,
      `                break;`,
      `            default: {`,
      `                char msg[160];`,
      `                snprintf(msg, sizeof(msg), "Déplacement servo en échec : %s", sureServo->getLastError());`,
      `                BorneUniverselle::prepareMessage(ERROR, msg);`,
      `                servoMoveActive = false;`,
      `                go(State::EMERGENCY);`,
      `                return;`,
      `            }`,
      `        }`,
      `    }`,
      `    if (servoHomingActive) {`,
      `        ServoDrive::OperationStatus st;`,
      `        if (!sureServo->handleHoming(st) || (st != ServoDrive::OperationStatus::IN_PROGRESS && st != ServoDrive::OperationStatus::COMPLETED)) {`,
      `            servoHomingActive = false;`,
      `            go(State::EMERGENCY);`,
      `            return;`,
      `        }`,
      `        if (st == ServoDrive::OperationStatus::COMPLETED) servoHomingActive = false;`,
      `    }`,
      `}`,
      ``,
      `void ${N}::servoMoveTo(int32_t target, int32_t speed) {`,
      `    // Butée logicielle appliquée à toutes les cibles (cf. audit CR-1).`,
      `    if (target < 0${maxRange ? ` || target > ${maxRange}` : ''}) {`,
      `        char msg[120];`,
      `        snprintf(msg, sizeof(msg), "Cible servo hors course : %ld", (long)target);`,
      `        BorneUniverselle::prepareMessage(ERROR, msg);`,
      `        go(State::EMERGENCY);`,
      `        return;`,
      `    }`,
      `    if (speed < 0) speed = 0;`,
      `    if (speed > 15) speed = 15;`,
      `    servoMoveDone = false;`,
      `    servoMovePending = true;`,
      `    servoPendingTarget = target;`,
      `    servoPendingSpeed = (int)speed;`,
      `}`,
      ``,
      `void ${N}::servoStartHoming() {`,
      `    servoMoveDone = false;`,
      `    servoHomingActive = true;`,
      `}`,
      ``,
      `void ${N}::servoStop() {`,
      `    if (sureServo && (servoMoveActive || servoHomingActive)) sureServo->stopMovement();`,
      `    servoMovePending = servoMoveActive = servoHomingActive = false;`,
      `    servoMoveDone = false;`,
      `}`,
      ``
    );
  }

  // Grafcet
  L(`void ${N}::grafcetClear() {`, `    for (int i = 0; i < STEP_COUNT; i++) { X[i] = false; Xnew[i] = false; Xstart[i] = 0; }`);
  for (const d of ir.driven) if (d.latch) L(`    latch_${d.sym} = false;`);
  L(`}`, ``);
  L(`void ${N}::grafcetReset() {`, `    grafcetClear();`);
  ir.steps.forEach((s, i) => s.initial && L(`    activateStep(${i});  // étape ${s.num} initiale`));
  L(`}`, ``);
  L(`bool ${N}::grafcetInInitialSituation() const {`);
  if (ir.steps.length) L(`    return ${ir.steps.map((s, i) => (s.initial ? `X[${i}]` : `!X[${i}]`)).join(' && ')};`);
  else L(`    return true;`);
  L(`}`, ``);

  L(`void ${N}::grafcetActivationActions() {`);
  ir.steps.forEach((s, i) => {
    const lines = [];
    for (const a of s.actions) {
      switch (a.type) {
        case 'S':
          lines.push(`latch_${a.target} = true;`);
          break;
        case 'R':
          lines.push(`latch_${a.target} = false;`);
          break;
        case 'SET':
          if (!drivenSet.has(a.target)) lines.push(setCall(a.target, cast(cx(a.value), a.targetType, a.value.type)));
          break;
        case 'INC':
          if (!drivenSet.has(a.target)) lines.push(setCall(a.target, `v_${a.target}() + 1`));
          break;
        case 'DEC':
          if (!drivenSet.has(a.target)) lines.push(setCall(a.target, `v_${a.target}() - 1`));
          break;
        case 'SERVO_MOVE':
          lines.push(`servoMoveTo((int32_t)(${cx(a.position)}), (int32_t)(${cx(a.speed)}));`);
          break;
        case 'SERVO_HOME':
          lines.push(`servoStartHoming();`);
          break;
        case 'SERVO_STOP':
          lines.push(`servoStop();`);
          break;
        case 'MSG':
          lines.push(`BorneUniverselle::prepareMessage(${{ info: 'INFO', warning: 'WARNING', error: 'ERROR', success: 'SUCCESS' }[a.level]}, ${cstr(a.text)});`);
          break;
        default:
          break;
      }
    }
    if (lines.length) L(`    if (Xnew[${i}]) {  // étape ${s.num}${s.label ? ' — ' + s.label : ''}`, ...lines.map((x) => `        ${x}`), `    }`);
  });
  L(`}`, ``);

  L(
    `void ${N}::grafcetEvolve() {`,
    `    bool fire[TRANSITION_COUNT] = {};`,
    `    bool any = false;`
  );
  ir.transitions.forEach((t, i) => {
    const up = t.from.map((k) => `X[${k}]`).join(' && ') || 'false';
    const cond = t.cond ? cx(t.cond) : 'false';
    L(`    fire[${i}] = ${up} && ${cond};  // T${t.num}: ${String(t.text || '').replace(/\*\//g, '* /').replace(/\n/g, ' ')}`, `    any = any || fire[${i}];`);
  });
  L(`    for (int i = 0; i < STEP_COUNT; i++) Xnew[i] = false;`, `    if (!any) return;`, `    // Désactivation des étapes amont, puis activation des étapes aval (prioritaire).`);
  ir.transitions.forEach((t, i) => {
    if (t.from.length) L(`    if (fire[${i}]) { ${t.from.map((k) => `X[${k}] = false;`).join(' ')} }`);
  });
  ir.transitions.forEach((t, i) => {
    if (t.to.length) L(`    if (fire[${i}]) { ${t.to.map((k) => `activateStep(${k});`).join(' ')} }`);
  });
  L(`}`, ``);

  // Sorties
  L(
    `void ${N}::computeOutputs() {`,
    `    const State s = cur();`,
    `    const bool run = (s == State::IDLE || s == State::RUNNING);`,
    `    const bool emergency = (s == State::EMERGENCY || s == State::RESETTING);`,
    `    (void)run; (void)emergency;`
  );
  // Actions continues
  const contBySym = new Map();
  const nsets = [];
  ir.steps.forEach((s, i) => {
    for (const a of s.actions) {
      if (a.type === 'N') {
        const list = contBySym.get(a.target) || [];
        list.push(a.condition ? `(X[${i}] && ${cx(a.condition)})` : `X[${i}]`);
        contBySym.set(a.target, list);
      }
      if (a.type === 'NSET' && !drivenSet.has(a.target)) nsets.push({ i, a });
    }
  });
  for (const d of ir.driven) {
    const terms = [...(contBySym.get(d.sym) || [])];
    if (d.latch) terms.push(`latch_${d.sym}`);
    const v = project.variables.find((x) => x.symbol === d.sym);
    let emergencyValue = 'false';
    if (v?.kind === 'output') emergencyValue = v.fallback === 'on' ? 'true' : v.fallback === 'hold' ? `v_${d.sym}()` : 'false';
    L(`    set_${d.sym}(run ? (${terms.join(' || ') || 'false'}) : (emergency ? ${emergencyValue} : false));`);
  }
  if (nsets.length) {
    L(`    if (run) {`);
    for (const { i, a } of nsets) L(`        if (X[${i}]) ${setCall(a.target, cast(cx(a.value), a.targetType, a.value.type))}`);
    L(`    }`);
  }
  // Sorties non pilotées en continu : repli en urgence, à 0 hors fonctionnement.
  const others = outputs.filter((v) => !drivenSet.has(v.symbol) && access.get(v.symbol)?.set);
  if (others.length) {
    L(`    if (emergency) {`);
    for (const v of others) if (v.fallback !== 'hold') L(`        set_${v.symbol}(${v.fallback === 'on' ? 'true' : 'false'});`);
    L(`    } else if (!run) {`);
    for (const v of others) L(`        set_${v.symbol}(false);`);
    L(`    }`);
  }
  L(`}`, ``);

  L(`void ${N}::updateEdges() {`);
  ir.edges.forEach((e, i) => L(`    E[${i}] = ${cx(e.ast)};`));
  L(`}`, ``);

  // Affichage de l'état
  L(`void ${N}::updateStateDisplay() {`);
  if (stateNode) {
    L(
      `    if (!n_sys_state) return;`,
      `    char text[160];`,
      `    int n = snprintf(text, sizeof(text), "%s", stateToString(static_cast<int>(cur())));`,
      `    if (cur() == State::IDLE || cur() == State::RUNNING) {`,
      `        n += snprintf(text + n, sizeof(text) - n, " :");`,
      `        for (int i = 0; i < STEP_COUNT && n < (int)sizeof(text) - 12; i++) {`,
      `            if (X[i]) n += snprintf(text + n, sizeof(text) - n, " X%d", STEP_NUMBERS[i]);`,
      `        }`,
      `    }`,
      `    if (strcmp(n_sys_state->getValue(), text) != 0) n_sys_state->setValue(text);`
    );
  }
  L(`}`, ``);
  if (stateNode) {
    // Déclaration statique des numéros d'étape (définition hors classe, compatible C++11).
    H.splice(H.indexOf(`    bool X[STEP_COUNT] = {};          // étapes actives`), 0, `    static const int STEP_NUMBERS[STEP_COUNT];`);
    C.splice(C.indexOf(`${N}::~${N}() {}`), 0, `const int ${N}::STEP_NUMBERS[${N}::STEP_COUNT] = { ${ir.steps.length ? ir.steps.map((s) => s.num).join(', ') : '0'} };`, ``);
  }

  // Paramètres
  L(`bool ${N}::readParams() {`, `    SafeJsonDocument doc(4096);`, `    PLC_Persistence& p = PLC_Persistence::getInstance();`, `    if (!p.readJsonFromFile(PARAMS_FILE, doc)) {`, `        Serial.printf("${N}: %s absent ou illisible, valeurs par défaut utilisées\\n", PARAMS_FILE);`, `        p.markDirty(PARAMS_FILE);`, `        return false;`, `    }`);
  for (const v of params) {
    const key = cstr(v.symbol);
    if (v.dataType === 'bool') L(`    if (doc[${key}].is<bool>()) init_${v.symbol} = doc[${key}].as<bool>();`);
    else if (v.dataType === 'int') L(`    if (doc[${key}].is<int32_t>()) init_${v.symbol} = doc[${key}].as<int32_t>();`);
    else if (v.dataType === 'float') L(`    if (doc[${key}].is<float>()) init_${v.symbol} = doc[${key}].as<float>();`);
    else L(`    if (doc[${key}].is<const char*>()) init_${v.symbol} = doc[${key}].as<const char*>();`);
  }
  L(`    return true;`, `}`, ``);

  L(`void ${N}::applyParamsToNodes() {`);
  for (const v of params) {
    if (v.dataType === 'text') L(`    set_${v.symbol}(init_${v.symbol}.c_str());`, `    saved_${v.symbol} = init_${v.symbol};`);
    else L(`    set_${v.symbol}(init_${v.symbol});`, `    saved_${v.symbol} = init_${v.symbol};`);
  }
  L(`}`, ``);

  L(`void ${N}::checkParams() {`, `    bool dirty = false;`);
  for (const v of params) {
    const hasMin = v.min !== undefined && v.min !== '' && v.dataType !== 'text' && v.dataType !== 'bool';
    const hasMax = v.max !== undefined && v.max !== '' && v.dataType !== 'text' && v.dataType !== 'bool';
    if (hasMin) L(`    if (v_${v.symbol}() < ${cppLiteral(v.min, v.dataType)}) set_${v.symbol}(${cppLiteral(v.min, v.dataType)});`);
    if (hasMax) L(`    if (v_${v.symbol}() > ${cppLiteral(v.max, v.dataType)}) set_${v.symbol}(${cppLiteral(v.max, v.dataType)});`);
    if (v.dataType === 'text') L(`    if (saved_${v.symbol} != v_${v.symbol}()) { saved_${v.symbol} = v_${v.symbol}(); dirty = true; }`);
    else L(`    if (saved_${v.symbol} != v_${v.symbol}()) { saved_${v.symbol} = v_${v.symbol}(); dirty = true; }`);
  }
  L(`    if (dirty) PLC_Persistence::getInstance().markDirty(PARAMS_FILE);`, `}`, ``);

  L(`bool ${N}::saveParams() {`, `    SafeJsonDocument doc(4096);`, `    doc["machine name"] = ${cstr(N)};`);
  for (const v of params) L(`    doc[${cstr(v.symbol)}] = ${v.dataType === 'text' ? `saved_${v.symbol}.c_str()` : `saved_${v.symbol}`};`);
  L(
    `    markInternalSave(PARAMS_FILE);`,
    `    if (!PLC_Persistence::getInstance().saveJsonToFile(PARAMS_FILE, doc)) {`,
    `        BorneUniverselle::prepareMessage(ERROR, "Échec de la sauvegarde des paramètres");`,
    `        return false;`,
    `    }`,
    `    return true;`,
    `}`,
    ``
  );

  // Surcharges BusinessLogic
  L(
    `const char* ${N}::stateToString(int state) const {`,
    `    switch (static_cast<State>(state)) {`,
    `        case State::UNDEFINED:    return "INDÉFINI";`,
    `        case State::INITIALIZING: return "INITIALISATION";`,
    `        case State::STOP:         return "ARRÊT";`,
    `        case State::IDLE:         return "REPOS";`,
    `        case State::EMERGENCY:    return "URGENCE";`,
    `        case State::JOGGING:      return "MANUEL";`,
    `        case State::HOMING:       return "PRISE D'ORIGINE";`,
    `        case State::RUNNING:      return "CYCLE";`,
    `        case State::RESETTING:    return "RÉARMEMENT";`,
    `        default:                  return "INCONNU";`,
    `    }`,
    `}`,
    ``,
    `BusinessLogic::StateType ${N}::classifyState(int state) const {`,
    `    switch (static_cast<State>(state)) {`,
    `        case State::EMERGENCY:`,
    `        case State::RESETTING:    return StateType::EMERGENCY;`,
    `        case State::STOP:`,
    `        case State::IDLE:         return StateType::STABLE;`,
    `        case State::JOGGING:`,
    `        case State::HOMING:       return StateType::UTILITY;`,
    `        default:                  return StateType::TRANSIENT;`,
    `    }`,
    `}`,
    ``,
    `bool ${N}::shouldShowClock() const {`,
    `    State s = cur();`,
    `    return s == State::IDLE || s == State::STOP || s == State::EMERGENCY;`,
    `}`,
    ``,
    `void ${N}::onStateEnter(int newState, int oldState) {`,
    `    (void)oldState;`,
    `    // Pas de changement d'état ici (règle BusinessLogic).`,
    `    switch (static_cast<State>(newState)) {`,
    `        case State::EMERGENCY:`,
    hasServo ? `            if (sureServo) sureServo->emergencyStop();` : null,
    hasServo ? `            servoMovePending = servoMoveActive = servoHomingActive = false;` : null,
    hasServo ? `            jogForwardHeld = jogReverseHeld = false;` : null,
    `            emergencyClearSince = 0;`,
    `            grafcetClear();`,
    `            break;`,
    `        case State::STOP:`,
    `            grafcetClear();`,
    `            break;`,
    `        default:`,
    `            break;`,
    `    }`,
    `    updateStateDisplay();`,
    `}`,
    ``,
    `void ${N}::onStateExit(int oldState, int newState) {`,
    `    (void)oldState; (void)newState;`,
    `}`,
    ``,
    `bool ${N}::canDeleteFile(const char* path) {`,
    `    return strcmp(path, PARAMS_FILE) != 0;`,
    `}`,
    ``,
    `bool ${N}::validateConfigFile(const char* path, const JsonDocument& doc, String& errorMsg) {`,
    `    (void)path; (void)doc; (void)errorMsg;`,
    `    return true;`,
    `}`,
    ``,
    `void ${N}::onFileSaved(const char* path, bool success) {`,
    `    if (!path || !success || !shouldProcessFileCallback(path)) return;`,
    `    if (strcmp(path, PARAMS_FILE) == 0) {`,
    `        if (!isIdle()) {`,
    `            BorneUniverselle::prepareMessage(WARNING, "Paramètres modifiés : rechargement à l'arrêt seulement");`,
    `            return;`,
    `        }`,
    `        readParams();`,
    `        applyParamsToNodes();`,
    `        BorneUniverselle::prepareMessage(SUCCESS, "Paramètres rechargés");`,
    `    }`,
    `}`,
    ``,
    `void ${N}::printPersistance() {`,
    `    Serial.printf("${N}: fichier de paramètres %s\\n", PARAMS_FILE);`
  );
  for (const v of params) {
    const fmt = { bool: '%d', int: '%ld', float: '%.3f', text: '%s' }[v.dataType];
    const val = v.dataType === 'int' ? `(long)saved_${v.symbol}` : v.dataType === 'text' ? `saved_${v.symbol}.c_str()` : v.dataType === 'bool' ? `(int)saved_${v.symbol}` : `saved_${v.symbol}`;
    L(`    Serial.printf("  ${v.symbol} = ${fmt}\\n", ${val});`);
  }
  L(`}`, ``);

  return {
    header: H.join('\n') + '\n',
    source: C.filter((x) => x !== null).join('\n') + '\n',
  };
}

// Conversion explicite entre types numériques (évite les avertissements de conversion).
function cast(code, targetType, sourceType) {
  if (targetType === 'int' && sourceType === 'float') return `(int32_t)(${code})`;
  if (targetType === 'float' && sourceType === 'int') return `(float)(${code})`;
  return code;
}

// ---------------------------------------------------------------------------
// Bascule de src/main.cpp vers la classe générée.
export function patchMainCpp(main, newClass) {
  const m = main.match(/(\w+)\s*=\s*new\s+([A-Za-z_]\w*)\s*\(\s*\)\s*;/);
  if (!m) return { ok: false, message: 'Instanciation de la classe d’application introuvable dans src/main.cpp.' };
  const varName = m[1];
  const oldClass = m[2];
  if (oldClass === newClass) return { ok: true, content: main, oldClass, changed: false };
  let out = main;
  out = out.replace(new RegExp(`#include\\s+"${oldClass}/${oldClass}\\.h"`), `#include "${newClass}/${newClass}.h"`);
  out = out.replace(new RegExp(`\\b${oldClass}\\b`, 'g'), newClass);
  if (varName !== 'plcApp') out = out.replace(new RegExp(`\\b${varName}\\b`, 'g'), 'plcApp');
  if (!out.includes(`#include "${newClass}/${newClass}.h"`)) {
    return { ok: false, message: `Include de ${oldClass} introuvable dans src/main.cpp.` };
  }
  return { ok: true, content: out, oldClass, changed: true };
}

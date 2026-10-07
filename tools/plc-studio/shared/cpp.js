// Génération de la classe C++ d'application (dérivée de BusinessLogic).

import { toCpp, AXIS_PROPS } from './expr.js';
import { paramsFileName } from './model.js';
import { axisList } from './axes.js';

const CPP_TYPE = { bool: 'bool', int: 'int32_t', float: 'float', text: 'const char*' };
const DEFAULT_VALUE = { bool: 'false', int: '0', float: '0.0f', text: '""' };

// Littéral chaîne C++ (UTF-8 conservé ; caractères de contrôle autres que \n et \t retirés).
function cstr(s) {
  return JSON.stringify(String(s ?? '').replace(/[\u0000-\u0008\u000b-\u001f\u007f]/g, ''));
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
    acc.init.push(`initNodePtr(${ptr}, ${project.name}Nodes::H_${v.symbol}, context);`);
    // Sortie Modbus : la valeur de repos est écrite au démarrage (sinon l'équipement garde
    // l'état d'avant le redémarrage de l'automate, ex. un départ robot resté à 1).
    if (String(node.sectionType).startsWith('ModbusWrite')) acc.init.push(`if (${ptr}) ${ptr}->forceIsChanged();`);
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
        // Registre signé (positions d'un robot…) : -32768 à 32767.
        acc.get = node.signed ? `(int32_t)(int16_t)${ptr}->getValue()` : `(int32_t)${ptr}->getValue()`;
        break;
      case 'Uint16OutputNode':
        // Registre Modbus écrit (0 à 65535) : écrit seulement quand la valeur change.
        acc.get = `(int32_t)${ptr}->getValue()`;
        acc.set = (x) => `{ uint16_t _v = (uint16_t)(${x}); if (${ptr}->getValue() != _v) ${ptr}->setValue(_v); }`;
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
// Axes : réglages, câblage des signaux et création du drive (un cas par type d'axe).

// Unités par unité de course (impulsions ou pas) : option pulsesPerUnit ou stepsPerUnit.
function unitsFactor(eq) {
  return Number(eq.options?.pulsesPerUnit) || Number(eq.options?.stepsPerUnit) || 1;
}

// Butées et bornes de vitesse de l'AxisController.
function axisConfig(a) {
  const d = a.def;
  // Course maximale en unités natives = course (unités) x impulsions par unité ; 0 = pas de butée haute.
  const maxPosition = Math.max(0, Math.round((Number(a.eq.options?.maxRange) || 0) * unitsFactor(a.eq)));
  return {
    minPosition: 0,
    maxPosition,
    minSpeed: d.speedMin ?? 0,
    maxSpeed: d.speedMax ?? 15,
    jogMinSpeed: d.jogSpeedMin ?? 1,
    jogMaxSpeed: d.jogSpeedMax ?? 3000,
    jogReverseStopsAtZero: 'true',
  };
}

// Signal TOR de l'axe (servo ON, step, dir...) : nœud de la sortie câblée.
function axisSignalAssign(a, v) {
  const sig = (a.entry.signals || []).find((x) => x.key === v.system.signal) || {};
  const target = `axNodes_${a.symbol}.${v.system.field}`;
  if (sig.cppType && sig.cppType !== 'BooleanOutputNode') {
    // Sortie directe de l'ESP32 exigée (impulsions d'un pas-à-pas) : contrôle du type réel du nœud.
    return [`    ${target} = (n_${v.symbol} && n_${v.symbol}->classType() == ${sig.classId}) ? static_cast<${sig.cppType}*>(n_${v.symbol}) : nullptr;`];
  }
  return [`    ${target} = n_${v.symbol};`];
}

// Nœuds d'affichage d'un axe (display: 'position') : position calculée par le drive
// (ex. mots 32 bits remis dans l'ordre), recopiée à chaque cycle.
function axisDisplayNodes(a, layout) {
  return [...layout.nodes].filter(([ref, n]) => ref.startsWith(`axis:${a.uid}:`) && n.display === 'position').map(([, n]) => n);
}

function axisCreate(a, N) {
  const o = a.eq.options || {};
  const s = a.symbol;
  const lines = [`    if (BorneUniverselle::getInstance()->isPlcBroken()) return false;`];
  const broken = (what) => `BorneUniverselle::setPlcBroken(${cstr(`${N}: ${what}`)});`;
  switch (a.def.kind) {
    case 'sureservo':
      lines.push(
        `    if (!axNodes_${s}.validateRequired()) { ${broken(`nœuds obligatoires de l'axe « ${a.label} » manquants`)} return false; }`,
        `    axDrive_${s}.reset(new SureServo(${Number(o.pulsesPerUnit) || 1}, axNodes_${s}, HomePositionPolicy::${o.homePolicy === 'NEGATIVE_TOLERATED' ? 'NEGATIVE_TOLERATED' : 'NEGATIVE_INVALIDATES_HOME'}, ${cppLiteral(Math.max(0, Number(o.maxRange) || 0), 'float')}, ${cstr(a.label)}${o.invertJog === false ? ', false' : ''}));`,
        `    {`,
        `        const char* err = axDrive_${s}->getLastError();`,
        `        if (err && strlen(err) > 0 && strcmp(err, "Pas d'erreur") != 0) {`,
        `            char msg[160];`,
        `            snprintf(msg, sizeof(msg), ${cstr(`${N}: création de l'axe « ${a.label} » impossible : %s`)}, err);`,
        `            BorneUniverselle::setPlcBroken(msg);`,
        `            return false;`,
        `        }`,
        `    }`
      );
      break;
    case 'lichuan': {
      const accel = Math.max(0, Math.min(65535, Math.round(Number(o.accelTime) || 0)));
      lines.push(
        `    if (!axNodes_${s}.validateRequired()) { ${broken(`nœuds obligatoires de l'axe « ${a.label} » manquants`)} return false; }`,
        `    axDrive_${s}.reset(new LichuanServo(LichuanFamily::${a.def.family === 'A5' ? 'A5' : 'A6'}, axNodes_${s}, ${cstr(a.label)}));`
      );
      if (accel) lines.push(`    axDrive_${s}->setAccelTime(${accel});`);
      break;
    }
    case 'stepperonline-rs': {
      const models = a.def.models || ['DM556RS'];
      const model = models.includes(o.model) ? o.model : models[0];
      const accel = Math.max(0, Math.min(10000, Math.round(Number(o.accelTime ?? 300) || 0)));
      const ref = Math.max(0, Math.min(82, Math.round(Number(o.torqueRefCurrent) || 0)));
      lines.push(
        `    if (!axNodes_${s}.validateRequired()) { ${broken(`nœuds obligatoires de l'axe « ${a.label} » manquants`)} return false; }`,
        `    axDrive_${s}.reset(new StepperOnlineRS(StepperOnlineRSModel::${model}, axNodes_${s}, ${cstr(a.label)}));`
      );
      if (accel !== 300) lines.push(`    axDrive_${s}->setAccelTime(${accel});`);
      if (ref) lines.push(`    axDrive_${s}->setTorqueReferenceCurrent(${ref});`);
      break;
    }
    case 'stepper': {
      const factor = Math.max(1, Math.round(Number(o.stepsPerUnit) || 1));
      const maxSteps = Math.max(0, Math.round((Number(o.maxRange) || 0) * factor));
      const accel = Math.max(1, Math.round(Number(o.acceleration) || 5000));
      lines.push(
        `    if (!axNodes_${s}.validateRequired()) { ${broken(`STEP et DIR de l'axe « ${a.label} » doivent être câblés sur des sorties GPIO directes`)} return false; }`,
        `    axDrive_${s}.reset(new StepperMotor(${factor}, axNodes_${s}, ${maxSteps}, ${cstr(a.label)}, ${accel}));`,
        `    axDrive_${s}->setHomingParameters(${Number(o.homingDirection) > 0 ? 1 : -1}, ${Math.max(1, Math.round(Number(o.homingSpeed) || 1000))}, ${Math.max(0, Math.round(Number(o.homingBackoff) || 0))});`,
        `    axDrive_${s}->setDirectionInverted(${o.invertDirection === true ? 'true' : 'false'});`,
        `    axDrive_${s}->setEnableActiveLow(${o.enableActiveLow === false ? 'false' : 'true'});`
      );
      break;
    }
    default:
      throw new Error(`Type d'axe non géré par le générateur : ${a.eq.type}`);
  }
  lines.push(`    ${`ax_${s}`}.attach(axDrive_${s}.get());`);
  return lines;
}

// ---------------------------------------------------------------------------
// Texte sûr dans un commentaire C++ d'une ligne (pas de saut de ligne ni de « \ » final).
const comment = (s) => String(s ?? '').replace(/[\\\r\n]+/g, ' ').replace(/\*\//g, '* /').slice(0, 120);

const MAX_EVOLUTIONS = 16;

export function generateCpp(project, layout, ir) {
  const N = project.name;
  // Axes (SureServo, Lichuan, pas-à-pas) : un AxisController par équipement.
  const axes = axisList(project).map((a) => ({ ...a, block: (ir.axes || []).find((b) => b.uid === a.uid) || { homing: 'auto', homingOrder: 1 }, def: a.entry.axis || {} }));
  const hasAxes = axes.length > 0;
  const axisOf = new Map(axes.map((a) => [a.uid, a]));
  const ax = (a) => `ax_${a.symbol}`;
  const homingAxes = axes.filter((a) => a.block.homing !== 'none');
  const jogAxes = axes.filter((a) => a.block.jog);
  const sureServos = axes.filter((a) => a.def.kind === 'sureservo');
  const { access, banks } = variableAccess(project, layout);
  const stepIndex = new Map(ir.steps.map((s, i) => [s.num, i]));
  const edgeIndex = new Map(ir.edges.map((e, i) => [e.key, i]));
  const stateNode = layout.nodes.get('sys:state');
  const clockNode = layout.nodes.get('sys:clock');
  const outputs = project.variables.filter((v) => v.kind === 'output' && !v.system);
  const params = project.variables.filter((v) => v.kind === 'parameter');
  const drivenSet = new Map(ir.driven.map((d) => [d.sym, d]));
  const mirrors = [...layout.nodes.entries()]
    .filter(([ref]) => ref.startsWith('mirror:'))
    .map(([, node]) => ({ node, variable: project.variables.find((v) => v.uid === node.variable) }))
    .filter((m) => m.variable);

  const baseNames = {
    var: (sym) => `v_${sym}()`,
    step: (num) => `X[${stepIndex.get(num)}]`,
    stepTime: (num) => `stepTime(${stepIndex.get(num)})`,
    axis: (uid, prop) => `${ax(axisOf.get(uid))}.${AXIS_PROPS[prop].cpp}`,
  };
  // Les fronts comparent la valeur capturée en début de cycle (S) à celle du cycle précédent (E).
  const names = { ...baseNames, cur: (key) => `S[${edgeIndex.get(key)}]`, prev: (key) => `E[${edgeIndex.get(key)}]` };
  const cx = (ast) => toCpp(ast, names);
  const raw = (ast) => toCpp(ast, baseNames); // valeur directe (capture des fronts)
  const setCall = (sym, valueCpp) => `set_${sym}(${valueCpp});`;
  const emergencyExpr = ir.blocks.emergency ? cx(ir.blocks.emergency) : 'false';

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
  if (hasAxes) H.push(`#include "AxisController/AxisController.h"`);
  for (const inc of new Set(axes.map((a) => a.def.include))) H.push(`#include "${inc}"`);
  H.push(``, `// Identifiants des nœuds (CRC-32 de "<id><section>", voir data/config.json).`, `namespace ${N}Nodes {`);
  for (const [sym, acc] of access) {
    if (!acc.hashName) continue;
    H.push(`    constexpr uint32_t H_${sym} = ${acc.node.hash}u;  // ${comment(acc.node.section)} / id ${acc.node.id}`);
  }
  for (const b of banks.values()) H.push(`    constexpr uint32_t H_${b.hashName} = ${b.hash}u;  // ${comment(b.section)}`);
  for (const m of mirrors) H.push(`    constexpr uint32_t H_MIRROR_${m.variable.symbol} = ${m.node.hash}u;  // ${comment(m.node.section)} / id ${m.node.id}`);
  if (stateNode) H.push(`    constexpr uint32_t H_SYS_STATE = ${stateNode.hash}u;`);
  if (clockNode) H.push(`    constexpr uint32_t H_SYS_CLOCK = ${clockNode.hash}u;`);
  for (const a of axes) {
    for (const [ref, node] of layout.nodes) {
      if (ref.startsWith(`axis:${a.uid}:`)) H.push(`    constexpr uint32_t H_AX_${a.symbol}_${ident(node.field)} = ${node.hash}u;  // ${comment(node.section)} / id ${node.id}`);
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
    sureServos.length ? `    bool isPmActive() const { return ${sureServos.map((a) => `(axDrive_${a.symbol} && axDrive_${a.symbol}->isPmActive())`).join(' || ')}; }` : `    bool isPmActive() const { return false; }`,
    sureServos.length ? `    void pmAccumulateRefresh(uint32_t ms) { ${sureServos.map((a) => `if (axDrive_${a.symbol}) axDrive_${a.symbol}->pmAccumulateRefresh(ms);`).join(' ')} }` : `    void pmAccumulateRefresh(uint32_t) {}`,
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
  for (const m of mirrors) H.push(`    BooleanOutputNode* mirror_${m.variable.symbol} = nullptr;  // recopie de la voie pour l'écran`);
  if (stateNode) H.push(`    VirtualTextOutputNode* sysStateNode = nullptr;`);
  if (clockNode) H.push(`    VirtualTextOutputNode* sysClockNode = nullptr;`);

  H.push(
    ``,
    `    // ---- Grafcet (${ir.steps.length} étapes, ${ir.transitions.length} transitions)`,
    `    static constexpr int STEP_COUNT = ${stepCount};`,
    `    static constexpr int TRANSITION_COUNT = ${transCount};`,
    `    static constexpr int EDGE_COUNT = ${edgeCount};`,
    `    static constexpr int MAX_EVOLUTIONS = ${MAX_EVOLUTIONS};  // recherche de stabilité`,
    stateNode ? `    static const int STEP_NUMBERS[STEP_COUNT];` : null,
    `    bool X[STEP_COUNT] = {};          // étapes actives`,
    `    bool Xnew[STEP_COUNT] = {};       // étapes activées, actions à l'activation à exécuter`,
    `    uint32_t Xstart[STEP_COUNT] = {}; // instant d'activation`,
    `    bool S[EDGE_COUNT] = {};          // opérandes des fronts, capturés en début de cycle`,
    `    bool E[EDGE_COUNT] = {};          // mêmes opérandes au cycle précédent`,
    `    uint32_t now = 0;`,
    `    uint32_t emergencyClearSince = 0;`,
    `    bool unstableReported = false;`,
    `    bool initialised = false;`
  );
  for (const d of ir.driven) if (d.latch) H.push(`    bool latch_${d.sym} = false;`);
  for (const a of axes) {
    const c = axisConfig(a);
    H.push(
      ``,
      `    // ---- Axe « ${comment(a.label)} » (${comment(a.entry.label)}${a.entry.modbus ? `, adresse Modbus ${Number(a.eq.address)}` : ''})`,
      `    ${a.def.nodesStruct} axNodes_${a.symbol};`,
      ...axisDisplayNodes(a, layout).map((n) => `    Uint32OutputNode* axDisp_${a.symbol}_${ident(n.field)} = nullptr;  // ${comment(n.label)} (écrit par la classe)`),
      `    std::unique_ptr<${a.def.cppClass}> axDrive_${a.symbol};`,
      `    static AxisController::Config axConfig_${a.symbol}() {`,
      `        AxisController::Config c;`,
      ...Object.entries(c).map(([k, v]) => `        c.${k} = ${v};`),
      `        return c;`,
      `    }`,
      `    AxisController ${ax(a)}{${cstr(a.label)}, axConfig_${a.symbol}()};`
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
    `    int32_t stepTime(int i) const { return X[i] ? (int32_t)(now - Xstart[i]) : 0; }`,
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
    `    bool grafcetEvolve();`,
    `    void computeOutputs();`,
    `    void captureEdges();`,
    `    void updateEdges();`,
    `    bool emergencyCondition();`,
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
  if (hasAxes) {
    H.push(
      `    bool createAxes(const char* context);`,
      `    bool axesService();                 // déplacements et prises d'origine du grafcet ; false : défaut`
    );
  }
  H.push(`    bool axesMoving() const { return ${hasAxes ? axes.map((a) => `${ax(a)}.isMoving()`).join(' || ') : 'false'}; }`);
  H.push(`};`, ``);

  // ---------------- Implémentation ----------------
  const C = [];
  const L = (...lines) => C.push(...lines.filter((x) => x !== null && x !== undefined));
  L(
    `// =============================================================================`,
    `// ${N}.cpp — généré par PLC Studio (tools/plc-studio)`,
    `// NE PAS MODIFIER À LA MAIN : ce fichier est régénéré depuis projets/${N}.plc.json`,
    `// =============================================================================`,
    `#include "${N}.h"`,
    clockNode ? `#include "DS3231_RTC/ClockDisplay.h"` : null,
    ``,
    stateNode ? `const int ${N}::STEP_NUMBERS[${N}::STEP_COUNT] = { ${ir.steps.length ? ir.steps.map((s) => s.num).join(', ') : '0'} };` : null,
    stateNode ? `` : null,
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
  for (const b of banks.values()) {
    L(`    initNodePtr(${b.name}, ${N}Nodes::H_${b.hashName}, context);`);
    if (b.cls === 'ModbusWriteMultipleCoilslNode') L(`    if (${b.name}) ${b.name}->forceIsChanged();  // état de repos écrit au démarrage`);
  }
  for (const m of mirrors) L(`    initNodePtr(mirror_${m.variable.symbol}, ${N}Nodes::H_MIRROR_${m.variable.symbol}, context);`);
  if (stateNode) L(`    initNodePtr(sysStateNode, ${N}Nodes::H_SYS_STATE, context);`);
  if (clockNode) L(`    initNodePtr(sysClockNode, ${N}Nodes::H_SYS_CLOCK, context);`, `    ClockDisplay::attach(sysClockNode);`);
  L(`    if (BorneUniverselle::getInstance()->isPlcBroken()) {`, `        Serial.println("${N}: erreur pendant l'initialisation des pointeurs");`, `        return false;`, `    }`);
  if (hasAxes) L(`    if (!createAxes(context)) return false;`);
  L(`    applyParamsToNodes();`, `    return true;`, `}`, ``);

  if (hasAxes) {
    L(`bool ${N}::createAxes(const char* context) {`);
    for (const a of axes) {
      L(`    // Axe « ${comment(a.label)} »`);
      for (const [ref, node] of layout.nodes) {
        if (!ref.startsWith(`axis:${a.uid}:`)) continue;
        const target = node.display ? `axDisp_${a.symbol}_${ident(node.field)}` : `axNodes_${a.symbol}.${node.field}`;
        L(`    initNodePtr(${target}, ${N}Nodes::H_AX_${a.symbol}_${ident(node.field)}, context);`);
      }
      // Signaux câblés (les signaux facultatifs non câblés restent à nullptr).
      for (const v of project.variables.filter((x) => x.system?.eq === a.uid && layout.nodes.has(`var:${x.uid}`))) {
        L(...axisSignalAssign(a, v));
      }
      L(...axisCreate(a, N));
    }
    L(`    return true;`, `}`, ``);
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
    ...axes.map((a) => `    ${ax(a)}.process();`),
    ...axes.flatMap((a) =>
      axisDisplayNodes(a, layout).map((n) => {
        const d = `axDisp_${a.symbol}_${ident(n.field)}`;
        return `    if (${d}) { int32_t _p = ${ax(a)}.position(); if (${d}->getValueAsInt32() != _p) ${d}->setValueFromInt32(_p); }`;
      })
    ),
    `    if (BorneUniverselle::getInstance()->isPlcBroken()) return false;`,
    `    captureEdges();`,
    ``,
    `    // Arrêt d'urgence : prioritaire sur tous les modes (y compris pendant le réarmement).`,
    `    State s = cur();`,
    `    if (s != State::EMERGENCY && s != State::UNDEFINED) {`,
    `        if (emergencyCondition()) {`,
    `            char reason[160];`,
    `            snprintf(reason, sizeof(reason), "Arrêt d'urgence : %s", ${cstr(project.logic?.blocks?.emergency?.condition || '')});`,
    `            BorneUniverselle::prepareMessage(ERROR, reason);`,
    `            PLC_Tools::logDiagnostic(reason);`,
    `            go(State::EMERGENCY);`,
    `        }`,
    ...axes.flatMap((a) => [
      `        else if (s != State::RESETTING && ${ax(a)}.hasAlarm()) {`,
      `            char reason[160];`,
      `            snprintf(reason, sizeof(reason), "Alarme ${cstr(a.label).slice(1, -1)} 0x%04X : %s", ${ax(a)}.alarmCode(), ${ax(a)}.alarmText());`,
      `            BorneUniverselle::prepareMessage(ERROR, reason);`,
      `            PLC_Tools::logDiagnostic(reason);`,
      `            go(State::EMERGENCY);`,
      `        }`,
    ]),
    `    }`
  );
  if (ir.blocks.stop) {
    L(
      `    // Arrêt demandé : retour en ARRÊT (mouvements stoppés et grafcet remis à zéro en entrant dans ARRÊT).`,
      `    s = cur();`,
      `    if ((s == State::IDLE || s == State::RUNNING || s == State::HOMING || s == State::JOGGING) && ${cx(ir.blocks.stop)}) {`,
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
    clockNode ? `    if (shouldShowClock()) updateClockNode(sysClockNode);` : null,
    `    return true;`,
    `}`,
    ``
  );

  L(`bool ${N}::emergencyCondition() {`, `    return ${emergencyExpr};`, `}`, ``);

  // Modes
  L(`void ${N}::handleInitializing() {`);
  if (hasAxes) {
    // Tous les variateurs s'initialisent en parallèle ; ARRÊT quand ils ont tous fini.
    L(`    bool done = true;`, `    bool failed = false;`);
    for (const a of axes) {
      L(`    switch (${ax(a)}.initStep()) {`, `        case AxisController::Progress::DONE: break;`, `        case AxisController::Progress::RUNNING: done = false; break;`, `        default: failed = true; break;`, `    }`);
    }
    L(`    if (failed) { go(State::EMERGENCY); return; }`, `    if (done) go(State::STOP);`);
  } else {
    L(`    go(State::STOP);`);
  }
  L(`}`, ``);

  const startExpr = ir.blocks.start ? cx(ir.blocks.start) : 'true';
  const stopExpr = ir.blocks.stop ? cx(ir.blocks.stop) : 'false';
  const jogModeExpr = jogAxes.length ? jogAxes.map((a) => `(${cx(a.block.jog.mode)})`).join(' || ') : null;
  const homingNeededExpr = homingAxes.map((a) => `!${ax(a)}.homeDone()`).join(' || ');
  L(`void ${N}::handleStop() {`, `    if (${stopExpr}) return;  // arrêt maintenu`);
  if (jogModeExpr) L(`    if (${jogModeExpr}) { go(State::JOGGING); return; }`);
  L(`    if (!(${startExpr})) return;`);
  if (homingAxes.length) {
    L(`    if (${homingNeededExpr}) {`, `        bool armed = false;`);
    for (const a of homingAxes) {
      const trigger = a.block.homing === 'condition' ? cx(a.block.homingCondition) : 'true';
      L(`        if (!${ax(a)}.homeDone() && (${trigger})) { ${ax(a)}.armHoming(); armed = true; }`);
    }
    L(`        if (armed) go(State::HOMING);`, `        return;`, `    }`);
  }
  L(`    grafcetReset();`, `    go(State::IDLE);`, `}`, ``);

  L(`void ${N}::handleHoming() {`);
  if (homingAxes.length) {
    // Prises d'origine par ordre croissant (homingOrder) ; les axes d'un même ordre en parallèle.
    const orders = [...new Set(homingAxes.map((a) => a.block.homingOrder || 1))].sort((x, y) => x - y);
    for (const order of orders) {
      const group = homingAxes.filter((a) => (a.block.homingOrder || 1) === order);
      L(`    {  // ordre ${order} : ${group.map((a) => comment(a.label)).join(', ')}`);
      for (const a of group) L(`        const AxisController::Progress p_${a.symbol} = ${ax(a)}.homingStep();`);
      L(
        `        if (${group.map((a) => `p_${a.symbol} == AxisController::Progress::FAILED`).join(' || ')}) { go(State::EMERGENCY); return; }`,
        `        if (${group.map((a) => `p_${a.symbol} != AxisController::Progress::DONE`).join(' || ')}) return;`,
        `    }`
      );
    }
    L(
      `    BorneUniverselle::prepareMessage(SUCCESS, "Prise d'origine terminée");`,
      `    if (${homingNeededExpr}) { go(State::STOP); return; }  // axe à condition non déclenchée`,
      `    grafcetReset();`,
      `    go(State::IDLE);`
    );
  } else L(`    go(State::STOP);`);
  L(`}`, ``);

  L(`void ${N}::handleRun() {`);
  if (jogModeExpr) L(`    if ((${jogModeExpr}) && grafcetInInitialSituation() && !axesMoving()) { go(State::JOGGING); return; }`);
  if (hasAxes) L(`    if (!axesService()) { go(State::EMERGENCY); return; }`);
  L(
    `    // Actions à l'activation en attente (étapes initiales après une remise à zéro).`,
    `    grafcetActivationActions();`,
    `    if (cur() == State::EMERGENCY) return;  // déclenchée par une action (ex. cible hors course)`,
    `    // Recherche de stabilité : on franchit tant que possible dans le même cycle ; les étapes`,
    `    // instables exécutent leurs actions à l'activation mais pas leurs actions continues.`,
    `    for (int iter = 0; iter < MAX_EVOLUTIONS; iter++) {`,
    `        if (!grafcetEvolve()) break;`,
    `        if (iter == 0) for (int k = 0; k < EDGE_COUNT; k++) E[k] = S[k];  // un front ne vaut qu'une fois`,
    `        grafcetActivationActions();`,
    `        if (cur() == State::EMERGENCY) return;`,
    `        if (iter == MAX_EVOLUTIONS - 1 && !unstableReported) {`,
    `            BorneUniverselle::prepareMessage(WARNING, "Grafcet instable : boucle de franchissements sans fin");`,
    `            unstableReported = true;`,
    `        }`,
    `    }`,
    `    for (int i = 0; i < STEP_COUNT; i++) Xnew[i] = false;`,
    `    go(grafcetInInitialSituation() && !axesMoving() ? State::IDLE : State::RUNNING);`,
    `    updateStateDisplay();`,
    `}`,
    ``
  );

  L(`void ${N}::handleJogging() {`);
  if (jogAxes.length) {
    L(`    bool anyMode = false;`);
    for (const a of jogAxes) {
      const j = a.block.jog;
      L(
        `    {  // ${comment(a.label)}`,
        `        const bool mode = ${cx(j.mode)};`,
        `        anyMode = anyMode || mode;`,
        `        // Le drive attend un appel à chaque cycle tant que le bouton est maintenu (butée, zone lente).`,
        `        if (!${ax(a)}.jogStep(mode && (${cx(j.plus)}), mode && (${cx(j.minus)}), (int32_t)(${cx(j.speed)}))) { go(State::EMERGENCY); return; }`,
        `    }`
      );
    }
    L(`    if (!anyMode) go(State::STOP);  // le jog est arrêté en entrant dans ARRÊT`);
  } else L(`    go(State::STOP);`);
  L(`}`, ``);

  L(
    `void ${N}::handleEmergency() {`,
    `    // Seule la condition d'urgence bloque la sortie : une alarme d'axe est justement`,
    `    // effacée par le réarmement (startReset). Si elle persiste, l'urgence se redéclenche.`,
    `    if (emergencyCondition()) { emergencyClearSince = 0; return; }`,
    `    if (emergencyClearSince == 0) emergencyClearSince = now ? now : 1;`
  );
  if (ir.blocks.emergencyReset) L(`    if (!(${cx(ir.blocks.emergencyReset)})) return;   // acquittement`);
  else L(`    if (now - emergencyClearSince < 1000) return;   // sortie automatique après 1 s`);
  if (hasAxes) {
    L(`    bool started = true;`);
    for (const a of axes) L(`    started = ${ax(a)}.startReset() && started;`);
    L(`    if (started) {`, `        BorneUniverselle::prepareMessage(INFO, ${cstr(axes.length === 1 ? `Réarmement de ${axes[0].label}…` : 'Réarmement des axes…')});`, `        go(State::RESETTING);`, `    }`);
  } else L(`    BorneUniverselle::prepareMessage(SUCCESS, "Arrêt d'urgence acquitté");`, `    go(State::STOP);`);
  L(`}`, ``);

  L(`void ${N}::handleResetting() {`);
  if (hasAxes) {
    L(`    bool done = true;`);
    for (const a of axes) {
      L(`    switch (${ax(a)}.resetStep()) {`, `        case AxisController::Progress::RUNNING: done = false; break;`, `        case AxisController::Progress::FAILED: go(State::EMERGENCY); return;`, `        default: break;`, `    }`);
    }
    L(
      `    if (!done) return;`,
      `    BorneUniverselle::prepareMessage(SUCCESS, "Arrêt d'urgence acquitté");`,
      `    // Le réarmement relance l'initialisation des variateurs si elle avait été interrompue.`,
      `    go((${axes.map((a) => `${ax(a)}.needsInitialization()`).join(' || ')}) ? State::INITIALIZING : State::STOP);`
    );
  } else L(`    go(State::STOP);`);
  L(`}`, ``);

  // Axes : déplacements et prises d'origine demandés par le grafcet, couple maximal.
  if (hasAxes) {
    L(`bool ${N}::axesService() {`);
    for (const a of axes) {
      if (a.block.torque) L(`    ${ax(a)}.setTorque((int)(${cx(a.block.torque)}));`);
      L(`    if (!${ax(a)}.service()) return false;`);
    }
    L(`    return true;`, `}`, ``);
  }

  // Grafcet
  L(`void ${N}::grafcetClear() {`, `    for (int i = 0; i < STEP_COUNT; i++) { X[i] = false; Xnew[i] = false; Xstart[i] = 0; }`);
  for (const d of ir.driven) if (d.latch) L(`    latch_${d.sym} = false;`);
  L(`    unstableReported = false;`, `}`, ``);
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
          // Cible hors course : message affiché par l'axe, passage en URGENCE.
          lines.push(`if (!${ax(axisOf.get(a.axis))}.moveTo((int32_t)(${cx(a.position)}), (int32_t)(${cx(a.speed)}))) go(State::EMERGENCY);`);
          break;
        case 'SERVO_HOME':
          lines.push(`${ax(axisOf.get(a.axis))}.startHoming();`);
          break;
        case 'SERVO_STOP':
          lines.push(`${ax(axisOf.get(a.axis))}.halt();`);
          break;
        case 'MSG':
          lines.push(`BorneUniverselle::prepareMessage(${{ info: 'INFO', warning: 'WARNING', error: 'ERROR', success: 'SUCCESS' }[a.level]}, ${cstr(a.text)});`);
          break;
        default:
          break;
      }
    }
    if (lines.length) L(`    if (Xnew[${i}]) {  // étape ${s.num}${s.label ? ' — ' + comment(s.label) : ''}`, ...lines.map((x) => `        ${x}`), `    }`);
  });
  L(`    for (int i = 0; i < STEP_COUNT; i++) Xnew[i] = false;`, `}`, ``);

  L(`bool ${N}::grafcetEvolve() {`, `    bool fire[TRANSITION_COUNT] = {};`, `    bool any = false;`);
  ir.transitions.forEach((t, i) => {
    const up = t.from.map((k) => `X[${k}]`).join(' && ') || 'false';
    const cond = t.cond ? cx(t.cond) : 'false';
    L(`    fire[${i}] = ${up} && ${cond};  // T${t.num}: ${comment(t.text)}`, `    any = any || fire[${i}];`);
  });
  L(`    if (!any) return false;`, `    // Désactivation des étapes amont, puis activation des étapes aval (prioritaire).`);
  ir.transitions.forEach((t, i) => {
    if (t.from.length) L(`    if (fire[${i}]) { ${t.from.map((k) => `X[${k}] = false;`).join(' ')} }`);
  });
  ir.transitions.forEach((t, i) => {
    if (t.to.length) L(`    if (fire[${i}]) { ${t.to.map((k) => `activateStep(${k});`).join(' ')} }`);
  });
  L(`    return true;`, `}`, ``);

  // Sorties
  L(
    `void ${N}::computeOutputs() {`,
    `    const State s = cur();`,
    `    const bool run = (s == State::IDLE || s == State::RUNNING);`,
    `    const bool emergency = (s == State::EMERGENCY || s == State::RESETTING);`,
    `    (void)run; (void)emergency;`
  );
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
  const others = outputs.filter((v) => !drivenSet.has(v.symbol) && access.get(v.symbol)?.set);
  if (others.length) {
    // Sorties booléennes : true/false ; registres Modbus écrits (entiers) : 1/0.
    const lit = (v, on) => (v.dataType === 'int' ? (on ? '1' : '0') : on ? 'true' : 'false');
    L(`    if (emergency) {`);
    for (const v of others) if (v.fallback !== 'hold') L(`        set_${v.symbol}(${lit(v, v.fallback === 'on')});`);
    L(`    } else if (!run) {`);
    for (const v of others) L(`        set_${v.symbol}(${lit(v, false)});`);
    L(`    }`);
  }
  for (const m of mirrors) L(`    mirror_${m.variable.symbol}->setValue(v_${m.variable.symbol}());`);
  L(`}`, ``);

  L(`void ${N}::captureEdges() {`);
  ir.edges.forEach((e, i) => L(`    S[${i}] = ${raw(e.ast)};`));
  L(`}`, ``, `void ${N}::updateEdges() {`, `    for (int k = 0; k < EDGE_COUNT; k++) E[k] = S[k];`, `}`, ``);

  // Affichage de l'état
  L(`void ${N}::updateStateDisplay() {`);
  if (stateNode) {
    L(
      `    if (!sysStateNode) return;`,
      `    char text[160];`,
      `    int n = snprintf(text, sizeof(text), "%s", stateToString(static_cast<int>(cur())));`,
      `    if (cur() == State::IDLE || cur() == State::RUNNING) {`,
      `        n += snprintf(text + n, sizeof(text) - n, " :");`,
      `        for (int i = 0; i < STEP_COUNT && n < (int)sizeof(text) - 12; i++) {`,
      `            if (X[i]) n += snprintf(text + n, sizeof(text) - n, " X%d", STEP_NUMBERS[i]);`,
      `        }`,
      `    }`,
      `    if (strcmp(sysStateNode->getValue(), text) != 0) sysStateNode->setValue(text);`
    );
  }
  L(`}`, ``);

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
    const numeric = v.dataType === 'int' || v.dataType === 'float';
    const has = (x) => numeric && x !== undefined && x !== null && x !== '' && Number.isFinite(Number(x));
    if (has(v.min)) L(`    if (v_${v.symbol}() < ${cppLiteral(v.min, v.dataType)}) set_${v.symbol}(${cppLiteral(v.min, v.dataType)});`);
    if (has(v.max)) L(`    if (v_${v.symbol}() > ${cppLiteral(v.max, v.dataType)}) set_${v.symbol}(${cppLiteral(v.max, v.dataType)});`);
    L(`    if (saved_${v.symbol} != v_${v.symbol}()) { saved_${v.symbol} = v_${v.symbol}(); dirty = true; }`);
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
    `    // Pas de changement d'état ici (règle BusinessLogic).`,
    `    switch (static_cast<State>(newState)) {`,
    `        case State::EMERGENCY:`,
    ...axes.map((a) => `            ${ax(a)}.onEmergency();`),
    `            emergencyClearSince = 0;`,
    `            grafcetClear();`,
    `            break;`,
    `        case State::STOP:`,
    ...axes.map((a) => `            ${ax(a)}.halt(oldState == static_cast<int>(State::HOMING));`),
    `            grafcetClear();`,
    `            break;`,
    `        default:`,
    `            break;`,
    `    }`,
    hasAxes ? null : `    (void)oldState;`,
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
    header: H.filter((x) => x !== null && x !== undefined).join('\n') + '\n',
    source: C.join('\n') + '\n',
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

// Grafcet : modèle, validation et compilation en représentation intermédiaire (IR).
//
// project.logic = {
//   steps:       [{ id, num, label, initial, x, y, actions: [Action] }],
//   transitions: [{ id, num, from: [stepId], to: [stepId], condition, comment, x, y }],
//   blocks:      { emergency, run, axes: { <uid axe>: bloc d'axe } }   (modes de marche, voir DEFAULT_BLOCKS)
// }
//
// Action = { id, type, target?, value?, condition?, axis?, position?, speed?, level?, text? }
//   N        sortie vraie tant que l'étape est active (condition optionnelle)
//   S / R    mise à 1 / à 0 mémorisée à l'activation
//   SET      affectation à l'activation          (target := value)
//   NSET     affectation continue tant qu'active (target := value)
//   INC/DEC  +1 / -1 à l'activation
//   SERVO_MOVE  déplacement d'un axe (position, vitesse en unités du drive) à l'activation
//   SERVO_HOME  prise d'origine d'un axe à l'activation
//   SERVO_STOP  arrêt du mouvement d'un axe à l'activation
//   (axis = uid de l'équipement ; facultatif quand le projet n'a qu'un axe)
//   MSG      message à l'écran à l'activation (level : info | warning | error | success)
//
// Sémantique d'un cycle (identique en simulation et en C++) :
//   1. transitions franchissables = étapes amont actives ET réceptivité vraie
//   2. franchissement simultané : désactivation des amont puis activation des aval
//   3. actions à l'activation des étapes nouvellement actives (dans l'ordre des étapes)
//   4. actions continues (N, NSET) puis écriture des sorties
//   5. mémorisation des valeurs pour les fronts

import { parse, tryParse, projectContext, walk, edgeKey, usedSymbols } from './expr.js';
import { axisList } from './axes.js';
import { DEFAULT_AXIS_BLOCK } from './model.js';

export const ACTION_TYPES = {
  N: { label: 'Continue (N)', help: 'Vraie tant que l’étape est active', needs: ['target'], targetType: 'bool', optional: ['condition'] },
  S: { label: 'Mise à 1 mémorisée (S)', help: 'Reste à 1 jusqu’à une action R', needs: ['target'], targetType: 'bool' },
  R: { label: 'Mise à 0 mémorisée (R)', help: 'Annule une action S', needs: ['target'], targetType: 'bool' },
  SET: { label: 'Affecter à l’activation', help: 'cible := valeur, une fois', needs: ['target', 'value'] },
  NSET: { label: 'Affecter en continu', help: 'cible := valeur à chaque cycle', needs: ['target', 'value'] },
  INC: { label: 'Incrémenter (+1)', help: 'À l’activation', needs: ['target'], targetType: 'int' },
  DEC: { label: 'Décrémenter (-1)', help: 'À l’activation', needs: ['target'], targetType: 'int' },
  SERVO_MOVE: { label: 'Axe : aller à', help: 'Position et vitesse dans les unités de l’axe (SureServo : impulsions, vitesse 0-15)', needs: ['axis', 'position', 'speed'], servo: true },
  SERVO_HOME: { label: 'Axe : prise d’origine', help: 'À l’activation', needs: ['axis'], servo: true },
  SERVO_STOP: { label: 'Axe : arrêt', help: 'Arrêt immédiat du mouvement', needs: ['axis'], servo: true },
  MSG: { label: 'Message à l’écran', help: 'Notification à l’activation', needs: ['text'] },
};

export { DEFAULT_AXIS_BLOCK };

export const DEFAULT_BLOCKS = {
  emergency: { condition: '', reset: '' },
  run: { start: '', stop: '' },
  servo: DEFAULT_AXIS_BLOCK, // ancien nom (bloc de l'axe unique)
};

// Texte d'une action tel qu'affiché dans l'éditeur de grafcet (sert aussi à la mise en page).
// axisName(uid) : nom de l'axe d'une action SERVO_* (facultatif).
export function actionText(a, axisName) {
  const ax = (a.axis && axisName && axisName(a.axis)) || 'Axe';
  switch (a.type) {
    case 'N':
      return `N  ${a.target || '?'}${a.condition ? `  si ${a.condition}` : ''}`;
    case 'S':
      return `S  ${a.target || '?'}`;
    case 'R':
      return `R  ${a.target || '?'}`;
    case 'SET':
      return `${a.target || '?'} := ${a.value || '?'}`;
    case 'NSET':
      return `${a.target || '?'} = ${a.value || '?'}  (continu)`;
    case 'INC':
      return `${a.target || '?'} + 1`;
    case 'DEC':
      return `${a.target || '?'} − 1`;
    case 'SERVO_MOVE':
      return `${ax} → ${a.position || '?'}  (vitesse ${a.speed || '?'})`;
    case 'SERVO_HOME':
      return `${ax} : prise d’origine`;
    case 'SERVO_STOP':
      return `${ax} : arrêt`;
    case 'MSG':
      return `Message : ${a.text || ''}`;
    default:
      return a.type;
  }
}

// project (facultatif) : rattache le bloc « servo » des anciens projets à l'axe unique.
export function normalizeLogic(logic, project) {
  const l = logic || {};
  let axesBlocks = l.blocks?.axes || {};
  if (l.blocks?.servo && project) {
    const axes = axisList(project);
    if (axes.length === 1 && !axesBlocks[axes[0].uid]) axesBlocks = { ...axesBlocks, [axes[0].uid]: l.blocks.servo };
  }
  return {
    steps: Array.isArray(l.steps) ? l.steps.map((s) => ({ actions: [], ...s, actions: Array.isArray(s.actions) ? s.actions : [] })) : [],
    transitions: Array.isArray(l.transitions) ? l.transitions.map((t) => ({ from: [], to: [], condition: '', ...t })) : [],
    blocks: {
      emergency: { ...DEFAULT_BLOCKS.emergency, ...(l.blocks?.emergency || {}) },
      run: { ...DEFAULT_BLOCKS.run, ...(l.blocks?.run || {}) },
      axes: Object.fromEntries(Object.entries(axesBlocks).map(([uid, b]) => [uid, { ...DEFAULT_AXIS_BLOCK, ...(b || {}) }])),
    },
  };
}

// Grafcet de départ : étape initiale 0, transition, étape 1, retour.
export function starterGrafcet() {
  return {
    steps: [
      { id: 's0', num: 0, label: 'Attente', initial: true, x: 160, y: 60, actions: [] },
      { id: 's1', num: 1, label: 'Cycle', initial: false, x: 160, y: 220, actions: [] },
    ],
    transitions: [
      { id: 't0', num: 0, from: ['s0'], to: ['s1'], condition: 'VRAI', x: 160, y: 150 },
      { id: 't1', num: 1, from: ['s1'], to: ['s0'], condition: 'VRAI', x: 160, y: 310 },
    ],
  };
}

const writableKinds = new Set(['output', 'indicator', 'memory', 'parameter', 'command']);

export function isWritable(v) {
  return v && writableKinds.has(v.kind) && !v.system;
}

// Bloc d'un axe (valeurs par défaut si l'axe n'a jamais été réglé).
export function axisBlock(logic, uid) {
  return { ...DEFAULT_AXIS_BLOCK, ...(logic?.blocks?.axes?.[uid] || {}) };
}

// Compile le grafcet. Retourne { ir, issues }.
export function compileLogic(project) {
  const logic = normalizeLogic(project.logic, project);
  const issues = [];
  const err = (message, ref) => issues.push({ level: 'error', step: 'logic', message, ref });
  const warn = (message, ref) => issues.push({ level: 'warning', step: 'logic', message, ref });
  const axes = axisList(project);
  const axisByUid = new Map(axes.map((a) => [a.uid, a]));
  const defaultAxis = axes.length === 1 ? axes[0].uid : null;

  const steps = [...logic.steps].sort((a, b) => a.num - b.num);
  const stepNums = new Set();
  const stepIndexById = new Map();
  steps.forEach((s, i) => {
    if (!Number.isInteger(s.num) || s.num < 0 || s.num > 9999) err(`Étape « ${s.label || s.id} » : numéro invalide.`, s.id);
    if (stepNums.has(s.num)) err(`Deux étapes portent le numéro ${s.num}.`, s.id);
    stepNums.add(s.num);
    stepIndexById.set(s.id, i);
  });
  if (steps.length && !steps.some((s) => s.initial)) err('Le grafcet doit avoir au moins une étape initiale.');
  if (steps.length > 250) err('250 étapes au maximum.');

  const ctx = projectContext(project, { steps: stepNums });
  const varBySym = new Map(project.variables.map((v) => [v.symbol, v]));
  const edges = new Map(); // clé -> { key, ast }
  const collectEdges = (ast) =>
    walk(ast, (n) => {
      if (n.k === 'edge') {
        const key = edgeKey(n.a);
        if (!edges.has(key)) edges.set(key, { key, ast: n.a });
      }
    });

  const parseField = (text, what, ref, expectType) => {
    const r = tryParse(text, ctx);
    if (r.error) {
      err(`${what} : ${r.error}`, ref);
      return null;
    }
    if (expectType === 'bool' && r.ast.type !== 'bool') {
      err(`${what} : une condition booléenne est attendue.`, ref);
      return null;
    }
    if (expectType === 'num' && r.ast.type !== 'int' && r.ast.type !== 'float') {
      err(`${what} : une valeur numérique est attendue.`, ref);
      return null;
    }
    collectEdges(r.ast);
    return r.ast;
  };

  // Transitions
  const transitions = [];
  const transNums = new Set();
  for (const t of [...logic.transitions].sort((a, b) => a.num - b.num)) {
    const label = `Transition ${t.num}`;
    if (transNums.has(t.num)) err(`Deux transitions portent le numéro ${t.num}.`, t.id);
    transNums.add(t.num);
    const from = t.from.map((id) => stepIndexById.get(id)).filter((i) => i !== undefined);
    const to = t.to.map((id) => stepIndexById.get(id)).filter((i) => i !== undefined);
    if (!from.length) err(`${label} : aucune étape amont.`, t.id);
    if (!to.length) err(`${label} : aucune étape aval.`, t.id);
    if (!String(t.condition ?? '').trim()) err(`${label} : réceptivité vide (écrivez VRAI pour une transition toujours franchissable).`, t.id);
    const cond = String(t.condition ?? '').trim() ? parseField(t.condition, label, t.id, 'bool') : null;
    transitions.push({ id: t.id, num: t.num, from, to, cond, text: t.condition, comment: t.comment || '' });
  }

  // Étapes isolées
  steps.forEach((s, i) => {
    const hasIn = transitions.some((t) => t.to.includes(i));
    const hasOut = transitions.some((t) => t.from.includes(i));
    if (!hasOut) warn(`Étape ${s.num} : aucune transition en sortie (étape puits).`, s.id);
    if (!hasIn && !s.initial) warn(`Étape ${s.num} : jamais activée (aucune transition n’y mène).`, s.id);
  });

  // Actions
  const drive = new Map(); // symbole -> { continuous: bool, assigned: bool }
  const irSteps = steps.map((s) => {
    const actions = [];
    for (const a of s.actions) {
      const def = ACTION_TYPES[a.type];
      const where = `Étape ${s.num}, action « ${def?.label || a.type} »`;
      if (!def) {
        err(`${where} : type inconnu.`, s.id);
        continue;
      }
      const out = { type: a.type };
      if (def.servo) {
        if (!axes.length) {
          err(`${where} : aucun axe dans le projet.`, s.id);
          continue;
        }
        const uid = a.axis || defaultAxis;
        if (!uid) {
          err(`${where} : choisissez l’axe.`, s.id);
          continue;
        }
        if (!axisByUid.has(uid)) {
          err(`${where} : axe introuvable (équipement supprimé ?).`, s.id);
          continue;
        }
        out.axis = uid;
      }
      if (def.needs.includes('target')) {
        const v = varBySym.get(a.target);
        if (!a.target) {
          err(`${where} : choisissez la variable cible.`, s.id);
          continue;
        }
        if (!v) {
          err(`${where} : variable « ${a.target} » inconnue.`, s.id);
          continue;
        }
        if (!isWritable(v)) {
          err(`${where} : « ${v.symbol} » ne peut pas être écrite par la logique (${v.system ? 'signal piloté par le drive de l’axe' : 'entrée'}).`, s.id);
          continue;
        }
        if (def.targetType === 'bool' && v.dataType !== 'bool') err(`${where} : « ${v.symbol} » doit être booléenne.`, s.id);
        if (def.targetType === 'int' && v.dataType !== 'int' && v.dataType !== 'float') err(`${where} : « ${v.symbol} » doit être numérique.`, s.id);
        out.target = v.symbol;
        out.targetType = v.dataType;
        const d = drive.get(v.symbol) || { continuous: false, assigned: false, latch: false };
        if (a.type === 'N') d.continuous = true;
        if (a.type === 'S' || a.type === 'R') d.latch = true;
        if (['SET', 'NSET', 'INC', 'DEC'].includes(a.type)) d.assigned = true;
        drive.set(v.symbol, d);
      }
      if (def.needs.includes('value')) {
        const ast = parseField(a.value, where, s.id);
        if (!ast) continue;
        const tt = out.targetType;
        const compatible =
          (tt === 'bool' && ast.type === 'bool') ||
          (tt === 'text' && ast.type === 'text') ||
          ((tt === 'int' || tt === 'float') && (ast.type === 'int' || ast.type === 'float'));
        if (!compatible) err(`${where} : la valeur n’est pas du type de « ${out.target} ».`, s.id);
        out.value = ast;
      }
      if (a.type === 'N' && String(a.condition ?? '').trim()) {
        out.condition = parseField(a.condition, `${where} (condition)`, s.id, 'bool');
      }
      if (a.type === 'SERVO_MOVE') {
        out.position = parseField(a.position, `${where} (position)`, s.id, 'num');
        out.speed = parseField(a.speed || String(axisByUid.get(out.axis)?.entry.axis?.defaultSpeed ?? 5), `${where} (vitesse)`, s.id, 'num');
        if (!out.position || !out.speed) continue;
      }
      if (a.type === 'MSG') {
        if (!a.text?.trim()) err(`${where} : texte vide.`, s.id);
        out.level = ['info', 'warning', 'error', 'success'].includes(a.level) ? a.level : 'info';
        out.text = String(a.text || '').slice(0, 300);
      }
      actions.push(out);
    }
    return { id: s.id, num: s.num, label: s.label || '', initial: !!s.initial, actions };
  });

  for (const [sym, d] of drive) {
    if ((d.continuous || d.latch) && d.assigned) {
      warn(`« ${sym} » est à la fois pilotée en continu (N/S/R) et affectée : l’affectation sera écrasée à chaque cycle.`);
    }
  }

  // Blocs (modes de marche)
  const b = logic.blocks;
  const blocks = { emergency: null, emergencyReset: null, start: null, stop: null, axes: [] };
  if (String(b.emergency.condition).trim()) blocks.emergency = parseField(b.emergency.condition, 'Arrêt d’urgence (condition)', 'blocks', 'bool');
  else if (steps.length) warn('Aucune condition d’arrêt d’urgence n’est définie (Modes et sécurités).');
  if (String(b.emergency.reset).trim()) blocks.emergencyReset = parseField(b.emergency.reset, 'Arrêt d’urgence (acquittement)', 'blocks', 'bool');
  if (String(b.run.start).trim()) blocks.start = parseField(b.run.start, 'Mise en marche', 'blocks', 'bool');
  if (String(b.run.stop).trim()) blocks.stop = parseField(b.run.stop, 'Arrêt', 'blocks', 'bool');
  for (const axis of axes) {
    const sv = axisBlock(logic, axis.uid);
    const def = axis.entry.axis || {};
    const name = axes.length > 1 ? ` — ${axis.label}` : '';
    const s = { uid: axis.uid, symbol: axis.symbol, label: axis.label, type: axis.eq.type, homing: ['auto', 'condition', 'none'].includes(sv.homing) ? sv.homing : 'auto' };
    s.homingOrder = Number.isInteger(Number(sv.homingOrder)) && Number(sv.homingOrder) > 0 ? Number(sv.homingOrder) : 1;
    if (s.homing === 'condition') s.homingCondition = parseField(sv.homingCondition || 'FAUX', `Prise d’origine (condition)${name}`, 'blocks', 'bool');
    if (sv.jogEnabled) {
      s.jog = {
        mode: parseField(sv.jogMode || 'FAUX', `Jog (mode manuel)${name}`, 'blocks', 'bool'),
        plus: parseField(sv.jogPlus || 'FAUX', `Jog +${name}`, 'blocks', 'bool'),
        minus: parseField(sv.jogMinus || 'FAUX', `Jog -${name}`, 'blocks', 'bool'),
        speed: parseField(String(sv.jogSpeed ?? '').trim() || String(def.defaultJogSpeed ?? 500), `Jog (vitesse)${name}`, 'blocks', 'num'),
      };
    }
    if (String(sv.torque || '').trim()) {
      if (def.torque === false) warn(`Couple maximal${name} : non géré par ce type d’axe, ignoré.`);
      else s.torque = parseField(sv.torque, `Couple maximal${name}`, 'blocks', 'num');
    }
    blocks.axes.push(s);
  }

  // Symboles utilisés (pour la génération des accès aux nœuds)
  const used = new Set();
  const addUsed = (ast) => ast && usedSymbols(ast).forEach((x) => used.add(x));
  transitions.forEach((t) => addUsed(t.cond));
  irSteps.forEach((s) =>
    s.actions.forEach((a) => {
      if (a.target) used.add(a.target);
      ['value', 'condition', 'position', 'speed'].forEach((k) => addUsed(a[k]));
    })
  );
  Object.values(blocks).forEach((x) => {
    if (x && x.k) addUsed(x);
  });
  for (const ax of blocks.axes) {
    addUsed(ax.homingCondition);
    if (ax.jog) Object.values(ax.jog).forEach(addUsed);
    addUsed(ax.torque);
  }

  // Sorties pilotées en continu (N / S / R) : recalculées à chaque cycle.
  const driven = [...drive.entries()].filter(([, d]) => d.continuous || d.latch).map(([sym, d]) => ({ sym, latch: d.latch }));

  const ir = {
    steps: irSteps,
    transitions,
    edges: [...edges.values()],
    blocks,
    driven,
    used: [...used],
    axes: blocks.axes,
    servo: axes.length > 0,
  };
  return { ir, issues };
}

// Validation seule (utilisée par l'étape Génération).
export function validateLogic(project) {
  return compileLogic(project).issues;
}

export { parse };

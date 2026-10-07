// Cahier des charges compact (« spec ») <-> projet PLC Studio (projets/<Nom>.plc.json).
//
// La spec décrit la machine avec des noms lisibles (voies « plc.X1 », étapes par numéro,
// actions en texte court) ; buildFromSpec() produit un projet complet, identique à ce que
// l'on obtiendrait en le saisissant dans PLC Studio : identifiants, câblage, grafcet placé
// sur la grille de l'éditeur, écrans automatiques ou explicites. specFromProject() fait
// l'inverse, pour modifier un projet existant (y compris retouché dans l'éditeur).
//
// Format détaillé : .claude/skills/plc-projet/references/spec.md

import { CATALOG } from './catalog.js';
import { newProject, normalizeProject, normalizeEquipment, normalizeVariable, systemVariablesFor, listChannels, toHostname, toSymbol, DEFAULT_AXIS_BLOCK } from './model.js';
import { ACTION_TYPES, DEFAULT_BLOCKS, actionText } from './grafcet.js';
import { axisList } from './axes.js';
import { buildLayout } from './layout.js';
import { effectivePages } from './generate.js';
import { validateProject } from './validate.js';

export const SPEC_FORMAT = 'plc-studio-spec';

// Alias lisibles des nœuds d'un axe pour les écrans (<Axe>.<alias> ou <axe>:<champ> ;
// « Servo. » / « servo: » désignent l'axe unique). Un type d'axe peut les redéfinir (axis.refAliases).
const SERVO_REF_ALIASES = {
  pret: 'servoReady',
  active: 'servoActivated',
  initialise: 'driveInitialised',
  origineFaite: 'homeDone',
  enPosition: 'targetPositionReached',
  alarme: 'servoAlarm',
  erreurModbus: 'modbusError',
  position: 'position',
  codeAlarme: 'alarms',
  etat: 'status',
};

// ---------------------------------------------------------------------------
// Actions : forme objet (format du projet) ou texte court.
//   "N Verin"  "N Verin si Piece"  "S Mem"  "R Mem"  "INC Pieces"  "DEC Stock"
//   "SET Compteur := 0"  "NSET Consigne := Cible * 2"
//   "SERVO_MOVE Cible @ Vitesse"  "SERVO_HOME"  "SERVO_STOP"            (projet à un seul axe)
//   "SERVO_MOVE[axe_x] Cible @ Vitesse"  "SERVO_HOME[axe_x]"  "SERVO_STOP[axe_x]"
//                                        (axe : id de l'équipement dans la spec, ou son nom)
//   "MSG success: Pièce terminée"  "MSG Attention bourrage"
export function parseAction(a) {
  if (a && typeof a === 'object') return { ...a, type: String(a.type || '').toUpperCase() };
  const src = String(a || '').trim();
  const m = /^([A-Za-z_]+)(?:\[\s*([^\]]*?)\s*\])?\s*(.*)$/s.exec(src);
  if (!m) throw new Error(`action vide`);
  const type = m[1].toUpperCase();
  const rest = m[3].trim();
  if (!ACTION_TYPES[type]) throw new Error(`type d'action inconnu « ${m[1]} » (${Object.keys(ACTION_TYPES).join(', ')})`);
  if (m[2] !== undefined && !ACTION_TYPES[type].servo) throw new Error(`action ${type} : « [axe] » réservé aux actions SERVO_*`);
  const withAxis = (o) => (m[2] ? { ...o, axis: m[2] } : o);
  switch (type) {
    case 'N': {
      const c = /^(\S+)(?:\s+(?:si|if)\s+(.+))?$/is.exec(rest);
      if (!c) throw new Error(`action N : « N <sortie> [si <condition>] » attendu`);
      return c[2] ? { type, target: c[1], condition: c[2].trim() } : { type, target: c[1] };
    }
    case 'S':
    case 'R':
    case 'INC':
    case 'DEC':
      if (!/^\S+$/.test(rest)) throw new Error(`action ${type} : « ${type} <variable> » attendu`);
      return { type, target: rest };
    case 'SET':
    case 'NSET': {
      const c = /^(\S+)\s*:?=\s*(.+)$/s.exec(rest);
      if (!c) throw new Error(`action ${type} : « ${type} <variable> := <valeur> » attendu`);
      return { type, target: c[1], value: c[2].trim() };
    }
    case 'SERVO_MOVE': {
      const c = /^(.+?)\s*@\s*(.+)$/s.exec(rest);
      if (!c) throw new Error('action SERVO_MOVE : « SERVO_MOVE[axe] <position> @ <vitesse> » attendu');
      return withAxis({ type, position: c[1].trim(), speed: c[2].trim() });
    }
    case 'SERVO_HOME':
    case 'SERVO_STOP':
      return withAxis({ type });
    case 'MSG': {
      const c = /^(info|warning|error|success)\s*:\s*(.+)$/is.exec(rest);
      return c ? { type, level: c[1].toLowerCase(), text: c[2].trim() } : { type, level: 'info', text: rest };
    }
    default:
      return { type };
  }
}

// axisRef(uid) : nom de l'axe à écrire entre crochets (null : projet à un seul axe).
export function formatAction(a, axisRef) {
  const ax = a.axis && axisRef ? axisRef(a.axis) : a.axis && !axisRef ? a.axis : null;
  const t = ax ? `${a.type}[${ax}]` : a.type;
  switch (a.type) {
    case 'N':
      return a.condition ? `N ${a.target} si ${a.condition}` : `N ${a.target}`;
    case 'S':
    case 'R':
    case 'INC':
    case 'DEC':
      return `${a.type} ${a.target}`;
    case 'SET':
    case 'NSET':
      return `${a.type} ${a.target} := ${a.value}`;
    case 'SERVO_MOVE':
      return `${t} ${a.position} @ ${a.speed}`;
    case 'SERVO_HOME':
    case 'SERVO_STOP':
      return t;
    case 'MSG':
      return `MSG ${a.level || 'info'}: ${a.text || ''}`;
    default:
      return a.type;
  }
}

// ---------------------------------------------------------------------------
// Mise en page automatique du grafcet (coordonnées de l'éditeur : centre des étapes).
const STEP_W = 56;
const ROW = 90; // étape -> transition -> étape : 180 px, comme « Étape suivante » dans l'éditeur
const TOP = 60;
const LEFT = 240;
const MIN_COL = 260;

export function layoutGrafcet(logic) {
  const steps = logic.steps;
  const trans = logic.transitions;
  // Grafcets indépendants (plusieurs étapes initiales sans lien) : placés l'un sous l'autre,
  // le grafcet principal (plus petite étape initiale) en premier.
  const comp = new Map();
  const stepIds = new Set(steps.map((s) => s.id));
  const adj = new Map(steps.map((s) => [s.id, new Set()]));
  for (const t of trans) {
    const ids = [...t.from, ...t.to].filter((id) => stepIds.has(id));
    for (const a of ids) for (const b of ids) if (a !== b) adj.get(a).add(b);
  }
  let n = 0;
  for (const s of steps) {
    if (comp.has(s.id)) continue;
    const stack = [s.id];
    comp.set(s.id, n);
    while (stack.length) for (const x of adj.get(stack.pop())) if (!comp.has(x)) (comp.set(x, n), stack.push(x));
    n++;
  }
  const groups = Array.from({ length: n }, (_, i) => ({
    steps: steps.filter((s) => comp.get(s.id) === i),
    trans: trans.filter((t) => [...t.from, ...t.to].some((id) => comp.get(id) === i)),
  }));
  const rank = (g) => {
    const init = g.steps.filter((s) => s.initial).map((s) => s.num);
    return [init.length ? 0 : 1, Math.min(...(init.length ? init : g.steps.map((s) => s.num)))];
  };
  groups.sort((a, b) => rank(a)[0] - rank(b)[0] || rank(a)[1] - rank(b)[1]);
  const orphans = trans.filter((t) => ![...t.from, ...t.to].some((id) => stepIds.has(id)));
  let top = TOP;
  for (const g of groups) {
    layoutOne(g.steps, g.trans, top);
    top = Math.max(...g.steps.map((s) => s.y), ...g.trans.map((t) => t.y)) + 2 * ROW;
  }
  orphans.forEach((t, i) => Object.assign(t, { x: LEFT, y: top + i * ROW }));
  return logic;
}

function layoutOne(steps, trans, top) {
  const stepById = new Map(steps.map((s) => [s.id, s]));
  const transByKey = new Map(trans.map((t) => [`t:${t.id}`, t]));
  const outOf = new Map(steps.map((s) => [s.id, []]));
  const inTo = new Map(steps.map((s) => [s.id, []]));
  for (const t of trans) {
    for (const id of t.from) outOf.get(id)?.push(t);
    for (const id of t.to) inTo.get(id)?.push(t);
  }

  // 1. Arcs de retour (reprises) : arcs vers un nœud en cours d'exploration (DFS).
  const back = new Set(); // "s:<id>>t:<id>" ou "t:<id>>s:<id>"
  const state = new Map();
  const visit = (key) => {
    state.set(key, 1);
    const next = key.startsWith('s:') ? (outOf.get(key.slice(2)) || []).map((t) => `t:${t.id}`) : transByKey.get(key).to.filter((id) => stepById.has(id)).map((id) => `s:${id}`);
    for (const nk of next) {
      if (state.get(nk) === 1) back.add(`${key}>${nk}`);
      else if (!state.has(nk)) visit(nk);
    }
    state.set(key, 2);
  };
  const byNum = [...steps].sort((a, b) => (b.initial ? 1 : 0) - (a.initial ? 1 : 0) || a.num - b.num);
  for (const s of byNum) if (!state.has(`s:${s.id}`)) visit(`s:${s.id}`);

  // 2. Rangées (plus long chemin sans les reprises) : étapes aux rangs pairs, transitions aux impairs.
  const layer = new Map();
  const order = [];
  const indeg = new Map();
  const succ = new Map();
  const addEdge = (a, b) => {
    if (back.has(`${a}>${b}`)) return;
    succ.get(a).push(b);
    indeg.set(b, (indeg.get(b) || 0) + 1);
  };
  for (const s of steps) succ.set(`s:${s.id}`, []);
  for (const t of trans) succ.set(`t:${t.id}`, []);
  for (const t of trans) {
    for (const id of t.from) if (stepById.has(id)) addEdge(`s:${id}`, `t:${t.id}`);
    for (const id of t.to) if (stepById.has(id)) addEdge(`t:${t.id}`, `s:${id}`);
  }
  const queue = [...succ.keys()].filter((k) => !indeg.get(k));
  queue.sort((a, b) => (a.startsWith('s:') && stepById.get(a.slice(2))?.initial ? -1 : 0) - (b.startsWith('s:') && stepById.get(b.slice(2))?.initial ? -1 : 0));
  for (const k of queue) layer.set(k, k.startsWith('s:') ? 0 : 1);
  while (queue.length) {
    const k = queue.shift();
    order.push(k);
    for (const nk of succ.get(k)) {
      layer.set(nk, Math.max(layer.get(nk) || 0, layer.get(k) + 1));
      indeg.set(nk, indeg.get(nk) - 1);
      if (!indeg.get(nk)) queue.push(nk);
    }
  }

  // 3. Colonnes : une branche OU ou ET ouvre une nouvelle colonne à droite.
  const isLoop = (t) => (t.to.length && t.to.every((id) => back.has(`t:${t.id}>s:${id}`)) ? 1 : 0);
  const col = new Map();
  let maxCol = -1;
  const newCol = () => ++maxCol;
  const setCol = (k, c) => {
    if (!col.has(k)) {
      col.set(k, c);
      maxCol = Math.max(maxCol, c);
    }
  };
  for (const k of order) {
    if (!col.has(k)) {
      // Prédécesseurs déjà placés (arcs avant) : on reprend la colonne la plus à gauche.
      const preds = k.startsWith('s:') ? (inTo.get(k.slice(2)) || []).map((t) => `t:${t.id}`) : transByKey.get(k).from.map((id) => `s:${id}`);
      const placed = preds.filter((p) => col.has(p) && !back.has(`${p}>${k}`)).map((p) => col.get(p));
      setCol(k, placed.length ? Math.min(...placed) : newCol());
    }
    const c = col.get(k);
    if (k.startsWith('s:')) {
      // Ordre des colonnes : la suite principale (première transition qui descend), puis les
      // reprises (lignes de retour courtes, sans croiser les autres branches), puis les autres branches.
      const all = outOf.get(k.slice(2)) || [];
      const main = all.find((t) => !isLoop(t));
      const outs = [main, ...all.filter((t) => isLoop(t)), ...all.filter((t) => t !== main && !isLoop(t))].filter(Boolean);
      outs.forEach((t, i) => {
        if (!back.has(`${k}>t:${t.id}`)) setCol(`t:${t.id}`, i === 0 ? c : newCol());
      });
    } else {
      transByKey.get(k).to.forEach((id, i) => {
        if (stepById.has(id) && !back.has(`${k}>s:${id}`)) setCol(`s:${id}`, i === 0 ? c : newCol());
      });
    }
  }

  // 4. Abscisses : chaque colonne se place juste à droite de ce qui occupe les mêmes rangées
  //    (actions, réceptivités et libellés compris), au moins une colonne à droite de la principale.
  const nodes = [
    ...steps.map((s) => ({
      c: col.get(`s:${s.id}`) ?? 0,
      layer: layer.get(`s:${s.id}`) ?? 0,
      right: STEP_W / 2 + 20 + (s.actions?.length ? Math.max(...s.actions.map((a) => actionText(a).length)) * 7 + 24 : 0),
      left: s.label ? s.label.length * 6.5 + 8 : STEP_W / 2,
    })),
    ...trans.map((t) => ({ c: col.get(`t:${t.id}`) ?? 0, layer: layer.get(`t:${t.id}`) ?? 1, right: 24 + String(t.condition || '').length * 7.2, left: 50 })),
  ];
  const xs = [LEFT];
  for (let c = 1; c <= maxCol; c++) {
    let x = LEFT + MIN_COL;
    for (const nd of nodes.filter((z) => z.c === c))
      for (const m of nodes)
        if (m.c < c && Math.abs(m.layer - nd.layer) <= 1) x = Math.max(x, xs[m.c] + MIN_COL, xs[m.c] + m.right + 50 + nd.left);
    xs[c] = Math.ceil(x / 20) * 20;
  }

  for (const s of steps) {
    const k = `s:${s.id}`;
    s.x = xs[col.get(k) ?? 0];
    s.y = top + (layer.get(k) ?? 0) * ROW;
  }
  // Reprises d'une même rangée décalées verticalement : leurs lignes de retour ne se superposent pas.
  const loopsInRow = new Map();
  for (const t of [...trans].sort((a, b) => (col.get(`t:${a.id}`) ?? 0) - (col.get(`t:${b.id}`) ?? 0))) {
    const k = `t:${t.id}`;
    const L = layer.get(k) ?? 1;
    let dy = 0;
    if (isLoop(t)) {
      const i = loopsInRow.get(L) || 0;
      loopsInRow.set(L, i + 1);
      dy = 20 * i;
    }
    t.x = xs[col.get(k) ?? 0];
    t.y = top + L * ROW + dy;
  }
}

// ---------------------------------------------------------------------------
const asList = (x) => (x === undefined || x === null ? [] : Array.isArray(x) ? x : [x]);

// Spec -> projet. Retourne { project, errors, warnings, issues } :
//   errors/warnings : problèmes de la spec elle-même (voie inconnue, étape absente…) ;
//   issues : vérification complète de PLC Studio (validateProject) sur le projet produit.
// options.previous : version actuelle du projet (reconstruction d'un projet existant) — ses
//   identifiants internes et l'ordre de ses champs sont conservés, pour un diff git lisible.
export function buildFromSpec(spec, { previous = null } = {}) {
  const prev = previousIds(previous);
  const errors = [];
  const warnings = [];
  const fail = (msg) => errors.push(msg);
  if (!spec || typeof spec !== 'object') return { project: null, errors: ['spec vide ou illisible'], warnings, issues: [] };

  const name = String(spec.name || '').trim();
  if (!name) fail('« name » (nom du projet) est obligatoire.');
  const project = newProject(name || 'Projet');
  project.description = spec.description || '';
  project.hostname = spec.hostname || toHostname(name);
  if (spec.execution) project.execution = { mode: spec.execution };
  if (spec.options) project.options = { ...project.options, ...spec.options };
  if (spec.wifi) project.wifi = spec.wifi;
  else project.wifi[0].ap_name = project.hostname;
  if (spec.ethernet) project.ethernet = { ...project.ethernet, ...spec.ethernet };
  if (spec.otaUrl) project.otaUrl = spec.otaUrl;
  if (spec.editorPassword) project.editorPassword = spec.editorPassword;

  // Équipements (l'automate est toujours placé en premier).
  const eqById = new Map();
  const eqSpecs = asList(spec.equipment).slice();
  eqSpecs.sort((a, b) => (CATALOG[a.type]?.role === 'controller' ? -1 : 0) - (CATALOG[b.type]?.role === 'controller' ? -1 : 0));
  for (const e of eqSpecs) {
    if (!e.id) fail(`équipement ${e.type || '?'} : « id » obligatoire (ex. "plc").`);
    if (!CATALOG[e.type]) {
      fail(`équipement « ${e.id} » : type inconnu « ${e.type} » (types : ${Object.keys(CATALOG).join(', ')}).`);
      continue;
    }
    if (eqById.has(e.id)) fail(`deux équipements ont l'id « ${e.id} ».`);
    const entry = CATALOG[e.type];
    const eq = normalizeEquipment({ uid: prev.equipment(e) || `eq_${toSymbol(e.id)}`, type: e.type, label: e.label || entry.defaultLabel || entry.label, options: e.options || {} });
    if (entry.modbus) eq.address = e.address ?? entry.defaultAddress;
    project.equipment.push(eq);
    project.variables.push(...systemVariablesFor(eq));
    eqById.set(e.id, eq);
  }

  // Résolution d'une voie « <id équipement>.<nom de voie> » (ex. plc.X1, io.DI3, relais.DO12).
  const channels = listChannels(project);
  const resolveChannel = (at, what) => {
    const m = /^([^.\s]+)\.(\S+)$/.exec(String(at || '').trim());
    if (!m) {
      fail(`${what} : voie « ${at} » invalide (format « <équipement>.<voie> », ex. plc.X1).`);
      return null;
    }
    const eq = eqById.get(m[1]);
    if (!eq) {
      fail(`${what} : équipement « ${m[1]} » inconnu.`);
      return null;
    }
    const want = m[2].toUpperCase().replace(/^GPIO2_1$/, 'GPIO2');  // ancien nom de la sortie GPIO2 de la KC868-A8S
    const ch = channels.find((c) => c.eq === eq && c.name.toUpperCase() === want);
    if (!ch) {
      const names = channels.filter((c) => c.eq === eq).map((c) => c.name);
      fail(`${what} : voie « ${m[2]} » absente de ${eq.label} (voies : ${names.join(', ') || 'aucune'}).`);
      return null;
    }
    return ch;
  };

  // Axes : désignés par l'id de leur équipement dans la spec, leur symbole ou leur nom.
  const axes = axisList(project);
  const specIdOf = new Map([...eqById.entries()].map(([id, eq]) => [eq.uid, id]));
  const resolveAxis = (ref, what) => {
    if (ref === undefined || ref === null || ref === '') {
      if (axes.length === 1) return axes[0].uid;
      fail(axes.length ? `${what} : précisez l'axe (${axes.map((a) => specIdOf.get(a.uid)).join(', ')}).` : `${what} : aucun axe dans le projet.`);
      return null;
    }
    const r = String(ref).trim();
    const lower = r.toLowerCase();
    const hit =
      axes.find((a) => specIdOf.get(a.uid) === r) ||
      axes.find((a) => a.symbol.toLowerCase() === lower || a.label.toLowerCase() === lower) ||
      (/^servo$/i.test(r) && axes.length === 1 ? axes[0] : null);
    if (!hit) fail(`${what} : axe « ${r} » inconnu (${axes.map((a) => specIdOf.get(a.uid)).join(', ') || 'aucun axe'}).`);
    return hit?.uid || null;
  };

  // Signaux TOR des axes (servo ON, step, dir…) sur des sorties : « signals » de l'équipement,
  // ou « servoSignals » (ancien format, axe unique).
  const bindSignal = (eqUid, sig, at, what) => {
    const v = project.variables.find((x) => x.system?.signal === sig && (!eqUid || x.system.eq === eqUid));
    if (!v) {
      const known = [...new Set(project.variables.filter((x) => x.system && (!eqUid || x.system.eq === eqUid)).map((x) => x.system.signal))];
      fail(`${what} : signal « ${sig} » inconnu (${known.join(', ') || 'aucun signal'}).`);
      return;
    }
    const ch = resolveChannel(at, `signal ${sig}`);
    if (ch) v.binding = { eq: ch.eq.uid, group: ch.group.key, channel: ch.channel };
  };
  for (const e of eqSpecs) {
    const eq = eqById.get(e.id);
    if (eq) for (const [sig, at] of Object.entries(e.signals || {})) bindSignal(eq.uid, sig, at, `équipement « ${e.id} »`);
  }
  for (const [sig, at] of Object.entries(spec.servoSignals || {})) bindSignal(axes.length === 1 ? axes[0].uid : null, sig, at, 'servoSignals');

  const symbols = new Set(project.variables.map((v) => v.symbol));
  const addVar = (raw, extra, what) => {
    const symbol = raw.symbol || toSymbol(raw.label);
    if (symbols.has(symbol)) fail(`${what} : symbole « ${symbol} » déjà utilisé.`);
    symbols.add(symbol);
    const v = normalizeVariable({
      uid: prev.variable(symbol) || `var_${symbol}`,
      symbol,
      label: raw.label || symbol,
      comment: raw.comment || '',
      ...extra,
    });
    for (const k of ['inverse', 'fallback', 'initial', 'min', 'max', 'step', 'unit', 'choices']) if (raw[k] !== undefined) v[k] = raw[k];
    if (raw.initial !== undefined && v.kind !== 'parameter') warnings.push(`${what} : « initial » n'est utilisé que pour les paramètres (les autres variables démarrent à 0 / faux / "").`);
    if (v.kind === 'parameter' && raw.initial === undefined) v.initial = v.dataType === 'bool' ? false : v.dataType === 'text' ? '' : 0;
    v.hmi = { show: raw.show ?? v.kind !== 'memory' };
    if (raw.widget) v.hmi.widget = raw.widget;
    project.variables.push(v);
    return v;
  };

  // Entrées / sorties câblées.
  for (const io of asList(spec.io)) {
    const what = `E/S « ${io.symbol || io.label || '?'} »`;
    if (!io.at) {
      // Voie pas encore choisie : variable créée, à câbler dans PLC Studio.
      if (!['input', 'output'].includes(io.kind)) {
        fail(`${what} : « at » (voie) ou « kind » (input/output) obligatoire.`);
        continue;
      }
      warnings.push(`${what} : pas de voie (« at »), à câbler dans PLC Studio.`);
      addVar(io, { kind: io.kind, dataType: io.dataType || 'bool' }, what);
      continue;
    }
    const ch = resolveChannel(io.at, what);
    if (!ch) continue;
    const kind = ch.group.dir === 'in' ? 'input' : 'output';
    if (io.kind && io.kind !== kind) warnings.push(`${what} : « kind » ignoré, la voie ${io.at} est une ${kind === 'input' ? 'entrée' : 'sortie'}.`);
    addVar(io, { kind, dataType: ch.group.dataType, binding: { eq: ch.eq.uid, group: ch.group.key, channel: ch.channel } }, what);
  }

  // Variables internes.
  for (const v of asList(spec.variables)) {
    const what = `variable « ${v.symbol || v.label || '?'} »`;
    if (['input', 'output'].includes(v.kind)) {
      fail(`${what} : les entrées/sorties câblées se déclarent dans « io » avec « at ».`);
      continue;
    }
    addVar(v, { kind: v.kind || 'memory', dataType: v.dataType || 'bool' }, what);
  }

  // Grafcet.
  const g = spec.grafcet || {};
  const stepByNum = new Map();
  const logicSteps = [];
  for (const s of asList(g.steps)) {
    const num = Number(s.num);
    if (!Number.isInteger(num) || num < 0) {
      fail(`étape « ${s.label || '?'} » : « num » entier attendu.`);
      continue;
    }
    if (stepByNum.has(num)) fail(`deux étapes portent le numéro ${num}.`);
    const actions = [];
    for (const a of asList(s.actions)) {
      try {
        const act = parseAction(a);
        if (ACTION_TYPES[act.type]?.servo) {
          const { axis: ref, ...rest } = act;
          const uid = resolveAxis(ref, `étape ${num}, action ${act.type}`);
          actions.push(uid ? { ...rest, axis: uid } : rest);
        } else actions.push(act);
      } catch (e) {
        fail(`étape ${num} : ${e.message}.`);
      }
    }
    const st = { id: prev.step(num) || `s${num}`, num, label: s.label || '', ...(s.initial ? { initial: true } : {}), actions };
    if (Number.isFinite(s.x) && Number.isFinite(s.y)) Object.assign(st, { x: s.x, y: s.y });
    stepByNum.set(num, st);
    logicSteps.push(st);
  }
  if (logicSteps.length && !logicSteps.some((s) => s.initial)) {
    const first = logicSteps.reduce((a, b) => (a.num <= b.num ? a : b));
    first.initial = true;
    warnings.push(`aucune étape initiale : l'étape ${first.num} est choisie.`);
  }
  const usedT = new Set(asList(g.transitions).map((t) => t.num).filter((n) => Number.isInteger(n)));
  let nextT = 0;
  const logicTrans = [];
  for (const t of asList(g.transitions)) {
    let num = t.num;
    if (!Number.isInteger(num)) {
      while (usedT.has(nextT)) nextT++;
      num = nextT;
      usedT.add(num);
    }
    const ref = (n, side) => {
      const st = stepByNum.get(Number(n));
      if (!st) fail(`transition T${num} : étape ${side} ${n} inexistante.`);
      return st?.id;
    };
    const from = asList(t.from).map((n) => ref(n, 'amont')).filter(Boolean);
    const to = asList(t.to).map((n) => ref(n, 'aval')).filter(Boolean);
    if (!from.length || !to.length) fail(`transition T${num} : « from » et « to » obligatoires.`);
    const tr = { id: prev.transition(num) || `t${num}`, num, from, to, condition: String(t.condition ?? 'VRAI').trim() || 'VRAI' };
    if (Number.isFinite(t.x) && Number.isFinite(t.y)) Object.assign(tr, { x: t.x, y: t.y });
    logicTrans.push(tr);
  }
  const m = spec.modes || {};
  // Modes par axe : « axes » { <axe>: bloc } ; « servo » (ancien format) = l'axe unique.
  const axisBlocks = {};
  const addAxisBlock = (ref, b, what) => {
    const uid = resolveAxis(ref, what);
    if (!uid) return;
    const block = { ...DEFAULT_AXIS_BLOCK, ...(b || {}) };
    for (const k of ['jogSpeed', 'torque', 'homingCondition', 'jogMode', 'jogPlus', 'jogMinus']) if (typeof block[k] === 'number') block[k] = String(block[k]);
    axisBlocks[uid] = block;
  };
  if (m.servo && axes.length) addAxisBlock(null, m.servo, 'modes.servo');
  for (const [ref, b] of Object.entries(m.axes || {})) addAxisBlock(ref, b, `modes.axes « ${ref} »`);
  project.logic = {
    steps: logicSteps,
    transitions: logicTrans,
    blocks: {
      emergency: { ...DEFAULT_BLOCKS.emergency, ...(m.emergency || {}) },
      run: { ...DEFAULT_BLOCKS.run, ...(m.run || {}) },
      axes: Object.fromEntries(axes.filter((a) => axisBlocks[a.uid]).map((a) => [a.uid, axisBlocks[a.uid]])),
    },
  };
  const positioned = [...logicSteps, ...logicTrans].every((x) => Number.isFinite(x.x) && Number.isFinite(x.y));
  if (!positioned) layoutGrafcet(project.logic);

  // Écrans : "auto" (défaut) ou pages explicites.
  const layout0 = buildLayout(project);
  if (spec.hmi && spec.hmi !== 'auto') {
    const bySymbol = new Map(project.variables.map((v) => [v.symbol, v]));
    const refOf = (r, where) => {
      const s = String(r || '').trim();
      if (s === '@etat') return 'sys:state';
      if (s === '@horloge') return 'sys:clock';
      const sv = /^([A-Za-z_][\w-]*)(\.|:)(\w+)$/.exec(s);
      if (sv && !bySymbol.has(s)) {
        const uid = resolveAxis(/^servo$/i.test(sv[1]) && axes.length === 1 ? null : sv[1], where);
        if (!uid) return null;
        const aliases = { ...SERVO_REF_ALIASES, ...(axes.find((a) => a.uid === uid).entry.axis?.refAliases || {}) };
        const field = sv[2] === '.' ? aliases[sv[3]] || sv[3] : sv[3];
        if (layout0.nodes.has(`axis:${uid}:${field}`)) return `axis:${uid}:${field}`;
        fail(`${where} : nœud d'axe « ${s} » inconnu (${Object.keys(aliases).map((k) => `${sv[1]}.${k}`).join(', ')}).`);
        return null;
      }
      const v = bySymbol.get(s);
      if (!v) {
        fail(`${where} : symbole « ${s} » inconnu.`);
        return null;
      }
      return `var:${v.uid}`;
    };
    project.hmi = {
      auto: false,
      pages: asList(spec.hmi.pages).map((pg, pi) => ({
        id: prev.page(pg.name) || `page_${pi + 1}`,
        name: pg.name || `Page ${pi + 1}`,
        ...(pg.password ? { password: String(pg.password) } : {}),
        sections: asList(pg.sections).map((sec, si) => ({
          id: prev.section(pg.name, sec.name) || `sec_${pi + 1}_${si + 1}`,
          name: sec.name || `Section ${si + 1}`,
          orientation: sec.orientation || 'horizontal',
          ...(sec.password ? { password: String(sec.password) } : {}),
          items: asList(sec.items)
            .map((it) => {
              const o = typeof it === 'string' ? { ref: it } : it;
              const where = `écran « ${pg.name} / ${sec.name} »`;
              const ref = refOf(o.ref ?? o.symbol, where);
              if (!ref) return null;
              const item = { ref, label: o.label || '', widget: o.widget || '' };
              if (o.props) item.props = { ...o.props };
              return item;
            })
            .filter(Boolean),
        })),
      })),
    };
  }

  const normalized = normalizeProject(project);
  // Libellés et widgets par défaut des éléments d'écran explicites (comme l'éditeur).
  if (normalized.hmi.auto === false) {
    const auto = effectivePages({ ...normalized, hmi: { auto: true, pages: [] } }, layout0);
    const autoItems = new Map(auto.flatMap((p) => p.sections.flatMap((s) => s.items)).map((i) => [i.ref, i]));
    for (const pg of normalized.hmi.pages)
      for (const s of pg.sections)
        for (const it of s.items) {
          const a = autoItems.get(it.ref);
          const node = layout0.nodes.get(it.ref);
          if (!it.label) it.label = a?.label || node?.label || node?.name || it.ref;
          if (!it.widget) it.widget = a?.widget || defaultWidget(normalized, layout0, it.ref);
        }
  }
  const layout = buildLayout(normalized);
  const pages = effectivePages(normalized, layout);
  const issues = validateProject({ ...normalized, hmi: { ...normalized.hmi, pages } }, layout);
  return { project: previous ? orderLike(normalized, previous) : normalized, errors, warnings, issues };
}

// Identifiants internes d'une version précédente du projet, retrouvés par nom.
function previousIds(previous) {
  const none = () => null;
  if (!previous) return { equipment: none, variable: none, step: none, transition: none, page: none, section: none };
  const eqs = previous.equipment || [];
  const vars = new Map((previous.variables || []).filter((v) => !v.system).map((v) => [v.symbol, v.uid]));
  const steps = new Map((previous.logic?.steps || []).map((s) => [s.num, s.id]));
  const trans = new Map((previous.logic?.transitions || []).map((t) => [t.num, t.id]));
  const pages = previous.hmi?.auto === false ? previous.hmi.pages || [] : [];
  const taken = new Set();
  return {
    equipment(e) {
      const hit = eqs.find((x) => !taken.has(x.uid) && x.type === e.type && x.label === (e.label || x.label)) || eqs.find((x) => !taken.has(x.uid) && x.type === e.type);
      if (hit) taken.add(hit.uid);
      return hit?.uid || null;
    },
    variable: (symbol) => vars.get(symbol) || null,
    step: (num) => steps.get(num) || null,
    transition: (num) => trans.get(num) || null,
    page: (name) => pages.find((p) => p.name === name)?.id || null,
    section: (pageName, name) => pages.find((p) => p.name === pageName)?.sections?.find((s) => s.name === name)?.id || null,
  };
}

// Réordonne les champs comme dans la version précédente (objets et éléments de même identité).
function orderLike(obj, ref) {
  if (Array.isArray(obj)) {
    if (!Array.isArray(ref)) return obj;
    const key = (x) => (x && typeof x === 'object' ? x.uid ?? x.id ?? null : null);
    const byKey = new Map(ref.filter((x) => key(x) !== null).map((x) => [key(x), x]));
    return obj.map((x, i) => orderLike(x, key(x) !== null ? byKey.get(key(x)) : ref[i]));
  }
  if (!obj || typeof obj !== 'object' || !ref || typeof ref !== 'object' || Array.isArray(ref)) return obj;
  const out = {};
  for (const k of Object.keys(ref)) if (k in obj) out[k] = orderLike(obj[k], ref[k]);
  for (const k of Object.keys(obj)) if (!(k in out)) out[k] = obj[k];
  return out;
}

function defaultWidget(project, layout, ref) {
  if (ref === 'sys:state' || ref === 'sys:clock') return 'rx-label';
  const node = layout.nodes.get(ref);
  if (ref.startsWith('axis:')) return node?.cppClass === 'BooleanOutputNode' ? 'rx-indicator' : 'rx-numeric';
  const v = ref.startsWith('var:') ? project.variables.find((x) => `var:${x.uid}` === ref) : null;
  if (v?.dataType === 'bool') return v.kind === 'command' ? 'tx-button-hold' : v.kind === 'parameter' ? 'tx-bool' : 'rx-indicator';
  if (v?.dataType === 'text') return v.kind === 'parameter' ? 'tx-string' : 'rx-label';
  return v && ['command', 'parameter'].includes(v.kind) ? 'tx-numeric' : 'rx-numeric';
}

// ---------------------------------------------------------------------------
// Projet -> spec (pour modifier un projet existant, retouché ou non dans l'éditeur).
export function specFromProject(input) {
  const project = normalizeProject(input);
  const idOf = new Map();
  const used = new Set();
  for (const eq of project.equipment) {
    const role = CATALOG[eq.type]?.role;
    const singleAxis = role === 'axis' && project.equipment.filter((e) => CATALOG[e.type]?.role === 'axis').length === 1;
    let base = role === 'controller' ? 'plc' : singleAxis ? 'servo' : toSymbol(eq.label).toLowerCase();
    let id = base;
    for (let i = 2; used.has(id); i++) id = `${base}${i}`;
    used.add(id);
    idOf.set(eq.uid, id);
  }
  const channels = listChannels(project);
  const chName = (b) => {
    const ch = channels.find((c) => c.eq.uid === b.eq && c.group.key === b.group && c.channel === b.channel);
    return ch ? `${idOf.get(b.eq)}.${ch.name}` : null;
  };
  const keep = (v, keys) => Object.fromEntries(keys.filter((k) => v[k] !== undefined && v[k] !== '').map((k) => [k, v[k]]));

  const spec = {
    format: SPEC_FORMAT,
    name: project.name,
    description: project.description || undefined,
    hostname: project.hostname,
    execution: project.execution?.mode || 'hmi',
    options: project.options,
    wifi: project.wifi,
    equipment: project.equipment.map((eq) => {
      const e = { id: idOf.get(eq.uid), type: eq.type, label: eq.label };
      if (CATALOG[eq.type]?.modbus) e.address = eq.address;
      if (eq.options && Object.keys(eq.options).length) e.options = eq.options;
      const signals = {};
      for (const v of project.variables.filter((x) => x.system?.eq === eq.uid)) if (v.binding) signals[v.system.signal] = chName(v.binding);
      if (Object.keys(signals).length) e.signals = signals;
      return e;
    }),
  };
  const eth = project.ethernet;
  if (eth && (eth.enabled || eth.ip || eth.gateway || eth.dns)) spec.ethernet = eth;
  if (project.otaUrl) spec.otaUrl = project.otaUrl;
  if (project.editorPassword) spec.editorPassword = project.editorPassword;
  const axes = axisList(project);
  const axisRef = axes.length > 1 ? (uid) => idOf.get(uid) || uid : () => null;

  const extra = (v) => {
    const o = keep(v, ['comment', 'inverse', 'fallback', 'initial', 'min', 'max', 'step', 'unit', 'choices']);
    if (v.hmi?.show === false && v.kind !== 'memory') o.show = false;
    if (v.hmi?.show === true && v.kind === 'memory') o.show = true;
    if (v.hmi?.widget) o.widget = v.hmi.widget;
    if (o.inverse === false) delete o.inverse;
    if (o.comment === '') delete o.comment;
    return o;
  };
  spec.io = project.variables
    .filter((v) => !v.system && ['input', 'output'].includes(v.kind))
    .map((v) => {
      const at = v.binding ? chName(v.binding) : null;
      return at ? { symbol: v.symbol, label: v.label, at, ...extra(v) } : { symbol: v.symbol, label: v.label, kind: v.kind, dataType: v.dataType, ...extra(v) };
    });
  spec.variables = project.variables.filter((v) => !v.system && !['input', 'output'].includes(v.kind)).map((v) => ({ symbol: v.symbol, label: v.label, kind: v.kind, dataType: v.dataType, ...extra(v) }));

  const logic = project.logic || { steps: [], transitions: [] };
  const numOf = new Map(logic.steps.map((s) => [s.id, s.num]));
  const one = (l) => (l.length === 1 ? l[0] : l);
  spec.grafcet = {
    steps: [...logic.steps]
      .sort((a, b) => a.num - b.num)
      .map((s) => ({ num: s.num, label: s.label || undefined, initial: s.initial || undefined, actions: (s.actions || []).map((a) => formatAction(a, axisRef)), x: s.x, y: s.y })),
    transitions: [...logic.transitions]
      .sort((a, b) => a.num - b.num)
      .map((t) => ({ num: t.num, from: one(t.from.map((id) => numOf.get(id))), to: one(t.to.map((id) => numOf.get(id))), condition: t.condition, x: t.x, y: t.y })),
  };
  const blocks = logic.blocks || {};
  spec.modes = { emergency: { ...DEFAULT_BLOCKS.emergency, ...blocks.emergency }, run: { ...DEFAULT_BLOCKS.run, ...blocks.run } };
  const axisModes = {};
  for (const a of axes) if (blocks.axes?.[a.uid]) axisModes[idOf.get(a.uid)] = { ...DEFAULT_AXIS_BLOCK, ...blocks.axes[a.uid] };
  if (Object.keys(axisModes).length) spec.modes.axes = axisModes;

  if (project.hmi?.auto === false && project.hmi.pages?.length) {
    const symOf = new Map(project.variables.map((v) => [`var:${v.uid}`, v.symbol]));
    const refName = (ref) => {
      if (ref === 'sys:state') return '@etat';
      if (ref === 'sys:clock') return '@horloge';
      const am = /^axis:([^:]+):(\w+)$/.exec(ref);
      if (am) {
        const a = axes.find((x) => x.uid === am[1]);
        const aliases = { ...SERVO_REF_ALIASES, ...(a?.entry.axis?.refAliases || {}) };
        const aliasOf = Object.fromEntries(Object.entries(aliases).map(([k, f]) => [f, k]));
        const name = axes.length === 1 ? 'Servo' : idOf.get(am[1]) || am[1];
        return aliasOf[am[2]] ? `${name}.${aliasOf[am[2]]}` : `${name}:${am[2]}`;
      }
      return symOf.get(ref) || ref;
    };
    spec.hmi = {
      pages: project.hmi.pages.map((pg) => ({
        name: pg.name,
        ...(pg.password ? { password: pg.password } : {}),
        sections: pg.sections.map((s) => ({
          name: s.name,
          orientation: s.orientation || 'horizontal',
          ...(s.password ? { password: s.password } : {}),
          items: s.items.map((it) => ({ ref: refName(it.ref), label: it.label, widget: it.widget, ...(it.props && Object.keys(it.props).length ? { props: it.props } : {}) })),
        })),
      })),
    };
  } else spec.hmi = 'auto';
  return JSON.parse(JSON.stringify(spec));
}

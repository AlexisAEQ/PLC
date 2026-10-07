// Modèle de projet PLC Studio (fichier projets/<Nom>.plc.json).

import { CATALOG } from './catalog.js';

export const FORMAT = 'plc-studio-project';
export const FORMAT_VERSION = 1;

// Bloc de modes d'un axe (voir grafcet.js) : prise d'origine (auto | condition | none, par
// ordre homingOrder croissant), jog en mode manuel, couple maximal. jogSpeed vide : vitesse
// de jog par défaut du type d'axe.
export const DEFAULT_AXIS_BLOCK = { homing: 'auto', homingCondition: '', homingOrder: 1, jogEnabled: false, jogMode: '', jogPlus: '', jogMinus: '', jogSpeed: '', torque: '' };

export function uid(prefix = 'id') {
  const rnd = Math.random().toString(36).slice(2, 8);
  return `${prefix}_${Date.now().toString(36)}${rnd}`;
}

export function newProject(name = 'MonProjet') {
  return normalizeProject({
    format: FORMAT,
    version: FORMAT_VERSION,
    name,
    hostname: toHostname(name),
    description: '',
  });
}

export function toHostname(name) {
  return String(name || 'plc')
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .replace(/[^A-Za-z0-9_-]+/g, '_')
    .replace(/^_+|_+$/g, '')
    .toLowerCase()
    .slice(0, 32) || 'plc';
}

export function toSymbol(label) {
  let s = String(label || '')
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .replace(/[^A-Za-z0-9_]+/g, '_')
    .replace(/^_+|_+$/g, '');
  if (!s) s = 'var';
  if (/^[0-9]/.test(s)) s = 'v_' + s;
  return s.slice(0, 40);
}

// Complète un projet chargé avec les valeurs par défaut (compatibilité ascendante).
export function normalizeProject(p) {
  const project = { ...p };
  project.format = FORMAT;
  project.version = FORMAT_VERSION;
  project.name = project.name || 'MonProjet';
  project.hostname = project.hostname || toHostname(project.name);
  project.description = project.description || '';
  project.execution = { mode: 'hmi', ...(project.execution || {}) };
  project.options = {
    logDiagnostic: true,
    deleteDiagnosticAtStartup: false,
    showSuccessMessages: false,
    showInfoMessages: true,
    logHistory: false,
    stateDisplay: true,
    clockDisplay: false,
    ...(project.options || {}),
  };
  project.wifi = Array.isArray(project.wifi)
    ? project.wifi
    : [
        {
          name: "Point d'accès",
          mode: 'access_point',
          ap_name: project.hostname,
          ap_password: 'ChangeMoi123',
          ap_ip: '192.168.4.1',
          ap_channel: 6,
        },
      ];
  // Ethernet filaire des cartes qui en ont (W5500 / LAN8720), en plus du WiFi.
  project.ethernet = { enabled: false, dhcp: true, ip: '', gateway: '', mask: '255.255.255.0', dns: '', ...(project.ethernet || {}) };
  project.otaUrl = project.otaUrl || '';
  project.editorPassword = project.editorPassword || '';
  project.equipment = Array.isArray(project.equipment) ? project.equipment.map(normalizeEquipment) : [];
  project.variables = Array.isArray(project.variables) ? project.variables.map(normalizeVariable) : [];
  project.hmi = project.hmi && Array.isArray(project.hmi.pages) ? project.hmi : { auto: true, pages: [] };
  if (project.hmi.auto === undefined) project.hmi.auto = true;
  project.logic = project.logic || { steps: [], transitions: [], blocks: {} };
  migrateSingleServo(project);
  if (project.logic.blocks?.axes) {
    project.logic.blocks.axes = Object.fromEntries(Object.entries(project.logic.blocks.axes).map(([k, b]) => [k, { ...DEFAULT_AXIS_BLOCK, ...(b || {}) }]));
  }
  return project;
}

// Projets d'avant les axes multiples (un seul SureServo) : bloc « servo », actions sans axe
// et références « servo:<champ> » des écrans sont rattachés à l'axe unique du projet.
function migrateSingleServo(project) {
  const axes = project.equipment.filter((e) => CATALOG[e.type]?.role === 'axis');
  if (axes.length !== 1) return;
  const axisUid = axes[0].uid;
  const logic = project.logic;
  if (logic.blocks?.servo) {
    if (!logic.blocks.axes) logic.blocks = { ...logic.blocks, axes: { [axisUid]: logic.blocks.servo } };
    const { servo, ...rest } = logic.blocks;
    logic.blocks = rest;
  }
  for (const st of logic.steps || []) {
    for (const a of st.actions || []) {
      if (/^SERVO_/.test(a.type) && !a.axis) a.axis = axisUid;
    }
  }
  for (const page of project.hmi?.pages || []) {
    for (const sec of page.sections || []) {
      for (const it of sec.items || []) {
        if (typeof it.ref === 'string' && it.ref.startsWith('servo:')) it.ref = `axis:${axisUid}:${it.ref.slice(6)}`;
      }
    }
  }
}

export function normalizeEquipment(e) {
  const entry = CATALOG[e.type];
  const eq = { uid: e.uid || uid('eq'), type: e.type, label: e.label || entry?.label || e.type, ...e };
  eq.options = { ...(eq.options || {}) };
  for (const opt of entry?.options || []) {
    if (eq.options[opt.key] === undefined) eq.options[opt.key] = opt.default;
  }
  if (entry?.modbus && (eq.address === undefined || eq.address === null)) eq.address = entry.defaultAddress;
  return eq;
}

export function normalizeVariable(v) {
  const variable = {
    uid: v.uid || uid('var'),
    symbol: v.symbol || toSymbol(v.label),
    label: v.label || v.symbol || 'Variable',
    kind: v.kind || 'memory',
    dataType: v.dataType || 'bool',
    comment: v.comment || '',
    ...v,
  };
  if (variable.kind === 'output' && !variable.fallback) variable.fallback = 'off';
  if (variable.kind === 'parameter' && variable.initial === undefined) {
    variable.initial = variable.dataType === 'bool' ? false : variable.dataType === 'text' ? '' : 0;
  }
  if (!variable.hmi) variable.hmi = { show: variable.kind !== 'memory' };
  return variable;
}

export function paramsFileName(project) {
  return `${project.name}.json`;
}

export function equipmentByUid(project, eqUid) {
  return project.equipment.find((e) => e.uid === eqUid) || null;
}

export function variableByUid(project, varUid) {
  return project.variables.find((v) => v.uid === varUid) || null;
}

// Nom affiché d'une voie : préfixe + numéro (numérotation à partir de 1 pour les bancs
// Waveshare), ou nom imposé par le catalogue (GPIO de l'automate).
export function channelName(group, ch) {
  if (group.names?.[ch]) return group.names[ch];
  return `${group.prefix}${group.bank ? ch + 1 : ch}`;
}

// Liste des voies câblables de tout le projet, avec la variable affectée le cas échéant.
export function listChannels(project) {
  const used = new Map();
  for (const v of project.variables) {
    if (v.binding?.eq && v.binding.group) used.set(`${v.binding.eq}/${v.binding.group}/${v.binding.channel}`, v);
  }
  const channels = [];
  for (const eq of project.equipment) {
    const entry = CATALOG[eq.type];
    if (!entry) continue;
    for (const g of entry.groups) {
      const ids = g.bank ? Array.from({ length: g.bank.bits }, (_, i) => i) : g.ids;
      for (const ch of ids) {
        const key = `${eq.uid}/${g.key}/${ch}`;
        channels.push({
          key,
          eq,
          group: g,
          channel: ch,
          name: channelName(g, ch),
          variable: used.get(key) || null,
        });
      }
    }
  }
  return channels;
}

// Premier axe du projet (ancien nom : un seul servo par projet).
export function servoEquipment(project) {
  return project.equipment.find((e) => CATALOG[e.type]?.role === 'axis') || null;
}

// Les signaux TOR d'un équipement (ex. servo ON, STEP/DIR, capteur d'origine) sont des
// variables "système", créées automatiquement et à câbler sur une voie d'un autre équipement :
// sortie (défaut) ou entrée (dir: 'in'). "optional" : le câblage peut rester vide.
export function systemVariablesFor(eq) {
  const entry = CATALOG[eq.type];
  return (entry?.signals || []).map((sig) =>
    normalizeVariable({
      uid: `sys_${eq.uid}_${sig.key}`,
      symbol: toSymbol(`${eq.label}_${sig.key}`),
      label: `${eq.label} : ${sig.label}`,
      kind: sig.dir === 'in' ? 'input' : 'output',
      dataType: 'bool',
      ...(sig.dir === 'in' ? {} : { fallback: 'off' }),
      system: { eq: eq.uid, signal: sig.key, field: sig.field },
      hmi: { show: false },
    })
  );
}

// Définition catalogue du signal d'une variable système (null sinon).
export function signalDef(project, v) {
  if (!v?.system) return null;
  const eq = project.equipment.find((e) => e.uid === v.system.eq);
  return (CATALOG[eq?.type]?.signals || []).find((s) => s.key === v.system.signal) || null;
}

// Ajoute un équipement et ses variables système.
export function addEquipment(project, type, label) {
  const entry = CATALOG[type];
  const sameType = project.equipment.filter((e) => e.type === type).length;
  const base = entry.defaultLabel || entry.label;
  const eq = normalizeEquipment({ type, label: label || (sameType ? `${base} ${sameType + 1}` : base) });
  if (entry.modbus && eq.options.transport !== 'tcp') {
    // Adresse libre sur le bus RS485 (les équipements Modbus TCP ont leur propre n° d'unité).
    const onBus = (e) => CATALOG[e.type]?.modbus && e.options?.transport !== 'tcp';
    const usedAddr = new Set(project.equipment.filter(onBus).map((e) => Number(e.address)));
    let addr = entry.defaultAddress;
    while (usedAddr.has(addr) && addr < 247) addr++;
    eq.address = addr;
  }
  project.equipment.push(eq);
  project.variables.push(...systemVariablesFor(eq));
  return eq;
}

export function removeEquipment(project, eqUid) {
  project.equipment = project.equipment.filter((e) => e.uid !== eqUid);
  project.variables = project.variables.filter((v) => v.system?.eq !== eqUid);
  for (const v of project.variables) {
    if (v.binding?.eq === eqUid) delete v.binding;
  }
}

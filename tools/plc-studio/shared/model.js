// Modèle de projet PLC Studio (fichier projets/<Nom>.plc.json).

import { CATALOG } from './catalog.js';

export const FORMAT = 'plc-studio-project';
export const FORMAT_VERSION = 1;

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
  project.otaUrl = project.otaUrl || '';
  project.editorPassword = project.editorPassword || '';
  project.equipment = Array.isArray(project.equipment) ? project.equipment.map(normalizeEquipment) : [];
  project.variables = Array.isArray(project.variables) ? project.variables.map(normalizeVariable) : [];
  project.hmi = project.hmi && Array.isArray(project.hmi.pages) ? project.hmi : { auto: true, pages: [] };
  if (project.hmi.auto === undefined) project.hmi.auto = true;
  project.logic = project.logic || { steps: [], transitions: [], blocks: {} };
  return project;
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

export function equipmentByUid(project, eqUid) {
  return project.equipment.find((e) => e.uid === eqUid) || null;
}

export function variableByUid(project, varUid) {
  return project.variables.find((v) => v.uid === varUid) || null;
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
          name: `${g.prefix}${g.bank ? ch + 1 : ch}`,
          variable: used.get(key) || null,
        });
      }
    }
  }
  return channels;
}

export function servoEquipment(project) {
  return project.equipment.find((e) => CATALOG[e.type]?.role === 'servo') || null;
}

// Les signaux TOR d'un équipement (ex. servo ON) sont des variables "système" de type
// sortie, créées automatiquement et à câbler sur une sortie d'un autre équipement.
export function systemVariablesFor(eq) {
  const entry = CATALOG[eq.type];
  return (entry?.signals || []).map((sig) =>
    normalizeVariable({
      uid: `sys_${eq.uid}_${sig.key}`,
      symbol: toSymbol(`${eq.label}_${sig.key}`),
      label: `${eq.label} : ${sig.label}`,
      kind: 'output',
      dataType: 'bool',
      fallback: 'off',
      system: { eq: eq.uid, signal: sig.key, field: sig.field },
      hmi: { show: false },
    })
  );
}

// Ajoute un équipement et ses variables système.
export function addEquipment(project, type, label) {
  const entry = CATALOG[type];
  const sameType = project.equipment.filter((e) => e.type === type).length;
  const base = entry.defaultLabel || entry.label;
  const eq = normalizeEquipment({ type, label: label || (sameType ? `${base} ${sameType + 1}` : base) });
  if (entry.modbus) {
    const usedAddr = new Set(project.equipment.filter((e) => CATALOG[e.type]?.modbus).map((e) => Number(e.address)));
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

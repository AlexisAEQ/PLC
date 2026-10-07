// Construit la liste des sections/nœuds config.json à partir du projet.
//
// Résultat partagé par tous les générateurs (config.json, interface.json, C++)
// et par le simulateur : chaque élément adressable est identifié par une "ref" :
//   var:<uid>          variable du projet
//   bank:<eq>:<group>  nœud "banc" Waveshare (plusieurs voies dans un seul nœud)
//   axis:<eq>:<field>  nœud d'un axe (champ de la structure de nœuds du drive : ServoNodes...)
//   sys:state          texte "état machine"
//   sys:clock          texte "horloge"

import { CATALOG } from './catalog.js';
import { nodeHash, utf8Length, truncateUtf8 } from './hash.js';

// Classe C++ "pointeur" à utiliser pour chaque type de section.
const CPP_CLASS = {
  'PF8574_rx-bool': 'BooleanInputNode',
  'rx-bool@hw': 'BooleanInputNode',
  'PF8574_tx-bool': 'BooleanOutputNode',
  'PCA9554_tx-bool': 'BooleanOutputNode',
  'EXP_rx-bool': 'BooleanInputNode',
  'EXP_tx-bool': 'BooleanOutputNode',
  'tx-bool@hw': 'BooleanOutputNode',
  ModbusReadMultipleInputsStatus: 'ModbusReadMultipleInputsRegistersNode',
  ModbusWriteMultipleCoils: 'ModbusWriteMultipleCoilslNode',
  ModbusReadInputRegister: 'Uint16InputNode',
  ModbusReadHoldingRegister: 'Uint16InputNode',
  ModbusWriteHoldingRegister: 'Uint16OutputNode',
  ModbusReadDobbleHoldingRegister: 'Uint32InputNode',
  ModbusWriteDobbleHoldingRegister: 'Uint32OutputNode',
  ModbusReadCoil: 'BooleanInputNode',
  ModbusWriteCoil: 'BooleanOutputNode',
  'rx-bool': 'VirtualBooleanInputNode',
  'tx-bool': 'BooleanOutputNode',
  'rx-uint32': 'VirtualUint32InputNode',
  'tx-uint32': 'Uint32OutputNode',
  'rx-float': 'VirtualFloatInputNode',
  'tx-float': 'FloatOutputNode',
  'rx-text': 'VirtualTextInputNode',
  'tx-text': 'VirtualTextOutputNode',
};

export function cppClassFor(sectionType, isVirtual) {
  if (!isVirtual && (sectionType === 'rx-bool' || sectionType === 'tx-bool')) return CPP_CLASS[`${sectionType}@hw`];
  return CPP_CLASS[sectionType];
}

// Sections virtuelles : (nature, type de donnée) -> section.
const VIRTUAL_SECTIONS = [
  { kind: 'command', dataType: 'bool', name: 'Commandes', type: 'rx-bool' },
  { kind: 'command', dataType: 'int', name: 'Commandes numeriques', type: 'rx-uint32' },
  { kind: 'command', dataType: 'float', name: 'Commandes reelles', type: 'rx-float' },
  { kind: 'parameter', dataType: 'int', name: 'Parametres', type: 'tx-uint32' },
  { kind: 'parameter', dataType: 'float', name: 'Parametres reels', type: 'tx-float' },
  { kind: 'parameter', dataType: 'text', name: 'Parametres texte', type: 'tx-text' },
  { kind: 'parameter', dataType: 'bool', name: 'Parametres booleens', type: 'tx-bool' },
  { kind: 'indicator', dataType: 'bool', name: 'Voyants', type: 'tx-bool' },
  { kind: 'indicator', dataType: 'int', name: 'Valeurs', type: 'tx-uint32' },
  { kind: 'indicator', dataType: 'float', name: 'Valeurs reelles', type: 'tx-float' },
  { kind: 'indicator', dataType: 'text', name: 'Messages', type: 'tx-text' },
];

export const NODE_NAME_MAX_BYTES = 78; // strlen(section) + strlen(node) < 80, avec le '/' et le '\0'

function sectionNameFor(eq, group) {
  return `${eq.label} ${group.short}`;
}

// Une mémoire interne affichée à l'écran devient un indicateur côté nœuds.
function effectiveKind(v) {
  if (v.kind === 'memory' && v.hmi?.show) return 'indicator';
  return v.kind;
}

// Variante du fichier matériel d'un automate : sans horloge et/ou avec un autre réglage RS485.
// Ex. Kincony_KC868_A8S_noRTC, Waveshare_ESP32S3_POE_8DI8DO_19200_8N1.
export function hardwareVariantFor(eq, entry) {
  if (entry.role !== 'controller') return null;
  const hasOption = (key) => (entry.options || []).some((o) => o.key === key);
  const removeRtc = hasOption('rtc') && eq.options?.rtc === false;
  const speed = hasOption('rs485Speed') && eq.options?.rs485Speed ? Number(eq.options.rs485Speed) : null;
  const config = hasOption('rs485Config') && eq.options?.rs485Config ? String(eq.options.rs485Config) : null;
  if (!removeRtc && !speed && !config) return null;
  let name = entry.hardware;
  if (removeRtc) name += '_noRTC';
  if (speed || config) name += `_${speed || 'def'}_${config ? config.replace(/^SERIAL_/, '') : 'def'}`;
  const variant = { base: entry.hardware, name, removeRtc };
  if (speed) variant.rs485Speed = speed;
  if (config) variant.rs485Config = config;
  return variant;
}

export function buildLayout(project) {
  const sections = [];
  const nodes = new Map(); // ref -> descripteur de nœud
  const errors = [];
  const warnings = [];

  const addSection = (section) => {
    sections.push(section);
    return section;
  };
  const addNode = (section, node) => {
    const name = truncateUtf8(node.name, NODE_NAME_MAX_BYTES - utf8Length(section.name));
    const full = {
      ...node,
      name,
      hash: nodeHash(node.id, section.name),
      section: section.name,
      sectionType: section.type,
      virtual: section.hardware === 'virtual',
      cppClass: node.cppClass || cppClassFor(section.type, section.hardware === 'virtual'),
    };
    section.nodes.push(full);
    if (node.ref) nodes.set(node.ref, full);
    return full;
  };

  const varsByChannel = new Map();
  for (const v of project.variables) {
    if (v.binding?.eq && v.binding.group !== undefined) {
      varsByChannel.set(`${v.binding.eq}/${v.binding.group}/${v.binding.channel}`, v);
    }
  }

  // 1. Équipements matériels : contrôleur (RS485) d'abord, puis équipements Modbus.
  const ordered = [...project.equipment].sort((a, b) => {
    const ra = CATALOG[a.type]?.rs485 ? 0 : 1;
    const rb = CATALOG[b.type]?.rs485 ? 0 : 1;
    return ra - rb;
  });

  const hardwareVariants = [];
  const mirrorVars = []; // voies de bancs Waveshare : recopiées dans un nœud booléen pour l'écran
  for (const eq of ordered) {
    const entry = CATALOG[eq.type];
    if (!entry) {
      errors.push({ step: 'equipment', ref: eq.uid, message: `Type d'équipement inconnu : ${eq.type}` });
      continue;
    }
    let hardware = entry.hardware;
    const variant = hardwareVariantFor(eq, entry);
    if (variant) {
      hardware = variant.name;
      hardwareVariants.push(variant);
    }

    for (const g of entry.groups) {
      const sectionName = sectionNameFor(eq, g);
      if (g.bank) {
        const bits = [];
        for (let bit = 0; bit < g.bank.bits; bit++) {
          const v = varsByChannel.get(`${eq.uid}/${g.key}/${bit}`);
          if (v) bits.push({ bit, v });
        }
        if (!bits.length) continue;
        const section = addSection({ name: sectionName, hardware, type: g.type, address: entry.modbus ? Number(eq.address) : undefined, nodes: [], eq: eq.uid });
        const inverse = g.dir === 'in' ? Array.from({ length: g.bank.bits }, (_, i) => !!bits.find((b) => b.bit === i)?.v.inverse) : undefined;
        const bank = addNode(section, {
          id: g.bank.nodeId,
          name: g.label,
          ref: `bank:${eq.uid}:${g.key}`,
          extra: inverse && inverse.some(Boolean) ? { inverse } : {},
          bank: { bits: g.bank.bits, dir: g.dir },
        });
        for (const { bit, v } of bits) {
          nodes.set(`var:${v.uid}`, { ...bank, ref: `var:${v.uid}`, bankRef: bank.ref, bit, variable: v.uid });
          mirrorVars.push(v);
        }
        continue;
      }
      // Les entrées de l'automate sont toujours toutes déclarées : cela garantit que la
      // première section matérielle est celle qui porte le RS485, et offre un diagnostic.
      const declareAll = entry.role === 'controller' && g.dir === 'in';
      const used = g.ids
        .map((id) => ({ id, v: varsByChannel.get(`${eq.uid}/${g.key}/${id}`) }))
        .filter((x) => x.v || declareAll);
      if (!used.length) continue;
      const section = addSection({ name: sectionName, hardware, type: g.type, address: entry.modbus ? Number(eq.address) : undefined, nodes: [], eq: eq.uid });
      for (const { id, v } of used) {
        const extra = {};
        if (g.supportsInverse && v?.inverse) extra.inverse = true;
        if (g.refreshInterval) extra.refreshInterval = g.refreshInterval;
        if (v) addNode(section, { id, name: v.symbol, ref: `var:${v.uid}`, extra, variable: v.uid });
        else addNode(section, { id, name: `${g.prefix}${id}`, ref: `chan:${eq.uid}:${g.key}:${id}`, extra, label: `${g.prefix}${id}` });
      }
    }

    for (const s of entry.sections || []) {
      const section = addSection({
        name: s.name || `${eq.label} ${s.suffix}`,
        hardware: s.virtual ? 'virtual' : hardware,
        type: s.type,
        address: s.modbus ? Number(eq.address) : undefined,
        nodes: [],
        eq: eq.uid,
      });
      for (const n of s.nodes) {
        const extra = {};
        if (n.refreshInterval) extra.refreshInterval = n.refreshInterval;
        addNode(section, { id: n.id, name: n.name, ref: `axis:${eq.uid}:${n.field}`, extra, cppClass: n.cpp, label: n.label || n.name, field: n.field, axis: eq.uid });
      }
    }
  }

  // Le firmware envoie un banc entier sous forme de texte « 0101… » : un voyant lié au banc
  // serait toujours allumé. Chaque voie a donc un nœud miroir (tx-bool) recopié par la logique.
  if (mirrorVars.length) {
    const section = addSection({ name: 'Miroirs ES', hardware: 'virtual', type: 'tx-bool', nodes: [] });
    mirrorVars.slice(0, 255).forEach((v, k) => addNode(section, { id: k + 1, name: v.symbol, ref: `mirror:${v.uid}`, extra: {}, variable: v.uid, label: v.label }));
  }

  // 2. Variables internes -> sections virtuelles.
  for (const def of VIRTUAL_SECTIONS) {
    const vars = project.variables.filter((v) => effectiveKind(v) === def.kind && v.dataType === def.dataType && !v.binding);
    if (!vars.length) continue;
    let chunk = 0;
    for (let i = 0; i < vars.length; i += 255) {
      const name = chunk ? `${def.name} ${chunk + 1}` : def.name;
      const section = addSection({ name, hardware: 'virtual', type: def.type, nodes: [] });
      vars.slice(i, i + 255).forEach((v, k) => addNode(section, { id: k + 1, name: v.symbol, ref: `var:${v.uid}`, extra: {}, variable: v.uid }));
      chunk++;
    }
  }

  // 3. Nœuds système.
  const sys = [];
  if (project.options?.stateDisplay !== false) sys.push({ id: 1, name: 'etat machine', ref: 'sys:state', label: 'État machine' });
  if (project.options?.clockDisplay) sys.push({ id: 2, name: 'horloge', ref: 'sys:clock', label: 'Horloge' });
  if (sys.length) {
    const section = addSection({ name: 'Systeme', hardware: 'virtual', type: 'tx-text', nodes: [] });
    for (const n of sys) addNode(section, { ...n, extra: {} });
  }

  // Contrôles d'intégrité (doublons de noms de section, collisions de hash).
  const seenSections = new Map();
  for (const s of sections) {
    if (seenSections.has(s.name)) {
      errors.push({ step: 'equipment', message: `Deux sections portent le même nom « ${s.name} » : renommez un des équipements.` });
    }
    seenSections.set(s.name, s);
    if (/^[0-9]/.test(s.name)) warnings.push({ step: 'equipment', message: `La section « ${s.name} » commence par un chiffre (risque de collision d'identifiants).` });
    if (utf8Length(s.name) >= 70) errors.push({ step: 'equipment', message: `Nom de section trop long : « ${s.name} ».` });
  }
  const seenHash = new Map();
  for (const s of sections) {
    for (const n of s.nodes) {
      const other = seenHash.get(n.hash);
      if (other) errors.push({ step: 'equipment', message: `Collision d'identifiant ${n.hash} entre « ${other} » et « ${s.name}/${n.name} ».` });
      seenHash.set(n.hash, `${s.name}/${n.name}`);
    }
  }
  const total = sections.reduce((acc, s) => acc + s.nodes.length, 0);
  if (total > 300) warnings.push({ step: 'variables', message: `${total} nœuds : l'envoi de l'état complet à l'écran risque de dépasser 20 000 octets.` });

  return { sections, nodes, hardwareVariants, errors, warnings, nodeCount: total };
}

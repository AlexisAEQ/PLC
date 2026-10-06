// Règles de validation du projet. Chaque règle reflète une contrainte du firmware
// (blocage au démarrage, dépassement de tampon, etc.) ou de l'outil.

import { CATALOG, VARIABLE_KINDS, widgetsFor } from './catalog.js';
import { utf8Length } from './hash.js';
import { buildLayout } from './layout.js';
import { compileLogic } from './grafcet.js';

const CPP_RESERVED = new Set(
  (
    'alignas alignof and and_eq asm auto bitand bitor bool break case catch char char16_t char32_t class compl const ' +
    'constexpr const_cast continue decltype default delete do double dynamic_cast else enum explicit export extern false ' +
    'float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private ' +
    'protected public register reinterpret_cast return short signed sizeof static static_assert static_cast struct switch ' +
    'template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t ' +
    'while xor xor_eq NULL INPUT OUTPUT HIGH LOW ERROR WARNING INFO SUCCESS FATAL DEBUG ' +
    'BusinessLogic BorneUniverselle SureServo Node State String Serial millis'
  ).split(' ')
);
// Mots réservés par le langage des réceptivités.
const EXPR_RESERVED = new Set(['ET', 'OU', 'NON', 'FM', 'FD', 'VRAI', 'FAUX', 'AND', 'OR', 'NOT', 'MIN', 'MAX', 'ABS', 'Servo']);

const IPV4 = /^(25[0-5]|2[0-4]\d|1?\d?\d)(\.(25[0-5]|2[0-4]\d|1?\d?\d)){3}$/;

export function validateProject(project, layout = buildLayout(project)) {
  const issues = [];
  const err = (step, message, ref) => issues.push({ level: 'error', step, message, ref });
  const warn = (step, message, ref) => issues.push({ level: 'warning', step, message, ref });

  // --- Projet -----------------------------------------------------------
  if (!/^[A-Za-z][A-Za-z0-9_]{0,23}$/.test(project.name || '')) {
    err('project', 'Nom de projet invalide : lettre en premier, puis lettres, chiffres ou _ (24 caractères maximum). Il sert de nom de classe C++ et de fichier.');
  } else if (CPP_RESERVED.has(project.name)) {
    err('project', `« ${project.name} » est un nom réservé.`);
  }
  if (!/^[A-Za-z0-9_-]{1,32}$/.test(project.hostname || '')) {
    err('project', "Nom réseau invalide : lettres, chiffres, - ou _, sans espace (c'est le nom mDNS de l'automate).");
  }
  if (!['hmi', 'headless'].includes(project.execution?.mode)) err('project', "Mode d'exécution inconnu.");

  const wifi = project.wifi || [];
  if (!wifi.length) warn('project', "Aucun réseau WiFi : l'automate ne sera pas joignable.");
  if (wifi.length > 10) err('project', 'Au plus 10 réseaux WiFi (les suivants seraient ignorés).');
  wifi.forEach((w, i) => {
    const where = `WiFi n°${i + 1}`;
    if (!w.name) err('project', `${where} : le nom est obligatoire (sinon blocage au démarrage).`);
    for (const k of ['name', 'ssid', 'pwd', 'ap_name', 'ap_password']) {
      if (w[k] && utf8Length(w[k]) > 79) err('project', `${where} : « ${k} » dépasse 79 octets.`);
    }
    if ((w.mode || 'station') === 'station') {
      if (!w.ssid) err('project', `${where} : SSID obligatoire.`);
      if (!w.pwd) err('project', `${where} : mot de passe obligatoire.`);
      if (w.dhcp === false) {
        for (const k of ['ip', 'gateway', 'mask']) if (!IPV4.test(w[k] || '')) err('project', `${where} : adresse « ${k} » invalide.`);
        if (w.dns && !IPV4.test(w.dns)) err('project', `${where} : DNS invalide.`);
      }
    } else if (w.mode === 'access_point') {
      if (w.ap_password && w.ap_password.length < 8) err('project', `${where} : le mot de passe du point d'accès doit faire au moins 8 caractères.`);
      if (w.ap_ip && !IPV4.test(w.ap_ip)) err('project', `${where} : adresse IP du point d'accès invalide.`);
      const ch = Number(w.ap_channel ?? 1);
      if (!(ch >= 1 && ch <= 13)) err('project', `${where} : canal entre 1 et 13.`);
    } else {
      err('project', `${where} : mode inconnu « ${w.mode} ».`);
    }
  });
  if (project.otaUrl && utf8Length(project.otaUrl) > 79) err('project', "L'URL OTA dépasse 79 octets.");

  // --- Équipements ------------------------------------------------------
  const counts = {};
  const labels = new Set();
  const addresses = new Map();
  let hasController = false;
  let hasHardware = false;
  for (const eq of project.equipment) {
    const entry = CATALOG[eq.type];
    if (!entry) {
      err('equipment', `Équipement inconnu : ${eq.type}`, eq.uid);
      continue;
    }
    hasHardware = true;
    if (entry.rs485) hasController = true;
    counts[eq.type] = (counts[eq.type] || 0) + 1;
    if (entry.max && counts[eq.type] > entry.max) err('equipment', `${entry.label} : ${entry.max} exemplaire(s) maximum.`, eq.uid);
    if (!eq.label?.trim()) err('equipment', 'Chaque équipement doit avoir un nom.', eq.uid);
    else {
      if (labels.has(eq.label)) err('equipment', `Deux équipements s'appellent « ${eq.label} ».`, eq.uid);
      labels.add(eq.label);
      if (/^[0-9]/.test(eq.label)) err('equipment', `Le nom « ${eq.label} » ne doit pas commencer par un chiffre.`, eq.uid);
      if (eq.label.includes('/')) err('equipment', `Le nom « ${eq.label} » ne doit pas contenir « / ».`, eq.uid);
    }
    if (entry.modbus) {
      const a = Number(eq.address);
      if (!Number.isInteger(a) || a < 1 || a > 247) err('equipment', `${eq.label} : adresse Modbus entre 1 et 247.`, eq.uid);
      else if (addresses.has(a)) err('equipment', `${eq.label} et ${addresses.get(a)} ont la même adresse Modbus ${a}.`, eq.uid);
      else addresses.set(a, eq.label);
    }
  }
  if (hasHardware && !hasController) {
    err('equipment', "Ajoutez l'automate (Kincony KC868-A8S) : il porte le bus RS485 et doit être déclaré en premier.");
  }
  if (addresses.size > 8) warn('equipment', 'Plus de 8 esclaves Modbus : les statistiques du bus ne suivent que les 8 premiers.');

  // --- Variables --------------------------------------------------------
  const symbols = new Map();
  const channelUse = new Map();
  for (const v of project.variables) {
    const where = `« ${v.label || v.symbol} »`;
    if (!/^[A-Za-z_][A-Za-z0-9_]{0,39}$/.test(v.symbol || '')) err('variables', `${where} : symbole invalide (lettres, chiffres, _ ; 40 caractères maximum).`, v.uid);
    else if (CPP_RESERVED.has(v.symbol) || EXPR_RESERVED.has(v.symbol.toUpperCase()) || /^X\d+$/i.test(v.symbol)) {
      err('variables', `${where} : le symbole « ${v.symbol} » est réservé.`, v.uid);
    }
    const key = (v.symbol || '').toLowerCase();
    if (symbols.has(key)) err('variables', `Symbole en double : « ${v.symbol} ».`, v.uid);
    symbols.set(key, v);
    if (!v.label?.trim()) err('variables', `La variable ${v.symbol} n'a pas de libellé.`, v.uid);
    const kind = VARIABLE_KINDS[v.kind];
    if (!kind) err('variables', `${where} : nature inconnue « ${v.kind} ».`, v.uid);
    else if (!kind.dataTypes.includes(v.dataType)) err('variables', `${where} : type ${v.dataType} impossible pour « ${kind.label} ».`, v.uid);

    if (v.kind === 'input' || v.kind === 'output') {
      if (!v.binding?.eq) {
        if (v.system) err('variables', `${where} : signal obligatoire, câblez-le sur une sortie.`, v.uid);
        else warn('variables', `${where} : pas encore câblée sur une voie.`, v.uid);
      } else {
        const eq = project.equipment.find((e) => e.uid === v.binding.eq);
        const group = eq && CATALOG[eq.type]?.groups.find((g) => g.key === v.binding.group);
        if (!group) err('variables', `${where} : voie introuvable (équipement supprimé ?).`, v.uid);
        else {
          const expectedDir = v.kind === 'input' ? 'in' : 'out';
          if (group.dir !== expectedDir) err('variables', `${where} : une ${v.kind === 'input' ? 'entrée' : 'sortie'} ne peut pas être câblée sur « ${group.label} ».`, v.uid);
          if (group.dataType !== v.dataType) err('variables', `${where} : type ${v.dataType} incompatible avec « ${group.label} » (${group.dataType}).`, v.uid);
          const chKey = `${v.binding.eq}/${v.binding.group}/${v.binding.channel}`;
          if (channelUse.has(chKey)) err('variables', `${where} et « ${channelUse.get(chKey).label} » sont câblées sur la même voie.`, v.uid);
          channelUse.set(chKey, v);
        }
      }
    } else if (v.binding) {
      err('variables', `${where} : seules les entrées et sorties se câblent.`, v.uid);
    }
    if (v.kind === 'parameter' && v.dataType !== 'text' && v.dataType !== 'bool') {
      if (v.min !== undefined && v.min !== '' && v.max !== undefined && v.max !== '' && Number(v.min) > Number(v.max)) {
        err('variables', `${where} : minimum supérieur au maximum.`, v.uid);
      }
    }
    if (v.hmi?.show && v.hmi.widget && !widgetsFor(v).includes(v.hmi.widget)) {
      warn('variables', `${where} : widget « ${v.hmi.widget} » inadapté, un widget par défaut sera utilisé.`, v.uid);
    }
  }

  // --- Écrans -----------------------------------------------------------
  const pageNames = new Set();
  for (const page of project.hmi?.pages || []) {
    if (!page.name?.trim()) err('hmi', 'Chaque page doit avoir un nom.');
    if (pageNames.has(page.name)) err('hmi', `Deux pages s'appellent « ${page.name} ».`);
    pageNames.add(page.name);
    for (const section of page.sections || []) {
      if (!section.name?.trim()) err('hmi', `Page « ${page.name} » : chaque section doit avoir un nom.`);
      for (const item of section.items || []) {
        const node = layout.nodes.get(item.ref);
        if (!node) {
          err('hmi', `Page « ${page.name} », section « ${section.name} » : élément introuvable (${item.ref}).`);
          continue;
        }
        const label = item.label || '';
        if (!label.trim()) err('hmi', `Page « ${page.name} » : un élément n'a pas de libellé.`);
        if (utf8Length(label) + utf8Length(section.name) > 77) {
          err('hmi', `« ${section.name}/${label} » dépasse 77 octets (blocage au démarrage) : raccourcissez le libellé ou la section.`);
        }
      }
    }
  }

  issues.push(...compileLogic(project).issues);

  for (const e of layout.errors) err(e.step, e.message, e.ref);
  for (const w of layout.warnings) warn(w.step, w.message, w.ref);
  return issues;
}

export function hasErrors(issues) {
  return issues.some((i) => i.level === 'error');
}

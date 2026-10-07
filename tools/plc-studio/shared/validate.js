// Règles de validation du projet. Chaque règle reflète une contrainte du firmware
// (blocage au démarrage, dépassement de tampon, etc.) ou de l'outil.

import { CATALOG, VARIABLE_KINDS, widgetsFor } from './catalog.js';
import { utf8Length } from './hash.js';
import { buildLayout, modbusTcpOf, genericStartAddress } from './layout.js';
import { compileLogic } from './grafcet.js';
import { signalDef } from './model.js';
import { axisList } from './axes.js';

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
// (comparaison en majuscules)
const EXPR_RESERVED = new Set(['ET', 'OU', 'NON', 'FM', 'FD', 'VRAI', 'FAUX', 'AND', 'OR', 'NOT', 'MIN', 'MAX', 'ABS', 'SERVO', 'TRUE', 'FALSE']);
// Noms de projet qui écraseraient un fichier du firmware (data/<Nom>.json).
const RESERVED_PROJECT_NAMES = new Set(['config', 'interface', 'renderers', 'component-manifest', 'index', 'styles', 'diagnostic', 'main']);

const IPV4 = /^(25[0-5]|2[0-4]\d|1?\d?\d)(\.(25[0-5]|2[0-4]\d|1?\d?\d)){3}$/;

export function validateProject(project, layout = buildLayout(project)) {
  const issues = [];
  const err = (step, message, ref) => issues.push({ level: 'error', step, message, ref });
  const warn = (step, message, ref) => issues.push({ level: 'warning', step, message, ref });

  // --- Projet -----------------------------------------------------------
  if (!/^[A-Za-z][A-Za-z0-9_]{0,23}$/.test(project.name || '')) {
    err('project', 'Nom de projet invalide : lettre en premier, puis lettres, chiffres ou _ (24 caractères maximum). Il sert de nom de classe C++ et de fichier.');
  } else if (CPP_RESERVED.has(project.name) || RESERVED_PROJECT_NAMES.has(project.name.toLowerCase())) {
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

  const eth = project.ethernet;
  if (eth?.enabled) {
    const controller = project.equipment.find((e) => CATALOG[e.type]?.role === 'controller');
    if (controller && !CATALOG[controller.type]?.ethernet) {
      err('project', `Ethernet activé mais ${CATALOG[controller.type].label} n'a pas de port Ethernet utilisable : désactivez-le.`);
    }
    if (eth.dhcp === false) {
      for (const k of ['ip', 'gateway', 'mask']) if (!IPV4.test(String(eth[k] || '').trim())) err('project', `Ethernet : adresse « ${k} » invalide.`);
      if (eth.dns && !IPV4.test(String(eth.dns).trim())) err('project', 'Ethernet : DNS invalide.');
    }
  }

  // --- Équipements ------------------------------------------------------
  const counts = {};
  const labels = new Set();
  const addresses = new Map(); // bus RS485 : adresse -> équipement
  const tcpUnits = new Map(); // "ip:port/unité" -> équipement
  const tcpPorts = new Map(); // ip -> { port, label }
  let modbusCount = 0;
  let hasController = false;
  let controllerCount = 0;
  let hasHardware = false;
  for (const eq of project.equipment) {
    const entry = CATALOG[eq.type];
    if (!entry) {
      err('equipment', `Équipement inconnu : ${eq.type}`, eq.uid);
      continue;
    }
    hasHardware = true;
    if (entry.rs485) hasController = true;
    if (entry.role === 'controller' && ++controllerCount > 1) err('equipment', `${eq.label} : un seul automate par projet.`, eq.uid);
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
      modbusCount++;
      const a = Number(eq.address);
      const tcp = modbusTcpOf(eq, entry);
      if (!['rtu', 'tcp'].includes(eq.options?.transport || 'rtu')) err('equipment', `${eq.label} : liaison Modbus inconnue « ${eq.options.transport} ».`, eq.uid);
      if (tcp) {
        const port = Number(eq.options?.port ?? 502);
        if (!IPV4.test(tcp.ip)) err('equipment', `${eq.label} : adresse IP Modbus TCP invalide (ex. 192.168.1.50).`, eq.uid);
        if (!Number.isInteger(port) || port < 1 || port > 65535) err('equipment', `${eq.label} : port Modbus TCP entre 1 et 65535.`, eq.uid);
        if (!Number.isInteger(a) || a < 0 || a > 255) err('equipment', `${eq.label} : n° d'unité Modbus TCP entre 0 et 255.`, eq.uid);
        else if (IPV4.test(tcp.ip)) {
          const key = `${tcp.ip}:${tcp.port}/${a}`;
          if (tcpUnits.has(key)) err('equipment', `${eq.label} et ${tcpUnits.get(key)} ont la même adresse Modbus TCP (${tcp.ip}:${tcp.port}, unité ${a}).`, eq.uid);
          else tcpUnits.set(key, eq.label);
          const other = tcpPorts.get(tcp.ip);
          // La bibliothèque Modbus ouvre une seule connexion par adresse IP.
          if (other && other.port !== tcp.port) err('equipment', `${eq.label} et ${other.label} : même adresse IP ${tcp.ip} sur deux ports différents, non pris en charge.`, eq.uid);
          else tcpPorts.set(tcp.ip, { port: tcp.port, label: eq.label });
        }
      } else if (!Number.isInteger(a) || a < 1 || a > 247) err('equipment', `${eq.label} : adresse Modbus entre 1 et 247.`, eq.uid);
      else if (addresses.has(a)) err('equipment', `${eq.label} et ${addresses.get(a)} ont la même adresse Modbus ${a} sur le bus RS485.`, eq.uid);
      else addresses.set(a, eq.label);
    }
    if (entry.generic) {
      for (const g of entry.groups) {
        const start = genericStartAddress(eq, g);
        if (start < 0 || start + g.ids.length - 1 > 65535) err('equipment', `${eq.label} : adresse de départ de « ${g.label} » hors de 0 à 65535${eq.options?.oneBased ? ' (numérotation à partir de 1)' : ''}.`, eq.uid);
      }
    }
  }
  if (tcpPorts.size > 8) err('equipment', `${tcpPorts.size} adresses IP Modbus TCP : 8 au maximum (connexions simultanées).`);
  if (tcpUnits.size > 16) err('equipment', `${tcpUnits.size} équipements Modbus TCP : 16 au maximum.`);
  if (tcpUnits.size) {
    const hasStation = (project.wifi || []).some((w) => (w.mode || 'station') === 'station');
    if (!hasStation && !eth?.enabled) {
      warn('equipment', "Équipements Modbus TCP sans WiFi station ni Ethernet : ils devront se connecter au point d'accès de l'automate.");
    }
    warn('equipment', "Modbus TCP passe par le réseau : ne l'utilisez pas pour une fonction de sécurité (arrêt d'urgence câblé obligatoire).");
  }
  if (hasHardware && !hasController) {
    err('equipment', "Ajoutez l'automate (KinCony, Waveshare ESP32-S3, M5Stack StamPLC, Homemaster MiniPLC…) : il porte le bus RS485 et doit être déclaré en premier.");
  }
  if (modbusCount > 16) warn('equipment', 'Plus de 16 équipements Modbus : les statistiques ne suivent que les 16 premiers.');

  // Axes : leur nom sert de préfixe dans les expressions (Axe_X.enPosition) et en C++ (ax_Axe_X).
  const axisSymbols = new Map();
  for (const a of axisList(project)) {
    const key = a.symbol.toLowerCase();
    if (axisSymbols.has(key)) err('equipment', `Les axes « ${axisSymbols.get(key)} » et « ${a.label} » ont le même nom dans les expressions (${a.symbol}) : renommez l'un des deux.`, a.uid);
    axisSymbols.set(key, a.label);
    const upper = a.symbol.toUpperCase();
    if (/^X\d+$/i.test(a.symbol) || (upper !== 'SERVO' && EXPR_RESERVED.has(upper))) {
      err('equipment', `L'axe « ${a.label} » porte un nom réservé dans les expressions (${a.symbol}) : renommez-le.`, a.uid);
    }
  }

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
        if (v.system && !signalDef(project, v)?.optional) err('variables', `${where} : signal obligatoire, câblez-le sur une ${v.kind === 'input' ? 'entrée' : 'sortie'}.`, v.uid);
        else if (!v.system) warn('variables', `${where} : pas encore câblée sur une voie.`, v.uid);
      } else {
        const eq = project.equipment.find((e) => e.uid === v.binding.eq);
        const group = eq && CATALOG[eq.type]?.groups.find((g) => g.key === v.binding.group);
        if (!group) err('variables', `${where} : voie introuvable (équipement supprimé ?).`, v.uid);
        else {
          if ((eq.type === 'Kincony_KC868_A8S' && group.key === 'gpio') || group.key === 'buzzer') {
            warn('variables', `${where} : buzzer de la carte ; main.cpp le remet au repos à chaque connexion WiFi (turnOffBuzzer).`, v.uid);
          }
          const expectedDir = v.kind === 'input' ? 'in' : 'out';
          if (group.dir !== expectedDir) err('variables', `${where} : une ${v.kind === 'input' ? 'entrée' : 'sortie'} ne peut pas être câblée sur « ${group.label} ».`, v.uid);
          if (group.dataType !== v.dataType) err('variables', `${where} : type ${v.dataType} incompatible avec « ${group.label} » (${group.dataType}).`, v.uid);
          if (v.system && group.bank) err('variables', `${where} : un signal d'axe doit être câblé sur une voie de l'automate, pas sur un module Waveshare.`, v.uid);
          if (v.system && signalDef(project, v)?.gpio && !group.gpio) {
            err('variables', `${where} : signal d'impulsions, à câbler sur une sortie GPIO directe de l'automate (groupe « GPIO »), pas sur « ${group.label} ».`, v.uid);
          }
          const chKey = `${v.binding.eq}/${v.binding.group}/${v.binding.channel}`;
          if (channelUse.has(chKey)) err('variables', `${where} et « ${channelUse.get(chKey).label} » sont câblées sur la même voie.`, v.uid);
          channelUse.set(chKey, v);
        }
      }
    } else if (v.binding) {
      err('variables', `${where} : seules les entrées et sorties se câblent.`, v.uid);
    }
    if (v.dataType === 'int' || v.dataType === 'float') {
      const set = (x) => x !== undefined && x !== null && x !== '';
      for (const k of ['min', 'max', ...(v.kind === 'parameter' ? ['initial'] : [])]) {
        if (set(v[k]) && !Number.isFinite(Number(v[k]))) err('variables', `${where} : « ${k} » n'est pas un nombre.`, v.uid);
      }
      if (set(v.min) && set(v.max) && Number(v.min) > Number(v.max)) err('variables', `${where} : minimum supérieur au maximum.`, v.uid);
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

  // Sécurité : sortie d'urgence et redémarrage sans action de l'opérateur.
  const blocks = project.logic?.blocks;
  if (blocks?.emergency?.condition?.trim() && !blocks.emergency.reset?.trim()) {
    warn('logic', "Sans condition d'acquittement, la machine sort seule de l'arrêt d'urgence 1 s après sa disparition : prévoyez un bouton d'acquittement.");
    if (!blocks.run?.start?.trim()) warn('logic', "Sans acquittement ni condition de mise en marche, le cycle peut redémarrer seul après un arrêt d'urgence.");
  }

  for (const e of layout.errors) err(e.step, e.message, e.ref);
  for (const w of layout.warnings) warn(w.step, w.message, w.ref);
  return issues;
}

export function hasErrors(issues) {
  return issues.some((i) => i.level === 'error');
}

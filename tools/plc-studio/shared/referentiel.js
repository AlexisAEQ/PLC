// Chargeur du référentiel d'infrastructure (infrastructure.jsonc, à la racine du dépôt).
//
// Entrée : le texte du fichier (clés françaises, gabarits, plages d'ids).
// Sortie : le CATALOG historique de PLC Studio (même forme, même ordre de clés que l'ancien
// catalog.js), plus les parties propres au référentiel : firmware (version, limites, types
// de section), compilation, sites, logiciels, alias, écarts connus, et les « annexes » de
// chaque équipement (marque, logiciel requis, bornes, format série...), jamais projetées
// dans CATALOG pour ne pas changer sa forme.
// Chaque erreur porte la ligne du fichier. Fonction pure : aucun accès disque, le même code
// tourne dans Node (CLI, serveur, tests) et dans le navigateur.

import { lireJsonc, ligneDe } from './jsonc-pos.js';

// Types d'axe que le CODE sait générer (cpp.js, axisCreate) et simuler (sim.js, SPEED_MODELS).
// Un pilote absent de cette liste est refusé : un nouveau type d'axe demande du code.
export const KINDS_GERES = ['sureservo', 'lichuan', 'stepperonline-rs', 'stepperonline-servo', 'stepper'];

export const FORMAT_REFERENTIEL = 'plc-studio-referentiel';
export const VERSION_REFERENTIEL = 1;

// Sections de premier niveau, et sections d'équipements dans l'ordre du CATALOG.
const SECTIONS = ['format', 'version', 'revision', 'sites', 'logiciels', 'automates', 'robots', 'modules', 'axes', 'compilation', 'firmware', 'pilotes', 'gabarits', 'alias', 'ecartsConnus'];
export const SECTIONS_EQUIPEMENTS = ['automates', 'robots', 'modules', 'axes'];

// Formats série acceptés par le firmware (borneUniverselle.cpp) : contrôle de "formatSerie" et de "bus".
export const FORMATS_SERIE = ['SERIAL_8N1', 'SERIAL_8N2', 'SERIAL_8E1', 'SERIAL_8E2', 'SERIAL_8O1', 'SERIAL_8O2'];

const OBLIGATOIRE = '<obligatoire>';
const SECRET = /(^|_)(pwd|password|passwd|motdepasse|mdp|ssid|secret|token|apikey)$/i;
export const IPV4 = /^(25[0-5]|2[0-4]\d|1?\d?\d)(\.(25[0-5]|2[0-4]\d|1?\d?\d)){3}$/;
const estObjet = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);

// --- schémas : clé française -> { en: clé du CATALOG (null = annexe), type, requis } ---------
const T = { texte: 'texte', entier: 'entier', bool: 'bool', liste: 'liste', objet: 'objet', tout: 'tout' };
const SCHEMA = {
  voie: {
    cle: { en: 'key', type: T.texte, requis: true },
    libelle: { en: 'label', type: T.texte, requis: true },
    suffixeSection: { en: 'short', type: T.texte, requis: true },
    type: { en: 'type', type: 'typeSection', requis: true },
    sens: { en: 'dir', type: ['entree', 'sortie'], requis: true },
    donnee: { en: 'dataType', type: ['bool', 'int'], requis: true },
    ids: { en: 'ids', type: 'plage' },
    banc: { en: 'bank', type: T.objet },
    prefixe: { en: 'prefix', type: T.texte, requis: true },
    noms: { en: 'names', type: T.tout },
    inversable: { en: 'supportsInverse', type: T.bool },
    rafraichissement: { en: 'refreshInterval', type: T.entier },
    signe: { en: 'signed', type: T.bool },
    gpio: { en: 'gpio', type: T.bool },
    broches: { en: null, type: T.liste }, // notation : ids 1..n et noms GPIO<n>
    broche: { en: null, type: T.entier }, // paramètre du gabarit buzzer
    buzzerCarte: { en: null, type: T.bool }, // annexe : sortie qui pilote le buzzer de la carte
  },
  option: {
    cle: { en: 'key', type: T.texte, requis: true },
    libelle: { en: 'label', type: T.texte, requis: true },
    type: { en: 'type', type: ['bool', 'select', 'number', 'text'], requis: true },
    defaut: { en: 'default', type: T.tout, requis: true },
    choix: { en: 'choices', type: T.liste },
    aide: { en: 'help', type: T.texte },
  },
  entreeCommune: {
    nomParDefaut: { type: T.texte, requis: true },
    libelle: { type: T.texte, requis: true },
    description: { type: T.texte, requis: true },
    fichierMateriel: { type: 'texteOuNull', requis: true },
    options: { type: T.liste },
    voies: { type: T.liste },
  },
  automate: { marque: { type: T.texte, requis: true }, envPlatformIO: { type: T.texte, requis: true }, ethernet: { type: T.bool }, horloge: { type: T.texte }, maxParProjet: { type: T.entier } },
  equipementModbus: {
    adresseParDefaut: { type: T.entier, requis: true },
    transportParDefaut: { type: ['rtu', 'tcp'] },
    ipParDefaut: { type: 'ip' },
    generique: { type: T.bool },
    logicielRequis: { type: T.objet },
  },
  axe: {
    adresseParDefaut: { type: T.entier },
    pilote: { type: T.texte, requis: true },
    famille: { type: T.texte },
    vitesseMax: { type: T.entier },
    vitesseJogMax: { type: T.entier },
    boucleFermee: { type: T.bool },
    modelesVariateur: { type: T.objet },
    couple: { type: T.bool },
    ihm: { type: T.objet },
    aideRampe: { type: T.texte },
    noeuds: { type: T.objet },
    rampeMaxMs: { type: T.entier },
    formatSerie: { type: T.objet },
    adresses: { type: 'plage' },
  },
  pilote: {
    classeCpp: { en: 'cppClass', type: T.texte, requis: true },
    entete: { en: 'include', type: T.texte, requis: true },
    structureNoeuds: { en: 'nodesStruct', type: T.texte, requis: true },
    unitePosition: { en: 'positionUnit', type: T.texte, requis: true },
    uniteVitesse: { en: 'speedUnit', type: T.texte, requis: true },
    vitesseMin: { en: 'speedMin', type: T.entier, requis: true },
    vitesseMax: { en: 'speedMax', type: T.entier },
    vitesseParDefaut: { en: 'defaultSpeed', type: T.entier, requis: true },
    uniteVitesseJog: { en: 'jogSpeedUnit', type: T.texte, requis: true },
    vitesseJogMin: { en: 'jogSpeedMin', type: T.entier, requis: true },
    vitesseJogMax: { en: 'jogSpeedMax', type: T.entier },
    vitesseJogParDefaut: { en: 'defaultJogSpeed', type: T.entier, requis: true },
    couple: { en: 'torque', type: T.bool },
    ihm: { en: 'hmi', type: T.objet },
    aliasEcran: { en: 'refAliases', type: T.objet },
    familles: { type: T.liste },
    modbus: { type: T.bool },
    signaux: { type: T.liste },
    sections: { type: T.liste, requis: true },
    options: { type: T.liste },
    verite: { type: T.objet },
    rampeMaxMs: { type: T.entier },
    rampeParDefautMs: { type: T.entier },
  },
  section: { suffixe: { en: 'suffix', type: T.texte, requis: true }, type: { en: 'type', type: 'typeSection', requis: true }, virtuel: { en: 'virtual', type: T.bool }, noeuds: { en: 'nodes', type: T.tout, requis: true } },
  noeud: {
    id: { en: 'id', type: T.entier, requis: true },
    nom: { en: 'name', type: T.texte, requis: true },
    champ: { en: 'field', type: T.texte, requis: true },
    cpp: { en: 'cpp', type: T.texte, requis: true },
    rafraichissement: { en: 'refreshInterval', type: T.entier },
    libelle: { en: 'label', type: T.texte },
    affichage: { en: 'display', type: ['position'] },
    seulementSi: { type: ['boucleFermee'] },
  },
  signal: {
    cle: { en: 'key', type: T.texte, requis: true },
    libelle: { en: 'label', type: T.texte, requis: true },
    champ: { en: 'field', type: T.texte, requis: true },
    gpio: { en: 'gpio', type: T.bool },
    typeCpp: { en: 'cppType', type: T.texte },
    classeId: { en: 'classId', type: T.texte },
    sens: { en: 'dir', type: ['entree'] },
    facultatif: { en: 'optional', type: T.bool },
  },
  ihm: { numeriques: { en: 'numeric', type: T.liste }, voyants: { en: 'indicators', type: T.liste }, alarmes: { en: 'alarms', type: T.liste } },
  logicielRequis: { noms: { type: T.liste, requis: true }, versionMin: { type: 'version' }, sinon: { type: T.texte } },
  formatSerie: { impose: { type: FORMATS_SERIE, requis: true } },
  limite: { valeur: { type: T.entier, requis: true }, verite: { type: T.tout }, aide: { type: T.texte } },
  typeSection: { classe: { type: T.texte, requis: true }, classeMateriel: { type: T.texte }, nonUtilise: { type: T.bool }, inversionParNoeud: { type: T.bool } },
  env: { mcu: { type: T.texte, requis: true }, carte: { type: T.texte, requis: true }, flash: { type: T.texte, requis: true }, arduino: { type: T.entier, requis: true }, partitions: { type: T.texte, requis: true } },
  logiciel: { editeur: { type: T.texte, requis: true }, libelle: { type: T.texte, requis: true }, serveurModbusTcp: { type: T.bool, requis: true }, remarque: { type: T.texte } },
  site: { libelle: { type: T.texte, requis: true }, reseau: { type: T.objet }, appareils: { type: T.objet, requis: true }, remarque: { type: T.texte } },
  reseau: { sousReseau: { type: 'cidr' }, passerelle: { type: 'ip' } },
  appareil: {
    type: { type: T.texte },
    materiel: { type: T.texte },
    logiciel: { type: T.objet },
    ip: { type: 'ip' },
    modbusTcp: { type: 'objetOuNull' },
    rs485: { type: T.objet },
    ethernet: { type: T.objet },
    bus: { type: T.objet },
    remarque: { type: T.texte },
  },
  logicielAppareil: { nom: { type: T.texte, requis: true }, version: { type: 'version' }, releveLe: { type: 'date' } },
  modbusTcp: { port: { type: T.entier, requis: true }, unite: { type: T.entier, requis: true } },
  rs485: { adresse: { type: T.entier, requis: true } },
  ethernet: { ip: { type: 'ip', requis: true }, masque: { type: 'ip', requis: true }, passerelle: { type: 'ip', requis: true }, dns: { type: 'ip' } },
  bus: { vitesse: { type: 'entierOuNull' }, format: { type: 'texteOuNull' } },
};
const VALEURS = { sens: { entree: 'in', sortie: 'out' } };
const ALIAS_TABLES = ['types', 'voies', 'groupes', 'options', 'sites', 'appareils'];

// --- outils ---------------------------------------------------------------------------------
function distance(a, b) {
  const d = Array.from({ length: a.length + 1 }, (_, i) => [i, ...Array(b.length).fill(0)]);
  for (let j = 1; j <= b.length; j++) d[0][j] = j;
  for (let i = 1; i <= a.length; i++)
    for (let j = 1; j <= b.length; j++) d[i][j] = Math.min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
  return d[a.length][b.length];
}

// « (vouliez-vous dire « X » ?) » quand un nom connu est proche.
export function suggestion(mot, connus) {
  let best = null;
  let score = Infinity;
  for (const c of connus) {
    const s = distance(String(mot).toLowerCase(), String(c).toLowerCase());
    if (s < score) [best, score] = [c, s];
  }
  return best && score <= Math.max(2, Math.floor(String(mot).length / 3)) ? ` (vouliez-vous dire « ${best} » ?)` : '';
}

// "1-8", "1-4, 9", "9" ou 9 -> [1..8] ; null si invalide (vide, décroissant, non numérique).
export function lirePlage(texte) {
  if (typeof texte === 'number' && Number.isInteger(texte)) return [texte];
  if (typeof texte !== 'string') return null;
  const out = [];
  for (const morceau of texte.split(',').map((s) => s.trim()).filter(Boolean)) {
    const m = /^(\d+)(?:\s*-\s*(\d+))?$/.exec(morceau);
    if (!m) return null;
    const a = Number(m[1]);
    const b = m[2] === undefined ? a : Number(m[2]);
    if (b < a) return null;
    for (let i = a; i <= b; i++) out.push(i);
  }
  return out.length ? out : null;
}

// Comparaison de versions « 4.5.13 » segment par segment : <0, 0 ou >0.
export function comparerVersions(a, b) {
  const x = String(a).split('.').map(Number);
  const y = String(b).split('.').map(Number);
  for (let i = 0; i < Math.max(x.length, y.length); i++) {
    const d = (x[i] || 0) - (y[i] || 0);
    if (d) return d;
  }
  return 0;
}

// Le logiciel relevé sur un appareil convient-il au profil ? null si oui (ou non relevé),
// sinon { exige: « ARCS » / « FAIRINO 3.7.3 ou plus récent », sinon: conseil du profil }.
export function incompatibiliteLogiciel(requis, logiciel) {
  if (!requis || !logiciel?.nom) return null;
  if (!requis.noms.includes(logiciel.nom)) return { exige: requis.noms.join(' ou '), sinon: requis.sinon || '' };
  if (requis.versionMin && logiciel.version && comparerVersions(logiciel.version, requis.versionMin) < 0) {
    return { exige: `${logiciel.nom} ${requis.versionMin} ou plus récent`, sinon: '' };
  }
  return null;
}

const texteLogiciel = (l) => `${l.nom}${l.version ? ' ' + l.version : ''}`;

function ipVersEntier(ip) {
  return ip.split('.').reduce((n, x) => n * 256 + Number(x), 0);
}
export function ipDansSousReseau(ip, cidr) {
  const m = /^([\d.]+)\/(\d{1,2})$/.exec(cidr || '');
  if (!m || !IPV4.test(ip) || !IPV4.test(m[1])) return true;
  const bits = Number(m[2]);
  const masque = bits === 0 ? 0 : (0xffffffff << (32 - bits)) >>> 0;
  return ((ipVersEntier(ip) & masque) >>> 0) === ((ipVersEntier(m[1]) & masque) >>> 0);
}

// Fusion « gabarit puis clés locales » : objets fusionnés, listes et valeurs remplacées,
// null = retirer. Ordre des clés : celles du gabarit, puis les nouvelles clés locales.
function fusion(base, local) {
  const out = {};
  for (const [k, v] of Object.entries(base)) out[k] = v;
  for (const [k, v] of Object.entries(local)) {
    if (v === null && k in out) delete out[k];
    else if (estObjet(v) && estObjet(out[k])) out[k] = fusion(out[k], v);
    else out[k] = v;
  }
  return out;
}

// =============================================================================================
export function chargerReferentiel(texte, { kinds = KINDS_GERES } = {}) {
  const erreurs = [];
  const avertissements = [];
  let src;
  try {
    src = lireJsonc(texte);
  } catch (e) {
    erreurs.push({ ligne: e.ligne ?? null, colonne: e.colonne ?? null, chemin: '', message: e.texte || e.message });
    return { erreurs, avertissements };
  }
  if (!estObjet(src)) {
    erreurs.push({ ligne: 1, chemin: '', message: 'le fichier doit contenir un objet { … }.' });
    return { erreurs, avertissements };
  }
  // Ligne de la clé ; pour un objet issu d'un gabarit (sans position), ligne de l'objet source.
  const err = (parent, cle, chemin, message, source) => erreurs.push({ ligne: ligneDe(parent, cle) ?? ligneDe(source, cle) ?? ligneDe(source), chemin, message });
  const att = (parent, cle, chemin, message) => avertissements.push({ ligne: ligneDe(parent, cle), chemin, message });

  // --- en-tête ------------------------------------------------------------------------------
  if (src.format !== FORMAT_REFERENTIEL) err(src, 'format', 'format', `« format » doit valoir "${FORMAT_REFERENTIEL}".`);
  if (src.version !== VERSION_REFERENTIEL) err(src, 'version', 'version', `version ${JSON.stringify(src.version)} inconnue de ce PLC Studio (attendu : ${VERSION_REFERENTIEL}). Mettez PLC Studio à jour.`);
  for (const k of Object.keys(src)) if (!SECTIONS.includes(k)) err(src, k, k, `section inconnue « ${k} »${suggestion(k, SECTIONS)}. Sections : ${SECTIONS.join(', ')}.`);
  for (const k of ['automates', 'firmware', 'compilation', 'pilotes', 'gabarits']) if (!estObjet(src[k])) err(src, undefined, k, `section « ${k} » obligatoire (objet { … }).`);

  // --- secrets : aucune clé de mot de passe ou d'identifiant WiFi -----------------------------
  (function chercherSecrets(o, chemin) {
    if (Array.isArray(o)) return o.forEach((x, i) => chercherSecrets(x, `${chemin}[${i}]`));
    if (!estObjet(o)) return;
    for (const [k, v] of Object.entries(o)) {
      if (SECRET.test(k)) err(o, k, `${chemin}.${k}`, `clé « ${k} » refusée : aucun mot de passe ni identifiant WiFi dans le référentiel (ils restent dans le projet).`);
      chercherSecrets(v, `${chemin}.${k}`);
    }
  })(src, '');

  const firmware = estObjet(src.firmware) ? src.firmware : {};
  const typesSection = estObjet(firmware.typesSection) ? firmware.typesSection : {};
  const gabarits = estObjet(src.gabarits) ? src.gabarits : {};
  const pilotesSrc = estObjet(src.pilotes) ? src.pilotes : {};
  const compilation = estObjet(src.compilation) ? src.compilation : {};
  const envs = estObjet(compilation.envs) ? compilation.envs : {};
  const pucesHorloge = estObjet(firmware.pucesHorloge) ? firmware.pucesHorloge : {};
  const logicielsSrc = estObjet(src.logiciels) ? src.logiciels : {};

  // --- contrôle de clés et de types -----------------------------------------------------------
  function controler(obj, schema, chemin, { parametres = [], source } = {}) {
    const errS = (p, k, c, m) => err(p, k, c, m, source);
    if (!estObjet(obj)) {
      errS(source, undefined, chemin, `${chemin} : objet { … } attendu.`);
      return false;
    }
    const permis = Object.keys(schema);
    for (const k of Object.keys(obj)) {
      if (k === 'gabarit' || parametres.includes(k)) continue;
      if (!schema[k]) {
        errS(obj, k, `${chemin}.${k}`, `clé inconnue « ${k} »${suggestion(k, permis)}. Clés possibles : ${permis.join(', ')}.`);
        continue;
      }
      const v = obj[k];
      if (v === OBLIGATOIRE) continue; // signalé à part
      const t = schema[k].type;
      const ko = (attendu) => errS(obj, k, `${chemin}.${k}`, `« ${k} » doit être ${attendu} (trouvé ${JSON.stringify(v)}).`);
      if (Array.isArray(t)) {
        if (!t.includes(v)) ko(`l'une des valeurs ${t.map((x) => `"${x}"`).join(', ')}`);
      } else if (t === T.texte && typeof v !== 'string') ko('un texte entre guillemets');
      else if (t === 'texteOuNull' && v !== null && typeof v !== 'string') ko('un texte ou null');
      else if (t === T.entier && !Number.isInteger(v)) ko('un nombre entier');
      else if (t === 'entierOuNull' && v !== null && !Number.isInteger(v)) ko('un nombre entier ou null');
      else if (t === T.bool && typeof v !== 'boolean') ko('true ou false');
      else if (t === T.liste && !Array.isArray(v)) ko('une liste [ … ]');
      else if (t === T.objet && !estObjet(v)) ko('un objet { … }');
      else if (t === 'objetOuNull' && v !== null && !estObjet(v)) ko('un objet { … } ou null');
      else if (t === 'ip' && !IPV4.test(String(v))) ko('une adresse IPv4 (ex. 192.168.10.50)');
      else if (t === 'cidr' && !/^([\d.]+)\/(\d|[12]\d|3[0-2])$/.test(String(v))) ko('un sous-réseau « 192.168.10.0/24 »');
      else if (t === 'version' && !/^\d+(\.\d+)*$/.test(String(v))) ko('une version en chiffres séparés par des points (ex. "4.5.13")');
      else if (t === 'date' && !/^\d{4}-\d{2}-\d{2}$/.test(String(v))) ko('une date AAAA-MM-JJ');
      else if (t === 'plage' && !lirePlage(v)) ko('une plage d\'ids : "1-8", "1-4, 9" ou "9"');
      else if (t === 'typeSection' && !typesSection[v]) {
        errS(obj, k, `${chemin}.${k}`, `type de section « ${v} » inconnu du firmware${suggestion(String(v), Object.keys(typesSection))} (voir firmware.typesSection).`);
      }
    }
    for (const [k, d] of Object.entries(schema)) if (d.requis && !(k in obj)) errS(obj, undefined, chemin, `« ${k} » obligatoire.`);
    return true;
  }

  // --- firmware et compilation -----------------------------------------------------------------
  if (firmware.version !== undefined && !/^\d+\.\d+\.\d+$/.test(String(firmware.version))) err(firmware, 'version', 'firmware.version', '« version » : x.y.z attendu (BORNE_UNIVERSELLE_VERSION).');
  const limites = estObjet(firmware.limites) ? firmware.limites : {};
  for (const [nom, l] of Object.entries(limites)) controler(l, SCHEMA.limite, `firmware.limites.${nom}`, { source: limites });
  for (const [nom, t] of Object.entries(typesSection)) controler(t, SCHEMA.typeSection, `firmware.typesSection.${nom}`, { source: typesSection });
  for (const [nom, env] of Object.entries(envs)) controler(env, SCHEMA.env, `compilation.envs.${nom}`, { source: envs });
  if (compilation.envParDefaut !== undefined && !envs[compilation.envParDefaut]) err(compilation, 'envParDefaut', 'compilation.envParDefaut', `environnement « ${compilation.envParDefaut} » absent de compilation.envs.`);
  for (const k of ['classesReservees', 'nomsProjetReserves', 'pucesExtension']) if (firmware[k] !== undefined && !Array.isArray(firmware[k])) err(firmware, k, `firmware.${k}`, `« ${k} » : liste attendue.`);
  for (const [nom, l] of Object.entries(logicielsSrc)) controler(l, SCHEMA.logiciel, `logiciels.${nom}`, { source: logicielsSrc });

  // --- gabarits ----------------------------------------------------------------------------------
  // Résout "gabarit" (récursif). Retourne { valeur, params, nomRacine } ou null.
  function resoudre(obj, famille, chemin, vus = []) {
    if (typeof obj === 'string') obj = { gabarit: obj };
    if (!estObjet(obj) || !obj.gabarit) return { valeur: obj, params: [], nomRacine: null };
    const nom = obj.gabarit;
    const table = estObjet(gabarits[famille]) ? gabarits[famille] : {};
    if (!table[nom]) {
      err(obj, 'gabarit', `${chemin}.gabarit`, `gabarit « ${nom} » introuvable dans gabarits.${famille}${suggestion(String(nom), Object.keys(table))}. Gabarits : ${Object.keys(table).join(', ')}.`);
      return null;
    }
    if (vus.includes(nom)) {
      err(obj, 'gabarit', chemin, `gabarits en boucle : ${[...vus, nom].join(' → ')}.`);
      return null;
    }
    const base = resoudre(table[nom], famille, `gabarits.${famille}.${nom}`, [...vus, nom]);
    if (!base) return null;
    const { gabarit, ...local } = obj;
    void gabarit;
    const params = [...base.params, ...Object.entries(table[nom]).filter(([, v]) => v === OBLIGATOIRE).map(([k]) => k)];
    return { valeur: fusion(base.valeur, local), params, nomRacine: base.nomRacine || nom };
  }

  function exigerFournis(obj, chemin, source, nomGabarit) {
    for (const [k, v] of Object.entries(obj)) {
      if (v === OBLIGATOIRE) err(source, undefined, chemin, `« ${k} » obligatoire (gabarit « ${nomGabarit} »).`);
    }
  }

  // Remplace les {cle} des textes. Un texte qui n'est QUE « {cle} » prend la valeur telle quelle.
  function remplir(v, contextes, chemin, source) {
    if (typeof v === 'string') {
      const seul = /^\{(\w+)\}$/.exec(v);
      const cherche = (k) => {
        for (const c of contextes) if (c && k in c && c[k] !== OBLIGATOIRE && c[k] !== undefined) return c[k];
        err(source, undefined, chemin, `« {${k}} » sans valeur : ajoutez « ${k} » à l'entrée.`);
        return '';
      };
      if (seul) return cherche(seul[1]);
      return v.replace(/\{(\w+)\}/g, (_, k) => String(cherche(k)));
    }
    if (Array.isArray(v)) return v.map((x) => remplir(x, contextes, chemin, source));
    if (estObjet(v)) return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, remplir(x, contextes, chemin, source)]));
    return v;
  }

  // Traduction d'un objet français vers l'anglais du CATALOG, dans l'ordre de ses clés.
  function traduire(obj, schema) {
    const out = {};
    for (const [k, v] of Object.entries(obj)) {
      const d = schema[k];
      if (!d || !d.en) continue;
      out[d.en] = VALEURS[k]?.[v] ?? v;
    }
    return out;
  }

  // --- voies ---------------------------------------------------------------------------------------
  function voie(src0, chemin, entree, annexeVoies) {
    const r = resoudre(src0, 'voies', chemin);
    if (!r) return null;
    const g = r.valeur;
    if (!estObjet(g)) {
      err(src0, undefined, chemin, 'voie : objet { … } attendu.');
      return null;
    }
    exigerFournis(g, chemin, src0, r.nomRacine);
    controler(g, SCHEMA.voie, chemin, { parametres: r.params, source: src0 });
    const v = remplir(g, [g, entree], chemin, src0);
    let ids = null;
    let noms = null;
    if (Array.isArray(v.broches)) {
      if (!v.broches.length || !v.broches.every((b) => Number.isInteger(b) && b >= 0 && b <= 48)) err(src0, 'broches', `${chemin}.broches`, 'broches : liste de numéros de GPIO (0 à 48).');
      if (new Set(v.broches).size !== v.broches.length) err(src0, 'broches', `${chemin}.broches`, 'broche GPIO en double.');
      ids = v.broches.map((_, i) => i + 1);
      noms = Object.fromEntries(v.broches.map((b, i) => [i + 1, `GPIO${b}`]));
    } else if (v.ids !== undefined) ids = lirePlage(v.ids);
    if (!ids && !v.banc && v.broches !== OBLIGATOIRE) err(src0, undefined, chemin, 'il faut « ids » (ex. "1-8"), « broches » ou « banc ».');
    if (ids && v.banc) err(src0, undefined, chemin, '« ids » et « banc » sont exclusifs.');
    if (ids && ids.some((x) => x > 255)) err(src0, 'ids', `${chemin}.ids`, 'id de voie > 255 (ids du fichier matériel en uint8).');
    let bank;
    if (v.banc) {
      const b = v.banc;
      if (!Number.isInteger(b.noeud) || !Number.isInteger(b.bits) || b.bits < 1 || b.bits > 255) err(src0, 'banc', `${chemin}.banc`, 'banc : { "noeud": <id>, "bits": 1 à 255 }.');
      for (const k of Object.keys(b)) if (!['noeud', 'bits'].includes(k)) err(b, k, `${chemin}.banc.${k}`, `clé inconnue « ${k} » (attendu : noeud, bits).`);
      bank = { nodeId: b.noeud, bits: b.bits };
    }
    if (v.noms !== undefined && !Array.isArray(v.broches)) {
      if (Array.isArray(v.noms)) {
        const cles = ids || Array.from({ length: bank?.bits || 0 }, (_, i) => i);
        if (v.noms.length > cles.length || (ids && v.noms.length !== ids.length)) {
          err(src0, 'noms', `${chemin}.noms`, `${v.noms.length} noms pour ${cles.length} ${ids ? 'ids' : 'bits'} : il en faut ${ids ? 'autant' : 'au plus autant'}.`);
        }
        noms = Object.fromEntries(v.noms.map((n, i) => [cles[i], n]));
      } else if (estObjet(v.noms)) noms = { ...v.noms };
      else err(src0, 'noms', `${chemin}.noms`, 'noms : liste ["A", "B"] ou objet { "7": "LED2" }.');
    }
    // Ordre des clés : préfixe fixe, puis le reste dans l'ordre (gabarit puis local).
    const t = traduire(v, SCHEMA.voie);
    const out = { key: t.key, label: t.label, short: t.short, type: t.type, dir: t.dir, dataType: t.dataType };
    if (ids) out.ids = ids;
    if (bank) out.bank = bank;
    out.prefix = t.prefix;
    for (const k of Object.keys(v)) {
      if (k === 'noms' || k === 'broches') {
        if (noms && !('names' in out)) out.names = noms;
      } else if (['inversable', 'rafraichissement', 'signe', 'gpio'].includes(k)) out[SCHEMA.voie[k].en] = v[k];
    }
    if (v.buzzerCarte) annexeVoies[out.key] = { buzzerCarte: true };
    return out;
  }

  // --- options -------------------------------------------------------------------------------------
  function option(src0, chemin, contexteEntree) {
    const r = resoudre(src0, 'options', chemin);
    if (!r) return null;
    if (!estObjet(r.valeur)) {
      err(src0, undefined, chemin, 'option : objet { … } ou nom de gabarit attendu.');
      return null;
    }
    const o = { ...r.valeur };
    if (r.nomRacine && !('cle' in o)) o.cle = r.nomRacine; // la clé de l'option = nom du gabarit
    const source = typeof src0 === 'string' ? undefined : src0;
    exigerFournis(o, chemin, source, r.nomRacine);
    controler(o, SCHEMA.option, chemin, { source });
    const v = remplir(o, [o, contexteEntree], chemin, src0);
    const out = { key: v.cle, label: v.libelle, type: v.type, default: v.defaut };
    if (v.choix) {
      out.choices = v.choix.map((c) => (typeof c === 'string' ? { value: c, label: c } : { value: c.valeur, label: c.libelle }));
      if (!out.choices.some((c) => c.value === out.default)) err(src0, undefined, chemin, `option « ${out.key} » : la valeur par défaut ${JSON.stringify(out.default)} n'est pas dans « choix ».`);
    } else if (v.type === 'select') err(src0, undefined, chemin, `option « ${out.key} » de type select sans « choix ».`);
    if (v.aide !== undefined) out.help = v.aide;
    return out;
  }
  const options = (liste, chemin, ctx) => (Array.isArray(liste) ? liste : []).map((o, i) => option(o, `${chemin}[${i}]`, ctx)).filter(Boolean);

  // --- sections et nœuds -------------------------------------------------------------------------
  // listeSource : la liste écrite dans le fichier (positions), « liste » pouvant en être une copie.
  function noeuds(liste, chemin, src0, entree, typeSec, listeSource) {
    if (!Array.isArray(liste)) {
      err(src0, undefined, chemin, 'nœuds : liste attendue.');
      return [];
    }
    const out = [];
    let dernier = 0;
    for (const [i, n] of liste.entries()) {
      const c = `${chemin}[${i}]`;
      const n0 = Array.isArray(listeSource) ? listeSource[i] : undefined;
      if (!controler(n, SCHEMA.noeud, c, { source: n0 ?? listeSource ?? src0 })) continue;
      if (n.id <= dernier) err(listeSource, i, c, `id ${n.id} : les ids d'une section doivent être croissants et distincts.`, src0);
      dernier = n.id;
      if (n.id > 255) err(listeSource, i, c, `id ${n.id} > 255.`, src0);
      const attendu = typesSection[typeSec]?.classe;
      if (attendu && n.cpp !== attendu) err(n0, 'cpp', `${c}.cpp`, `classe « ${n.cpp} » : une section « ${typeSec} » crée des « ${attendu} » (firmware.typesSection).`, src0);
      if (n.seulementSi && !entree[n.seulementSi]) continue;
      out.push(traduire(n, SCHEMA.noeud));
    }
    return out;
  }

  function sectionsAxe(pilote, kind, entree, chemin) {
    const parModele = estObjet(entree.noeuds) ? entree.noeuds : {};
    const utilises = new Set();
    const out = [];
    for (const [i, s0] of (pilote.sections || []).entries()) {
      const c = `pilotes.${kind}.sections[${i}]`;
      const r = resoudre(s0, 'sections', c);
      if (!r) continue;
      exigerFournis(r.valeur, c, s0, r.nomRacine);
      controler(r.valeur, SCHEMA.section, c, { parametres: r.params, source: s0 });
      const s = remplir(r.valeur, [r.valeur], c, s0);
      let liste = s.noeuds;
      let listeSource = estObjet(s0) && Array.isArray(s0.noeuds) ? s0.noeuds : r.valeur.noeuds;
      if (liste === 'par modèle') {
        if (!parModele[s.suffixe]) {
          err(entree, undefined, chemin, `le pilote « ${kind} » laisse au modèle les nœuds de la section « ${s.suffixe} » : ajoutez "noeuds": { "${s.suffixe}": [ … ] }.`);
          continue;
        }
        utilises.add(s.suffixe);
        liste = parModele[s.suffixe];
        listeSource = liste;
      }
      const sec = { suffix: s.suffixe, type: s.type };
      if (s.virtuel) sec.virtual = true;
      else sec.modbus = true;
      sec.nodes = noeuds(liste, utilises.has(s.suffixe) ? `${chemin}.noeuds.${s.suffixe}` : `${c}.noeuds`, s0, entree, s.type, listeSource);
      out.push(sec);
    }
    for (const k of Object.keys(parModele)) {
      if (!utilises.has(k)) {
        const parModeleSections = (pilote.sections || []).filter((s) => s.noeuds === 'par modèle').map((s) => s.suffixe);
        err(entree.noeuds, k, `${chemin}.noeuds.${k}`, `section « ${k} » : le pilote « ${kind} » ne la laisse pas au modèle (sections « par modèle » : ${parModeleSections.join(', ') || 'aucune'}).`);
      }
    }
    // Unicité des suffixes et des champs.
    const suffixes = new Set();
    const champs = new Map();
    for (const s of out) {
      if (suffixes.has(s.suffix)) err(entree, undefined, chemin, `deux sections « ${s.suffix} » (le nom de section serait en double).`);
      suffixes.add(s.suffix);
      for (const n of s.nodes) {
        if (champs.has(n.field)) err(entree, undefined, chemin, `champ « ${n.field} » utilisé deux fois (sections « ${champs.get(n.field)} » et « ${s.suffix} »).`);
        champs.set(n.field, s.suffix);
      }
    }
    return out;
  }

  // --- pilotes ---------------------------------------------------------------------------------------
  const pilotes = {};
  for (const [kind, p] of Object.entries(pilotesSrc)) {
    const c = `pilotes.${kind}`;
    if (!kinds.includes(kind)) {
      err(pilotesSrc, kind, c, `pilote « ${kind} » inconnu du générateur et du simulateur${suggestion(kind, kinds)}. Pilotes gérés par le code : ${kinds.join(', ')}. Un nouveau type d'axe demande du code (firmware, shared/cpp.js, shared/sim.js, KINDS_GERES de shared/referentiel.js).`);
      continue;
    }
    if (!controler(p, SCHEMA.pilote, c, { source: pilotesSrc })) continue;
    if (p.ihm) controler(p.ihm, SCHEMA.ihm, `${c}.ihm`, { source: p });
    for (const [i, s] of (p.signaux || []).entries()) controler(s, SCHEMA.signal, `${c}.signaux[${i}]`, { source: p.signaux });
    pilotes[kind] = p;
  }
  for (const k of kinds) if (!pilotesSrc[k]) erreurs.push({ ligne: ligneDe(src, 'pilotes'), chemin: 'pilotes', message: `le pilote « ${k} », géré par le code, n'est pas décrit.` });

  function axeDe(kind, p, entree) {
    const out = { kind };
    for (const [k, v] of Object.entries(p)) {
      const d = SCHEMA.pilote[k];
      if (!d?.en) continue;
      out[d.en] = k === 'ihm' ? traduire(v, SCHEMA.ihm) : estObjet(v) ? { ...v } : v;
    }
    // Clés du modèle, dans son ordre (sémantique de { ...PILOTE, famille, ... }).
    const MODELE = { famille: 'family', vitesseMax: 'speedMax', vitesseJogMax: 'jogSpeedMax', boucleFermee: 'closedLoop', modelesVariateur: 'models', couple: 'torque', ihm: 'hmi' };
    for (const k of Object.keys(entree)) {
      if (!MODELE[k]) continue;
      const v = entree[k];
      if (k === 'modelesVariateur') out.models = Object.keys(v);
      else if (k === 'ihm') out.hmi = fusion(out.hmi || {}, traduire(v, SCHEMA.ihm));
      else out[MODELE[k]] = v;
    }
    return out;
  }

  // --- entrées du catalogue --------------------------------------------------------------------------
  const catalog = {};
  const equipements = {}; // annexes par clé d'équipement
  const transport = ['transport', 'ip', 'port'].map((k) => option(k, `gabarits.options.${k}`, {}));

  function entree(type, e0, section) {
    const chemin = `${section}.${type}`;
    if (!estObjet(e0)) {
      err(src[section], type, chemin, 'entrée : objet { … } attendu.');
      return;
    }
    if (catalog[type]) {
      err(src[section], type, chemin, `« ${type} » existe déjà dans la section « ${equipements[type].section} » : une clé d'équipement est unique dans tout le fichier.`);
      return;
    }
    // Les automates héritent d'office de gabarits.automate.
    const e = section === 'automates' ? fusion(estObjet(gabarits.automate) ? gabarits.automate : {}, e0) : e0;
    const schema = { ...SCHEMA.entreeCommune, ...(section === 'automates' ? SCHEMA.automate : section === 'axes' ? SCHEMA.axe : SCHEMA.equipementModbus) };
    controler(e, schema, chemin, { source: e0 });
    const role = section === 'automates' ? 'controller' : section === 'axes' ? 'axis' : 'modbus-io';
    const kind = e.pilote;
    const pilote = section === 'axes' ? pilotes[kind] : null;
    if (section === 'axes' && !pilote) {
      err(e0, 'pilote', `${chemin}.pilote`, `pilote « ${kind} » inconnu${suggestion(String(kind), Object.keys(pilotes))}. Pilotes : ${Object.keys(pilotes).join(', ')}.`);
      return;
    }
    if (pilote?.familles && !pilote.familles.includes(e.famille)) {
      err(e0, 'famille', `${chemin}.famille`, `famille « ${e.famille} » inconnue du pilote « ${kind} » (familles : ${pilote.familles.join(', ')}).`);
    }
    if (section === 'automates') {
      if (!envs[e.envPlatformIO]) err(e0, 'envPlatformIO', `${chemin}.envPlatformIO`, `environnement « ${e.envPlatformIO} » absent de compilation.envs${suggestion(String(e.envPlatformIO), Object.keys(envs))}.`);
      if (e.horloge && !pucesHorloge[e.horloge]) err(e0, 'horloge', `${chemin}.horloge`, `puce d'horloge « ${e.horloge} » inconnue du firmware${suggestion(e.horloge, Object.keys(pucesHorloge))} (firmware.pucesHorloge : ${Object.keys(pucesHorloge).join(', ')}).`);
      if (e.ethernet && envs[e.envPlatformIO] && envs[e.envPlatformIO].arduino < (compilation.arduinoMinPourEthernet ?? 3)) {
        att(e0, 'ethernet', chemin, `Ethernet sur l'environnement « ${e.envPlatformIO} » (cœur Arduino ${envs[e.envPlatformIO].arduino}) : l'Ethernet ne sera pas compilé.`);
      }
    }
    if (e.logicielRequis) {
      controler(e.logicielRequis, SCHEMA.logicielRequis, `${chemin}.logicielRequis`, { source: e0 });
      for (const nom of Array.isArray(e.logicielRequis.noms) ? e.logicielRequis.noms : []) {
        if (!logicielsSrc[nom]) err(e.logicielRequis, 'noms', `${chemin}.logicielRequis.noms`, `logiciel « ${nom} » absent de la section « logiciels »${suggestion(String(nom), Object.keys(logicielsSrc))}.`);
      }
    }
    if (e.formatSerie) controler(e.formatSerie, SCHEMA.formatSerie, `${chemin}.formatSerie`, { source: e0 });
    const modeles = estObjet(e.modelesVariateur) ? Object.entries(e.modelesVariateur) : [];
    for (const [m, d] of modeles) {
      if (!estObjet(d) || !Number.isInteger(d.courantCreteMax)) err(e.modelesVariateur, m, `${chemin}.modelesVariateur.${m}`, `modèle « ${m} » : { "courantCreteMax": <0,1 A> } attendu.`);
    }

    // Contexte des {…} : l'entrée et des valeurs calculées.
    const ctx = { ...e, courantCreteMaxA: modeles.length ? Math.max(...modeles.map(([, m]) => Number(m?.courantCreteMax) || 0)) / 10 : undefined };

    // Voies
    const annexeVoies = {};
    const groups = [];
    for (const [i, v0] of (Array.isArray(e.voies) ? e.voies : []).entries()) {
      const g = voie(v0, `${chemin}.voies[${i}]`, ctx, annexeVoies);
      if (g) groups.push(g);
    }
    const cles = new Set();
    const shorts = new Set();
    for (const [i, g] of groups.entries()) {
      if (cles.has(g.key)) err(e0.voies, i, `${chemin}.voies[${i}]`, `deux voies ont la clé « ${g.key} ».`);
      cles.add(g.key);
      if (shorts.has(g.short)) err(e0.voies, i, `${chemin}.voies[${i}]`, `deux voies ont le suffixe de section « ${g.short} » : leurs sections porteraient le même nom.`);
      shorts.add(g.short);
    }
    if (role === 'controller' && groups[0] && groups[0].dir !== 'in') {
      err(e0.voies, 0, `${chemin}.voies[0]`, "la première voie d'un automate doit être une entrée (sa section porte le bloc RS485 du fichier matériel).");
    }

    // Options
    let opts = options(e.options || (pilote ? pilote.options : []), e.options ? `${chemin}.options` : `pilotes.${kind}.options`, ctx);
    if (section === 'automates' && e.horloge) opts = [option('rtc', `${chemin}.horloge`, ctx), ...opts].filter(Boolean);
    if (modeles.length) {
      opts = [{ key: 'model', label: 'Modèle', type: 'select', default: modeles[0][0], choices: modeles.map(([m]) => ({ value: m, label: m })) }, ...opts];
    }
    const modbus = section === 'axes' ? pilote.modbus !== false : section !== 'automates';
    if (modbus && transport.every(Boolean)) {
      opts = [
        { ...transport[0], ...(e.transportParDefaut ? { default: e.transportParDefaut } : {}) },
        { ...transport[1], ...(e.ipParDefaut ? { default: e.ipParDefaut } : {}) },
        transport[2],
        ...opts,
      ];
    }

    // Assemblage dans l'ordre historique des clés du CATALOG.
    const out = { defaultLabel: e.nomParDefaut };
    if (e.ethernet) out.ethernet = true;
    out.label = e.libelle;
    out.description = e.description;
    out.role = role;
    out.hardware = e.fichierMateriel ?? null;
    if (e.generique) out.generic = true;
    if (e.envPlatformIO) out.pioEnv = e.envPlatformIO;
    if (role === 'controller') out.max = e.maxParProjet;
    out.modbus = modbus;
    if (role === 'controller') out.rs485 = true;
    if (e.adresseParDefaut !== undefined) out.defaultAddress = e.adresseParDefaut;
    if (e.transportParDefaut) out.defaultTransport = e.transportParDefaut;
    if (e.ipParDefaut) out.defaultIp = e.ipParDefaut;
    const optionsAvant = role === 'controller' || e.generique;
    if (optionsAvant) out.options = opts;
    out.groups = groups;
    if (pilote) out.axis = axeDe(kind, pilote, e);
    if (!optionsAvant && opts.length) out.options = opts;
    if (pilote?.signaux) out.signals = pilote.signaux.map((s) => traduire(s, SCHEMA.signal));
    if (pilote) out.sections = sectionsAxe(pilote, kind, e, chemin);

    // Un axe doit avoir toutes ses bornes de vitesse (sinon le générateur prendrait des valeurs de repli).
    if (out.axis) {
      for (const k of ['speedMin', 'speedMax', 'defaultSpeed', 'jogSpeedMin', 'jogSpeedMax', 'defaultJogSpeed']) {
        if (!Number.isInteger(out.axis[k])) err(e0, undefined, chemin, `borne « ${k} » de l'axe manquante : à donner dans le pilote « ${kind} » ou dans le modèle (vitesseMax, vitesseJogMax...).`);
      }
    }

    // Annexes (jamais dans CATALOG).
    const bornes = {};
    const rampeMax = e.rampeMaxMs ?? pilote?.rampeMaxMs;
    if (rampeMax !== undefined) bornes.rampeMaxMs = rampeMax;
    if (pilote?.rampeParDefautMs !== undefined) bornes.rampeParDefautMs = pilote.rampeParDefautMs;
    if (modeles.length) bornes.courantCreteMax = Object.fromEntries(modeles.map(([m, d]) => [m, d?.courantCreteMax]));
    const adresses = e.adresses !== undefined ? lirePlage(e.adresses) : null;
    if (adresses && adresses.some((a) => a < 1 || a > 247)) err(e0, 'adresses', `${chemin}.adresses`, 'adresses RS485 entre 1 et 247.');
    equipements[type] = {
      section,
      ligne: ligneDe(src[section], type),
      fichierMateriel: e.fichierMateriel ?? null,
      ...(e.marque ? { marque: e.marque } : {}),
      ...(e.envPlatformIO ? { envPlatformIO: e.envPlatformIO } : {}),
      ...(e.ethernet ? { ethernet: true } : {}),
      ...(e.horloge ? { horloge: e.horloge } : {}),
      ...(e.logicielRequis ? { logicielRequis: e.logicielRequis } : {}),
      ...(e.formatSerie ? { formatSerie: e.formatSerie } : {}),
      ...(adresses ? { adresses } : {}),
      bornes,
      voies: annexeVoies,
    };
    catalog[type] = out;
  }

  for (const section of SECTIONS_EQUIPEMENTS) {
    const s = src[section];
    if (s === undefined) continue;
    if (!estObjet(s)) {
      err(src, section, section, `section « ${section} » : objet { … } attendu.`);
      continue;
    }
    for (const [type, e] of Object.entries(s)) {
      if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(type)) err(s, type, `${section}.${type}`, `clé d'équipement « ${type} » invalide : lettres, chiffres et _ seulement (elle sert de nom de fichier et d'identifiant).`);
      entree(type, e, section);
    }
  }

  // --- écran d'axe par défaut ------------------------------------------------------------------------
  const ecranAxe = { hmi: {}, refAliases: {} };
  if (gabarits.ecranAxe !== undefined) {
    const g = gabarits.ecranAxe;
    if (!estObjet(g)) err(gabarits, 'ecranAxe', 'gabarits.ecranAxe', 'objet { "ihm": …, "aliasEcran": … } attendu.');
    else {
      for (const k of Object.keys(g)) if (!['ihm', 'aliasEcran'].includes(k)) err(g, k, `gabarits.ecranAxe.${k}`, `clé inconnue « ${k} » (attendu : ihm, aliasEcran).`);
      if (g.ihm) {
        controler(g.ihm, SCHEMA.ihm, 'gabarits.ecranAxe.ihm', { source: g });
        ecranAxe.hmi = traduire(g.ihm, SCHEMA.ihm);
      }
      if (g.aliasEcran) ecranAxe.refAliases = { ...g.aliasEcran };
    }
  }

  // --- sites -----------------------------------------------------------------------------------------
  const sites = estObjet(src.sites) ? src.sites : {};
  const choixVitesses = gabarits.options?.rs485Speed?.choix || [];
  const vitessesPermises = choixVitesses.map((c) => (typeof c === 'string' ? c : c?.valeur)).filter((v) => v !== '');
  const formatsPermis = (gabarits.options?.rs485Config?.choix || []).map((c) => (typeof c === 'string' ? c : c?.valeur)).filter((v) => v !== '');
  for (const [nom, site] of Object.entries(sites)) {
    const cs = `sites.${nom}`;
    if (!/^[a-z0-9][a-z0-9-]*$/.test(nom)) err(sites, nom, cs, `nom de site « ${nom} » : minuscules, chiffres et - seulement.`);
    if (!controler(site, SCHEMA.site, cs, { source: sites })) continue;
    if (site.reseau) controler(site.reseau, SCHEMA.reseau, `${cs}.reseau`, { source: site });
    const ips = new Map();
    const adressesBus = new Map();
    for (const [app, a] of Object.entries(estObjet(site.appareils) ? site.appareils : {})) {
      const c = `${cs}.appareils.${app}`;
      if (!/^[A-Za-z0-9][A-Za-z0-9_-]*$/.test(app)) err(site.appareils, app, c, `nom d'appareil « ${app} » : lettres, chiffres, - et _ seulement.`);
      if (!controler(a, SCHEMA.appareil, c, { source: site.appareils })) continue;
      const entry = a.type ? catalog[a.type] : null;
      if (a.type && !entry) err(a, 'type', `${c}.type`, `type « ${a.type} » inconnu${suggestion(a.type, Object.keys(catalog))}.`);
      if (a.logiciel) {
        controler(a.logiciel, SCHEMA.logicielAppareil, `${c}.logiciel`, { source: a });
        if (a.logiciel.nom && !logicielsSrc[a.logiciel.nom]) err(a.logiciel, 'nom', `${c}.logiciel.nom`, `logiciel « ${a.logiciel.nom} » absent de la section « logiciels »${suggestion(String(a.logiciel.nom), Object.keys(logicielsSrc))}.`);
        if (a.modbusTcp && logicielsSrc[a.logiciel.nom]?.serveurModbusTcp === false) {
          err(a, 'modbusTcp', `${c}.modbusTcp`, `${a.logiciel.nom} n'a pas de serveur Modbus TCP (logiciels.${a.logiciel.nom}) : "modbusTcp" doit valoir null.`);
        }
      }
      const raison = entry && incompatibiliteLogiciel(equipements[a.type]?.logicielRequis, a.logiciel);
      if (raison) err(a, 'type', `${c}.type`, `le profil ${a.type} exige ${raison.exige} ; logiciel relevé : ${texteLogiciel(a.logiciel)}.${raison.sinon ? ' ' + raison.sinon : ''}`);
      if (a.modbusTcp) {
        controler(a.modbusTcp, SCHEMA.modbusTcp, `${c}.modbusTcp`, { source: a });
        const { port, unite } = a.modbusTcp;
        if (Number.isInteger(port) && (port < 1 || port > 65535)) err(a.modbusTcp, 'port', `${c}.modbusTcp.port`, 'port entre 1 et 65535.');
        if (Number.isInteger(unite) && (unite < 0 || unite > 255)) err(a.modbusTcp, 'unite', `${c}.modbusTcp.unite`, "n° d'unité entre 0 et 255.");
        if (!a.ip) err(a, 'modbusTcp', `${c}.modbusTcp`, '"modbusTcp" sans "ip".');
      }
      if (a.rs485) {
        controler(a.rs485, SCHEMA.rs485, `${c}.rs485`, { source: a });
        const ad = a.rs485.adresse;
        if (Number.isInteger(ad) && (ad < 1 || ad > 247)) err(a.rs485, 'adresse', `${c}.rs485.adresse`, 'adresse RS485 entre 1 et 247.');
        else if (adressesBus.has(ad)) err(a.rs485, 'adresse', `${c}.rs485.adresse`, `adresse RS485 ${ad} déjà prise par « ${adressesBus.get(ad)} ».`);
        else adressesBus.set(ad, app);
        const plage = a.type ? equipements[a.type]?.adresses : null;
        if (plage && Number.isInteger(ad) && !plage.includes(ad)) err(a.rs485, 'adresse', `${c}.rs485.adresse`, `${a.type} accepte les adresses ${plage[0]} à ${plage[plage.length - 1]}.`);
      }
      if (a.ethernet) {
        controler(a.ethernet, SCHEMA.ethernet, `${c}.ethernet`, { source: a });
        if (entry && entry.role === 'controller' && !entry.ethernet) err(a, 'ethernet', `${c}.ethernet`, `${a.type} n'a pas de port Ethernet utilisable.`);
      }
      if (a.bus) {
        controler(a.bus, SCHEMA.bus, `${c}.bus`, { source: a });
        if (a.bus.vitesse != null && !vitessesPermises.includes(String(a.bus.vitesse))) {
          err(a.bus, 'vitesse', `${c}.bus.vitesse`, `vitesse ${a.bus.vitesse} absente des choix (${vitessesPermises.join(', ')}) : ajoutez-la à gabarits.options.rs485Speed.choix.`);
        }
        if (a.bus.format != null && !formatsPermis.includes(a.bus.format)) err(a.bus, 'format', `${c}.bus.format`, `format « ${a.bus.format} » absent des choix (${formatsPermis.join(', ')}).`);
      }
      if ((a.ethernet || a.bus) && entry && entry.role !== 'controller') err(a, a.ethernet ? 'ethernet' : 'bus', c, '"ethernet" et "bus" ne concernent que l\'automate.');
      for (const [cle, ip] of [['ip', a.ip], ['ethernet', a.ethernet?.ip]]) {
        if (!ip || !IPV4.test(ip)) continue;
        if (ips.has(ip)) err(a, cle, c, `adresse IP ${ip} déjà prise par « ${ips.get(ip)} ».`);
        else ips.set(ip, app);
        if (site.reseau?.sousReseau && !ipDansSousReseau(ip, site.reseau.sousReseau)) att(a, cle, c, `${ip} hors du sous-réseau ${site.reseau.sousReseau} du site.`);
      }
    }
  }

  // --- alias -----------------------------------------------------------------------------------------
  const aliasSrc = estObjet(src.alias) ? src.alias : {};
  const alias = {};
  for (const t of ALIAS_TABLES) alias[t] = estObjet(aliasSrc[t]) ? aliasSrc[t] : {};
  for (const k of Object.keys(aliasSrc)) if (!ALIAS_TABLES.includes(k)) err(aliasSrc, k, `alias.${k}`, `table d'alias inconnue « ${k} »${suggestion(k, ALIAS_TABLES)}.`);
  for (const [ancien, nouveau] of Object.entries(alias.types)) {
    if (!catalog[nouveau]) err(alias.types, ancien, `alias.types.${ancien}`, `alias vers « ${nouveau} », type inconnu.`);
    if (catalog[ancien]) err(alias.types, ancien, `alias.types.${ancien}`, `« ${ancien} » existe encore : un alias ne peut pas masquer un type existant.`);
    if (alias.types[nouveau]) err(alias.types, ancien, `alias.types.${ancien}`, `alias en chaîne (${ancien} → ${nouveau} → ${alias.types[nouveau]}) : pointez directement vers le nom actuel.`);
  }
  for (const t of ['voies', 'groupes', 'options']) {
    for (const [type, table] of Object.entries(alias[t])) {
      if (!(t === 'voies' && type === '*') && !catalog[type]) err(alias[t], type, `alias.${t}.${type}`, `type « ${type} » inconnu${suggestion(type, Object.keys(catalog))}.`);
      if (!estObjet(table)) err(alias[t], type, `alias.${t}.${type}`, 'objet { "ancien": "nouveau" } attendu.');
    }
  }
  for (const [ancien, nouveau] of Object.entries(alias.sites)) {
    if (!sites[nouveau]) err(alias.sites, ancien, `alias.sites.${ancien}`, `alias vers le site « ${nouveau} », inconnu.`);
  }
  for (const [site, table] of Object.entries(alias.appareils)) {
    if (!sites[site]) err(alias.appareils, site, `alias.appareils.${site}`, `site « ${site} » inconnu.`);
    else for (const [ancien, nouveau] of Object.entries(estObjet(table) ? table : {})) if (!sites[site].appareils?.[nouveau]) err(table, ancien, `alias.appareils.${site}.${ancien}`, `appareil « ${nouveau} » inconnu dans le site « ${site} ».`);
  }

  // --- écarts connus ---------------------------------------------------------------------------------
  const ecartsConnus = estObjet(src.ecartsConnus) ? src.ecartsConnus : {};
  for (const [id, e] of Object.entries(ecartsConnus)) {
    if (!estObjet(e) || typeof e.texte !== 'string' || !('controle' in e) || (e.controle !== null && typeof e.controle !== 'string')) {
      err(ecartsConnus, id, `ecartsConnus.${id}`, '{ "controle": "<chemin>" ou null, "texte": "…" } attendu.');
    }
  }

  // --- noms réservés : classes du firmware + classes et structures de nœuds des pilotes ---------------
  const classesReservees = [...new Set([...(firmware.classesReservees || []), ...Object.values(pilotes).flatMap((p) => [p.classeCpp, p.structureNoeuds])])];

  return {
    catalog,
    transportOptions: transport,
    equipements,
    pilotes,
    ecranAxe,
    firmware: { ...firmware, limites, typesSection, classesReservees, nomsProjetReserves: firmware.nomsProjetReserves || [] },
    compilation: { ...compilation, envs },
    logiciels: logicielsSrc,
    sites,
    alias,
    ecartsConnus,
    revision: src.revision || '',
    erreurs,
    avertissements,
    source: src,
  };
}

export function formaterErreurs(r, fichier = 'infrastructure.jsonc', max = 20) {
  const lignes = r.erreurs.slice(0, max).map((e) => `  ${fichier}${e.ligne ? `:${e.ligne}${e.colonne ? `:${e.colonne}` : ''}` : ''}  ${e.chemin ? `[${e.chemin}] ` : ''}${e.message}`);
  if (r.erreurs.length > max) lignes.push(`  … et ${r.erreurs.length - max} autre(s).`);
  return `Référentiel d'infrastructure invalide : ${r.erreurs.length} erreur(s)\n${lignes.join('\n')}`;
}

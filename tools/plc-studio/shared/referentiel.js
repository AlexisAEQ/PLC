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
// Clés de secrets (mot de passe, identifiant WiFi, jeton), où qu'elles soient dans le nom.
const SECRET = /(pwd|passw(or)?d|mot_?de_?passe|mdp|ssid|secret|token|api_?key)/i;

// Limites du firmware lues par le code (firmware.limites.<nom>) : toutes obligatoires.
export const LIMITES_REQUISES = [
  'octetsNomNoeud', 'octetsNomEcran', 'octetsNomSection', 'noeudsParSection', 'noeudsAvantAlerte', 'octetsMessageEtat',
  'octetsUrlOta', 'caracteresNomProjet', 'octetsCheminParametres', 'reseauxWifi', 'octetsChaineWifi',
  'esclavesModbusSuivis', 'equipementsModbusTcp', 'adressesIpModbusTcp', 'axesPasAPas',
];

// Clés d'options et de voies que le CODE lit par leur nom (cpp.js, layout.js, validate.js) :
// les renommer demande de modifier le code ; un alias ne suffit pas, il est donc refusé.
export const CLES_LUES_PAR_LE_CODE = {
  options: ['transport', 'ip', 'port', 'rtc', 'rs485Speed', 'rs485Config', 'pulsesPerUnit', 'stepsPerUnit', 'maxRange', 'accelTime', 'model',
    'torqueRefCurrent', 'homePolicy', 'invertJog', 'acceleration', 'homingDirection', 'homingSpeed', 'homingBackoff', 'invertDirection',
    'enableActiveLow', 'oneBased', 'coilOutStart', 'coilInStart', 'regOutStart', 'regInStart', 'inputRegStart'],
  groupes: ['q', 'i', 'w', 'r', 'ir', 'buzzer'],
};
export const IPV4 = /^(25[0-5]|2[0-4]\d|1?\d?\d)(\.(25[0-5]|2[0-4]\d|1?\d?\d)){3}$/;
const estObjet = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
// Recherche d'un nom dans une table sans remonter la chaîne de prototypes (« toString »...).
export const possede = (table, cle) => table !== null && typeof table === 'object' && (typeof cle === 'string' || typeof cle === 'number') && Object.prototype.hasOwnProperty.call(table, cle);
// Valeur d'un « choix » d'option : "9600", 9600 ou { "valeur": …, "libelle": … }.
const valeurChoix = (c) => (typeof c === 'string' || typeof c === 'number' ? String(c) : estObjet(c) && c.valeur !== undefined ? String(c.valeur) : null);

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
    bus: { type: 'objetOuNull' },
    remarque: { type: T.texte },
  },
  logicielAppareil: { nom: { type: T.texte, requis: true }, version: { type: 'version' }, releveLe: { type: 'date' } },
  modbusTcp: { port: { type: T.entier, requis: true }, unite: { type: T.entier, requis: true } },
  rs485: { adresse: { type: T.entier, requis: true } },
  ethernet: { ip: { type: 'ip', requis: true }, masque: { type: 'ip', requis: true }, passerelle: { type: 'ip', requis: true }, dns: { type: 'ip' } },
  bus: { vitesse: { type: 'entierOuNull' }, format: { type: 'texteOuNull' } },
  firmware: {
    version: { type: T.texte },
    limites: { type: T.objet, requis: true },
    typesSection: { type: T.objet, requis: true },
    pucesExtension: { type: T.liste },
    pucesHorloge: { type: T.objet },
    classesReservees: { type: T.liste },
    nomsProjetReserves: { type: T.liste },
  },
  compilation: { envParDefaut: { type: T.texte }, envsIgnores: { type: T.liste }, envs: { type: T.objet, requis: true }, arduinoMinPourEthernet: { type: T.entier } },
  gabarits: { automate: { type: T.objet }, voies: { type: T.objet }, options: { type: T.objet }, sections: { type: T.objet }, ecranAxe: { type: T.objet } },
  puceHorloge: { autoDetectee: { type: T.bool, requis: true } },
  ecart: { controle: { type: 'cheminOuListe', requis: true }, texte: { type: T.texte, requis: true }, motif: { type: T.texte } },
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
  if (!requis || !Array.isArray(requis.noms) || !logiciel?.nom) return null;
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
  const m = /^([\d.]+)\/(\d{1,2})$/.exec(typeof cidr === 'string' ? cidr : '');
  if (!m || typeof ip !== 'string' || !IPV4.test(ip) || !IPV4.test(m[1])) return true;
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
export function chargerReferentiel(texte, options = {}) {
  const erreurs = [];
  const avertissements = [];
  let r;
  try {
    r = charger(texte, options, erreurs, avertissements);
  } catch (e) {
    if (options.relancer) throw e; // tests : voir l'exception d'origine
    // Filet de sécurité : une valeur d'un type inattendu que les contrôles n'ont pas arrêtée.
    erreurs.push({ ligne: null, chemin: '', message: `erreur interne du chargeur (${e.message}) : une valeur a sans doute un type inattendu ; corrigez d'abord les autres erreurs, sinon signalez ce cas.` });
    r = { erreurs, avertissements };
  }
  const unique = (liste) => [...new Map(liste.map((e) => [`${e.ligne}|${e.chemin}|${e.message}`, e])).values()];
  r.erreurs = unique(r.erreurs);
  r.avertissements = unique(r.avertissements);
  return r;
}

function charger(texte, { kinds = KINDS_GERES } = {}, erreurs, avertissements) {
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
  // Tables nettoyées des entrées qui ne sont pas des objets (signalées à part) : le reste du
  // chargeur peut les lire sans risque.
  const objetsSeuls = (t) => (estObjet(t) ? Object.fromEntries(Object.entries(t).filter(([, v]) => estObjet(v))) : {});
  const typesSection = objetsSeuls(firmware.typesSection);
  const gabarits = estObjet(src.gabarits) ? src.gabarits : {};
  const pilotesSrc = estObjet(src.pilotes) ? src.pilotes : {};
  const compilation = estObjet(src.compilation) ? src.compilation : {};
  const envs = objetsSeuls(compilation.envs);
  const pucesHorloge = estObjet(firmware.pucesHorloge) ? firmware.pucesHorloge : {};
  const logicielsSrc = estObjet(src.logiciels) ? src.logiciels : {};

  // --- contrôle de clés et de types -----------------------------------------------------------
  // partiel : gabarit (clés obligatoires fournies à l'usage) ; seulement : clés écrites à cet
  // endroit (les autres viennent d'un gabarit, contrôlé une fois pour lui-même).
  function controler(obj, schema, chemin, { parametres = [], source, partiel = false, seulement = null } = {}) {
    const errS = (p, k, c, m) => err(p, k, c, m, source);
    if (!estObjet(obj)) {
      errS(source, undefined, chemin, `${chemin} : objet { … } attendu.`);
      return false;
    }
    const permis = Object.keys(schema);
    for (const k of Object.keys(obj)) {
      if (parametres.includes(k) || (seulement && !seulement.includes(k))) continue;
      if (k === 'gabarit') {
        if (typeof obj[k] !== 'string') errS(obj, k, `${chemin}.gabarit`, `« gabarit » : nom de gabarit entre guillemets attendu (trouvé ${JSON.stringify(obj[k])}).`);
        continue;
      }
      if (!possede(schema, k)) {
        errS(obj, k, `${chemin}.${k}`, `clé inconnue « ${k} »${suggestion(k, permis)}. Clés possibles : ${permis.join(', ')}.`);
        continue;
      }
      const v = obj[k];
      if (v === OBLIGATOIRE) {
        errS(obj, k, `${chemin}.${k}`, `« ${k} » : "<obligatoire>" ne s'écrit que dans un gabarit (valeur que chaque utilisateur doit fournir).`);
        continue;
      }
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
      else if (t === 'ip' && (typeof v !== 'string' || !IPV4.test(v))) ko('une adresse IPv4 entre guillemets (ex. "192.168.10.50")');
      else if (t === 'cidr' && (typeof v !== 'string' || !/^([\d.]+)\/(\d|[12]\d|3[0-2])$/.test(v) || !IPV4.test(v.split('/')[0]))) ko('un sous-réseau « 192.168.10.0/24 »');
      else if (t === 'version' && (typeof v !== 'string' || !/^\d+(\.\d+)*$/.test(v))) ko('une version entre guillemets, chiffres séparés par des points (ex. "4.5.13")');
      else if (t === 'date' && (typeof v !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(v))) ko('une date "AAAA-MM-JJ"');
      else if (t === 'plage' && !lirePlage(v)) ko('une plage d\'ids : "1-8", "1-4, 9" ou "9"');
      else if (t === 'cheminOuListe' && v !== null && typeof v !== 'string' && !(Array.isArray(v) && v.every((x) => typeof x === 'string'))) ko('un chemin "section.cle…", une liste de chemins, ou null');
      else if (t === 'typeSection' && !possede(typesSection, v)) {
        errS(obj, k, `${chemin}.${k}`, `type de section « ${v} » inconnu du firmware${suggestion(String(v), Object.keys(typesSection))} (voir firmware.typesSection).`);
      }
    }
    if (!partiel) for (const [k, d] of Object.entries(schema)) if (d.requis && !possede(obj, k)) errS(obj, undefined, chemin, `« ${k} » obligatoire.`);
    return true;
  }
  const listeDeTextes = (v) => Array.isArray(v) && v.every((x) => typeof x === 'string');

  // --- firmware et compilation -----------------------------------------------------------------
  if (estObjet(src.firmware)) controler(src.firmware, SCHEMA.firmware, 'firmware', { source: src });
  if (estObjet(src.compilation)) controler(src.compilation, SCHEMA.compilation, 'compilation', { source: src });
  if (estObjet(src.gabarits)) controler(src.gabarits, SCHEMA.gabarits, 'gabarits', { source: src });
  for (const k of ['sites', 'logiciels', 'alias', 'ecartsConnus']) if (src[k] !== undefined && !estObjet(src[k])) err(src, k, k, `section « ${k} » : objet { … } attendu.`);
  if (firmware.version !== undefined && !/^\d+\.\d+\.\d+$/.test(String(firmware.version))) err(firmware, 'version', 'firmware.version', '« version » : x.y.z attendu (BORNE_UNIVERSELLE_VERSION).');
  const limites = estObjet(firmware.limites) ? firmware.limites : {};
  for (const [nom, l] of Object.entries(limites)) controler(l, SCHEMA.limite, `firmware.limites.${nom}`, { source: limites });
  for (const n of LIMITES_REQUISES) {
    if (!possede(limites, n)) err(firmware, 'limites', 'firmware.limites', `limite « ${n} » manquante : PLC Studio la lit${suggestion(n, Object.keys(limites).filter((k) => !LIMITES_REQUISES.includes(k)))}.`);
  }
  for (const [nom, t] of Object.entries(estObjet(firmware.typesSection) ? firmware.typesSection : {})) controler(t, SCHEMA.typeSection, `firmware.typesSection.${nom}`, { source: firmware.typesSection });
  for (const [nom, x] of Object.entries(pucesHorloge)) controler(x, SCHEMA.puceHorloge, `firmware.pucesHorloge.${nom}`, { source: pucesHorloge });
  for (const [nom, env] of Object.entries(estObjet(compilation.envs) ? compilation.envs : {})) controler(env, SCHEMA.env, `compilation.envs.${nom}`, { source: compilation.envs });
  if (compilation.envParDefaut !== undefined && !possede(envs, compilation.envParDefaut)) err(compilation, 'envParDefaut', 'compilation.envParDefaut', `environnement « ${compilation.envParDefaut} » absent de compilation.envs.`);
  for (const k of ['classesReservees', 'nomsProjetReserves', 'pucesExtension']) if (firmware[k] !== undefined && !listeDeTextes(firmware[k])) err(firmware, k, `firmware.${k}`, `« ${k} » : liste de textes attendue.`);
  if (compilation.envsIgnores !== undefined && !listeDeTextes(compilation.envsIgnores)) err(compilation, 'envsIgnores', 'compilation.envsIgnores', '« envsIgnores » : liste de textes attendue.');
  for (const [nom, l] of Object.entries(logicielsSrc)) controler(l, SCHEMA.logiciel, `logiciels.${nom}`, { source: logicielsSrc });

  // Gabarits contrôlés une fois pour eux-mêmes (leurs erreurs citent leur propre ligne) ; à
  // l'usage, seules les clés écrites localement sont contrôlées.
  for (const [famille, schema] of [['voies', SCHEMA.voie], ['options', SCHEMA.option], ['sections', SCHEMA.section]]) {
    const table = gabarits[famille];
    if (!estObjet(table)) continue;
    for (const [nom, g] of Object.entries(table)) {
      if (!estObjet(g)) {
        err(table, nom, `gabarits.${famille}.${nom}`, 'gabarit : objet { … } attendu.');
        continue;
      }
      const params = Object.entries(g).filter(([, v]) => v === OBLIGATOIRE).map(([k]) => k);
      controler(g, schema, `gabarits.${famille}.${nom}`, { parametres: params, source: table, partiel: true });
    }
  }
  if (estObjet(gabarits.automate)) {
    const params = Object.entries(gabarits.automate).filter(([, v]) => v === OBLIGATOIRE).map(([k]) => k);
    controler(gabarits.automate, { ...SCHEMA.entreeCommune, ...SCHEMA.automate }, 'gabarits.automate', { parametres: params, source: gabarits, partiel: true });
  }

  // --- gabarits ----------------------------------------------------------------------------------
  // Résout "gabarit" (récursif). Retourne { valeur, params, nomRacine } ou null.
  function resoudre(obj, famille, chemin, vus = []) {
    if (typeof obj === 'string') obj = { gabarit: obj };
    if (!estObjet(obj) || !obj.gabarit) return { valeur: obj, params: [], nomRacine: null };
    const nom = obj.gabarit;
    const table = estObjet(gabarits[famille]) ? gabarits[famille] : {};
    if (typeof nom !== 'string' || !possede(table, nom) || !estObjet(table[nom])) {
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
    if (!estObjet(obj)) return;
    for (const [k, v] of Object.entries(obj)) {
      if (v === OBLIGATOIRE) err(source, undefined, chemin, `« ${k} » obligatoire${nomGabarit ? ` (gabarit « ${nomGabarit} »)` : ''}.`);
    }
  }

  // Valeurs calculées par le chargeur : où les fournir.
  const CALCULEES = { courantCreteMaxA: 'ajoutez "modelesVariateur": { "<modèle>": { "courantCreteMax": <0,1 A> } }' };

  // Remplace les {cle} des textes. Un texte qui n'est QUE « {cle} » prend la valeur telle quelle.
  // origine : { chemin, e0 } de l'équipement qui utilise un texte venu d'un pilote ou d'un gabarit.
  function remplir(v, contextes, chemin, source, origine) {
    if (typeof v === 'string') {
      const seul = /^\{(\w+)\}$/.exec(v);
      const cherche = (k) => {
        for (const c of contextes) if (c && possede(c, k) && c[k] !== OBLIGATOIRE && c[k] !== undefined) return c[k];
        const conseil = CALCULEES[k] || `ajoutez « ${k} » à l'entrée`;
        if (origine && origine.chemin !== chemin) err(origine.e0, undefined, origine.chemin, `« {${k}} » sans valeur (texte de ${chemin}) : ${conseil}.`);
        else err(source, undefined, chemin, `« {${k}} » sans valeur : ${conseil}.`);
        return '';
      };
      if (seul) return cherche(seul[1]);
      return v.replace(/\{(\w+)\}/g, (_, k) => String(cherche(k)));
    }
    if (Array.isArray(v)) return v.map((x) => remplir(x, contextes, chemin, source, origine));
    if (estObjet(v)) return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, remplir(x, contextes, chemin, source, origine)]));
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
  function voie(src0, chemin, entree, annexeVoies, liste, i, origine) {
    if (!estObjet(src0)) {
      err(liste, i, chemin, 'voie : objet { … } attendu.');
      return null;
    }
    const r = resoudre(src0, 'voies', chemin);
    if (!r) return null;
    const g = r.valeur;
    exigerFournis(g, chemin, src0, r.nomRacine);
    const locales = Object.keys(src0).filter((k) => k !== 'gabarit');
    controler(g, SCHEMA.voie, chemin, { parametres: r.params, source: src0, seulement: r.nomRacine ? [...locales, 'gabarit'] : null });
    const v = remplir(g, [g, entree], chemin, src0, origine);
    let ids = null;
    let noms = null;
    if (Array.isArray(v.broches) && (v.ids !== undefined || v.noms !== undefined)) {
      err(src0, 'broches', `${chemin}.broches`, '« broches » exclut « ids » et « noms » (ids 1..n et noms GPIO<n> en sont déduits).');
    }
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
    if (v.banc && estObjet(v.banc)) {
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
  function option(src0, chemin, contexteEntree, liste, i, origine) {
    if (!estObjet(src0) && typeof src0 !== 'string') {
      err(liste, i, chemin, 'option : objet { … } ou nom de gabarit attendu.');
      return null;
    }
    const r = resoudre(src0, 'options', chemin);
    if (!r) return null;
    const o = { ...r.valeur };
    if (r.nomRacine && !possede(o, 'cle')) o.cle = r.nomRacine; // la clé de l'option = nom du gabarit
    const source = typeof src0 === 'string' ? liste ?? undefined : src0;
    exigerFournis(o, chemin, typeof src0 === 'string' ? liste : src0, r.nomRacine);
    const locales = typeof src0 === 'string' ? [] : Object.keys(src0).filter((k) => k !== 'gabarit');
    controler(o, SCHEMA.option, chemin, { source, parametres: r.params, seulement: r.nomRacine ? [...locales, 'gabarit'] : null });
    const v = remplir(o, [o, contexteEntree], chemin, typeof src0 === 'string' ? liste : src0, origine);
    const out = { key: v.cle, label: v.libelle, type: v.type, default: v.defaut };
    if (Array.isArray(v.choix)) {
      const listeChoix = Array.isArray(o.choix) ? o.choix : v.choix; // positions dans le fichier
      out.choices = v.choix
        .map((c, j) => {
          const valeur = valeurChoix(c);
          if (valeur !== null && (typeof c !== 'object' || (typeof c.libelle === 'string' && Object.keys(c).every((k) => k === 'valeur' || k === 'libelle')))) {
            return { value: valeur, label: typeof c === 'object' ? c.libelle : valeur };
          }
          err(listeChoix, j, `${chemin}.choix[${j}]`, `choix : "texte", nombre ou { "valeur": …, "libelle": "…" } attendu (trouvé ${JSON.stringify(c)}).`, source);
          return null;
        })
        .filter(Boolean);
      if (!out.choices.some((c) => c.value === out.default)) err(src0, undefined, chemin, `option « ${out.key} » : la valeur par défaut ${JSON.stringify(out.default)} n'est pas dans « choix ».`, liste);
    } else if (v.type === 'select') err(src0, undefined, chemin, `option « ${out.key} » de type select sans « choix ».`, liste);
    if (v.aide !== undefined) out.help = v.aide;
    return out;
  }
  const options = (liste, chemin, ctx, origine) => (Array.isArray(liste) ? liste : []).map((o, i) => option(o, `${chemin}[${i}]`, ctx, liste, i, origine)).filter(Boolean);

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
      if (!estObjet(n)) {
        err(listeSource, i, c, 'nœud : objet { "id": …, "nom": …, "champ": …, "cpp": … } attendu.', src0);
        continue;
      }
      if (!controler(n, SCHEMA.noeud, c, { source: n0 ?? listeSource ?? src0 })) continue;
      if (n.id <= dernier) err(listeSource, i, c, `id ${n.id} : les ids d'une section doivent être croissants et distincts.`, src0);
      dernier = n.id;
      if (n.id > 255) err(listeSource, i, c, `id ${n.id} > 255.`, src0);
      const attendu = possede(typesSection, typeSec) ? typesSection[typeSec].classe : undefined;
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
    for (const [i, s0] of pilote.sections.entries()) {
      const c = `pilotes.${kind}.sections[${i}]`;
      if (!estObjet(s0) && typeof s0 !== 'string') {
        err(pilote.sections, i, c, 'section : objet { … } ou nom de gabarit attendu.');
        continue;
      }
      const r = resoudre(s0, 'sections', c);
      if (!r) continue;
      exigerFournis(r.valeur, c, s0, r.nomRacine);
      const locales = estObjet(s0) ? Object.keys(s0).filter((k) => k !== 'gabarit') : [];
      controler(r.valeur, SCHEMA.section, c, { parametres: r.params, source: estObjet(s0) ? s0 : pilote.sections, seulement: r.nomRacine ? [...locales, 'gabarit'] : null });
      const s = remplir(r.valeur, [r.valeur], c, s0);
      let liste = s.noeuds;
      let listeSource = estObjet(s0) && Array.isArray(s0.noeuds) ? s0.noeuds : r.valeur.noeuds;
      if (liste === 'par modèle') {
        if (!possede(parModele, s.suffixe)) {
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
        const parModeleSections = pilote.sections.filter((s) => estObjet(s) && s.noeuds === 'par modèle').map((s) => s.suffixe);
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
    if (estObjet(p.ihm)) controler(p.ihm, SCHEMA.ihm, `${c}.ihm`, { source: p });
    const signaux = Array.isArray(p.signaux) ? p.signaux : [];
    for (const [i, x] of signaux.entries()) {
      if (estObjet(x)) controler(x, SCHEMA.signal, `${c}.signaux[${i}]`, { source: signaux });
      else err(signaux, i, `${c}.signaux[${i}]`, 'signal : objet { … } attendu.');
    }
    // Copie assainie : un champ d'un type faux (déjà signalé) ne doit pas faire planter la suite.
    const sain = { ...p, sections: Array.isArray(p.sections) ? p.sections : [] };
    if (p.signaux !== undefined) sain.signaux = signaux.filter(estObjet);
    if (p.familles !== undefined) sain.familles = Array.isArray(p.familles) ? p.familles : [];
    if (p.ihm !== undefined && !estObjet(p.ihm)) sain.ihm = {};
    if (p.aliasEcran !== undefined && !estObjet(p.aliasEcran)) sain.aliasEcran = {};
    pilotes[kind] = sain;
  }
  for (const k of kinds) if (!possede(pilotesSrc, k)) erreurs.push({ ligne: ligneDe(src, 'pilotes'), chemin: 'pilotes', message: `le pilote « ${k} », géré par le code, n'est pas décrit.` });

  function axeDe(kind, p, entree) {
    const out = { kind };
    for (const [k, v] of Object.entries(p)) {
      const d = SCHEMA.pilote[k];
      if (!d?.en) continue;
      out[d.en] = k === 'ihm' ? traduire(estObjet(v) ? v : {}, SCHEMA.ihm) : estObjet(v) ? { ...v } : v;
    }
    // Clés du modèle, dans son ordre (sémantique de { ...PILOTE, famille, ... }).
    const MODELE = { famille: 'family', vitesseMax: 'speedMax', vitesseJogMax: 'jogSpeedMax', boucleFermee: 'closedLoop', modelesVariateur: 'models', couple: 'torque', ihm: 'hmi' };
    for (const k of Object.keys(entree)) {
      if (!possede(MODELE, k)) continue;
      const v = entree[k];
      if (k === 'modelesVariateur') {
        if (estObjet(v)) out.models = Object.keys(v);
      } else if (k === 'ihm') {
        if (estObjet(v)) out.hmi = fusion(out.hmi || {}, traduire(v, SCHEMA.ihm));
      } else out[MODELE[k]] = v;
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
    if (possede(catalog, type)) {
      err(src[section], type, chemin, `« ${type} » existe déjà dans la section « ${equipements[type].section} » : une clé d'équipement est unique dans tout le fichier.`);
      return;
    }
    // Les automates héritent d'office de gabarits.automate.
    const e = section === 'automates' ? fusion(estObjet(gabarits.automate) ? gabarits.automate : {}, e0) : e0;
    const schema = { ...SCHEMA.entreeCommune, ...(section === 'automates' ? SCHEMA.automate : section === 'axes' ? SCHEMA.axe : SCHEMA.equipementModbus) };
    const paramsAutomate = section === 'automates' && estObjet(gabarits.automate) ? Object.keys(gabarits.automate).filter((k) => gabarits.automate[k] === OBLIGATOIRE) : [];
    if (section === 'automates') exigerFournis(e, chemin, e0, 'automate');
    controler(e, schema, chemin, { source: e0, parametres: paramsAutomate });
    const role = section === 'automates' ? 'controller' : section === 'axes' ? 'axis' : 'modbus-io';
    const kind = e.pilote;
    const pilote = section === 'axes' && possede(pilotes, kind) ? pilotes[kind] : null;
    const origine = { chemin, e0 };
    if (section === 'axes' && !pilote) {
      err(e0, 'pilote', `${chemin}.pilote`, `pilote « ${kind} » inconnu${suggestion(String(kind), Object.keys(pilotes))}. Pilotes : ${Object.keys(pilotes).join(', ')}.`);
      return;
    }
    if (Array.isArray(pilote?.familles) && !pilote.familles.includes(e.famille)) {
      err(e0, 'famille', `${chemin}.famille`, `famille « ${e.famille} » inconnue du pilote « ${kind} » (familles : ${pilote.familles.join(', ')}).`);
    }
    if (section === 'automates') {
      if (!possede(envs, e.envPlatformIO)) err(e0, 'envPlatformIO', `${chemin}.envPlatformIO`, `environnement « ${e.envPlatformIO} » absent de compilation.envs${suggestion(String(e.envPlatformIO), Object.keys(envs))}.`);
      if (e.horloge && !possede(pucesHorloge, e.horloge)) err(e0, 'horloge', `${chemin}.horloge`, `puce d'horloge « ${e.horloge} » inconnue du firmware${suggestion(String(e.horloge), Object.keys(pucesHorloge))} (firmware.pucesHorloge : ${Object.keys(pucesHorloge).join(', ')}).`);
      if (e.ethernet && possede(envs, e.envPlatformIO) && envs[e.envPlatformIO].arduino < (compilation.arduinoMinPourEthernet ?? 3)) {
        att(e0, 'ethernet', chemin, `Ethernet sur l'environnement « ${e.envPlatformIO} » (cœur Arduino ${envs[e.envPlatformIO].arduino}) : l'Ethernet ne sera pas compilé.`);
      }
    }
    if (e.logicielRequis) {
      controler(e.logicielRequis, SCHEMA.logicielRequis, `${chemin}.logicielRequis`, { source: e0 });
      for (const nom of Array.isArray(e.logicielRequis?.noms) ? e.logicielRequis.noms : []) {
        if (!possede(logicielsSrc, nom)) err(e.logicielRequis, 'noms', `${chemin}.logicielRequis.noms`, `logiciel « ${nom} » absent de la section « logiciels »${suggestion(String(nom), Object.keys(logicielsSrc))}.`);
      }
    }
    if (e.formatSerie !== undefined && estObjet(e.formatSerie)) controler(e.formatSerie, SCHEMA.formatSerie, `${chemin}.formatSerie`, { source: e0 });
    const modeles = estObjet(e.modelesVariateur) ? Object.entries(e.modelesVariateur) : [];
    for (const [m, d] of modeles) {
      if (!estObjet(d) || !Number.isInteger(d.courantCreteMax)) err(e.modelesVariateur, m, `${chemin}.modelesVariateur.${m}`, `modèle « ${m} » : { "courantCreteMax": <0,1 A> } attendu.`);
    }

    // Contexte des {…} : l'entrée et des valeurs calculées.
    const ctx = { ...e, courantCreteMaxA: modeles.length ? Math.max(...modeles.map(([, m]) => Number(m?.courantCreteMax) || 0)) / 10 : undefined };

    // Voies
    const annexeVoies = {};
    const groups = [];
    const listeVoies = Array.isArray(e.voies) ? e.voies : [];
    for (const [i, v0] of listeVoies.entries()) {
      const g = voie(v0, `${chemin}.voies[${i}]`, ctx, annexeVoies, listeVoies, i, origine);
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
    let opts = options(e.options || (pilote ? pilote.options : []), e.options ? `${chemin}.options` : `pilotes.${kind}.options`, ctx, origine);
    if (section === 'automates' && e.horloge) opts = [option('rtc', `${chemin}.horloge`, ctx, undefined, undefined, origine), ...opts].filter(Boolean);
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
    if (pilote?.signaux) out.signals = pilote.signaux.filter(estObjet).map((s) => traduire(s, SCHEMA.signal));
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
      if (g.ihm !== undefined && controler(g.ihm, SCHEMA.ihm, 'gabarits.ecranAxe.ihm', { source: g })) ecranAxe.hmi = traduire(g.ihm, SCHEMA.ihm);
      if (g.aliasEcran !== undefined) {
        if (estObjet(g.aliasEcran)) ecranAxe.refAliases = { ...g.aliasEcran };
        else err(g, 'aliasEcran', 'gabarits.ecranAxe.aliasEcran', 'objet { "alias": "champ" } attendu.');
      }
    }
  }

  // --- sites -----------------------------------------------------------------------------------------
  const sites = estObjet(src.sites) ? src.sites : {};
  const permis = (o) => (Array.isArray(o?.choix) ? o.choix.map(valeurChoix).filter((v) => v !== null && v !== '') : []);
  const vitessesPermises = permis(estObjet(gabarits.options) ? gabarits.options.rs485Speed : null);
  const formatsPermis = permis(estObjet(gabarits.options) ? gabarits.options.rs485Config : null);
  for (const [nom, site] of Object.entries(sites)) {
    const cs = `sites.${nom}`;
    if (!/^[a-z0-9][a-z0-9-]*$/.test(nom)) err(sites, nom, cs, `nom de site « ${nom} » : minuscules, chiffres et - seulement.`);
    if (!controler(site, SCHEMA.site, cs, { source: sites })) continue;
    if (site.reseau !== undefined) controler(site.reseau, SCHEMA.reseau, `${cs}.reseau`, { source: site });
    const ips = new Map();
    const adressesBus = new Map();
    for (const [app, a] of Object.entries(estObjet(site.appareils) ? site.appareils : {})) {
      const c = `${cs}.appareils.${app}`;
      if (!/^[A-Za-z0-9][A-Za-z0-9_-]*$/.test(app)) err(site.appareils, app, c, `nom d'appareil « ${app} » : lettres, chiffres, - et _ seulement.`);
      if (!controler(a, SCHEMA.appareil, c, { source: site.appareils })) continue;
      const entry = a.type !== undefined && possede(catalog, a.type) ? catalog[a.type] : null;
      if (a.type !== undefined && !entry) err(a, 'type', `${c}.type`, `type « ${a.type} » inconnu${suggestion(String(a.type), Object.keys(catalog))}.`);
      const logicielOk = estObjet(a.logiciel) && typeof a.logiciel.nom === 'string';
      if (a.logiciel !== undefined) {
        controler(a.logiciel, SCHEMA.logicielAppareil, `${c}.logiciel`, { source: a });
        if (logicielOk && !possede(logicielsSrc, a.logiciel.nom)) err(a.logiciel, 'nom', `${c}.logiciel.nom`, `logiciel « ${a.logiciel.nom} » absent de la section « logiciels »${suggestion(String(a.logiciel.nom), Object.keys(logicielsSrc))}.`);
        if (logicielOk && a.modbusTcp && possede(logicielsSrc, a.logiciel.nom) && logicielsSrc[a.logiciel.nom].serveurModbusTcp === false) {
          err(a, 'modbusTcp', `${c}.modbusTcp`, `${a.logiciel.nom} n'a pas de serveur Modbus TCP (logiciels.${a.logiciel.nom}) : "modbusTcp" doit valoir null.`);
        }
      }
      const requis = entry ? equipements[a.type]?.logicielRequis : null;
      const raison = logicielOk && incompatibiliteLogiciel(requis, a.logiciel);
      if (raison) err(a, 'type', `${c}.type`, `le profil ${a.type} exige ${raison.exige} ; logiciel relevé : ${texteLogiciel(a.logiciel)}.${raison.sinon ? ' ' + raison.sinon : ''}`);
      if (logicielOk && requis?.versionMin && !a.logiciel.version) att(a, 'logiciel', `${c}.logiciel`, `version de ${a.logiciel.nom} non relevée : le profil ${a.type} exige ${requis.versionMin} ou plus.`);
      const plage = entry ? equipements[a.type]?.adresses : null;
      if (estObjet(a.modbusTcp)) {
        controler(a.modbusTcp, SCHEMA.modbusTcp, `${c}.modbusTcp`, { source: a });
        const { port, unite } = a.modbusTcp;
        if (Number.isInteger(port) && (port < 1 || port > 65535)) err(a.modbusTcp, 'port', `${c}.modbusTcp.port`, 'port entre 1 et 65535.');
        if (Number.isInteger(unite) && (unite < 0 || unite > 255)) err(a.modbusTcp, 'unite', `${c}.modbusTcp.unite`, "n° d'unité entre 0 et 255.");
        else if (plage && Number.isInteger(unite) && !plage.includes(unite)) err(a.modbusTcp, 'unite', `${c}.modbusTcp.unite`, `${a.type} accepte les adresses ${plage[0]} à ${plage[plage.length - 1]} (unité = adresse RS485 du variateur derrière la passerelle).`);
        if (!a.ip) err(a, 'modbusTcp', `${c}.modbusTcp`, '"modbusTcp" sans "ip".');
      }
      if (a.rs485 !== undefined && controler(a.rs485, SCHEMA.rs485, `${c}.rs485`, { source: a })) {
        const ad = a.rs485.adresse;
        if (Number.isInteger(ad) && (ad < 1 || ad > 247)) err(a.rs485, 'adresse', `${c}.rs485.adresse`, 'adresse RS485 entre 1 et 247.');
        else if (adressesBus.has(ad)) err(a.rs485, 'adresse', `${c}.rs485.adresse`, `adresse RS485 ${ad} déjà prise par « ${adressesBus.get(ad)} ».`);
        else adressesBus.set(ad, app);
        if (plage && Number.isInteger(ad) && !plage.includes(ad)) err(a.rs485, 'adresse', `${c}.rs485.adresse`, `${a.type} accepte les adresses ${plage[0]} à ${plage[plage.length - 1]}.`);
      }
      if (a.ethernet !== undefined) {
        controler(a.ethernet, SCHEMA.ethernet, `${c}.ethernet`, { source: a });
        if (entry && entry.role === 'controller' && !entry.ethernet) err(a, 'ethernet', `${c}.ethernet`, `${a.type} n'a pas de port Ethernet utilisable.`);
      }
      if (estObjet(a.bus)) {
        controler(a.bus, SCHEMA.bus, `${c}.bus`, { source: a });
        if (a.bus.vitesse != null && !vitessesPermises.includes(String(a.bus.vitesse))) {
          err(a.bus, 'vitesse', `${c}.bus.vitesse`, `vitesse ${a.bus.vitesse} absente des choix (${vitessesPermises.join(', ')}) : ajoutez-la à gabarits.options.rs485Speed.choix.`);
        }
        if (a.bus.format != null && !formatsPermis.includes(a.bus.format)) err(a.bus, 'format', `${c}.bus.format`, `format « ${a.bus.format} » absent des choix (${formatsPermis.join(', ')}).`);
      }
      if ((a.ethernet || a.bus !== undefined) && entry && entry.role !== 'controller') err(a, a.ethernet ? 'ethernet' : 'bus', c, '"ethernet" et "bus" ne concernent que l\'automate.');
      for (const [cle, ip] of [['ip', a.ip], ['ethernet', estObjet(a.ethernet) ? a.ethernet.ip : undefined]]) {
        if (typeof ip !== 'string' || !IPV4.test(ip)) continue;
        if (ips.has(ip)) err(a, cle, c, `adresse IP ${ip} déjà prise par « ${ips.get(ip)} ».`);
        else ips.set(ip, app);
        if (site.reseau?.sousReseau && !ipDansSousReseau(ip, site.reseau.sousReseau)) att(a, cle, c, `${ip} hors du sous-réseau ${site.reseau.sousReseau} du site.`);
      }
    }
  }

  // --- alias -----------------------------------------------------------------------------------------
  // Règles communes : valeurs texte, ancien nom qui n'existe plus, nouveau nom qui existe, pas de
  // chaîne (ancien → nouveau → autre), pas d'alias sur une clé que le code lit par son nom.
  const aliasSrc = estObjet(src.alias) ? src.alias : {};
  const alias = {};
  for (const t of ALIAS_TABLES) {
    alias[t] = {};
    if (aliasSrc[t] === undefined) continue;
    if (!estObjet(aliasSrc[t])) err(aliasSrc, t, `alias.${t}`, 'objet { … } attendu.');
    else alias[t] = aliasSrc[t];
  }
  for (const k of Object.keys(aliasSrc)) if (!ALIAS_TABLES.includes(k)) err(aliasSrc, k, `alias.${k}`, `table d'alias inconnue « ${k} »${suggestion(k, ALIAS_TABLES)}.`);
  // table : { ancien: nouveau } ; existe(nom) : le nom existe aujourd'hui ; quoi : « type », « site »...
  function controlerAlias(table, chemin, existe, quoi, { normaliser = (x) => x, interdits = [] } = {}) {
    const anciens = new Set(Object.keys(table).map(normaliser));
    for (const [ancien, nouveau] of Object.entries(table)) {
      const c = `${chemin}.${ancien}`;
      if (typeof nouveau !== 'string') {
        err(table, ancien, c, `alias : nom actuel entre guillemets attendu (trouvé ${JSON.stringify(nouveau)}).`);
        continue;
      }
      if (interdits.includes(ancien)) err(table, ancien, c, `« ${ancien} » est lu par le code de PLC Studio : le renommer demande de modifier le code, un alias ne suffit pas.`);
      if (existe(ancien)) err(table, ancien, c, `« ${ancien} » existe encore : un alias ne peut pas masquer ${quoi} existant.`);
      if (!existe(nouveau)) err(table, ancien, c, `alias vers « ${nouveau} » : ${quoi} inconnu.`);
      if (anciens.has(normaliser(nouveau))) err(table, ancien, c, `alias en chaîne (${ancien} → ${nouveau} → …) : pointez directement vers le nom actuel.`);
    }
  }
  controlerAlias(alias.types, 'alias.types', (n) => possede(catalog, n), 'type d’équipement');
  controlerAlias(alias.sites, 'alias.sites', (n) => possede(sites, n), 'site');
  for (const [site, table] of Object.entries(alias.appareils)) {
    if (!possede(sites, site)) err(alias.appareils, site, `alias.appareils.${site}`, `site « ${site} » inconnu.`);
    else if (!estObjet(table)) err(alias.appareils, site, `alias.appareils.${site}`, 'objet { "ancien": "nouveau" } attendu.');
    else controlerAlias(table, `alias.appareils.${site}`, (n) => possede(sites[site].appareils, n), 'appareil');
  }
  // Noms de voie d'un type (comme model.js channelName : names, sinon préfixe + n°, bancs comptés depuis 1).
  const nomsVoies = (type) =>
    new Set(
      (catalog[type]?.groups || []).flatMap((g) => {
        const ids = g.bank ? Array.from({ length: g.bank.bits }, (_, i) => i) : g.ids || [];
        return ids.map((ch) => String(g.names?.[ch] ?? `${g.prefix}${g.bank ? ch + 1 : ch}`).toUpperCase());
      })
    );
  for (const [t, quoi] of [['groupes', 'voie (cle)'], ['options', 'option'], ['voies', 'nom de voie']]) {
    for (const [type, table] of Object.entries(alias[t])) {
      const c = `alias.${t}.${type}`;
      const tous = t === 'voies' && type === '*';
      if (!tous && !possede(catalog, type)) {
        err(alias[t], type, c, `type « ${type} » inconnu${suggestion(type, Object.keys(catalog))}.`);
        continue;
      }
      if (!estObjet(table)) {
        err(alias[t], type, c, 'objet { "ancien": "nouveau" } attendu.');
        continue;
      }
      const types = tous ? Object.keys(catalog) : [type];
      const existe =
        t === 'groupes'
          ? (n) => catalog[type].groups.some((g) => g.key === n)
          : t === 'options'
            ? (n) => (catalog[type].options || []).some((o) => o.key === n)
            : (n) => types.some((x) => nomsVoies(x).has(String(n).toUpperCase()));
      controlerAlias(table, c, existe, quoi, {
        normaliser: t === 'voies' ? (x) => String(x).toUpperCase() : (x) => x,
        interdits: t === 'groupes' ? CLES_LUES_PAR_LE_CODE.groupes : t === 'options' ? CLES_LUES_PAR_LE_CODE.options : [],
      });
    }
  }

  // --- écarts connus ---------------------------------------------------------------------------------
  const ecartsConnus = estObjet(src.ecartsConnus) ? src.ecartsConnus : {};
  for (const [id, e] of Object.entries(ecartsConnus)) controler(e, SCHEMA.ecart, `ecartsConnus.${id}`, { source: ecartsConnus });

  // --- noms réservés : classes du firmware + classes et structures de nœuds des pilotes ---------------
  const classesFirmware = listeDeTextes(firmware.classesReservees) ? firmware.classesReservees : [];
  const classesReservees = [...new Set([...classesFirmware, ...Object.values(pilotes).flatMap((p) => [p.classeCpp, p.structureNoeuds]).filter((x) => typeof x === 'string')])];

  return {
    catalog,
    transportOptions: transport,
    equipements,
    pilotes,
    ecranAxe,
    firmware: { ...firmware, limites, typesSection, classesReservees, nomsProjetReserves: listeDeTextes(firmware.nomsProjetReserves) ? firmware.nomsProjetReserves : [] },
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

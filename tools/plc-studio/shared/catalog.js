// Catalogue des équipements connus de PLC Studio : FAÇADE du référentiel d'infrastructure.
//
// Les données (cartes, modules, robots, axes, limites du firmware, environnements de
// compilation, sites...) viennent de infrastructure.jsonc, à la racine du dépôt : c'est le
// seul fichier à modifier quand l'infrastructure change (voir docs/referentiel-infrastructure.md).
// Il est lu une fois, au chargement de ce module (await de premier niveau) : readFileSync dans
// Node (CLI, serveur, tests), fetch de /infrastructure.jsonc dans le navigateur (texte servi
// par le serveur depuis sa mémoire : le navigateur voit la même version que le générateur).
// Le format historique est conservé : CATALOG (un objet par type d'équipement, voir
// shared/referentiel.js pour la correspondance des clés) et MODBUS_TRANSPORT_OPTIONS.
//
// Ce module ne doit importer que referentiel.js, jsonc-pos.js et hash.js (feuille du graphe
// d'imports : un cycle avec un module à await de premier niveau bloquerait le chargement).

import { chargerReferentiel, formaterErreurs, possede } from './referentiel.js';
import { crc32 } from './hash.js';

// Erreur de lecture ou de contenu du référentiel : le message se suffit (fichier:ligne).
export class ErreurReferentiel extends Error {
  constructor(message) {
    super(message);
    this.name = 'ErreurReferentiel';
  }
}

const URL_REFERENTIEL = new URL('../../../infrastructure.jsonc', import.meta.url);
const EST_NODE = typeof process !== 'undefined' && !!process.versions?.node;

async function lireReferentiel() {
  if (EST_NODE) {
    const { readFileSync } = await import('node:fs');
    const { fileURLToPath } = await import('node:url');
    // PLC_REFERENTIEL=<fichier> : essayer un brouillon sans toucher au fichier du dépôt (Node seulement).
    const chemin = process.env.PLC_REFERENTIEL || fileURLToPath(URL_REFERENTIEL);
    try {
      return { chemin, texte: readFileSync(chemin, 'utf8') };
    } catch (e) {
      throw new ErreurReferentiel(`Référentiel d'infrastructure illisible : ${chemin} (${e.code || e.message}).`);
    }
  }
  const rep = await fetch('/infrastructure.jsonc', { cache: 'no-store' });
  if (!rep.ok) throw new ErreurReferentiel(`Référentiel d'infrastructure introuvable (HTTP ${rep.status}) : /infrastructure.jsonc`);
  return { chemin: 'infrastructure.jsonc', texte: await rep.text() };
}

const { chemin: CHEMIN, texte: TEXTE } = await lireReferentiel();
const charge = chargerReferentiel(TEXTE);
if (charge.erreurs.length) throw new ErreurReferentiel(formaterErreurs(charge, CHEMIN));

const empreinte = (t) => (crc32(new TextEncoder().encode(t.replace(/^\uFEFF/, ''))) >>> 0).toString(16).padStart(8, '0');

// Tout le référentiel chargé : firmware, compilation, sites, logiciels, alias, annexes des
// équipements (REFERENTIEL.equipements[type] : marque, logicielRequis, bornes...), etc.
export const REFERENTIEL = { ...charge, chemin: CHEMIN, texte: TEXTE, empreinte: empreinte(TEXTE) };
export { empreinte as empreinteReferentiel };

export const CATALOG = charge.catalog;

// Transport de chaque équipement Modbus : bus RS485 de l'automate (RTU) ou réseau (TCP,
// équipement natif ou passerelle Modbus TCP -> RTU : l'adresse est alors le n° d'unité).
// Déjà placées en tête des options de chaque équipement Modbus par le chargeur.
export const MODBUS_TRANSPORT_OPTIONS = charge.transportOptions;

// Valeur d'une limite du firmware (firmware.limites.<nom>.valeur). Nom inconnu = bogue de code.
export function limite(nom) {
  if (!possede(charge.firmware.limites, nom)) throw new ErreurReferentiel(`Limite « ${nom} » absente de firmware.limites (${CHEMIN}).`);
  return charge.firmware.limites[nom].valeur;
}

// Annexes d'un type d'équipement (marque, logicielRequis, formatSerie, adresses, bornes, voies).
export function infoEquipement(type) {
  return possede(charge.equipements, type) ? charge.equipements[type] : null;
}

// Marques des automates, dans l'ordre du référentiel, sans doublon (messages d'aide).
export function marquesAutomates() {
  return [...new Set(Object.values(charge.equipements).filter((e) => e.section === 'automates' && e.marque).map((e) => e.marque))];
}

// Anciens noms encore acceptés (section « alias » du référentiel) ; sans alias, le nom est rendu tel quel.
export function aliasType(type) {
  return possede(charge.alias.types, type) ? charge.alias.types[type] : type;
}
export function aliasGroupe(type, cle) {
  const table = possede(charge.alias.groupes, type) ? charge.alias.groupes[type] : null;
  return possede(table, cle) ? table[cle] : cle;
}
// Options renommées : la valeur passe sur la nouvelle clé (si elle n'est pas déjà réglée).
export function aliasOptions(type, options) {
  const table = possede(charge.alias.options, type) ? charge.alias.options[type] : null;
  if (!table || !options || typeof options !== 'object') return options;
  const out = { ...options };
  for (const [ancien, nouveau] of Object.entries(table)) {
    if (ancien in out) {
      if (!(nouveau in out)) out[nouveau] = out[ancien];
      delete out[ancien];
    }
  }
  return out;
}
// Nom de voie (en majuscules) : alias propres au type, puis alias communs « * ».
export function aliasVoie(type, nom) {
  const N = String(nom).toUpperCase();
  for (const table of [possede(charge.alias.voies, type) ? charge.alias.voies[type] : null, charge.alias.voies['*']]) {
    for (const [ancien, nouveau] of Object.entries(table || {})) if (ancien.toUpperCase() === N) return String(nouveau).toUpperCase();
  }
  return N;
}

// Types de variables "internes" (non câblées) proposées à l'étape Variables.
export const VARIABLE_KINDS = {
  command: { label: 'Commande',  help: "Bouton ou interrupteur à l'écran, lu par la logique.", dataTypes: ['bool', 'int', 'float'] },
  parameter: { label: 'Paramètre', help: "Consigne réglable à l'écran et conservée après redémarrage.", dataTypes: ['int', 'float', 'text', 'bool'] },
  indicator: { label: 'Indicateur', help: "Valeur calculée par la logique et affichée à l'écran.", dataTypes: ['bool', 'int', 'float', 'text'] },
  memory: { label: 'Mémoire', help: 'Variable de travail de la logique (compteur, mémoire), sans affichage.', dataTypes: ['bool', 'int', 'float'] },
  input: { label: 'Entrée câblée', help: 'Voie d\'entrée d\'un équipement.', dataTypes: ['bool', 'int'] },
  output: { label: 'Sortie câblée', help: "Voie de sortie d'un équipement (bobine, relais, ou registre d'un équipement Modbus).", dataTypes: ['bool', 'int'] },
};

export const DATA_TYPE_LABELS = { bool: 'Booléen', int: 'Entier', float: 'Réel', text: 'Texte' };

// Widgets IHM (data/component-manifest.json) compatibles avec chaque nature de variable.
export function widgetsFor(variable) {
  const t = variable.dataType;
  switch (variable.kind) {
    case 'command':
      if (t === 'bool') return ['tx-button-hold', 'tx-bool'];
      return ['tx-numeric', 'tx-slider'];
    case 'parameter':
      if (t === 'bool') return ['tx-bool'];
      if (t === 'text') return ['tx-string', 'tx-dropdown'];
      return ['tx-numeric', 'tx-slider'];
    case 'indicator':
    case 'memory':
      if (t === 'bool') return ['rx-indicator', 'rx-bool'];
      if (t === 'text') return ['rx-label'];
      return ['rx-numeric', 'rx-level-bar'];
    case 'input':
      if (t === 'bool') return ['rx-indicator', 'rx-bool'];
      return ['rx-numeric', 'rx-level-bar'];
    case 'output':
      if (t === 'bool') return ['rx-indicator', 'rx-bool'];
      return ['rx-numeric', 'rx-level-bar'];
    default:
      return ['rx-label'];
  }
}

export const WIDGET_LABELS = {
  'tx-button-hold': 'Bouton (maintenu)',
  'tx-bool': 'Interrupteur',
  'tx-numeric': 'Saisie numérique',
  'tx-slider': 'Curseur',
  'tx-string': 'Saisie texte',
  'tx-dropdown': 'Liste déroulante',
  'rx-indicator': 'Voyant',
  'rx-bool': 'Interrupteur (lecture)',
  'rx-numeric': 'Valeur numérique',
  'rx-level-bar': 'Barre de niveau',
  'rx-label': 'Texte',
};

export function catalogEntry(type) {
  return CATALOG[type] || null;
}

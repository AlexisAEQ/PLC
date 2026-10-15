// Vérificateur du référentiel d'infrastructure (« plc.mjs infra verifier »).
//
// Le firmware ne lit jamais infrastructure.jsonc : sa vérité, ce sont data/hardware/*.json,
// platformio.ini et les en-têtes C++ de src/. Ce module compare le référentiel à cette vérité
// et dit, pour chaque écart, quel fichier corriger et à quelle ligne du référentiel.
//
// Fonction pure : les fichiers sont lus par lire(cheminRelatif) -> texte | null et
// lister(dossierRelatif) -> noms | null (chemins relatifs à la racine du dépôt, séparateur « / »).
// Les tests passent un système de fichiers en mémoire.
//
// Codes : H = fichiers matériels, P = platformio.ini, F = firmware C++, S = sites,
// R = avertissements du chargeur, EC = écarts connus.
// Écarts connus (ecartsConnus) : un constat dont le chemin commence par « controle » devient un
// avertissement « [écart connu Exx] » ; un écart qui ne couvre plus aucun constat est signalé
// (cliquet : un écart corrigé doit être retiré du référentiel).

import { parseJsonc } from './jsonc.js';
import { ligneDe } from './jsonc-pos.js';
import { FORMATS_SERIE } from './referentiel.js';

const MATERIEL = 'data/hardware';
const BORNE_H = 'src/BorneUniverselle/borneUniverselle.h';
const BORNE_CPP = 'src/BorneUniverselle/borneUniverselle.cpp'; // jamais le .ws_backup voisin
const NODE_H = 'src/Node/Node.h';
const EXPANDEUR_CPP = 'src/IoExpander/IoExpander.cpp';
const HORLOGE_CPP = 'src/DS3231_RTC/DS3231_RTC.cpp';

const estObjet = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
const echapper = (s) => String(s).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const pluriel = (n, mot) => `${mot}${n > 1 ? 's' : ''}`;

// =============================================================================================
export function verifierReferentiel(ref, { lire, lister }) {
  const v = contexte(ref, lire, lister);
  for (const a of ref.avertissements || []) v.ajouter('attention', 'R1', a.chemin || '', a.message, a.ligne);
  controlesMateriel(v);
  controlesCompilation(v);
  controlesFirmware(v);
  controlesPilotes(v);
  controleSites(v);
  appliquerEcarts(v);
  const compte = (n) => v.constats.filter((c) => c.niveau === n).length;
  return { constats: v.constats, resume: { erreurs: compte('erreur'), attentions: compte('attention'), infos: compte('info') } };
}

function contexte(ref, lire, lister) {
  const textes = new Map();
  const lu = (rel) => {
    if (!textes.has(rel)) textes.set(rel, lire(rel) ?? null);
    return textes.get(rel);
  };
  const cpp = new Map();
  const materiels = new Map();
  const v = {
    ref,
    constats: [],
    lire: lu,
    lister: (rel) => lister(rel) ?? null,
    // Texte C++ sans commentaires (null si absent).
    cpp(rel) {
      if (!cpp.has(rel)) cpp.set(rel, lu(rel) === null ? null : sansCommentaires(lu(rel)));
      return cpp.get(rel);
    },
    // Fichier matériel analysé : { donnees, erreur } (donnees null si absent ou illisible).
    lecture(nom) {
      if (!materiels.has(nom)) materiels.set(nom, analyserMateriel(lu(`${MATERIEL}/${nom}.json`)));
      return materiels.get(nom);
    },
    materiel: (nom) => v.lecture(nom).donnees,
    ajouter(niveau, code, chemin, message, ligne) {
      v.constats.push({ niveau, code, chemin, ligne: ligne ?? ligneChemin(ref.source, chemin), message });
    },
  };
  v.erreur = (code, chemin, message) => v.ajouter('erreur', code, chemin, message);
  v.attention = (code, chemin, message) => v.ajouter('attention', code, chemin, message);
  v.info = (code, chemin, message) => v.ajouter('info', code, chemin, message);
  return v;
}

function analyserMateriel(texte) {
  if (texte === null) return { donnees: null };
  try {
    return { donnees: parseJsonc(texte) };
  } catch (e) {
    return { donnees: null, erreur: e.message };
  }
}

// Ligne d'un chemin « automates.X.voies[2].broches » : celle de la clé la plus profonde qui existe.
export function ligneChemin(source, chemin) {
  let o = source;
  let ligne = null;
  for (const seg of String(chemin || '').split(/\.|\[(\d+)\]/).filter(Boolean)) {
    if (!o || typeof o !== 'object' || !(seg in o)) break;
    ligne = ligneDe(o, seg) ?? ligne;
    o = o[seg];
  }
  return ligne;
}

// =============================================================================================
// Outils C++ et INI (exportés pour les tests)

// Retire les commentaires // et /* */ (chaînes et caractères respectés), sauts de ligne gardés.
export function sansCommentaires(src) {
  let out = '';
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (c === '"' || c === "'") {
      const fin = finLitteral(src, i);
      out += src.slice(i, fin);
      i = fin;
    } else if (c === '/' && src[i + 1] === '/') {
      while (i < src.length && src[i] !== '\n') i++;
    } else if (c === '/' && src[i + 1] === '*') {
      const fin = src.indexOf('*/', i + 2);
      const bloc = src.slice(i, fin < 0 ? src.length : fin + 2);
      out += bloc.replace(/[^\n]/g, ' ');
      i += bloc.length;
    } else {
      out += c;
      i++;
    }
  }
  return out;
}

// Position qui suit la chaîne ou le caractère littéral commençant en i.
function finLitteral(src, i) {
  let j = i + 1;
  while (j < src.length && src[j] !== src[i] && src[j] !== '\n') j += src[j] === '\\' ? 2 : 1;
  return Math.min(j + 1, src.length);
}

// Position du délimiteur fermant qui répond à celui en « debut » (littéraux ignorés), -1 sinon.
function fermante(texte, debut, ouvre, ferme) {
  let prof = 0;
  for (let i = debut; i < texte.length; i++) {
    const c = texte[i];
    if (c === '"' || c === "'") i = finLitteral(texte, i) - 1;
    else if (c === ouvre) prof++;
    else if (c === ferme && --prof === 0) return i;
  }
  return -1;
}

// Corps { … } de « struct Nom { », « class Nom : Base { », « enum class Nom : uint8_t { ».
export function blocCpp(texte, motCle, nom) {
  const mot = motCle === 'enum' ? 'enum(?:\\s+(?:class|struct))?' : motCle;
  const m = new RegExp(`\\b${mot}\\s+${echapper(nom)}\\b[^;{}()]*\\{`).exec(texte);
  if (!m) return null;
  const debut = m.index + m[0].length - 1;
  const fin = fermante(texte, debut, '{', '}');
  return fin < 0 ? null : texte.slice(debut + 1, fin);
}

// Corps de la définition d'une fonction : « Type Classe::nom(…) { … } ».
function corpsFonction(texte, nomRegex) {
  const m = new RegExp(`\\b${nomRegex}\\s*\\([^()]*\\)\\s*(?:const\\s*)?\\{`).exec(texte);
  if (!m) return null;
  const debut = m.index + m[0].length - 1;
  const fin = fermante(texte, debut, '{', '}');
  return fin < 0 ? null : texte.slice(debut + 1, fin);
}

// « 80 », « 0x1F », « (16) », « 65535u » -> nombre ; null si ce n'est pas un entier littéral.
export function entierCpp(texte) {
  const m = /^\(?\s*(-?)\s*(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*\)?$/.exec(String(texte).trim());
  if (!m) return null;
  return (m[1] ? -1 : 1) * Number(m[2]);
}

// Définitions d'un symbole : « #define S <v> » et « [static] constexpr|const <type> S = <v>; ».
// Renvoie [{ texte, valeur }] (valeur null si non entière).
export function definitionsCpp(texte, symbole) {
  const s = echapper(symbole);
  const out = [];
  for (const m of texte.matchAll(new RegExp(`^[ \\t]*#[ \\t]*define[ \\t]+${s}(?![\\w(])[ \\t]*(.*)$`, 'gm'))) {
    out.push({ texte: m[1].trim(), valeur: entierCpp(m[1]) });
  }
  for (const m of texte.matchAll(new RegExp(`\\b(?:constexpr|const)\\b[^;=(){}]*?\\b${s}\\s*(?:=\\s*([^;{}]+?)|\\{\\s*([^;{}]*?)\\s*\\})\\s*;`, 'g'))) {
    const t = m[1] ?? m[2];
    out.push({ texte: t.trim(), valeur: entierCpp(t) });
  }
  return out;
}

// platformio.ini -> { section: { cle: valeur } } (lignes de suite indentées jointes par \n).
export function lireIni(texte) {
  const sections = {};
  let sec = null;
  let cle = null;
  for (const brut of String(texte).split(/\r?\n/)) {
    if (!brut.trim() || /^\s*[;#]/.test(brut)) continue;
    const titre = /^\[([^\]]+)\]\s*$/.exec(brut);
    if (titre) {
      sec = sections[titre[1].trim()] ??= {};
      cle = null;
    } else if (/^\s/.test(brut) && sec && cle) {
      sec[cle] += `\n${brut.trim()}`;
    } else {
      const kv = /^([^=]+?)\s*=\s*(.*)$/.exec(brut);
      if (kv && sec) {
        cle = kv[1].trim();
        sec[cle] = kv[2].replace(/\s+;.*$/, '').trim();
      }
    }
  }
  return sections;
}

// Valeur d'une clé dans une section, en suivant « extends » puis [env] pour les [env:x].
function valeurIni(ini, nom, cle, vus = new Set()) {
  const sec = ini[nom];
  if (!sec || vus.has(nom)) return undefined;
  vus.add(nom);
  if (cle in sec) return sec[cle];
  for (const parent of (sec.extends || '').split(',').map((s) => s.trim()).filter(Boolean)) {
    const x = valeurIni(ini, parent, cle, vus);
    if (x !== undefined) return x;
  }
  return nom.startsWith('env:') ? ini.env?.[cle] : undefined;
}

// Cœur Arduino d'une plateforme : URL pioarduino = 3, « espressif32 » = 2, sinon null.
function coeurArduino(plateforme) {
  if (!plateforme) return null;
  if (/pioarduino/i.test(plateforme)) return 3;
  if (/^(platformio\/)?espressif32(\s*@.*)?$/.test(plateforme.trim())) return 2;
  return null;
}

// =============================================================================================
// H — fichiers matériels (data/hardware)

function controlesMateriel(v) {
  const noms = v.lister(MATERIEL);
  for (const [type, a] of Object.entries(v.ref.equipements)) {
    if (!a.fichierMateriel) continue;
    if (!fichierMateriel(v, a.fichierMateriel, `${a.section}.${type}.fichierMateriel`, noms)) continue;
    const hw = v.materiel(a.fichierMateriel);
    const entree = v.ref.catalog[type];
    voiesMateriel(v, `${a.section}.${type}`, entree, hw, a.fichierMateriel);
    sectionsMateriel(v, type, a, entree, hw);
    if (entree.role === 'controller') automateMateriel(v, type, a, entree, hw);
  }
  fichiersNonReferences(v, noms);
}

// H1 : le fichier existe (casse exacte), se lit et son champ "hardware" est son nom.
function fichierMateriel(v, nom, chemin, noms) {
  const rel = `${MATERIEL}/${nom}.json`;
  const exact = `${nom}.json`;
  if (noms && !noms.includes(exact)) {
    const autre = noms.find((n) => n.toLowerCase() === exact.toLowerCase());
    v.erreur('H1', chemin, autre
      ? `${rel} introuvable : le fichier s'appelle ${autre} (casse différente : échec sous Linux). Renommez le fichier ou corrigez « fichierMateriel ».`
      : `${rel} introuvable : créez le fichier matériel ou corrigez « fichierMateriel ».`);
    return false;
  }
  if (v.lire(rel) === null) {
    v.erreur('H1', chemin, `${rel} introuvable : créez le fichier matériel ou corrigez « fichierMateriel ».`);
    return false;
  }
  const hw = v.materiel(nom);
  if (!estObjet(hw)) {
    v.erreur('H1', chemin, `${rel} illisible (${v.lecture(nom).erreur || 'objet { … } attendu'}) : corrigez le fichier matériel.`);
    return false;
  }
  if (hw.hardware !== nom) v.erreur('H1', chemin, `${rel} : "hardware" vaut ${JSON.stringify(hw.hardware)} au lieu de "${nom}" (nom du fichier) : corrigez le fichier matériel.`);
  return true;
}

// H2 : la liste "<type>" du fichier contient ces ids. Renvoie la liste (null si absente).
function idsMateriel(v, chemin, hw, type, ids, quoi, rel) {
  const liste = hw[type];
  if (!Array.isArray(liste)) {
    v.erreur('H2', chemin, `${quoi} : ${rel} n'a pas de liste "${type}" : ajoutez-la au fichier matériel ou corrigez le type.`);
    return null;
  }
  const presents = new Set(liste.map((n) => n?.id));
  const manquants = ids.filter((id) => !presents.has(id));
  if (manquants.length) {
    const n = manquants.length;
    v.erreur('H2', chemin, `${quoi} : ${pluriel(n, 'id')} ${manquants.join(', ')} ${pluriel(n, 'absent')} de "${type}" dans ${rel} : ${n > 1 ? 'ajoutez-les' : 'ajoutez-le'} au fichier matériel ou corrigez les ids.`);
  }
  return liste;
}

function voiesMateriel(v, base, entree, hw, nom) {
  const rel = `${MATERIEL}/${nom}.json`;
  entree.groups.forEach((g, i) => {
    const chemin = `${base}.voies[${i}]`;
    const liste = idsMateriel(v, chemin, hw, g.type, g.bank ? [g.bank.nodeId] : g.ids, `voie « ${g.key} »`, rel);
    if (!liste) return;
    const noeud = (id) => liste.find((n) => n?.id === id);
    if (g.bank) bancMateriel(v, `${chemin}.banc`, g, noeud(g.bank.nodeId), rel);
    if (g.gpio) brochesGpio(v, `${chemin}.broches`, g, noeud, rel);
    if (g.key === 'buzzer') buzzerMateriel(v, `${chemin}.broche`, g, noeud, hw, rel);
  });
}

// H3 : un banc de N bits = "nb_values": N.
function bancMateriel(v, chemin, g, n, rel) {
  if (n && n.nb_values !== g.bank.bits) {
    v.erreur('H3', chemin, `voie « ${g.key} » : banc de ${g.bank.bits} bits, mais "nb_values" vaut ${JSON.stringify(n.nb_values)} pour le nœud ${g.bank.nodeId} de "${g.type}" dans ${rel} : corrigez « bits » ou le fichier matériel.`);
  }
}

// H4 : la voie GPIO<n> d'id k est la sortie tx-bool d'id k, broche n.
function brochesGpio(v, chemin, g, noeud, rel) {
  for (const id of g.ids) {
    const m = /^GPIO(\d+)$/.exec(g.names?.[id] || '');
    const n = noeud(id);
    if (m && n && n.pin !== Number(m[1])) {
      v.erreur('H4', chemin, `GPIO${m[1]} (voie « ${g.key} », id ${id}) : ${rel} met "${g.type}" id ${id} sur la broche ${JSON.stringify(n.pin)} : corrigez « broches » ou le fichier matériel.`);
    }
  }
}

// H5 : buzzer = tx-bool id 9 sur la broche du libellé, et board.buzzer.pin s'il est donné.
function buzzerMateriel(v, chemin, g, noeud, hw, rel) {
  const m = /GPIO(\d+)/.exec(g.label || '');
  if (!m) return;
  const broche = Number(m[1]);
  const n = noeud(g.ids?.[0] ?? 9);
  if (n && n.pin !== broche) v.erreur('H5', chemin, `buzzer GPIO${broche} : ${rel} met "${g.type}" id ${n.id} sur la broche ${JSON.stringify(n.pin)} : corrigez « broche » ou le fichier matériel.`);
  const pin = hw.board?.buzzer?.pin;
  if (pin != null && pin !== broche) v.erreur('H5', chemin, `buzzer GPIO${broche} : ${rel} donne board.buzzer.pin = ${JSON.stringify(pin)} : corrigez « broche » ou le fichier matériel.`);
}

// H2 (axes) : sections Modbus du pilote (non virtuelles) présentes dans le fichier du modèle.
function sectionsMateriel(v, type, a, entree, hw) {
  const rel = `${MATERIEL}/${a.fichierMateriel}.json`;
  (entree.sections || []).forEach((s, j) => {
    if (s.virtual) return;
    const chemin = cheminSection(v.ref, a.section, type, entree.axis.kind, s, j);
    idsMateriel(v, chemin, hw, s.type, s.nodes.map((n) => n.id), `${type}, section « ${s.suffix} »`, rel);
  });
}

function cheminSection(ref, section, type, kind, s, j) {
  return Array.isArray(ref.source?.[section]?.[type]?.noeuds?.[s.suffix]) ? `${section}.${type}.noeuds.${s.suffix}` : `pilotes.${kind}.sections[${j}]`;
}

// H6 à H8 : Ethernet, RS485 et horloge d'un automate.
function automateMateriel(v, type, a, entree, hw) {
  const base = `automates.${type}`;
  const rel = `${MATERIEL}/${a.fichierMateriel}.json`;
  if (!!a.ethernet !== !!hw.ETH) {
    v.erreur('H6', `${base}.ethernet`, a.ethernet
      ? `"ethernet": true, mais ${rel} n'a pas de bloc "ETH" : retirez "ethernet" ou ajoutez le bloc ETH au fichier matériel.`
      : `${rel} a un bloc "ETH"${hw.ETH?.phy ? ` (${hw.ETH.phy})` : ''} : ajoutez "ethernet": true (sinon PLC Studio refuse l'Ethernet de cet automate).`);
  }
  rs485Materiel(v, base, entree, hw, rel);
  horlogeMateriel(v, base, a, hw, rel);
}

// H7 : bloc RS485, vitesse proposée par l'option rs485Speed, format connu du firmware.
function rs485Materiel(v, base, entree, hw, rel) {
  const bus = hw.RS485;
  if (!estObjet(bus)) {
    v.erreur('H7', `${base}.rs485`, `${rel} n'a pas de bloc "RS485" (bus Modbus de l'automate) : ajoutez-le au fichier matériel.`);
    return;
  }
  const choix = (entree.options || []).find((o) => o.key === 'rs485Speed')?.choices?.map((c) => c.value).filter((x) => x !== '') || [];
  if (choix.length && !choix.includes(String(bus.speed))) {
    v.erreur('H7', `${base}.rs485.vitesse`, `${rel} : RS485 à ${JSON.stringify(bus.speed)} bauds, absent des choix de l'option rs485Speed (${choix.join(', ')}) : corrigez le fichier matériel ou ajoutez la vitesse à gabarits.options.rs485Speed.choix.`);
  }
  if (!FORMATS_SERIE.includes(bus.config)) {
    v.erreur('H7', `${base}.rs485.format`, `${rel} : format RS485 ${JSON.stringify(bus.config)} inconnu du firmware (${FORMATS_SERIE.join(', ')}) : corrigez le fichier matériel.`);
  }
}

// H8 : "horloge" du référentiel contre I2C.RTC du fichier (true / "auto" = auto-détection).
function horlogeMateriel(v, base, a, hw, rel) {
  const chemin = `${base}.horloge`;
  const rtc = hw.I2C?.RTC;
  const absent = rtc == null || rtc === false || /^(none|false)$/i.test(String(rtc));
  const auto = rtc === true || /^auto$/i.test(String(rtc));
  const puce = a.horloge;
  if (absent) {
    if (puce) v.erreur('H8', chemin, `horloge ${puce} déclarée, mais ${rel} n'a pas d'horloge (I2C.RTC ${rtc === undefined ? 'absent' : JSON.stringify(rtc)}) : retirez « horloge » ou ajoutez "RTC": "${puce}" au fichier matériel.`);
  } else if (!puce) {
    v.erreur('H8', chemin, `${rel} déclare une horloge (I2C.RTC = ${JSON.stringify(rtc)}) : ajoutez « horloge » (${Object.keys(v.ref.firmware.pucesHorloge || {}).join(', ')}).`);
  } else if (auto) {
    if (v.ref.firmware.pucesHorloge?.[puce]?.autoDetectee === false) {
      v.erreur('H8', chemin, `${rel} : I2C.RTC = ${JSON.stringify(rtc)} (auto-détection), mais ${puce} n'est pas auto-détectée par le firmware : écrivez "RTC": "${puce}" dans le fichier matériel.`);
    } else v.info('H8', chemin, `${rel} : I2C.RTC = ${JSON.stringify(rtc)} (auto-détection) : la puce ${puce} n'y est pas nommée, non vérifiable.`);
  } else if (String(rtc).toLowerCase() !== puce.toLowerCase()) {
    v.erreur('H8', chemin, `horloge ${puce} déclarée, mais ${rel} nomme ${JSON.stringify(rtc)} (I2C.RTC) : corrigez « horloge » ou le fichier matériel.`);
  }
}

// H9 : fichiers de base de data/hardware que le référentiel ne cite pas (variantes générées exclues).
function fichiersNonReferences(v, noms) {
  if (!noms) return;
  const cites = new Set(Object.values(v.ref.equipements).map((a) => a.fichierMateriel).filter(Boolean));
  const variante = (nom) =>
    [...cites].some((b) => nom.startsWith(`${b}_`) && /^(noRTC(_(\d+|def)_(\w+))?|(\d+|def)_(\w+))$/.test(nom.slice(b.length + 1)));
  for (const f of noms.filter((n) => n.endsWith('.json')).sort()) {
    const nom = f.slice(0, -5);
    if (cites.has(nom) || nom.startsWith('Modbus_') || variante(nom)) continue;
    v.info('H9', '', `${MATERIEL}/${f} n'est cité par aucun équipement du référentiel (fichier ancien, ou équipement à décrire).`);
  }
}

// =============================================================================================
// P — platformio.ini

function controlesCompilation(v) {
  const texte = v.lire('platformio.ini');
  if (texte === null) v.erreur('P1', 'compilation', 'platformio.ini introuvable à la racine du dépôt : environnements de compilation non vérifiés.');
  else environnements(v, lireIni(texte));
  automatesCompilation(v);
}

// P1 et P2 : chaque environnement du référentiel contre son [env:x].
function environnements(v, ini) {
  const { compilation } = v.ref;
  for (const [nom, env] of Object.entries(compilation.envs || {})) {
    const base = `compilation.envs.${nom}`;
    if (!ini[`env:${nom}`]) {
      v.erreur('P1', base, `[env:${nom}] absent de platformio.ini : ajoutez l'environnement ou retirez-le de compilation.envs.`);
      continue;
    }
    const val = (cle) => valeurIni(ini, `env:${nom}`, cle);
    comparerIni(v, `${base}.carte`, nom, 'board', val('board'), env.carte, 'carte');
    comparerIni(v, `${base}.flash`, nom, 'board_build.flash_size', val('board_build.flash_size'), env.flash, 'flash');
    coeurEnv(v, `${base}.arduino`, nom, val('platform'), env.arduino);
    partitionsEnv(v, `${base}.partitions`, nom, val('board_build.partitions'), env.partitions);
  }
  const defaut = (ini.platformio?.default_envs || '').split(/[\s,]+/).filter(Boolean).join(', ');
  if (compilation.envParDefaut !== undefined && defaut !== compilation.envParDefaut) {
    v.erreur('P1', 'compilation.envParDefaut', `platformio.ini : default_envs = ${defaut || '(absent)'}, le référentiel dit « ${compilation.envParDefaut} » : alignez les deux.`);
  }
  const connus = [...Object.keys(compilation.envs || {}), ...(compilation.envsIgnores || [])];
  for (const sec of Object.keys(ini)) {
    const m = /^env:(.+)$/.exec(sec);
    if (m && !connus.includes(m[1])) v.attention('P1', 'compilation.envs', `[env:${m[1]}] de platformio.ini absent du référentiel : décrivez-le dans compilation.envs ou ajoutez-le à compilation.envsIgnores.`);
  }
}

function comparerIni(v, chemin, nom, cle, reel, attendu, cleRef) {
  if (reel === undefined) v.erreur('P1', chemin, `[env:${nom}] : ${cle} absent de platformio.ini (le référentiel attend « ${attendu} »).`);
  else if (reel !== attendu) v.erreur('P1', chemin, `[env:${nom}] : ${cle} = ${reel} dans platformio.ini, « ${attendu} » ici : corrigez « ${cleRef} » (ou platformio.ini).`);
}

function coeurEnv(v, chemin, nom, plateforme, attendu) {
  const coeur = coeurArduino(plateforme);
  if (coeur === null) {
    v.erreur('P1', chemin, `[env:${nom}] : platform = ${plateforme ?? '(absent)'} : cœur Arduino indéterminable (URL pioarduino = 3, « espressif32 » = 2).`);
  } else if (coeur !== attendu) {
    v.erreur('P1', chemin, `[env:${nom}] compile avec le cœur Arduino ${coeur} (platform = ${plateforme}), le référentiel dit ${attendu} : corrigez « arduino ».`);
  }
}

// P2 : le CSV de partitions cité existe à la racine AVEC LA MÊME CASSE (Windows et macOS la
// masquent, Linux non) et c'est celui du référentiel.
function partitionsEnv(v, chemin, nom, fichier, attendu) {
  if (!fichier) {
    v.erreur('P2', chemin, `[env:${nom}] : board_build.partitions absent de platformio.ini (le référentiel attend ${attendu}).`);
    return;
  }
  if (fichier !== attendu) v.erreur('P2', chemin, `[env:${nom}] : platformio.ini cite ${fichier}, le référentiel ${attendu} : alignez les deux.`);
  const noms = v.lister('.');
  if (noms ? noms.includes(fichier) : v.lire(fichier) !== null) return;
  const autre = noms?.find((n) => n.toLowerCase() === fichier.toLowerCase());
  v.erreur('P2', chemin, autre
    ? `[env:${nom}] : platformio.ini cite ${fichier}, mais le fichier s'appelle ${autre} : la compilation échoue sous Linux (Windows et macOS ignorent la casse). Renommez le fichier ou corrigez board_build.partitions.`
    : `[env:${nom}] : ${fichier} (board_build.partitions) introuvable à la racine du dépôt.`);
}

// P3 : MCU du fichier matériel = MCU de l'environnement ; Ethernet => cœur Arduino suffisant.
function automatesCompilation(v) {
  const { compilation } = v.ref;
  const min = compilation.arduinoMinPourEthernet ?? 3;
  for (const [type, a] of Object.entries(v.ref.equipements)) {
    const env = a.section === 'automates' ? compilation.envs?.[a.envPlatformIO] : null;
    if (!env) continue;
    const mcu = a.fichierMateriel ? v.materiel(a.fichierMateriel)?.board?.mcu : undefined;
    if (mcu && mcu !== env.mcu) {
      v.erreur('P3', `automates.${type}.envPlatformIO`, `${MATERIEL}/${a.fichierMateriel}.json : board.mcu = ${mcu}, mais l'environnement ${a.envPlatformIO} compile pour ${env.mcu} : corrigez « envPlatformIO » ou le fichier matériel.`);
    }
    if (a.ethernet && env.arduino < min) {
      v.erreur('P3', `automates.${type}.ethernet`, `Ethernet déclaré, mais l'environnement ${a.envPlatformIO} utilise le cœur Arduino ${env.arduino} : l'Ethernet n'est compilé qu'à partir du cœur ${min} (EthernetManager.cpp). Changez « envPlatformIO ».`);
    }
  }
}

// =============================================================================================
// F — firmware C++

function controlesFirmware(v) {
  versionFirmware(v);
  limitesFirmware(v);
  typesSection(v);
  pucesExtension(v);
  pucesHorloge(v);
}

// F1 : firmware.version = x.y.z de BORNE_UNIVERSELLE_VERSION.
function versionFirmware(v) {
  const attendu = v.ref.firmware.version;
  if (attendu === undefined) return;
  const t = v.cpp(BORNE_H);
  const m = t && /\bBORNE_UNIVERSELLE_VERSION\s*\[\s*\][^=;]*=\s*"[^"]*?(\d+\.\d+\.\d+)[^"]*"/.exec(t);
  if (t === null) v.erreur('F1', 'firmware.version', `${BORNE_H} introuvable : version du firmware non vérifiée.`);
  else if (!m) v.erreur('F1', 'firmware.version', `BORNE_UNIVERSELLE_VERSION (« … x.y.z ») introuvable dans ${BORNE_H}.`);
  else if (m[1] !== attendu) v.erreur('F1', 'firmware.version', `le firmware est en ${m[1]} (BORNE_UNIVERSELLE_VERSION, ${BORNE_H}), le référentiel dit ${attendu} : corrigez « version ».`);
}

// F2 : chaque limite qui cite sa vérité ("fichier#SYMBOLE", "fichier#SYMBOLE-2"...).
// Symbole introuvable = erreur : jamais de contrôle sauté en silence.
function limitesFirmware(v) {
  for (const [nom, l] of Object.entries(v.ref.firmware.limites || {})) {
    if (l.verite === undefined) continue;
    const chemin = `firmware.limites.${nom}`;
    for (const verite of Array.isArray(l.verite) ? l.verite : [l.verite]) limite(v, chemin, l.valeur, verite);
  }
}

function limite(v, chemin, valeur, verite) {
  const m = /^([^#\s]+)#([A-Za-z_]\w*)\s*(?:([+-])\s*(\d+))?$/.exec(String(verite));
  if (!m) {
    v.erreur('F2', chemin, `verite ${JSON.stringify(verite)} illisible : "fichier#SYMBOLE", "fichier#SYMBOLE-2" ou "fichier#SYMBOLE+1" attendu.`);
    return;
  }
  const [, fichier, symbole, signe, n] = m;
  const decalage = n ? (signe === '-' ? -1 : 1) * Number(n) : 0;
  const t = v.cpp(fichier);
  if (t === null) {
    v.erreur('F2', chemin, `${fichier} introuvable (verite ${verite}) : corrigez « verite ».`);
    return;
  }
  const defs = definitionsCpp(t, symbole);
  if (!defs.length) {
    v.erreur('F2', chemin, `${symbole} introuvable dans ${fichier} (#define ${symbole} <entier> ou constexpr … ${symbole} = <entier>) : corrigez « verite » ou le firmware.`);
    return;
  }
  for (const d of defs) {
    if (d.valeur === null) v.erreur('F2', chemin, `${fichier} : ${symbole} = « ${d.texte} », pas un entier littéral : contrôle impossible.`);
    else if (d.valeur + decalage !== valeur) {
      const calcul = decalage ? ` ${signe} ${n} = ${d.valeur + decalage}` : '';
      v.erreur('F2', chemin, `${fichier} : ${symbole} = ${d.valeur}${calcul}, le référentiel dit ${valeur} : corrigez « valeur » (ou le firmware).`);
    }
  }
}

// F3 : classes des types de section dans Node.h ; types connus de borneUniverselle.h/.cpp.
function typesSection(v) {
  const types = Object.entries(v.ref.firmware.typesSection || {});
  const node = v.cpp(NODE_H);
  if (node === null) v.erreur('F3', 'firmware.typesSection', `${NODE_H} introuvable : classes des nœuds non vérifiées.`);
  else {
    const parClasse = new Map();
    for (const [type, t] of types) for (const c of [t.classe, t.classeMateriel].filter(Boolean)) if (!parClasse.has(c)) parClasse.set(c, type);
    for (const [classe, type] of parClasse) {
      if (blocCpp(node, 'class', classe) === null) v.erreur('F3', `firmware.typesSection.${type}`, `classe ${classe} absente de ${NODE_H} : corrigez « classe » (ou le firmware).`);
    }
  }
  const sources = [BORNE_H, BORNE_CPP].map((f) => v.cpp(f));
  if (sources.every((t) => t === null)) {
    v.erreur('F3', 'firmware.typesSection', `${BORNE_H} et ${BORNE_CPP} introuvables : types de section non vérifiés.`);
    return;
  }
  for (const [type] of types) {
    if (!sources.some((t) => t?.includes(`"${type}"`))) {
      v.erreur('F3', `firmware.typesSection.${type}`, `type de section « ${type} » inconnu du firmware (chaîne "${type}" absente de ${BORNE_H} et ${BORNE_CPP}) : corrigez le nom (fautes du firmware comprises).`);
    }
  }
}

// Chaînes comparées par strcasecmp(x, "…") dans un fichier.
function chainesComparees(texte) {
  return [...texte.matchAll(/\bstrcasecmp\s*\(\s*[\w.>-]+\s*,\s*"([^"]+)"\s*\)/g)].map((m) => m[1]);
}

// Puces d'une liste du référentiel contre les puces que le firmware sait créer.
function comparerPuces(v, code, chemin, puces, fichier, connues) {
  const minus = connues.map((p) => p.toLowerCase());
  for (const p of puces) if (!minus.includes(p.toLowerCase())) v.erreur(code, chemin, `puce ${p} inconnue du firmware (${fichier} ne la crée pas ; puces connues : ${connues.join(', ')}) : retirez-la ou ajoutez-la au firmware.`);
  const ref = puces.map((p) => p.toLowerCase());
  for (const p of connues) if (!ref.includes(p.toLowerCase())) v.attention(code, chemin, `le firmware sait créer ${p} (${fichier}) : ajoutez-la à ${chemin}.`);
}

// F4 : firmware.pucesExtension contre IoExpander.cpp.
function pucesExtension(v) {
  const puces = v.ref.firmware.pucesExtension || [];
  const t = v.cpp(EXPANDEUR_CPP);
  const connues = t === null ? [] : chainesComparees(t);
  if (!connues.length) {
    v.erreur('F4', 'firmware.pucesExtension', `${EXPANDEUR_CPP} ${t === null ? 'introuvable' : 'sans liste strcasecmp(chip, "…")'} : puces d'extension non vérifiées.`);
    return;
  }
  comparerPuces(v, 'F4', 'firmware.pucesExtension', puces, EXPANDEUR_CPP, connues);
}

// F4 : firmware.pucesHorloge contre DS3231_RTC.cpp (puces, puis liste d'auto-détection).
function pucesHorloge(v) {
  const puces = v.ref.firmware.pucesHorloge || {};
  const t = v.cpp(HORLOGE_CPP);
  const connues = t === null ? [] : chainesComparees(t).filter((p) => !/^(auto|none|false)$/i.test(p));
  if (!connues.length) {
    v.erreur('F4', 'firmware.pucesHorloge', `${HORLOGE_CPP} ${t === null ? 'introuvable' : 'sans liste strcasecmp(chip, "…")'} : puces d'horloge non vérifiées.`);
    return;
  }
  comparerPuces(v, 'F4', 'firmware.pucesHorloge', Object.keys(puces), HORLOGE_CPP, [...new Set(connues)]);
  const m = /\b\w*AUTO\w*\s*\[\s*\]\s*=\s*\{([^}]*)\}/i.exec(t);
  if (!m) {
    v.info('F4', 'firmware.pucesHorloge', `liste d'auto-détection introuvable dans ${HORLOGE_CPP} : « autoDetectee » non vérifié.`);
    return;
  }
  const auto = [...m[1].matchAll(/"([^"]+)"/g)].map((x) => x[1].toLowerCase());
  for (const [p, d] of Object.entries(puces)) {
    const detectee = auto.includes(p.toLowerCase());
    if (!!d?.autoDetectee !== detectee) {
      v.erreur('F4', `firmware.pucesHorloge.${p}`, `${p} ${detectee ? 'est' : "n'est pas"} auto-détectée par le firmware (${HORLOGE_CPP}) : "autoDetectee": ${detectee}.`);
    }
  }
}

// =============================================================================================
// F5 et F6 — pilotes d'axe

function controlesPilotes(v) {
  for (const [kind, p] of Object.entries(v.ref.pilotes || {})) {
    const base = `pilotes.${kind}`;
    const entete = p.verite?.entete;
    if (!entete) {
      v.attention('F5', base, 'pilote sans "verite": { "entete": … } : classe, structure de nœuds et champs non vérifiés.');
      continue;
    }
    if (v.cpp(entete) === null) {
      v.erreur('F5', `${base}.verite`, `${entete} introuvable : corrigez verite.entete.`);
      continue;
    }
    const entrees = Object.entries(v.ref.catalog).filter(([, e]) => e.axis?.kind === kind);
    if (!chercherBloc(v, entete, 'class', p.classeCpp)) v.erreur('F5', `${base}.classeCpp`, `classe ${p.classeCpp} absente de ${entete} (et des autres .h du dossier) : corrigez « classeCpp ».`);
    champsPilote(v, kind, p, entrees);
    const valeurs = p.verite.enum ? enumPilote(v, kind, p, entrees) : [];
    if (p.verite.source && p.verite.options && valeurs) bornesPilote(v, kind, p, entrees, valeurs);
  }
}

// Bloc cherché dans l'en-tête puis dans les autres .h du même dossier : { fichier, corps }.
function chercherBloc(v, entete, motCle, nom) {
  const dossier = entete.includes('/') ? entete.slice(0, entete.lastIndexOf('/')) : '.';
  const autres = (v.lister(dossier) || []).filter((f) => f.endsWith('.h')).map((f) => `${dossier}/${f}`).filter((f) => f !== entete);
  for (const fichier of [entete, ...autres]) {
    const t = v.cpp(fichier);
    const corps = t === null ? null : blocCpp(t, motCle, nom);
    if (corps !== null) return { fichier, corps };
  }
  return null;
}

// F5 : champs des nœuds (toutes les entrées du pilote) et des signaux = membres de la structure.
function champsPilote(v, kind, p, entrees) {
  const struct = chercherBloc(v, p.verite.entete, 'struct', p.structureNoeuds);
  if (!struct) {
    v.erreur('F5', `pilotes.${kind}.structureNoeuds`, `struct ${p.structureNoeuds} absente de ${p.verite.entete} (et des autres .h du dossier) : corrigez « structureNoeuds ».`);
    return;
  }
  const membre = (champ) => new RegExp(`\\b${echapper(champ)}\\s*(?:;|=(?!=)|\\[|\\{)`).test(struct.corps);
  const signales = new Set();
  const signaler = (chemin, champ, quoi) => {
    if (signales.has(`${chemin}|${champ}`)) return;
    signales.add(`${chemin}|${champ}`);
    v.erreur('F5', chemin, `${quoi} : champ « ${champ} » absent de struct ${p.structureNoeuds} (${struct.fichier}) : corrigez « champ » (ou le firmware).`);
  };
  // Les nœuds d'affichage (« affichage ») sont des variables de la classe générée (axDisp_…, cpp.js).
  for (const [type, e] of entrees) {
    (e.sections || []).forEach((s, j) => {
      for (const n of s.nodes) if (!n.display && !membre(n.field)) signaler(cheminNoeud(v.ref, type, kind, s, j, n.field), n.field, `nœud « ${n.name} » (section « ${s.suffix} »)`);
    });
  }
  (p.signaux || []).forEach((s, i) => {
    if (!membre(s.champ)) signaler(`pilotes.${kind}.signaux[${i}]`, s.champ, `signal « ${s.cle} »`);
  });
}

// Chemin du nœud dans le référentiel : modèle (« par modèle »), section du pilote, ou gabarit.
function cheminNoeud(ref, type, kind, s, j, champ) {
  const section = ref.equipements[type]?.section || 'axes';
  const indice = (liste) => Math.max(0, liste.findIndex((n) => n?.champ === champ));
  const parModele = ref.source?.[section]?.[type]?.noeuds?.[s.suffix];
  if (Array.isArray(parModele)) return `${section}.${type}.noeuds.${s.suffix}[${indice(parModele)}]`;
  const brut = ref.pilotes[kind]?.sections?.[j];
  if (Array.isArray(brut?.noeuds)) return `pilotes.${kind}.sections[${j}].noeuds[${indice(brut.noeuds)}]`;
  const nomGabarit = typeof brut === 'string' ? brut : brut?.gabarit;
  const gabarit = nomGabarit ? ref.source?.gabarits?.sections?.[nomGabarit] : null;
  if (Array.isArray(gabarit?.noeuds)) return `gabarits.sections.${nomGabarit}.noeuds[${indice(gabarit.noeuds)}]`;
  return `pilotes.${kind}.sections[${j}]`;
}

// F5 : familles du pilote et modèles des entrées = énumérateurs de verite.enum. Renvoie les
// énumérateurs (null si l'enum est introuvable).
function enumPilote(v, kind, p, entrees) {
  const { entete, enum: nom } = p.verite;
  const b = chercherBloc(v, entete, 'enum', nom);
  if (!b) {
    v.erreur('F5', `pilotes.${kind}.verite`, `enum ${nom} absente de ${entete} (et des autres .h du dossier) : corrigez verite.enum.`);
    return null;
  }
  const valeurs = b.corps.split(',').map((s) => s.split('=')[0].trim()).filter((s) => /^\w+$/.test(s));
  for (const f of p.familles || []) {
    if (!valeurs.includes(f)) v.erreur('F5', `pilotes.${kind}.familles`, `famille ${f} absente de enum ${nom} (${b.fichier} : ${valeurs.join(', ')}) : corrigez « familles ».`);
  }
  for (const [type, e] of entrees) {
    for (const m of e.axis.models || []) {
      if (!valeurs.includes(m)) v.erreur('F5', `${v.ref.equipements[type].section}.${type}.modelesVariateur.${m}`, `modèle ${m} absent de enum ${nom} (${b.fichier} : ${valeurs.join(', ')}) : corrigez le nom du modèle.`);
    }
  }
  return valeurs;
}

// Valeurs initiales des membres d'une structure : « uint16_t maxAccelMs = 2500; ».
function initialiseurs(corps) {
  const out = {};
  for (const m of corps.matchAll(/\b(\w+)\s*=\s*([^;,{}()=]+);/g)) {
    const x = entierCpp(m[2]);
    if (x !== null && !(m[1] in out)) out[m[1]] = x;
  }
  return out;
}

// Affectations « o.membre = <entier>; » d'un morceau de code.
function affectations(code) {
  const out = {};
  for (const m of code.matchAll(/\b\w+\s*\.\s*(\w+)\s*=(?!=)\s*([^;]+);/g)) {
    const x = entierCpp(m[2]);
    if (x !== null) out[m[1]] = x;
  }
  return out;
}

// Bloc { … } ou instruction seule qui suit la position p : { texte, fin }.
function blocSuivant(code, p) {
  const debut = code.slice(p).search(/\S/) + p;
  if (code[debut] === '{') {
    const fin = fermante(code, debut, '{', '}');
    return fin < 0 ? null : { texte: code.slice(debut + 1, fin), fin: fin + 1 };
  }
  const fin = code.indexOf(';', debut);
  return fin < 0 ? null : { texte: code.slice(debut, fin + 1), fin: fin + 1 };
}

// Corps de defaultsFor -> { "<énumérateur>": { membre: valeur }, "": communes } ; null si la
// structure n'est pas une chaîne if/else ou un switch sur l'enum.
export function defautsParValeur(corps, nomEnum, valeurs) {
  const branches = Object.fromEntries(valeurs.map((x) => [x, {}]));
  const appliquer = (cibles, code) => cibles.forEach((x) => Object.assign(branches[x], affectations(code)));
  let horsBranches = '';
  let i = 0;
  for (;;) {
    const re = /\b(if|switch)\s*\(/g;
    re.lastIndex = i;
    const m = re.exec(corps);
    if (!m) break;
    horsBranches += corps.slice(i, m.index);
    i = (m[1] === 'if' ? chaineIf : blocSwitch)(corps, m.index, nomEnum, valeurs, appliquer);
    if (i < 0) return null;
  }
  horsBranches += corps.slice(i);
  const communes = affectations(horsBranches);
  return Object.fromEntries([['', communes], ...valeurs.map((x) => [x, { ...communes, ...branches[x] }])]);
}

// if (x == Enum::A) { … } else if (…) { … } else { … } : renvoie la position de fin (-1 si illisible).
function chaineIf(code, p, nomEnum, valeurs, appliquer) {
  const restantes = new Set(valeurs);
  for (;;) {
    const ouvre = code.indexOf('(', p);
    const ferme = fermante(code, ouvre, '(', ')');
    const b = ferme < 0 ? null : blocSuivant(code, ferme + 1);
    if (!b || !nomEnum) return -1;
    const cond = code.slice(ouvre + 1, ferme);
    const cites = [...cond.matchAll(new RegExp(`\\b${echapper(nomEnum)}\\s*::\\s*(\\w+)`, 'g'))].map((x) => x[1]);
    if (!cites.length) return -1;
    const cibles = /!=/.test(cond) ? [...restantes].filter((x) => !cites.includes(x)) : cites.filter((x) => restantes.has(x));
    appliquer(cibles, b.texte);
    cibles.forEach((x) => restantes.delete(x));
    const sinon = /^\s*else\b/.exec(code.slice(b.fin));
    if (!sinon) return b.fin;
    p = b.fin + sinon[0].length;
    if (/^\s*if\s*\(/.test(code.slice(p))) continue;
    const dernier = blocSuivant(code, p);
    if (!dernier) return -1;
    appliquer([...restantes], dernier.texte);
    return dernier.fin;
  }
}

// switch (x) { case Enum::A: … break; default: … } : renvoie la position de fin (-1 si illisible).
function blocSwitch(code, p, nomEnum, valeurs, appliquer) {
  const ouvre = code.indexOf('(', p);
  const ferme = fermante(code, ouvre, '(', ')');
  const b = ferme < 0 ? null : blocSuivant(code, ferme + 1);
  if (!b || !nomEnum) return -1;
  const etiquettes = [...b.texte.matchAll(new RegExp(`\\bcase\\s+${echapper(nomEnum)}\\s*::\\s*(\\w+)\\s*:|\\bdefault\\s*:`, 'g'))];
  if (!etiquettes.length) return -1;
  const nommees = new Set(etiquettes.map((e) => e[1]).filter(Boolean));
  let ouvertes = [];
  etiquettes.forEach((e, k) => {
    ouvertes.push(...(e[1] ? [e[1]] : valeurs.filter((x) => !nommees.has(x))));
    const fin = k + 1 < etiquettes.length ? etiquettes[k + 1].index : b.texte.length;
    const morceau = b.texte.slice(e.index + e[0].length, fin);
    appliquer(ouvertes.filter((x) => valeurs.includes(x)), morceau);
    if (/\b(break|return)\b/.test(morceau)) ouvertes = [];
  });
  return b.fin;
}

// F6 : bornes du référentiel contre <Options>::defaultsFor (+ valeurs initiales de la structure).
function bornesPilote(v, kind, p, entrees, valeurs) {
  const { entete, source, options, enum: nomEnum } = p.verite;
  const base = `pilotes.${kind}.verite`;
  const struct = chercherBloc(v, entete, 'struct', options);
  if (!struct) {
    v.erreur('F6', base, `struct ${options} absente de ${entete} (et des autres .h du dossier) : corrigez verite.options.`);
    return;
  }
  const t = v.cpp(source);
  const corps = t === null ? null : corpsFonction(t, `${echapper(options)}\\s*::\\s*defaultsFor`);
  if (corps === null) {
    v.erreur('F6', base, `${t === null ? `${source} introuvable` : `${options}::defaultsFor absente de ${source}`} : bornes du firmware non vérifiées ; corrigez verite.source.`);
    return;
  }
  const parValeur = defautsParValeur(corps, nomEnum, valeurs);
  if (!parValeur) {
    v.attention('F6', base, `${options}::defaultsFor (${source}) : structure non reconnue (if/else ou switch sur ${nomEnum || "l'enum"}) : bornes non vérifiées.`);
    return;
  }
  const initiales = initialiseurs(struct.corps);
  const defaut = (variante, membre) => (parValeur[variante] ?? parValeur[''])[membre] ?? initiales[membre];
  const lieu = { struct, source, options, nomEnum, valeurs };
  for (const [type, e] of entrees) comparerBornes(v, kind, type, e, defaut, 'maxSpeedRpm' in initiales, lieu);
}

function comparerBornes(v, kind, type, e, defaut, avecVitesse, lieu) {
  const { section, bornes } = v.ref.equipements[type];
  const srcEntree = v.ref.source?.[section]?.[type] || {};
  const cheminBorne = (cle) => (cle in srcEntree ? `${section}.${type}.${cle}` : `pilotes.${kind}.${cle}`);
  const comparer = (chemin, cle, attendu, membre, variante) => {
    const reel = defaut(variante ?? '', membre);
    const qui = `${type}${variante ? ` (${variante})` : ''}`;
    const ou = `${lieu.options}${variante && lieu.nomEnum ? `, ${lieu.nomEnum}::${variante}` : ''} : ${lieu.source}`;
    if (reel === undefined) v.erreur('F6', chemin, `${qui} : ${membre} introuvable dans struct ${lieu.options} (${lieu.struct.fichier}) et dans defaultsFor (${lieu.source}) : « ${cle} » non vérifiable.`);
    else if (reel !== attendu) v.erreur('F6', chemin, `${qui} : « ${cle} » = ${attendu} ici, ${membre} = ${reel} dans le firmware (${ou}) : corrigez le référentiel (ou le firmware).`);
  };
  // Une famille ou un modèle absent de l'enum est déjà signalé (F5) : pas de comparaison.
  const variantes = e.axis.models?.length ? e.axis.models : e.axis.family ? [e.axis.family] : [null];
  for (const variante of variantes) {
    if (variante && lieu.valeurs.length && !lieu.valeurs.includes(variante)) continue;
    if (bornes.rampeMaxMs !== undefined) comparer(cheminBorne('rampeMaxMs'), 'rampeMaxMs', bornes.rampeMaxMs, 'maxAccelMs', variante);
    if (bornes.rampeParDefautMs !== undefined) comparer(cheminBorne('rampeParDefautMs'), 'rampeParDefautMs', bornes.rampeParDefautMs, 'defaultAccelMs', variante);
    if (bornes.courantCreteMax && variante in bornes.courantCreteMax) {
      comparer(`${section}.${type}.modelesVariateur.${variante}.courantCreteMax`, 'courantCreteMax', bornes.courantCreteMax[variante], 'maxPeakCurrent', variante);
    }
    if (avecVitesse && Number.isInteger(e.axis.speedMax)) comparer(cheminBorne('vitesseMax'), 'vitesseMax', e.axis.speedMax, 'maxSpeedRpm', variante);
  }
}

// =============================================================================================
// S — sites ; EC — écarts connus

function controleSites(v) {
  const sites = Object.entries(v.ref.sites || {});
  const detail = sites.map(([nom, s]) => `${nom} (${Object.keys(s.appareils || {}).length} appareil(s))`);
  v.info('S1', 'sites', sites.length ? `${sites.length} site(s) : ${detail.join(', ')} ; IP, adresses et logiciels contrôlés au chargement.` : 'aucun site décrit.');
}

// Le chemin « controle » couvre-t-il ce constat (égalité ou préfixe suivi de « . » ou « [ ») ?
const couvre = (controle, chemin) => chemin === controle || (chemin.startsWith(controle) && /^[.[]/.test(chemin.slice(controle.length)));

function appliquerEcarts(v) {
  const ecarts = Object.entries(v.ref.ecartsConnus || {});
  for (const c of v.constats) {
    if (c.niveau === 'info') continue;
    const e = ecarts.find(([, x]) => typeof x?.controle === 'string' && couvre(x.controle, c.chemin));
    if (!e) continue;
    c.niveau = 'attention';
    c.ecart = e[0];
    c.message = `[écart connu ${e[0]}] ${c.message}`;
  }
  for (const [id, x] of ecarts) {
    const chemin = `ecartsConnus.${id}`;
    const n = v.constats.length;
    if (x?.controle == null) v.info('EC', chemin, `note ${id} : ${x?.texte ?? ''}`);
    else if (!v.constats.some((c) => c.ecart === id)) v.attention('EC', chemin, `écart ${id} corrigé ? Plus aucun constat sous « ${x.controle} » : retirez-le de ecartsConnus.`);
    if (v.constats.length > n) v.constats[n].ecart = id;
  }
}

// Commande « infra » de PLC Studio en ligne de commande : le référentiel d'infrastructure
// (infrastructure.jsonc, à la racine du dépôt).
//
//   node tools/plc-studio/cli/plc.mjs infra <sous-commande> [arguments]
//
//   verifier [--json] [--strict]    contrôle le référentiel contre data/hardware/, platformio.ini
//                                   et les en-têtes C++ de src/ ; code 1 s'il reste une erreur
//                                   (avec --strict : aussi s'il reste un avertissement)
//   liste                           automates (compilation, Ethernet, horloge, GPIO libres),
//                                   robots, modules, axes, environnements, sites et appareils
//   montrer <type|site|site.app>    entrée développée (CATALOG + annexes, JSON) et sa ligne
//   impact <site>[.<appareil>]      projets liés au site (ou à l'appareil), et projets non liés
//                                   qui contiennent une de ses IP
//
// Option commune : --root <dépôt> (défaut : dépôt qui contient l'outil). Le référentiel lu est
// <dépôt>/infrastructure.jsonc (ou PLC_REFERENTIEL=<fichier>, comme pour le reste de l'outil).

import { existsSync, readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { REFERENTIEL, ErreurReferentiel, empreinteReferentiel } from '../shared/catalog.js';
import { chargerReferentiel, formaterErreurs, suggestion } from '../shared/referentiel.js';
import { verifierReferentiel } from '../shared/referentiel-verif.js';
import { parseJsonc } from '../shared/jsonc.js';
import { ligneDe } from '../shared/jsonc-pos.js';

const ICI = fileURLToPath(import.meta.url);
const RACINE_OUTIL = path.resolve(path.dirname(ICI), '..', '..', '..');
const SOUS_COMMANDES = { verifier, liste, montrer, impact };

export async function cmdInfra(args = [], flags = {}, { ROOT } = {}) {
  const racine = path.resolve(ROOT || RACINE_OUTIL);
  const [sous, ...reste] = args;
  if (!SOUS_COMMANDES[sous]) {
    if (sous) console.error(`Sous-commande « ${sous} » inconnue${suggestion(sous, Object.keys(SOUS_COMMANDES))}.`);
    console.log(usage());
    return sous ? 2 : 0;
  }
  let ref;
  try {
    ref = referentielPour(racine);
  } catch (e) {
    if (e?.name !== 'ErreurReferentiel') throw e;
    console.error(e.message);
    return 3;
  }
  return SOUS_COMMANDES[sous](ref, reste, flags, racine);
}

function usage() {
  const lignes = readFileSync(ICI, 'utf8').split('\n');
  return lignes.slice(0, lignes.findIndex((l) => !l.startsWith('//'))).map((l) => l.replace(/^\/\/ ?/, '')).join('\n');
}

// Référentiel de la racine demandée : celui déjà chargé par catalog.js s'il s'agit du même
// fichier (ou de PLC_REFERENTIEL), sinon <racine>/infrastructure.jsonc chargé ici.
function referentielPour(racine) {
  const fichier = path.join(racine, 'infrastructure.jsonc');
  if (process.env.PLC_REFERENTIEL || path.resolve(REFERENTIEL.chemin) === fichier || !existsSync(fichier)) return REFERENTIEL;
  const texte = readFileSync(fichier, 'utf8');
  const r = chargerReferentiel(texte);
  if (r.erreurs.length) throw new ErreurReferentiel(formaterErreurs(r, fichier));
  return { ...r, chemin: fichier, texte, empreinte: empreinteReferentiel(texte) };
}

// Nom du référentiel dans les messages : relatif à la racine quand il y est.
function nomFichier(ref, racine) {
  const r = path.relative(racine, ref.chemin);
  return !r || r.startsWith('..') ? ref.chemin : r.split(path.sep).join('/');
}

function fichiersDepuis(racine) {
  return {
    lire: (rel) => {
      try {
        return readFileSync(path.join(racine, rel), 'utf8');
      } catch {
        return null;
      }
    },
    lister: (rel) => {
      try {
        return readdirSync(path.join(racine, rel));
      } catch {
        return null;
      }
    },
  };
}

// --- verifier ----------------------------------------------------------------------------------
const NIVEAUX = [
  ['erreur', 'ERREURS'],
  ['attention', 'AVERTISSEMENTS'],
  ['info', 'INFORMATIONS'],
];

function verifier(ref, args, flags, racine) {
  const r = verifierReferentiel(ref, fichiersDepuis(racine));
  const fichier = nomFichier(ref, racine);
  if (flags.json) console.log(JSON.stringify({ fichier, revision: ref.revision, empreinte: ref.empreinte, ...r }, null, 2));
  else {
    console.log(`Vérification de ${fichier} contre data/hardware/, platformio.ini et src/ (${racine})`);
    for (const [niveau, titre] of NIVEAUX) {
      const liste = r.constats.filter((c) => c.niveau === niveau);
      if (!liste.length) continue;
      console.log(`\n${titre} (${liste.length})`);
      for (const c of liste) {
        console.log(`  ${fichier}${c.ligne ? `:${c.ligne}` : ''}  ${c.code}${c.chemin ? `  ${c.chemin}` : ''}`);
        console.log(`      ${c.message}`);
      }
    }
    const { erreurs, attentions, infos } = r.resume;
    console.log(`\nRéférentiel ${ref.revision || '(sans révision)'} (${ref.empreinte}) : ${erreurs} erreur(s), ${attentions} avertissement(s), ${infos} information(s)`);
  }
  return r.resume.erreurs || (flags.strict && r.resume.attentions) ? 1 : 0;
}

// --- liste -------------------------------------------------------------------------------------
function liste(ref, args, flags, racine) {
  const out = [`Référentiel ${nomFichier(ref, racine)} — révision : ${ref.revision || '—'} (${ref.empreinte})`];
  const parSection = (s) => Object.entries(ref.catalog).filter(([type]) => ref.equipements[type]?.section === s);
  out.push('', 'AUTOMATES');
  for (const [type, e] of parSection('automates')) out.push(...ligneAutomate(ref, type, e));
  for (const [section, titre] of [['robots', 'ROBOTS'], ['modules', 'MODULES'], ['axes', 'AXES']]) {
    out.push('', titre);
    for (const [type, e] of parSection(section)) out.push(`  ${type.padEnd(30)} ${e.label}  (${detailEquipement(ref, type, e)})`);
  }
  out.push(...lignesCompilation(ref));
  out.push(...lignesSites(ref));
  console.log(out.join('\n'));
  return 0;
}

function ligneAutomate(ref, type, e) {
  const a = ref.equipements[type];
  const env = ref.compilation.envs?.[a.envPlatformIO];
  const gpio = e.groups.filter((g) => g.gpio).flatMap((g) => g.ids.map((id) => g.names?.[id] + (a.voies?.[g.key]?.buzzerCarte ? ' (buzzer de la carte)' : '')));
  const buzzer = e.groups.find((g) => g.key === 'buzzer');
  return [
    `  ${type} — ${e.label}`,
    `      compilation : ${a.envPlatformIO}${env ? ` (${env.mcu}, ${env.flash}, Arduino ${env.arduino})` : ''}  →  pio run -e ${a.envPlatformIO}`,
    `      Ethernet : ${a.ethernet ? 'oui' : 'non'}   horloge : ${a.horloge || 'aucune'}${buzzer ? `   ${buzzer.label}` : ''}`,
    `      GPIO libres : ${gpio.join(', ') || 'aucun'}`,
  ];
}

function detailEquipement(ref, type, e) {
  const a = ref.equipements[type];
  const d = [];
  if (e.axis) d.push(`pilote ${e.axis.kind}${e.axis.family ? `, famille ${e.axis.family}` : ''}${e.axis.models ? `, modèles ${e.axis.models.join('/')}` : ''}`);
  if (e.axis) d.push(`vitesse ${e.axis.speedMin}-${e.axis.speedMax} (${e.axis.speedUnit})`);
  if (e.modbus) d.push(`${e.defaultTransport === 'tcp' ? 'Modbus TCP' : 'Modbus RTU'}, adresse ${e.defaultAddress ?? '—'}`);
  if (!e.modbus && e.axis) d.push('STEP/DIR');
  d.push(e.hardware ? `data/hardware/${e.hardware}.json` : e.generic ? 'fichier matériel généré' : 'sans fichier matériel');
  if (a.logicielRequis) d.push(`exige ${a.logicielRequis.noms.join(' ou ')}${a.logicielRequis.versionMin ? ` ${a.logicielRequis.versionMin}+` : ''}`);
  if (a.bornes?.rampeMaxMs !== undefined) d.push(`rampe ≤ ${a.bornes.rampeMaxMs} ms`);
  return d.join(' ; ');
}

function lignesCompilation(ref) {
  const { envs = {}, envParDefaut, envsIgnores = [] } = ref.compilation;
  const out = ['', `COMPILATION (défaut : ${envParDefaut ?? '—'})`];
  for (const [nom, env] of Object.entries(envs)) {
    const automates = Object.entries(ref.equipements).filter(([, a]) => a.envPlatformIO === nom).map(([t]) => t);
    out.push(`  ${nom.padEnd(16)} ${env.mcu.padEnd(9)} ${env.carte.padEnd(19)} ${env.flash.padEnd(5)} Arduino ${env.arduino}  ${env.partitions}`);
    out.push(`      pio run -e ${nom}${automates.length ? `   (automates : ${automates.join(', ')})` : ''}`);
  }
  if (envsIgnores.length) out.push(`  non contrôlés : ${envsIgnores.join(', ')}`);
  return out;
}

function lignesSites(ref) {
  const out = ['', 'SITES'];
  const sites = Object.entries(ref.sites || {});
  if (!sites.length) out.push('  aucun');
  for (const [nom, s] of sites) {
    out.push(`  ${nom} — ${s.libelle}${s.reseau?.sousReseau ? `  (${s.reseau.sousReseau})` : ''}`);
    for (const [app, a] of Object.entries(s.appareils || {})) out.push(`    ${app.padEnd(12)} ${descriptionAppareil(a)}`);
  }
  return out;
}

function descriptionAppareil(a) {
  const d = [a.type || a.materiel || '(sans type)'];
  const ip = a.ip || a.ethernet?.ip;
  if (ip) d.push(`IP ${ip}${a.ethernet ? ' (Ethernet)' : ''}`);
  if (a.rs485) d.push(`RS485 adresse ${a.rs485.adresse}`);
  if (a.logiciel) d.push(`logiciel ${a.logiciel.nom}${a.logiciel.version ? ` ${a.logiciel.version}` : ''}${a.logiciel.releveLe ? ` (relevé le ${a.logiciel.releveLe})` : ''}`);
  if ('modbusTcp' in a) d.push(a.modbusTcp ? `Modbus TCP ${a.modbusTcp.port}, unité ${a.modbusTcp.unite}` : 'Modbus TCP : non');
  return d.join('   ');
}

// --- montrer -----------------------------------------------------------------------------------
function montrer(ref, [nom], flags, racine) {
  if (!nom) {
    console.error('Usage : infra montrer <type | site | site.appareil>');
    return 2;
  }
  const fichier = nomFichier(ref, racine);
  const type = ref.catalog[nom] ? nom : ref.alias?.types?.[nom];
  if (type && ref.catalog[type]) {
    const { section, ligne, ...annexes } = ref.equipements[type];
    if (type !== nom) console.log(`« ${nom} » est l'ancien nom de « ${type} » (alias.types).`);
    console.log(`${type} — section ${section}, ${fichier}:${ligne}`);
    console.log(`\nCATALOG.${type} :\n${JSON.stringify(ref.catalog[type], null, 2)}`);
    console.log(`\nAnnexes (REFERENTIEL.equipements.${type}) :\n${JSON.stringify(annexes, null, 2)}`);
    return 0;
  }
  const cible = resoudreSite(ref, nom);
  if (cible.erreur) return inconnu(ref, nom, cible.erreur);
  const { site, appareil } = cible;
  const objet = appareil ? ref.sites[site].appareils[appareil] : ref.sites[site];
  const ligne = appareil ? ligneDe(ref.sites[site].appareils, appareil) : ligneDe(ref.sites, site);
  console.log(`${appareil ? `${site}.${appareil} — appareil` : `${site} — site`}, ${fichier}${ligne ? `:${ligne}` : ''}`);
  console.log(JSON.stringify(objet, null, 2));
  if (appareil && objet.type) console.log(`\nType : ${objet.type} (détail : infra montrer ${objet.type})`);
  return 0;
}

// « site » ou « site.appareil », alias compris -> { site, appareil } ou { erreur }.
function resoudreSite(ref, nom) {
  const point = nom.indexOf('.');
  const s0 = point < 0 ? nom : nom.slice(0, point);
  const app0 = point < 0 ? null : nom.slice(point + 1);
  const site = ref.sites?.[s0] ? s0 : ref.alias?.sites?.[s0];
  if (!site || !ref.sites?.[site]) return { erreur: 'site' };
  if (app0 === null) return { site, appareil: null };
  const appareils = ref.sites[site].appareils || {};
  const appareil = appareils[app0] ? app0 : ref.alias?.appareils?.[site]?.[app0];
  if (!appareil || !appareils[appareil]) return { erreur: 'appareil', site };
  return { site, appareil };
}

function inconnu(ref, nom, quoi) {
  const sites = Object.keys(ref.sites || {});
  const apps = sites.flatMap((s) => Object.keys(ref.sites[s].appareils || {}).map((a) => `${s}.${a}`));
  const connus = quoi === 'appareil' ? apps : [...Object.keys(ref.catalog), ...sites, ...apps];
  console.error(`« ${nom} » inconnu${suggestion(nom, connus)}.`);
  console.error(`Types : ${Object.keys(ref.catalog).join(', ')}`);
  console.error(`Sites et appareils : ${[...sites, ...apps].join(', ') || 'aucun'}`);
  return 2;
}

// --- impact ------------------------------------------------------------------------------------
function impact(ref, [nom], flags, racine) {
  if (!nom) {
    console.error('Usage : infra impact <site>[.<appareil>]');
    return 2;
  }
  const cible = resoudreSite(ref, nom);
  if (cible.erreur) return inconnu(ref, nom, cible.erreur);
  const { site, appareil } = cible;
  const nomsSite = [site, ...Object.keys(ref.alias?.sites || {}).filter((k) => ref.alias.sites[k] === site)];
  const nomsApp = appareil ? [appareil, ...Object.keys(ref.alias?.appareils?.[site] || {}).filter((k) => ref.alias.appareils[site][k] === appareil)] : null;
  const projets = lireProjets(racine);
  for (const p of projets.filter((x) => x.erreur)) console.log(`attention : ${p.rel} illisible (${p.erreur}) : ignoré.`);
  const [a, de] = appareil ? [`à l'appareil « ${site}.${appareil} »`, `de l'appareil « ${site}.${appareil} »`] : [`au site « ${site} »`, `du site « ${site} »`];

  const lies = [];
  for (const p of projets) {
    if (!nomsSite.includes(p.projet?.site)) continue;
    const eqs = (p.projet.equipment || []).filter((e) => e?.appareil && (!nomsApp || nomsApp.includes(e.appareil)));
    if (!nomsApp || eqs.length) lies.push({ ...p, eqs });
  }
  console.log(`Projets liés ${a} : ${lies.length || 'aucun'}`);
  for (const p of lies) console.log(`  ${p.rel}${p.eqs.length ? `  (${p.eqs.map((e) => `${e.label || e.type} → ${e.appareil}`).join(', ')})` : ''}`);

  const ips = Object.entries(ref.sites[site].appareils || {})
    .filter(([app]) => !appareil || app === appareil)
    .flatMap(([app, a]) => [a.ip, a.ethernet?.ip].filter(Boolean).map((ip) => ({ app, ip })));
  const aLier = projets
    .filter((p) => !lies.some((l) => l.rel === p.rel))
    .map((p) => ({ ...p, trouvees: ips.filter(({ ip }) => contientIp(p.texte, ip)) }))
    .filter((p) => p.trouvees.length);
  if (aLier.length) {
    console.log(`\nProjets non liés qui contiennent une IP ${de} (liez-les au site pour suivre ses changements) :`);
    for (const p of aLier) console.log(`  ${p.rel}  (${p.trouvees.map(({ ip, app }) => `${ip} = ${app}`).join(', ')})`);
  }
  return 0;
}

function lireProjets(racine) {
  const dossier = path.join(racine, 'projets');
  if (!existsSync(dossier)) return [];
  return readdirSync(dossier)
    .filter((f) => f.endsWith('.plc.json'))
    .sort()
    .map((f) => {
      const texte = readFileSync(path.join(dossier, f), 'utf8');
      const rel = `projets/${f}`;
      try {
        return { rel, texte, projet: parseJsonc(texte) };
      } catch (e) {
        return { rel, texte, projet: null, erreur: e.message };
      }
    });
}

// IP présente telle quelle (pas 192.168.10.5 dans 192.168.10.50).
function contientIp(texte, ip) {
  return new RegExp(`(?<![\\d.])${ip.replace(/\./g, '\\.')}(?!\\d)`).test(texte);
}

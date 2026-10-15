// Sites du référentiel d'infrastructure (section « sites » de infrastructure.jsonc).
//
// Un projet peut choisir un site (project.site) et lier chacun de ses équipements à un
// appareil de ce site (equipment[].appareil). L'IP, le port, le n° d'unité ou l'adresse RS485,
// l'Ethernet et le bus de l'automate viennent alors du référentiel, à chaque ouverture et à
// chaque génération : changer l'IP d'un robot dans infrastructure.jsonc suffit, il n'y a plus
// à la recopier dans chaque projet. Un projet sans site n'est jamais modifié.
// Le logiciel relevé sur l'appareil (AUBOPE 4.5.13, ARCS...) est comparé au logiciel exigé
// par le profil de l'équipement (logicielRequis) : un profil incompatible bloque la génération.

import { CATALOG, REFERENTIEL, infoEquipement } from './catalog.js';
import { incompatibiliteLogiciel } from './referentiel.js';

const nomSite = (nom) => REFERENTIEL.alias.sites[nom] || nom;
const nomAppareil = (site, nom) => REFERENTIEL.alias.appareils[site]?.[nom] || nom;

export function listeSites() {
  return Object.entries(REFERENTIEL.sites).map(([nom, s]) => ({ nom, libelle: s.libelle, appareils: s.appareils || {} }));
}

export function siteDuProjet(project) {
  return project?.site ? REFERENTIEL.sites[nomSite(project.site)] || null : null;
}

// Appareil lié à un équipement : { nom, appareil } ou null.
export function appareilDe(project, eq) {
  const site = siteDuProjet(project);
  if (!site || !eq?.appareil) return null;
  const nom = nomAppareil(project.site, eq.appareil);
  return site.appareils?.[nom] ? { nom, appareil: site.appareils[nom] } : null;
}

// Champs d'un équipement fixés par son appareil (l'interface les grise et affiche leur provenance).
export function champsImposes(project, eq) {
  const lien = appareilDe(project, eq);
  const entry = CATALOG[eq?.type];
  if (!lien || !entry) return {};
  const a = lien.appareil;
  const out = {};
  if (entry.modbus && a.ip && a.modbusTcp) Object.assign(out, { transport: 'tcp', ip: a.ip, port: a.modbusTcp.port, address: a.modbusTcp.unite });
  else if (entry.modbus && a.rs485) Object.assign(out, { transport: 'rtu', address: a.rs485.adresse });
  if (entry.role === 'controller' && a.bus) {
    if (a.bus.vitesse != null) out.rs485Speed = String(a.bus.vitesse);
    if (a.bus.format != null) out.rs485Config = a.bus.format;
  }
  if (entry.role === 'controller' && a.ethernet && entry.ethernet) out.ethernet = a.ethernet;
  return out;
}

// Recopie dans le projet les valeurs de son site (fonction idempotente, appelée à la fin de
// normalizeProject). Les noms renommés (alias.sites / alias.appareils) sont mis à jour.
export function appliquerSite(project) {
  if (!project?.site) return project;
  project.site = nomSite(project.site);
  if (!REFERENTIEL.sites[project.site]) return project; // signalé par controlerSite
  for (const eq of project.equipment || []) {
    if (!eq.appareil) continue;
    eq.appareil = nomAppareil(project.site, eq.appareil);
    const c = champsImposes(project, eq);
    if (c.transport) {
      eq.options = { ...(eq.options || {}), transport: c.transport };
      if (c.transport === 'tcp') Object.assign(eq.options, { ip: c.ip, port: c.port });
      eq.address = c.address;
    }
    if (c.rs485Speed !== undefined) eq.options = { ...(eq.options || {}), rs485Speed: c.rs485Speed };
    if (c.rs485Config !== undefined) eq.options = { ...(eq.options || {}), rs485Config: c.rs485Config };
    if (c.ethernet) {
      const e = c.ethernet;
      project.ethernet = { ...(project.ethernet || {}), enabled: true, dhcp: false, ip: e.ip, gateway: e.passerelle, mask: e.masque, dns: e.dns || '' };
    }
  }
  return project;
}

const releve = (a) => (a.logiciel?.nom ? `${a.logiciel.nom}${a.logiciel.version ? ' ' + a.logiciel.version : ''}${a.logiciel.releveLe ? `, relevé le ${a.logiciel.releveLe}` : ''}` : '');

// Contrôles du site et de la compatibilité logicielle : issues au format de validate.js.
export function controlerSite(project) {
  const issues = [];
  const err = (step, message, ref) => issues.push({ level: 'error', step, message, ref });
  const warn = (step, message, ref) => issues.push({ level: 'warning', step, message, ref });
  const site = siteDuProjet(project);
  if (project.site && !site) {
    const connus = Object.keys(REFERENTIEL.sites);
    err('project', `Site « ${project.site} » inconnu dans infrastructure.jsonc (sites : ${connus.join(', ') || 'aucun'}). Renommé ? Ajoutez l'ancien nom dans alias.sites.`);
  }
  const liens = new Map();
  for (const eq of project.equipment || []) {
    const entry = CATALOG[eq.type];
    if (!entry) continue;
    const requis = infoEquipement(eq.type)?.logicielRequis;
    if (!eq.appareil) {
      if (requis) {
        warn('equipment', `${eq.label} : le profil ${eq.type} exige ${requis.noms.join(' ou ')}${requis.versionMin ? ` ${requis.versionMin} ou plus` : ''} ; logiciel du contrôleur non vérifié. Reliez l'équipement à un appareil d'un site (infrastructure.jsonc) dont le logiciel est relevé.`, eq.uid);
      }
      continue;
    }
    if (!project.site) {
      err('equipment', `${eq.label} : lié à l'appareil « ${eq.appareil} » sans site choisi (étape Projet).`, eq.uid);
      continue;
    }
    if (!site) continue;
    const lien = appareilDe(project, eq);
    if (!lien) {
      err('equipment', `${eq.label} : appareil « ${eq.appareil} » absent du site ${project.site} (appareils : ${Object.keys(site.appareils || {}).join(', ') || 'aucun'}). Renommé ? Ajoutez l'ancien nom dans alias.appareils.`, eq.uid);
      continue;
    }
    const { nom, appareil: a } = lien;
    if (liens.has(nom)) err('equipment', `${eq.label} et ${liens.get(nom)} sont liés au même appareil « ${nom} ».`, eq.uid);
    liens.set(nom, eq.label);
    if (a.type && a.type !== eq.type) {
      warn('equipment', `${eq.label} : le site ${project.site} indique « ${CATALOG[a.type]?.label || a.type} » pour « ${nom} », pas « ${entry.label} ».`, eq.uid);
    }
    if (entry.modbus && !(a.ip && a.modbusTcp) && !a.rs485) {
      const pourquoi = a.logiciel?.nom && REFERENTIEL.logiciels[a.logiciel.nom]?.serveurModbusTcp === false ? ` (${releve(a)} : pas de serveur Modbus TCP)` : '';
      err('equipment', `${eq.label} : « ${nom} » du site ${project.site} n'a pas de liaison Modbus déclarée${pourquoi}. Déclarez "modbusTcp" ou "rs485" dans infrastructure.jsonc, ou câblez le robot en E/S.`, eq.uid);
    }
    if (entry.role === 'controller' && a.ethernet && !entry.ethernet) err('equipment', `${eq.label} : le site donne une adresse Ethernet à « ${nom} », mais ${entry.label} n'a pas de port Ethernet utilisable.`, eq.uid);
    if (requis) {
      if (!a.logiciel?.nom) warn('equipment', `${eq.label} : logiciel du contrôleur de « ${nom} » non relevé dans le site ${project.site} (menu « À propos » du pendant) ; le profil ${eq.type} exige ${requis.noms.join(' ou ')}.`, eq.uid);
      else {
        const raison = incompatibiliteLogiciel(requis, a.logiciel);
        if (raison) err('equipment', `${eq.label} : le profil ${eq.type} exige ${raison.exige} ; « ${nom} » du site ${project.site} : ${releve(a)}.${raison.sinon ? ' ' + raison.sinon : ''}`, eq.uid);
      }
    }
  }
  return issues;
}

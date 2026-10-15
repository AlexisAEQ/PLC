// Sites du référentiel d'infrastructure (section « sites » de infrastructure.jsonc).
//
// Un projet peut choisir un site (project.site) et lier chacun de ses équipements à un
// appareil de ce site (equipment[].appareil). L'IP, le port, le n° d'unité ou l'adresse RS485,
// l'Ethernet et le bus de l'automate viennent alors du référentiel, à chaque ouverture et à
// chaque génération : changer l'IP d'un robot dans infrastructure.jsonc suffit, il n'y a plus
// à la recopier dans chaque projet. Un projet sans site n'est jamais modifié.
// Le logiciel relevé sur l'appareil (AUBOPE 4.5.13, ARCS...) est comparé au logiciel exigé
// par le profil de l'équipement (logicielRequis) : un profil incompatible bloque la génération.
//
// Ce que le site a imposé est noté dans le projet (equipment[].imposeParSite,
// project.ethernetParSite) : si le site cesse de l'imposer (appareil modifié ou changé), la
// valeur revient au défaut au lieu de rester figée en silence. Délier un équipement (ou retirer
// le site) garde ses valeurs actuelles.

import { CATALOG, REFERENTIEL, infoEquipement } from './catalog.js';
import { incompatibiliteLogiciel, possede } from './referentiel.js';

const CLES_IMPOSABLES = ['transport', 'ip', 'port', 'address', 'rs485Speed', 'rs485Config'];
const ETHERNET_DEFAUT = { enabled: false, dhcp: true, ip: '', gateway: '', mask: '255.255.255.0', dns: '' };

export const nomSite = (nom) => (possede(REFERENTIEL.alias.sites, nom) ? REFERENTIEL.alias.sites[nom] : nom);
const nomAppareil = (site, nom) => {
  const table = possede(REFERENTIEL.alias.appareils, site) ? REFERENTIEL.alias.appareils[site] : null;
  return possede(table, nom) ? table[nom] : nom;
};

export function listeSites() {
  return Object.entries(REFERENTIEL.sites).map(([nom, s]) => ({ nom, libelle: s.libelle, appareils: s.appareils || {} }));
}

export function siteDuProjet(project) {
  const nom = project?.site ? nomSite(project.site) : null;
  return nom && possede(REFERENTIEL.sites, nom) ? REFERENTIEL.sites[nom] : null;
}

// Appareil lié à un équipement : { nom, appareil } ou null.
export function appareilDe(project, eq) {
  const site = siteDuProjet(project);
  if (!site || !eq?.appareil) return null;
  const nom = nomAppareil(nomSite(project.site), eq.appareil);
  return possede(site.appareils, nom) ? { nom, appareil: site.appareils[nom] } : null;
}

// Un appareil décrit-il un automate (Ethernet, bus) plutôt qu'un équipement Modbus ?
const estAppareilAutomate = (a) => (a.type ? CATALOG[a.type]?.role === 'controller' : !a.ip && !a.modbusTcp && !a.rs485 && !a.logiciel);

// Champs d'un équipement fixés par son appareil (l'interface les grise et affiche leur provenance).
// « bus » présent : vitesse et format imposés, null = valeur du fichier matériel ('' dans le projet).
export function champsImposes(project, eq) {
  const lien = appareilDe(project, eq);
  const entry = CATALOG[eq?.type];
  if (!lien || !entry) return {};
  const a = lien.appareil;
  const out = {};
  if (entry.modbus && a.ip && a.modbusTcp) Object.assign(out, { transport: 'tcp', ip: a.ip, port: a.modbusTcp.port, address: a.modbusTcp.unite });
  else if (entry.modbus && a.rs485) Object.assign(out, { transport: 'rtu', address: a.rs485.adresse });
  if (entry.role === 'controller' && a.bus !== undefined) {
    out.rs485Speed = a.bus?.vitesse == null ? '' : String(a.bus.vitesse);
    out.rs485Config = a.bus?.format == null ? '' : a.bus.format;
  }
  if (entry.role === 'controller' && a.ethernet && entry.ethernet) out.ethernet = a.ethernet;
  return out;
}

const valeurDe = (eq, cle) => (cle === 'address' ? eq.address : eq.options?.[cle]);
function ecrire(eq, cle, valeur) {
  if (cle === 'address') eq.address = valeur;
  else eq.options = { ...(eq.options || {}), [cle]: valeur };
}
function defautDe(eq, cle) {
  const entry = CATALOG[eq.type];
  if (cle === 'address') return entry?.defaultAddress;
  return (entry?.options || []).find((o) => o.key === cle)?.default;
}

// Recopie dans le projet les valeurs de son site (fonction idempotente, appelée à la fin de
// normalizeProject). Les noms renommés (alias.sites / alias.appareils) sont mis à jour.
export function appliquerSite(project) {
  if (!project) return project;
  if (!project.site) {
    // Sans site : les valeurs restent celles du projet, les marques d'origine disparaissent.
    for (const eq of project.equipment || []) delete eq.imposeParSite;
    delete project.ethernetParSite;
    return project;
  }
  project.site = nomSite(project.site);
  if (!siteDuProjet(project)) return project; // site inconnu : signalé par controlerSite, rien ne bouge
  let ethernetImpose = false;
  for (const eq of project.equipment || []) {
    if (!eq.appareil) {
      delete eq.imposeParSite;
      continue;
    }
    eq.appareil = nomAppareil(project.site, eq.appareil);
    const c = champsImposes(project, eq);
    const avant = eq.imposeParSite && typeof eq.imposeParSite === 'object' ? eq.imposeParSite : {};
    // Valeur imposée la fois précédente et plus imposée aujourd'hui : retour au défaut.
    for (const [cle, valeur] of Object.entries(avant)) {
      if (!(cle in c) && CLES_IMPOSABLES.includes(cle) && valeurDe(eq, cle) === valeur) ecrire(eq, cle, defautDe(eq, cle));
    }
    const imposees = {};
    for (const cle of CLES_IMPOSABLES) {
      if (!(cle in c)) continue;
      ecrire(eq, cle, c[cle]);
      imposees[cle] = c[cle];
    }
    if (Object.keys(imposees).length) eq.imposeParSite = imposees;
    else delete eq.imposeParSite;
    if (c.ethernet) {
      const e = c.ethernet;
      project.ethernet = { ...(project.ethernet || {}), enabled: true, dhcp: false, ip: e.ip, gateway: e.passerelle, mask: e.masque, dns: e.dns || '' };
      ethernetImpose = true;
    }
  }
  if (ethernetImpose) project.ethernetParSite = true;
  else if (project.ethernetParSite) {
    // Le site n'impose plus l'Ethernet de l'automate lié : retour au défaut (désactivé).
    const automateLie = (project.equipment || []).some((eq) => eq.appareil && CATALOG[eq.type]?.role === 'controller');
    if (automateLie) project.ethernet = { ...ETHERNET_DEFAUT };
    delete project.ethernetParSite;
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
    err('project', `Site « ${project.site} » inconnu dans infrastructure.jsonc (sites : ${connus.join(', ') || 'aucun'}). Renommé ? Ajoutez l'ancien nom dans alias.sites ; sinon choisissez un autre site ou « aucun ».`);
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
      err('equipment', `${eq.label} : lié à l'appareil « ${eq.appareil} » sans site choisi : choisissez le site (étape Projet) ou déliez l'équipement (étape Équipements).`, eq.uid);
      continue;
    }
    if (!site) continue;
    const lien = appareilDe(project, eq);
    if (!lien) {
      err('equipment', `${eq.label} : appareil « ${eq.appareil} » absent du site ${project.site} (appareils : ${Object.keys(site.appareils || {}).join(', ') || 'aucun'}). Renommé ? Ajoutez l'ancien nom dans alias.appareils ; sinon déliez l'équipement.`, eq.uid);
      continue;
    }
    const { nom, appareil: a } = lien;
    if (liens.has(nom)) err('equipment', `${eq.label} et ${liens.get(nom)} sont liés au même appareil « ${nom} ».`, eq.uid);
    liens.set(nom, eq.label);
    if (a.type && a.type !== eq.type) {
      warn('equipment', `${eq.label} : le site ${project.site} indique « ${CATALOG[a.type]?.label || a.type} » pour « ${nom} », pas « ${entry.label} ».`, eq.uid);
    }
    if (entry.role === 'controller' && !estAppareilAutomate(a)) {
      warn('equipment', `${eq.label} : « ${nom} » du site ${project.site} décrit un équipement Modbus ou un robot, pas un automate : rien n'en est repris.`, eq.uid);
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
        else if (requis.versionMin && !a.logiciel.version) {
          warn('equipment', `${eq.label} : version de ${a.logiciel.nom} non relevée sur « ${nom} » (site ${project.site}) ; le profil ${eq.type} exige ${a.logiciel.nom} ${requis.versionMin} ou plus.`, eq.uid);
        }
      }
    }
  }
  // Automate du projet non lié alors que le site en décrit un : son Ethernet et son bus ne
  // suivraient pas les changements du référentiel.
  const ctrl = (project.equipment || []).find((e) => CATALOG[e.type]?.role === 'controller');
  if (site && ctrl && !ctrl.appareil) {
    const candidats = Object.entries(site.appareils || {}).filter(([, a]) => a.type && CATALOG[a.type]?.role === 'controller').map(([n]) => n);
    if (candidats.length) warn('equipment', `${ctrl.label} : le site ${project.site} décrit l'automate « ${candidats.join(' », « ')} » ; liez-le (étape Équipements) pour que son Ethernet et son bus suivent infrastructure.jsonc.`, ctrl.uid);
  }
  return issues;
}

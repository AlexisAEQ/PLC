// Référentiel d'infrastructure (infrastructure.jsonc) : chargement, messages d'erreur,
// identifiants gelés, cohérence avec le code et avec les fichiers matériels.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { chargerReferentiel, KINDS_GERES } from '../shared/referentiel.js';
import { CATALOG, REFERENTIEL } from '../shared/catalog.js';
import { verifierIdentifiants } from '../shared/identifiants.js';
import { newProject, addEquipment, normalizeVariable, listChannels } from '../shared/model.js';
import { generateFiles } from '../shared/generate.js';
import { hardwareFiles, mainCpp, repo } from './fixtures.js';

const TEXTE = readFileSync(repo + 'infrastructure.jsonc', 'utf8');
const lire = (rel) => readFileSync(new URL(`../${rel}`, import.meta.url), 'utf8');

// Remplace la première occurrence de `avant` (qui doit exister) et renvoie le texte modifié.
function muter(avant, apres) {
  assert.ok(TEXTE.includes(avant), `texte introuvable dans infrastructure.jsonc : ${avant}`);
  return TEXTE.replace(avant, apres);
}
const ligneDe = (texte, extrait) => texte.slice(0, texte.indexOf(extrait)).split('\n').length;

test('le référentiel du dépôt se charge sans erreur ni avertissement', () => {
  const r = chargerReferentiel(TEXTE);
  assert.deepEqual(r.erreurs, []);
  assert.deepEqual(r.avertissements, []);
  assert.ok(Object.keys(r.catalog).length >= 20);
  // Deux chargements donnent le même résultat (aucun état partagé entre chargements).
  assert.equal(JSON.stringify(chargerReferentiel(TEXTE).catalog), JSON.stringify(r.catalog));
});

test('identifiants gelés : aucun identifiant enregistré ou haché retiré sans alias', () => {
  const fige = JSON.parse(lire('test/snapshots/identifiants.json'));
  const pbs = verifierIdentifiants(fige, CATALOG, REFERENTIEL.alias);
  assert.deepEqual(pbs, [], `\n${pbs.join('\n')}\nRenommage voulu ? Ajoutez un alias dans infrastructure.jsonc ; identifiant haché : réinstallez les projets puis « node cli/plc.mjs infra figer ».`);
});

test('identifiants gelés : un renommage est détecté, et accepté avec un alias', () => {
  const fige = JSON.parse(lire('test/snapshots/identifiants.json'));
  const r = chargerReferentiel(muter('"Waveshare_8DIO": {', '"Waveshare_8DIO_v2": {'));
  assert.deepEqual(r.erreurs, []);
  assert.match(verifierIdentifiants(fige, r.catalog, r.alias).join('\n'), /type « Waveshare_8DIO » retiré/);
  const r2 = chargerReferentiel(muter('"Waveshare_8DIO": {', '"Waveshare_8DIO_v2": {').replace('"types": {},', '"types": { "Waveshare_8DIO": "Waveshare_8DIO_v2" },'));
  assert.deepEqual(r2.erreurs, []);
  assert.deepEqual(verifierIdentifiants(fige, r2.catalog, r2.alias), []);
  // Suffixe de section (haché) : pas d'alias possible, le test le signale.
  const r3 = chargerReferentiel(muter('"suffixe": "registres lus", "type": "ModbusReadHoldingRegister", "noeuds": [\n          { "id": 1, "nom": "status"', '"suffixe": "registres lus 2", "type": "ModbusReadHoldingRegister", "noeuds": [\n          { "id": 1, "nom": "status"'));
  assert.deepEqual(r3.erreurs, []);
  assert.match(verifierIdentifiants(fige, r3.catalog, r3.alias).join('\n'), /SureServo : section « registres lus » retirée ou renommée/);
});

// Fautes de saisie typiques : chaque erreur est expliquée et située (ligne du fichier).
const FAUTES = [
  { nom: 'vrai au lieu de true', avant: '"prefixe": "X", "inversable": true }', apres: '"prefixe": "X", "inversable": vrai }', message: /valeur inattendue « vrai » \(écrire true ou false\)/ },
  { nom: 'virgule manquante', avant: '"libelle": "Kincony KC868-A8S",', apres: '"libelle": "Kincony KC868-A8S"', message: /virgule manquante après la valeur de « libelle »/, ligneSuivante: true },
  { nom: 'clé en double', avant: '"libelle": "Kincony KC868-A8S",', apres: '"libelle": "Kincony KC868-A8S", "libelle": "A8S",', message: /clé « libelle » en double/ },
  { nom: 'clé mal orthographiée', avant: '"fichierMateriel": "Waveshare_8AI",', apres: '"fichierMaterial": "Waveshare_8AI",', message: /clé inconnue « fichierMaterial » \(vouliez-vous dire « fichierMateriel » \?\)/ },
  { nom: 'environnement inconnu', avant: '"envPlatformIO": "m5stack_stamplc",', apres: '"envPlatformIO": "m5stack_stampl",', message: /environnement « m5stack_stampl » absent de compilation\.envs \(vouliez-vous dire « m5stack_stamplc » \?\)/ },
  { nom: 'type de section inconnu', avant: '"type": "ModbusReadInputRegister", "sens": "entree", "donnee": "int", "ids": "1-8", "prefixe": "AI"', apres: '"type": "ModbusReadInputRegisters", "sens": "entree", "donnee": "int", "ids": "1-8", "prefixe": "AI"', message: /type de section « ModbusReadInputRegisters » inconnu du firmware/ },
  { nom: 'pilote non géré par le code', avant: '"pilote": "lichuan",\n      "famille": "A6",', apres: '"pilote": "lichuann",\n      "famille": "A6",', message: /pilote « lichuann » inconnu \(vouliez-vous dire « lichuan » \?\)/ },
  { nom: 'famille inconnue du pilote', avant: '"famille": "A5",', apres: '"famille": "A7",', message: /famille « A7 » inconnue du pilote « lichuan »/ },
  { nom: 'nombre de noms', avant: '"noms": ["PROG", "DEPART", "ABANDON", "ACQUIT", "P1", "P2", "P3", "P4", "VIE_API"]', apres: '"noms": ["PROG", "DEPART", "ABANDON", "ACQUIT", "P1", "P2", "P3", "P4"]', message: /8 noms pour 9 ids/ },
  { nom: 'broche en double', avant: '"broches": [40, 1, 47, 48]', apres: '"broches": [40, 1, 47, 40]', message: /broche GPIO en double/ },
  { nom: 'gabarit inconnu', avant: '{ "gabarit": "sorties", "type": "EXP_tx-bool", "ids": "1-4", "prefixe": "DO" }', apres: '{ "gabarit": "sortie", "type": "EXP_tx-bool", "ids": "1-4", "prefixe": "DO" }', message: /gabarit « sortie » introuvable dans gabarits\.voies \(vouliez-vous dire « sorties » \?\)/ },
  { nom: 'classe C++ différente du type de section', avant: '{ "id": 1, "nom": "cible", "champ": "targetPosition", "cpp": "Uint32OutputNode" } ] },\n        { "gabarit": "etatVariateur", "libelleAlarme": "Alarme servo", "libellePret": "Servo prêt", "libelleArret": "Vitesse nulle" }', apres: '{ "id": 1, "nom": "cible", "champ": "targetPosition", "cpp": "Uint16OutputNode" } ] },\n        { "gabarit": "etatVariateur", "libelleAlarme": "Alarme servo", "libellePret": "Servo prêt", "libelleArret": "Vitesse nulle" }', message: /classe « Uint16OutputNode » : une section « ModbusWriteDobbleHoldingRegister » crée des « Uint32OutputNode »/ },
  { nom: 'mot de passe dans un site', avant: '"materiel": "AUBO i10, coffret CB-M",', apres: '"materiel": "AUBO i10, coffret CB-M", "pwd": "x",', message: /clé « pwd » refusée/ },
  { nom: 'profil incompatible avec le logiciel relevé', avant: '"materiel": "AUBO i10, coffret CB-M",', apres: '"type": "Robot_Aubo", "materiel": "AUBO i10, coffret CB-M",', message: /le profil Robot_Aubo exige ARCS ; logiciel relevé : AUBOPE 4\.5\.13\. Sous AUBOPE 4\.x/ },
  { nom: 'Modbus TCP sous un logiciel sans serveur', avant: '"modbusTcp": null,', apres: '"modbusTcp": { "port": 502, "unite": 1 },', message: /AUBOPE n'a pas de serveur Modbus TCP/ },
  { nom: 'IP en double dans un site', avant: '"ip": "192.168.10.50",\n          "modbusTcp": null,', apres: '"ip": "192.168.10.20",\n          "modbusTcp": null,', message: /adresse IP 192\.168\.10\.20 déjà prise par « automate »/ },
  { nom: 'IP invalide', avant: '"ip": "192.168.10.50",', apres: '"ip": "192.168.10.500",', message: /« ip » doit être une adresse IPv4/ },
  { nom: 'version de format inconnue', avant: '"version": 1,', apres: '"version": 2,', message: /version 2 inconnue de ce PLC Studio/ },
];

for (const f of FAUTES) {
  test(`message d'erreur : ${f.nom}`, () => {
    const texte = muter(f.avant, f.apres);
    const r = chargerReferentiel(texte);
    const e = r.erreurs.find((x) => f.message.test(x.message));
    assert.ok(e, `message attendu ${f.message}, obtenu :\n${r.erreurs.map((x) => `${x.ligne} ${x.message}`).join('\n')}`);
    const attendu = ligneDe(texte, f.apres) + (f.ligneSuivante ? 1 : 0);
    if (!['version de format inconnue'].includes(f.nom)) assert.ok(Math.abs(e.ligne - attendu) <= 1, `ligne ${e.ligne}, attendue ${attendu} : ${e.message}`);
  });
}

test('une adresse RS485 de site hors plage et une vitesse de bus inconnue sont refusées', () => {
  const texte = muter('"remarque": "Tablette opérateur', '"bus": { "vitesse": 4800, "format": null }, "remarque": "Tablette opérateur');
  assert.match(chargerReferentiel(texte).erreurs.map((e) => e.message).join('\n'), /vitesse 4800 absente des choix/);
  const t2 = muter('"remarque": "Tablette opérateur', '"remarque": "Tablette opérateur').replace('"appareils": {\n        "automate": {', '"appareils": {\n        "io": { "type": "Waveshare_8DIO", "rs485": { "adresse": 300 } },\n        "automate": {');
  assert.match(chargerReferentiel(t2).erreurs.map((e) => e.message).join('\n'), /adresse RS485 entre 1 et 247/);
});

test('les pilotes du référentiel sont exactement ceux que le code sait générer et simuler', () => {
  const cpp = lire('shared/cpp.js');
  const axisCreate = cpp.slice(cpp.indexOf('function axisCreate'), cpp.indexOf('default:', cpp.indexOf('function axisCreate')));
  const cases = [...axisCreate.matchAll(/case '([^']+)':/g)].map((m) => m[1]).sort();
  assert.deepEqual(cases, [...KINDS_GERES].sort(), 'shared/cpp.js (axisCreate) et KINDS_GERES divergent');
  const sim = lire('shared/sim.js');
  const bloc = sim.slice(sim.indexOf('SPEED_MODELS'), sim.indexOf('};', sim.indexOf('SPEED_MODELS')));
  const modeles = [...bloc.matchAll(/^\s*'?([a-z-]+)'?\s*:/gm)].map((m) => m[1]).sort();
  assert.deepEqual(modeles, [...KINDS_GERES].sort(), 'shared/sim.js (SPEED_MODELS) et KINDS_GERES divergent');
  assert.deepEqual(Object.keys(REFERENTIEL.pilotes).sort(), [...KINDS_GERES].sort());
});

test('catalog.js reste une feuille du graphe d’imports (await de premier niveau)', () => {
  const imports = (f) => [...lire(`shared/${f}`).matchAll(/^import .* from '([^']+)';$/gm)].map((m) => m[1]).sort();
  assert.deepEqual(imports('catalog.js'), ['./hash.js', './referentiel.js']);
  assert.deepEqual(imports('referentiel.js'), ['./jsonc-pos.js']);
  assert.deepEqual(imports('jsonc-pos.js'), []);
  assert.deepEqual(imports('hash.js'), []);
});

test('le lecteur JSONC lit les fichiers matériels comme le lecteur historique', async () => {
  const { lireJsonc } = await import('../shared/jsonc-pos.js');
  const { parseJsonc } = await import('../shared/jsonc.js');
  const { readdirSync } = await import('node:fs');
  for (const f of readdirSync(repo + 'data/hardware').filter((x) => x.endsWith('.json'))) {
    const t = readFileSync(repo + 'data/hardware/' + f, 'utf8');
    assert.deepEqual(lireJsonc(t), parseJsonc(t), f);
  }
  assert.throws(() => lireJsonc('{ "__proto__": 1 }'), /interdite/);
  assert.deepEqual(lireJsonc('﻿{ "a": [1, 2,], }'), { a: [1, 2] });
});

// Chaque équipement du référentiel, toutes voies câblées, se génère sans erreur avec les vrais
// fichiers data/hardware (y compris les cartes et modules qu'aucun autre test ne génère).
function cablerTout(p, eq, vers) {
  const libres = (dir, gpio) => listChannels(p).filter((c) => c.eq === vers && !c.variable && c.group.dir === dir && (!gpio || c.group.gpio) && !c.group.bank);
  for (const v of p.variables.filter((x) => x.system?.eq === eq.uid)) {
    const sig = CATALOG[eq.type].signals.find((s) => s.key === v.system.signal);
    const c = libres(sig.dir === 'in' ? 'in' : 'out', sig.gpio)[0];
    if (c) v.binding = { eq: c.eq.uid, group: c.group.key, channel: c.channel };
  }
  let n = 0;
  for (const c of listChannels(p).filter((x) => x.eq === eq && !x.variable)) {
    const kind = c.group.dir === 'in' ? 'input' : 'output';
    p.variables.push(normalizeVariable({ label: `V${++n} ${eq.label}`, symbol: `v_${eq.uid.replace(/\W/g, '')}_${n}`, kind, dataType: c.group.dataType, binding: { eq: eq.uid, group: c.group.key, channel: c.channel } }));
  }
}

test('matrice : chaque équipement du référentiel se génère avec les vrais fichiers matériels', () => {
  const ctx = { hardwareFiles: hardwareFiles(), mainCpp: mainCpp() };
  const erreursMateriel = (p) => generateFiles(p, ctx).issues.filter((i) => i.level === 'error' && ['equipment', 'variables'].includes(i.step)).map((i) => i.message);
  const automates = Object.keys(CATALOG).filter((t) => CATALOG[t].role === 'controller');
  for (const type of automates) {
    const variantes = [{}];
    if (CATALOG[type].options.some((o) => o.key === 'rtc')) variantes.push({ rtc: false });
    variantes.push({ rs485Speed: '19200', rs485Config: 'SERIAL_8N1' });
    for (const opts of variantes) {
      const p = newProject('Matrice');
      const plc = addEquipment(p, type);
      Object.assign(plc.options, opts);
      cablerTout(p, plc, plc);
      assert.deepEqual(erreursMateriel(p), [], `${type} ${JSON.stringify(opts)}`);
    }
  }
  for (const type of Object.keys(CATALOG).filter((t) => CATALOG[t].role !== 'controller')) {
    const p = newProject('Matrice');
    const plc = addEquipment(p, 'Kincony_KC868_A16v3'); // 16 sorties et 6 GPIO : de quoi câbler tous les signaux
    const eq = addEquipment(p, type);
    if (eq.options.transport === 'tcp' && !eq.options.ip) eq.options.ip = '192.168.10.99'; // pas d'IP d'usine connue
    cablerTout(p, eq, plc);
    assert.deepEqual(erreursMateriel(p), [], type);
  }
});

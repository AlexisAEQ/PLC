// Référentiel d'infrastructure (infrastructure.jsonc) : chargement, messages d'erreur,
// identifiants gelés, cohérence avec le code et avec les fichiers matériels.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { chargerReferentiel, KINDS_GERES } from '../shared/referentiel.js';
import { CATALOG, REFERENTIEL } from '../shared/catalog.js';
import { verifierIdentifiants, identifiantsDuCatalogue } from '../shared/identifiants.js';
import { newProject, addEquipment, normalizeVariable, listChannels } from '../shared/model.js';
import { generateFiles } from '../shared/generate.js';
import { hardwareFiles, mainCpp, repo } from './fixtures.js';

// Le fichier vivant (infrastructure.jsonc) change avec l'installation : les tests de messages et
// de mutations travaillent sur une copie figée (test/donnees/infrastructure-essai.jsonc), pour que
// les procédures documentées (changer une IP, ajouter un alias...) ne cassent pas npm test.
const VIVANT = readFileSync(repo + 'infrastructure.jsonc', 'utf8');
const TEXTE = readFileSync(new URL('./donnees/infrastructure-essai.jsonc', import.meta.url), 'utf8');
const lire = (rel) => readFileSync(new URL(`../${rel}`, import.meta.url), 'utf8');

// Remplace la première occurrence de `avant` (qui doit exister) et renvoie le texte modifié.
function muter(avant, apres, texte = TEXTE) {
  assert.ok(texte.includes(avant), `texte introuvable dans la copie d'essai : ${avant}`);
  return texte.replace(avant, apres);
}
const messages = (texte) => chargerReferentiel(texte).erreurs.map((e) => `${e.ligne} [${e.chemin}] ${e.message}`).join('\n');
const ligneDe = (texte, extrait) => texte.slice(0, texte.indexOf(extrait)).split('\n').length;

test('le référentiel du dépôt et sa copie d’essai se chargent sans erreur ni avertissement', () => {
  assert.deepEqual(chargerReferentiel(TEXTE).erreurs, []);
  const r = chargerReferentiel(VIVANT);
  assert.deepEqual(r.erreurs, []);
  assert.deepEqual(r.avertissements, []);
  assert.ok(Object.keys(r.catalog).length >= 20);
  // Deux chargements donnent le même résultat (aucun état partagé entre chargements).
  assert.equal(JSON.stringify(chargerReferentiel(VIVANT).catalog), JSON.stringify(r.catalog));
});

test('identifiants gelés : aucun identifiant enregistré ou haché retiré sans alias', () => {
  const fige = JSON.parse(lire('test/snapshots/identifiants.json'));
  const pbs = verifierIdentifiants(fige, CATALOG, REFERENTIEL.alias, REFERENTIEL.sites);
  assert.deepEqual(pbs, [], `\n${pbs.join('\n')}\nRenommage voulu ? Ajoutez un alias dans infrastructure.jsonc ; identifiant haché : réinstallez les projets puis « node cli/plc.mjs infra figer ».`);
});

test('identifiants gelés : un renommage est détecté, et accepté avec un alias', () => {
  const base = chargerReferentiel(TEXTE);
  const fige = identifiantsDuCatalogue(base.catalog, base.sites);
  const pbs = (texte) => {
    const r = chargerReferentiel(texte);
    assert.deepEqual(r.erreurs, []);
    return verifierIdentifiants(fige, r.catalog, r.alias, r.sites).join('\n');
  };
  const renomme = muter('"Waveshare_8DIO": {', '"Waveshare_8DIO_v2": {');
  assert.match(pbs(renomme), /type « Waveshare_8DIO » retiré/);
  assert.equal(pbs(muter('"types": {},', '"types": { "Waveshare_8DIO": "Waveshare_8DIO_v2" },', renomme)), '');
  // Suffixe de section (haché) : pas d'alias possible, le test le signale.
  assert.match(pbs(muter('"suffixe": "registres lus", "type": "ModbusReadHoldingRegister", "noeuds": [\n          { "id": 1, "nom": "status"', '"suffixe": "registres lus 2", "type": "ModbusReadHoldingRegister", "noeuds": [\n          { "id": 1, "nom": "status"')), /SureServo : section « registres lus » retirée ou renommée/);
  // Préfixe de voie (noms écrits dans les specs) : signalé, puis accepté avec alias.voies.
  const prefixe = muter('{ "gabarit": "entrees", "type": "rx-bool", "ids": "1-4", "prefixe": "DI" },', '{ "gabarit": "entrees", "type": "rx-bool", "ids": "1-4", "prefixe": "IN" },');
  assert.match(pbs(prefixe), /Homemaster_MiniPLC : nom de voie « DI1 » disparu/);
  assert.equal(pbs(muter('"voies": {\n      "*": { "GPIO2_1": "GPIO2" }', '"voies": {\n      "Homemaster_MiniPLC": { "DI1": "IN1", "DI2": "IN2", "DI3": "IN3", "DI4": "IN4" },\n      "*": { "GPIO2_1": "GPIO2" }', prefixe)), '');
  // Site et appareil renommés.
  const site = muter('"banc-aubo": {\n      "libelle"', '"banc-atelier": {\n      "libelle"');
  assert.match(pbs(site), /site « banc-aubo » retiré/);
  assert.equal(pbs(muter('"sites": {},', '"sites": { "banc-aubo": "banc-atelier" },', site)), '');
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

test('limites : une limite lue par le code et absente est une erreur située', () => {
  assert.match(messages(muter('"axesPasAPas":', '"axesPasAPAS":')), /limite « axesPasAPas » manquante : PLC Studio la lit \(vouliez-vous dire « axesPasAPAS » \?\)/);
});

test('choix d’options : nombres acceptés, objets mal formés refusés', () => {
  const r = chargerReferentiel(muter('"choix": [{ "valeur": "", "libelle": "Celle du fichier matériel" }, "9600",', '"choix": [{ "valeur": "", "libelle": "Celle du fichier matériel" }, 4800, "9600",'));
  assert.deepEqual(r.erreurs, []);
  assert.ok(r.catalog.Kincony_KC868_A8S.options.find((o) => o.key === 'rs485Speed').choices.some((c) => c.value === '4800' && c.label === '4800'));
  assert.match(messages(muter('{ "valeur": "SERIAL_8N2", "libelle": "8N2 (2 bits de stop)" }', '{ "value": "SERIAL_8N2", "label": "8N2" }')), /choix : "texte", nombre ou \{ "valeur": …, "libelle": "…" \} attendu/);
});

test('alias : masquage, chaîne, cible inconnue et clé lue par le code refusés', () => {
  assert.match(messages(muter('"sites": {},', '"sites": { "banc-aubo": "autre" },')), /« banc-aubo » existe encore/);
  assert.match(messages(muter('"options": {},\n    "sites"', '"options": { "SureServo": { "homePolicy": "politique" } },\n    "sites"')), /« homePolicy » est lu par le code de PLC Studio/);
  assert.match(messages(muter('"groupes": {},', '"groupes": { "Waveshare_8DIO": { "x": "nope" } },')), /alias vers « nope » : voie \(cle\) inconnu/);
  assert.match(messages(muter('"types": {},', '"types": { "A": "B", "B": "Waveshare_8DIO" },')), /alias en chaîne/);
  assert.match(messages(muter('"types": {},', '"types": { "Ancien": ["Stepper"] },')), /nom actuel entre guillemets attendu/);
});

test('valeurs mal typées : erreurs situées, jamais d’exception', () => {
  const cas = [
    ['"modelesVariateur": { "DM556RS": { "courantCreteMax": 56 }, "DM882RS": { "courantCreteMax": 82 } }', '"modelesVariateur": null', /« modelesVariateur » doit être un objet/],
    ['"familles": ["A6", "A5"],', '"familles": 5,', /« familles » doit être une liste/],
    ['"ip": "192.168.10.50",', '"ip": ["192.168.10.50"],', /« ip » doit être une adresse IPv4/],
    ['"sousReseau": "192.168.10.0/24"', '"sousReseau": "999.168.10/24"', /sous-réseau/],
    ['"adresseParDefaut": 127,', '"adresseParDefaut": 0127,', /zéro en tête/],
    ['"type": "rx-bool", "ids": "1-8", "prefixe": "DI" },', '"type": "constructor", "ids": "1-8", "prefixe": "DI" },', /type de section « constructor » inconnu/],
    ['"envPlatformIO": "m5stack_stamplc",', '"envPlatformIO": "toString",', /environnement « toString » absent/],
    ['"marque": "KinCony",', '"marque": "<obligatoire>",', /"<obligatoire>" ne s'écrit que dans un gabarit/],
    ['{ "gabarit": "gpio", "broches": [32, 33, 14], "libelle": "GPIO libres HT1-HT3" }', '{ "gabarit": "gpio", "broches": [32, 33, 14], "ids": "1-8", "libelle": "GPIO libres HT1-HT3" }', /« broches » exclut « ids » et « noms »/],
    ['"wifi"', '"wifi"', null],
  ];
  for (const [avant, apres, motif] of cas) {
    if (!motif) continue;
    const r = chargerReferentiel(muter(avant, apres), { relancer: true });
    const e = r.erreurs.find((x) => motif.test(x.message));
    assert.ok(e, `${apres} : attendu ${motif}, obtenu\n${r.erreurs.map((x) => x.message).join('\n')}`);
    assert.ok(Number.isInteger(e.ligne), `${apres} : ligne attendue (${e.message})`);
  }
});

test('erreur dans un gabarit : signalée une fois, à la ligne du gabarit', () => {
  const texte = muter('"entrees": { "cle": "di", "libelle": "Entrées TOR", "suffixeSection": "Entrees", "sens": "entree", "donnee": "bool", "prefixe": "X", "inversable": true }',
    '"entrees": { "cle": "di", "libelle": "Entrées TOR", "suffixeSection": "Entrees", "sens": "entree", "donnee": "bool", "prefixe": "X", "inversable": "oui" }');
  const e = chargerReferentiel(texte).erreurs.filter((x) => /« inversable » doit être true ou false/.test(x.message));
  assert.equal(e.length, 1, e.map((x) => x.chemin).join(', '));
  assert.equal(e[0].chemin, 'gabarits.voies.entrees.inversable');
  assert.equal(e[0].ligne, ligneDe(texte, '"entrees": { "cle": "di"'));
});

test('texte « {cle} » sans valeur : l’erreur nomme l’équipement qui doit la fournir', () => {
  const texte = muter('"aideRampe": "0 = réglage du variateur conservé. A6 : ms pour 0 → 1000 tr/min (PA_058/PA_059).",', '');
  const e = chargerReferentiel(texte).erreurs.find((x) => /aideRampe/.test(x.message));
  assert.equal(e.chemin, 'axes.LichuanA6');
  assert.equal(e.ligne, ligneDe(texte, '"LichuanA6": {'));
});

test('clés de secrets refusées partout, y compris en camelCase', () => {
  assert.match(messages(muter('"materiel": "AUBO i10, coffret CB-M",', '"materiel": "AUBO i10, coffret CB-M", "wifiPassword": "x",')), /clé « wifiPassword » refusée/);
});

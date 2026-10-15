// Sites du référentiel : les équipements liés à un appareil prennent leur IP, leur port, leur
// n° d'unité, leur adresse RS485 (et l'automate son Ethernet et son bus) dans infrastructure.jsonc.
// Ce fichier charge un référentiel d'essai (le vrai + un site « essai ») : node --test lance
// chaque fichier de test dans son propre processus, PLC_REFERENTIEL ne touche donc que lui.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// Pas d'import statique de fixtures.js ni de shared/ : ils chargeraient le référentiel avant
// que PLC_REFERENTIEL ne soit posé.
const repo = fileURLToPath(new URL('../../../', import.meta.url));

const SITE_ESSAI = `
    "essai": {
      "libelle": "Cellule d'essai",
      "reseau": { "sousReseau": "192.168.20.0/24", "passerelle": "192.168.20.1" },
      "appareils": {
        "automate": { "type": "Waveshare_ESP32S3_POE_8DI8DO", "ethernet": { "ip": "192.168.20.20", "masque": "255.255.255.0", "passerelle": "192.168.20.1" },
                      "bus": { "vitesse": 19200, "format": "SERIAL_8N1" } },
        "passerelle": { "ip": "192.168.20.50", "modbusTcp": { "port": 502, "unite": 3 } },
        "io": { "type": "Waveshare_8DIO", "rs485": { "adresse": 4 } },
        "aubo": { "materiel": "AUBO i10", "logiciel": { "nom": "AUBOPE", "version": "4.5.13", "releveLe": "2026-10-11" }, "ip": "192.168.20.60", "modbusTcp": null },
        "cobot": { "type": "Robot_Aubo", "logiciel": { "nom": "ARCS", "version": "1.5" }, "ip": "192.168.20.61", "modbusTcp": { "port": 502, "unite": 1 } },
        "fr5": { "logiciel": { "nom": "FAIRINO", "version": "3.6.0" }, "ip": "192.168.20.62", "modbusTcp": { "port": 502, "unite": 1 } }
      }
    },`;
const texte = readFileSync(repo + 'infrastructure.jsonc', 'utf8')
  .replace('"sites": {', `"sites": {${SITE_ESSAI}`)
  .replace('"sites": {},', '"sites": { "ancien-banc": "essai" },');
const dir = mkdtempSync(path.join(tmpdir(), 'plc-ref-'));
process.env.PLC_REFERENTIEL = path.join(dir, 'infrastructure.jsonc');
writeFileSync(process.env.PLC_REFERENTIEL, texte);

const { newProject, addEquipment, normalizeProject, normalizeVariable } = await import('../shared/model.js');
const { generateFiles } = await import('../shared/generate.js');
const { validateProject } = await import('../shared/validate.js');
const { buildFromSpec, specFromProject } = await import('../shared/spec.js');
const { REFERENTIEL } = await import('../shared/catalog.js');
const { hardwareFiles, mainCpp } = await import('./fixtures.js');
const ctx = { hardwareFiles: hardwareFiles(), mainCpp: mainCpp() };
const messages = (issues, level = 'error') => issues.filter((i) => i.level === level).map((i) => i.message).join('\n');

function cellule() {
  const p = newProject('Cellule');
  p.wifi = [{ name: 'Atelier', ssid: 'atelier', pwd: 'secret123', dhcp: true }];
  p.site = 'essai';
  const plc = addEquipment(p, 'Waveshare_ESP32S3_POE_8DI8DO');
  plc.appareil = 'automate';
  const gw = addEquipment(p, 'ModbusGeneric', 'Passerelle');
  gw.appareil = 'passerelle';
  const io = addEquipment(p, 'Waveshare_8DIO');
  io.appareil = 'io';
  p.variables.push(normalizeVariable({ label: 'Départ', symbol: 'Depart', kind: 'input', dataType: 'bool', binding: { eq: plc.uid, group: 'di', channel: 1 } }));
  p.variables.push(normalizeVariable({ label: 'Prêt', symbol: 'Pret', kind: 'input', dataType: 'int', binding: { eq: gw.uid, group: 'r', channel: 1 } }));
  p.logic = { steps: [{ id: 's0', num: 0, label: 'Attente', initial: true, actions: [] }], transitions: [], blocks: {} };
  return normalizeProject(p);
}

test('le site de test est chargé', () => {
  assert.ok(REFERENTIEL.sites.essai);
  assert.equal(REFERENTIEL.chemin, process.env.PLC_REFERENTIEL);
});

test('un équipement lié prend IP, port, unité, adresse RS485, Ethernet et bus du site', () => {
  const p = cellule();
  const [plc, gw, io] = p.equipment;
  assert.deepEqual([gw.options.transport, gw.options.ip, gw.options.port, gw.address], ['tcp', '192.168.20.50', 502, 3]);
  assert.deepEqual([io.options.transport, io.address], ['rtu', 4]);
  assert.deepEqual([plc.options.rs485Speed, plc.options.rs485Config], ['19200', 'SERIAL_8N1']);
  assert.deepEqual([p.ethernet.enabled, p.ethernet.dhcp, p.ethernet.ip, p.ethernet.gateway, p.ethernet.mask], [true, false, '192.168.20.20', '192.168.20.1', '255.255.255.0']);
  const r = generateFiles(p, ctx);
  assert.equal(messages(r.issues), '');
  const config = JSON.parse(r.files.find((f) => f.path === 'data/config.json').content);
  assert.deepEqual(config.children.find((s) => s.tcp).tcp, { ip: '192.168.20.50', port: 502 });
  assert.equal(config.config.find((c) => c.Ethernet).Ethernet.ip, '192.168.20.20');
  assert.ok(r.files.some((f) => f.path === 'data/hardware/Waveshare_ESP32S3_POE_8DI8DO_19200_8N1.json'), 'variante du bus du site');
});

test('une valeur périmée dans le projet est remplacée par celle du site (changement d’IP propagé)', () => {
  const p = cellule();
  p.equipment[1].options.ip = '192.168.20.99';
  p.equipment[1].address = 9;
  const q = normalizeProject(JSON.parse(JSON.stringify(p)));
  assert.equal(q.equipment[1].options.ip, '192.168.20.50');
  assert.equal(q.equipment[1].address, 3);
});

test('un projet sans site n’est pas touché', () => {
  const p = cellule();
  delete p.site;
  for (const e of p.equipment) delete e.appareil;
  p.equipment[1].options.ip = '10.0.0.5';
  assert.equal(normalizeProject(p).equipment[1].options.ip, '10.0.0.5');
});

test('profil Robot_Aubo lié à un robot sous AUBOPE : erreur explicite, génération bloquée', () => {
  const p = cellule();
  const robot = addEquipment(p, 'Robot_Aubo');
  robot.appareil = 'aubo';
  const issues = validateProject(normalizeProject(p));
  assert.match(messages(issues), /le profil Robot_Aubo exige ARCS ; « aubo » du site essai : AUBOPE 4\.5\.13, relevé le 2026-10-11\. Sous AUBOPE 4\.x/);
  assert.match(messages(issues), /« aubo » du site essai n'a pas de liaison Modbus déclarée \(AUBOPE 4\.5\.13, relevé le 2026-10-11 : pas de serveur Modbus TCP\)/);
  assert.equal(generateFiles(normalizeProject(p), ctx).installable, false);
});

test('profil Robot_Aubo lié à un robot sous ARCS : accepté ; FAIRINO trop ancien : refusé', () => {
  const p = cellule();
  const robot = addEquipment(p, 'Robot_Aubo');
  robot.appareil = 'cobot';
  const fr = addEquipment(p, 'Robot_Fairino');
  fr.appareil = 'fr5';
  const issues = validateProject(normalizeProject(p));
  assert.doesNotMatch(messages(issues), /Robot_Aubo/);
  assert.match(messages(issues), /le profil Robot_Fairino exige FAIRINO 3\.7\.3 ou plus récent ; « fr5 » du site essai : FAIRINO 3\.6\.0/);
});

test('un robot non lié à un site : avertissement « logiciel non vérifié »', () => {
  const p = cellule();
  addEquipment(p, 'Robot_Aubo').options.ip = '192.168.20.61';
  assert.match(messages(validateProject(normalizeProject(p)), 'warning'), /le profil Robot_Aubo exige ARCS ; logiciel du contrôleur non vérifié/);
});

test('site ou appareil inconnu : erreur, jamais de valeur périmée en silence', () => {
  const p = cellule();
  p.equipment[1].appareil = 'passerele';
  assert.match(messages(validateProject(normalizeProject(p))), /appareil « passerele » absent du site essai/);
  const q = cellule();
  q.site = 'inconnu';
  assert.match(messages(validateProject(normalizeProject(q))), /Site « inconnu » inconnu dans infrastructure\.jsonc/);
});

test('site renommé : l’ancien nom (alias.sites) est accepté et réécrit', () => {
  const p = cellule();
  p.site = 'ancien-banc';
  assert.equal(normalizeProject(p).site, 'essai');
});

test('spec : site et appareil ; une adresse recopiée en plus est refusée ; aller-retour stable', () => {
  const spec = specFromProject(cellule());
  assert.equal(spec.site, 'essai');
  const gw = spec.equipment.find((e) => e.appareil === 'passerelle');
  assert.equal(gw.address, undefined);
  assert.equal(gw.options.ip, undefined);
  assert.equal(spec.ethernet, undefined, "l'Ethernet de l'automate vient du site");
  const r = buildFromSpec(spec);
  assert.deepEqual(r.errors, []);
  assert.equal(r.project.equipment.find((e) => e.appareil === 'passerelle').options.ip, '192.168.20.50');
  const conflit = JSON.parse(JSON.stringify(spec));
  conflit.equipment.find((e) => e.appareil === 'passerelle').address = 7;
  assert.match(buildFromSpec(conflit).errors.join('\n'), /address vient de l'appareil « passerelle » du site essai/);
});

test('variateur à format série imposé (Lichuan A6, 8E1) sur un bus 8N1 : erreur', () => {
  const p = cellule(); // bus du site : 19200 8N1
  const ax = addEquipment(p, 'LichuanA6');
  ax.address = 5;
  assert.match(messages(validateProject(normalizeProject(p))), /format série imposé 8E1 par le variateur ; le bus RS485 est réglé en 8N1/);
  ax.address = 40;
  assert.match(messages(validateProject(normalizeProject(p))), /adresse 40 hors de la plage du variateur \(1 à 32\)/);
});

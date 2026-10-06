import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { newProject, addEquipment, normalizeVariable, listChannels } from '../shared/model.js';
import { generateFiles } from '../shared/generate.js';
import { nodeHash } from '../shared/hash.js';
import { parseJsonc } from '../shared/jsonc.js';

const repo = fileURLToPath(new URL('../../../', import.meta.url));
const hardwareFiles = Object.fromEntries(
  readdirSync(repo + 'data/hardware')
    .filter((f) => f.endsWith('.json'))
    .map((f) => [f.slice(0, -5), parseJsonc(readFileSync(repo + 'data/hardware/' + f, 'utf8'))])
);

function wire(project, eq, group, channel, label, kind, extra = {}) {
  const v = normalizeVariable({ label, symbol: label.replace(/\W+/g, '_'), kind, dataType: 'bool', binding: { eq: eq.uid, group, channel }, hmi: { show: true }, ...extra });
  project.variables.push(v);
  return v;
}

function sampleProject() {
  const p = newProject('Demo');
  p.wifi = [{ name: 'Atelier', ssid: 'atelier', pwd: 'secret123', dhcp: true }];
  const plc = addEquipment(p, 'Kincony_KC868_A8S');
  const io = addEquipment(p, 'Waveshare_8DIO');
  const servo = addEquipment(p, 'SureServo');
  wire(p, plc, 'di', 1, 'AU_OK', 'input', { inverse: true });
  wire(p, plc, 'do', 1, 'Moteur', 'output');
  wire(p, io, 'di', 2, 'Capteur', 'input', { inverse: true });
  wire(p, io, 'do', 0, 'Voyant', 'output');
  // Signaux du servo sur des relais de l'automate.
  const signals = p.variables.filter((v) => v.system);
  signals.forEach((v, i) => (v.binding = { eq: plc.uid, group: 'do', channel: 5 + i }));
  p.variables.push(normalizeVariable({ label: 'Départ', symbol: 'Depart', kind: 'command', dataType: 'bool' }));
  p.variables.push(normalizeVariable({ label: 'Cible', symbol: 'Cible', kind: 'parameter', dataType: 'int', initial: 5000, min: 0, max: 100000 }));
  p.variables.push(normalizeVariable({ label: 'Cycles', symbol: 'Cycles', kind: 'indicator', dataType: 'int' }));
  return { p, plc, io, servo };
}

test('projet exemple : pas d’erreur, fichiers produits', () => {
  const { p } = sampleProject();
  const r = generateFiles(p, { hardwareFiles });
  assert.deepEqual(r.issues.filter((i) => i.level === 'error'), []);
  assert.deepEqual(r.files.map((f) => f.path), ['data/config.json', 'data/interface.json', 'data/Demo.json']);
});

test('config.json : automate en premier, hash corrects, ids présents dans les fichiers hardware', () => {
  const { p } = sampleProject();
  const r = generateFiles(p, { hardwareFiles });
  const cfg = JSON.parse(r.files[0].content);
  assert.equal(cfg.name, 'demo');
  assert.equal(cfg.type, 2);
  assert.equal(cfg.execution_mode, 'hmi');
  const firstHw = cfg.children.find((s) => s.hardware !== 'virtual');
  assert.equal(firstHw.hardware, 'Kincony_KC868_A8S');
  assert.ok(hardwareFiles[firstHw.hardware].RS485);
  for (const s of cfg.children) {
    for (const c of s.children) {
      assert.equal(c.hash, nodeHash(c.id, s.name));
      if (s.hardware !== 'virtual') assert.ok(hardwareFiles[s.hardware][s.type].some((e) => e.id === c.id), `${s.name} id ${c.id}`);
    }
    if (/^Modbus/.test(s.type)) assert.ok(s.address >= 1 && s.address <= 247);
  }
  // Toutes les entrées de l'automate sont déclarées (diagnostic + RS485 en premier).
  assert.equal(firstHw.children.length, 8);
  // Inversion d'une entrée Kincony.
  assert.equal(firstHw.children[0].inverse, true);
  // Banc Waveshare : inverse sous forme de tableau.
  const bank = cfg.children.find((s) => s.type === 'ModbusReadMultipleInputsStatus');
  assert.deepEqual(bank.children[0].inverse, [false, false, true, false, false, false, false, false]);
  // Sections servo présentes, adresse 127.
  const servoRead = cfg.children.find((s) => s.name === 'Servo registres lus');
  assert.equal(servoRead.address, 127);
  // Hash uniques.
  const hashes = cfg.children.flatMap((s) => s.children.map((c) => c.hash));
  assert.equal(new Set(hashes).size, hashes.length);
});

test('interface.json : chaque hash existe dans config.json, inverse annulé', () => {
  const { p } = sampleProject();
  const r = generateFiles(p, { hardwareFiles });
  const cfg = JSON.parse(r.files[0].content);
  const ui = JSON.parse(r.files[1].content);
  const hashes = new Set(cfg.children.flatMap((s) => s.children.map((c) => c.hash)));
  let n = 0;
  for (const page of ui.pages) {
    assert.ok(page.PageName);
    for (const s of page.sections) {
      assert.ok(s.SectionName);
      for (const node of s.NodesList) {
        assert.ok(hashes.has(node.hash), node.name);
        assert.ok(node.name && node.component);
        assert.ok(new TextEncoder().encode(node.name + s.SectionName).length <= 77);
        n++;
      }
    }
  }
  assert.ok(n >= 8);
  const au = ui.pages.flatMap((pg) => pg.sections.flatMap((s) => s.NodesList)).find((x) => x.name === 'AU_OK');
  assert.equal(au.inverse, false);
});

test('paramètres : fichier data/<Nom>.json', () => {
  const { p } = sampleProject();
  const r = generateFiles(p, { hardwareFiles });
  assert.deepEqual(JSON.parse(r.files[2].content), { 'machine name': 'Demo', Cible: 5000 });
});

test('automate sans horloge : variante du fichier hardware sans RTC', () => {
  const { p, plc } = sampleProject();
  plc.options.rtc = false;
  const r = generateFiles(p, { hardwareFiles });
  const variant = r.files.find((f) => f.path === 'data/hardware/Kincony_KC868_A8S_noRTC.json');
  assert.ok(variant);
  const doc = JSON.parse(variant.content);
  assert.equal(doc.hardware, 'Kincony_KC868_A8S_noRTC');
  assert.equal(doc.I2C.RTC, undefined);
  assert.ok(doc.RS485);
  const cfg = JSON.parse(r.files[0].content);
  assert.equal(cfg.children.find((s) => s.hardware !== 'virtual').hardware, 'Kincony_KC868_A8S_noRTC');
  assert.deepEqual(r.issues.filter((i) => i.level === 'error'), []);
});

test('erreurs détectées : signal servo non câblé, voie en double, module Modbus sans automate', () => {
  const p = newProject('Demo');
  const io = addEquipment(p, 'Waveshare_8DIO');
  addEquipment(p, 'SureServo');
  wire(p, io, 'do', 1, 'A', 'output');
  wire(p, io, 'do', 1, 'B', 'output');
  const r = generateFiles(p, { hardwareFiles });
  const msgs = r.issues.filter((i) => i.level === 'error').map((i) => i.message).join('\n');
  assert.match(msgs, /automate/);
  assert.match(msgs, /même voie/);
  assert.match(msgs, /signal obligatoire/);
});

test('listChannels : voies Waveshare numérotées à partir de 1', () => {
  const p = newProject('Demo');
  const io = addEquipment(p, 'Waveshare_8DIO');
  const names = listChannels(p).filter((c) => c.eq === io && c.group.key === 'di').map((c) => c.name);
  assert.deepEqual(names, ['DI1', 'DI2', 'DI3', 'DI4', 'DI5', 'DI6', 'DI7', 'DI8']);
});

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { generateFiles } from '../shared/generate.js';
import { validateProject } from '../shared/validate.js';
import { buildFromSpec, specFromProject } from '../shared/spec.js';
import { addEquipment, normalizeProject } from '../shared/model.js';
import { Simulator } from '../shared/sim.js';
import { robotCellProject, hardwareFiles, mainCpp } from './fixtures.js';

const build = (p) => generateFiles(p, { hardwareFiles: hardwareFiles(), mainCpp: mainCpp() });
const errors = (issues) => issues.filter((i) => i.level === 'error').map((i) => i.message);

test('Modbus TCP : sections "tcp", élément Ethernet et fichier matériel de l’équipement générique', () => {
  const r = build(robotCellProject());
  assert.deepEqual(errors(r.issues), []);
  const cfg = JSON.parse(r.files.find((f) => f.kind === 'config').content);
  assert.deepEqual(cfg.config[1], { Ethernet: { enabled: true, dhcp: false, ip: '192.168.10.20', gateway: '192.168.10.1', mask: '255.255.255.0' } });
  assert.ok(cfg.config[2].parameters, 'parameters reste après Ethernet');

  const robot = cfg.children.filter((s) => s.hardware === 'Modbus_Robot');
  assert.deepEqual(robot.map((s) => s.type), ['ModbusWriteCoil', 'ModbusReadCoil', 'ModbusWriteHoldingRegister', 'ModbusReadHoldingRegister']);
  for (const s of robot) {
    assert.equal(s.address, 1);
    assert.deepEqual(s.tcp, { ip: '192.168.10.50', port: 502 });
  }
  // Module Waveshare derrière une passerelle TCP -> RTU : même fichier matériel, liaison TCP.
  const io = cfg.children.find((s) => s.name === 'ES distantes Entrees');
  assert.equal(io.hardware, 'Waveshare_8DIO');
  assert.deepEqual(io.tcp, { ip: '192.168.10.60', port: 502 });
  // L'automate reste en tête, sans liaison TCP.
  assert.equal(cfg.children[0].hardware, 'Waveshare_ESP32S3_POE_8DI8DO');
  assert.equal(cfg.children[0].tcp, undefined);

  // Numérotation « documentation » : 1 -> 0, 101 -> 100, 40001 -> 0, 40101 -> 100.
  const hw = JSON.parse(r.files.find((f) => f.path === 'data/hardware/Modbus_Robot.json').content);
  assert.equal(hw.hardware, 'Modbus_Robot');
  assert.equal(hw.registersOffset, 0);
  assert.deepEqual(hw.ModbusWriteCoil[0], { id: 1, offset: 0 });
  assert.deepEqual(hw.ModbusReadCoil[1], { id: 2, offset: 101 });
  assert.deepEqual(hw.ModbusWriteHoldingRegister[15], { id: 16, offset: 15 });
  assert.deepEqual(hw.ModbusReadHoldingRegister[0], { id: 1, offset: 100 });
  assert.deepEqual(hw.ModbusReadInputRegister[0], { id: 1, offset: 0 });
});

test('Modbus TCP : registres écrits accessibles en C++ (Uint16OutputNode) et repli à 0', () => {
  const r = build(robotCellProject());
  const src = r.files.find((f) => f.path === 'src/CelluleRobot/CelluleRobot.cpp').content;
  const hdr = r.files.find((f) => f.path === 'src/CelluleRobot/CelluleRobot.h').content;
  assert.match(hdr, /Uint16OutputNode\* n_Programme = nullptr;/);
  assert.match(src, /set_Programme\(v_NumProg\(\)\);/);
  assert.match(src, /set_Programme\(0\);/);
  assert.doesNotMatch(src, /set_Programme\(false\)/);
});

test('Modbus TCP : règles de validation (IP, unités, ports, Ethernet)', () => {
  // Adresse RTU et n° d'unité TCP sont indépendants.
  const ok = robotCellProject();
  const rtu = addEquipment(ok, 'Waveshare_8DIO', 'ES locales');
  assert.equal(rtu.options.transport, 'rtu');
  assert.equal(rtu.address, 1, 'adresse libre sur le bus RS485 : les équipements TCP ne comptent pas');
  assert.deepEqual(errors(validateProject(ok)), []);

  const p = robotCellProject();
  p.equipment.find((e) => e.label === 'Robot').options.ip = '192.168.10.300';
  assert.ok(errors(validateProject(p)).some((m) => /Robot : adresse IP Modbus TCP invalide/.test(m)));

  const q = robotCellProject();
  const io = q.equipment.find((e) => e.label === 'ES distantes');
  io.options.ip = '192.168.10.50';
  io.options.port = 503;
  assert.ok(errors(validateProject(q)).some((m) => /même adresse IP 192\.168\.10\.50 sur deux ports/.test(m)));
  io.options.port = 502;
  assert.ok(errors(validateProject(q)).some((m) => /même adresse Modbus TCP \(192\.168\.10\.50:502, unité 1\)/.test(m)));
  io.address = 2; // deux unités derrière la même passerelle : accepté
  assert.deepEqual(errors(validateProject(q)), []);

  const e = robotCellProject();
  e.ethernet.ip = '192.168.10';
  assert.ok(errors(validateProject(e)).some((m) => /Ethernet : adresse « ip » invalide/.test(m)));

  const k = robotCellProject();
  k.equipment = k.equipment.filter((x) => x.type !== 'Waveshare_ESP32S3_POE_8DI8DO');
  addEquipment(k, 'M5Stack_StamPLC');
  assert.ok(errors(validateProject(k)).some((m) => /n'a pas de port Ethernet/.test(m)));

  const ap = robotCellProject();
  ap.ethernet.enabled = false;
  ap.wifi = [{ name: 'AP', mode: 'access_point', ap_name: 'cellule', ap_password: 'motdepasse', ap_ip: '192.168.4.1', ap_channel: 6 }];
  assert.ok(validateProject(ap).some((i) => i.level === 'warning' && /sans WiFi station ni Ethernet/.test(i.message)));
});

test('Modbus TCP : export-spec puis build conserve liaison, IP et Ethernet', () => {
  const p = normalizeProject(robotCellProject());
  const spec = specFromProject(p);
  assert.equal(spec.ethernet.ip, '192.168.10.20');
  const robot = spec.equipment.find((e) => e.type === 'ModbusGeneric');
  assert.equal(robot.options.transport, 'tcp');
  assert.equal(robot.options.ip, '192.168.10.50');
  const r = buildFromSpec(spec);
  assert.deepEqual(r.errors, []);
  assert.deepEqual(r.project.ethernet, p.ethernet);
  const io = r.project.equipment.find((e) => e.type === 'Waveshare_8DIO');
  assert.equal(io.options.transport, 'tcp');
});

test('Modbus TCP : simulation de la poignée de main robot (registre écrit, repli)', () => {
  const sim = new Simulator(robotCellProject());
  sim.set('AU_OK', true);
  sim.set('Robot_pret', true);
  sim.set('Piece', true);
  sim.run(2000);
  assert.equal(sim.mode, 'IDLE');
  sim.set('Depart', true);
  sim.run(50);
  assert.deepEqual(sim.activeSteps(), [1]);
  assert.equal(sim.get('Programme'), 3);
  assert.equal(sim.get('Depart_robot'), true);
  sim.set('Robot_fini', true);
  sim.run(50);
  assert.deepEqual(sim.activeSteps(), [2]);
  assert.equal(sim.get('Depart_robot'), false);
  // Registre 16 bits : -1 devient 65535 comme dans le firmware.
  sim.set('Vitesse_robot', -1);
  assert.equal(sim.get('Vitesse_robot'), 65535);
  // Arrêt d'urgence : Programme forcé à 0, Vitesse_robot maintenue.
  sim.set('AU_OK', false);
  sim.run(50);
  assert.equal(sim.mode, 'EMERGENCY');
  assert.equal(sim.get('Programme'), 0);
  assert.equal(sim.get('Vitesse_robot'), 65535);
});

test('Robots AUBO / FAIRINO et pas-à-pas StepperOnline : génération, registres signés, écriture au repos', async () => {
  const { robotIslandProject } = await import('./fixtures.js');
  const r = build(robotIslandProject());
  assert.deepEqual(errors(r.issues), []);
  const cfg = JSON.parse(r.files.find((f) => f.kind === 'config').content);
  const fr = cfg.children.find((s) => s.name === 'Fairino Depart');
  assert.deepEqual(fr.tcp, { ip: '192.168.58.2', port: 502 }, 'IP d’usine FAIRINO par défaut');
  assert.equal(cfg.children.find((s) => s.name === 'Convoyeur registres lus').tcp, undefined);
  assert.deepEqual(cfg.children.find((s) => s.name === 'Ascenseur registres lus').tcp, { ip: '192.168.10.70', port: 502 });

  const src = r.files.filter((f) => f.kind === 'cpp').map((f) => f.content).join('\n');
  assert.match(src, /new StepperOnlineRS\(StepperOnlineRSModel::DM882RS, axNodes_Convoyeur, "Convoyeur"\)/);
  assert.match(src, /new StepperOnlineRS\(StepperOnlineRSModel::CL86RS, axNodes_Ascenseur, "Ascenseur"\)/);
  assert.match(src, /axDrive_Convoyeur->setAccelTime\(500\);/);
  assert.match(src, /axDrive_Convoyeur->setTorqueReferenceCurrent\(60\);/);
  assert.match(src, /axNodes_Rotation\.validateRequired\(StepperOnlineFamily::A6RS\)/);
  assert.match(src, /new StepperOnlineServo\(StepperOnlineFamily::A6RS, axNodes_Rotation, "Rotation"\)/);
  assert.match(src, /axDrive_Rotation->setAccelTime\(150\);/);
  assert.match(src, /new StepperOnlineServo\(StepperOnlineFamily::T6, axNodes_Pince, "Pince"\)/);
  assert.doesNotMatch(src, /axDrive_Pince->setAccelTime/);
  // Position affichée : recopiée de l'AxisController (mots 32 bits remis dans l'ordre par le drive).
  assert.match(src, /initNodePtr\(axDisp_Ascenseur_displayPosition,/);
  assert.match(src, /axDisp_Ascenseur_displayPosition->setValueFromInt32\(_p\)/);
  assert.doesNotMatch(src, /axNodes_Ascenseur\.displayPosition/);
  // Positions robot signées, sorties Modbus écrites au démarrage.
  assert.match(src, /v_Aubo_X\(\) \{ return \(int32_t\)\(int16_t\)n_Aubo_X->getValue\(\); \}/);
  assert.match(src, /v_Aubo_pret\(\) \{ return \(int32_t\)n_Aubo_pret->getValue\(\); \}/);
  assert.match(src, /if \(n_Fr_start\) n_Fr_start->forceIsChanged\(\);/);
  assert.match(src, /if \(bank_0\) bank_0->forceIsChanged\(\);/);
  assert.doesNotMatch(src, /n_Aubo_X->forceIsChanged/);

  // Simulation : les deux axes StepperOnline prennent leur origine puis se déplacent.
  const sim = new Simulator(robotIslandProject());
  sim.set('AU_OK', true);
  sim.run(3000);
  assert.equal(sim.mode, 'IDLE');
  sim.set('Aubo_pret', 1);
  sim.set('Depart', true);
  sim.run(50);
  assert.deepEqual(sim.activeSteps(), [1]);
  assert.equal(sim.get('Aubo_prog'), 2);
  assert.equal(sim.get('Fr_start'), true);
  sim.run(5000);
  assert.ok(sim.axisByUid.get(robotIslandProjectUid(sim, 'Ascenseur')).position() > 0);
});

function robotIslandProjectUid(sim, label) {
  return [...sim.axisByUid.values()].find((a) => a.label === label).uid;
}

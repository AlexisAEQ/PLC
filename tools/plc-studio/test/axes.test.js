import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Simulator } from '../shared/sim.js';
import { generateFiles } from '../shared/generate.js';
import { buildFromSpec, specFromProject } from '../shared/spec.js';
import { normalizeProject } from '../shared/model.js';
import { multiAxisProject, pressProject, hardwareFiles, mainCpp } from './fixtures.js';

const ctx = () => ({ hardwareFiles: hardwareFiles(), mainCpp: mainCpp() });

test('plusieurs axes : génération sans erreur, un AxisController par axe', () => {
  const g = generateFiles(multiAxisProject(), ctx());
  assert.deepEqual(g.issues.filter((i) => i.level === 'error'), []);
  const h = g.files.find((f) => f.path.endsWith('TableXYZ.h')).content;
  const c = g.files.find((f) => f.path.endsWith('TableXYZ.cpp')).content;
  for (const ax of ['Axe_X', 'Axe_Y', 'Axe_Z']) assert.match(h, new RegExp(`AxisController ax_${ax}\\{`));
  assert.match(h, /std::unique_ptr<SureServo> axDrive_Axe_X;/);
  assert.match(h, /std::unique_ptr<StepperMotor> axDrive_Axe_Z;/);
  assert.match(c, /new StepperMotor\(1, axNodes_Axe_Z, 20000, "Axe Z", 5000\)/);
  assert.match(c, /axNodes_Axe_Z\.stepPin = \(n_Axe_Z_step && n_Axe_Z_step->classType\(\) == CLASS_HW_BOOLEAN_OUTPUT_NODE\)/);
  assert.match(c, /axNodes_Axe_Z\.homeSwitch = n_Axe_Z_home;/);
  assert.doesNotMatch(c, /axNodes_Axe_Z\.limitSwitchPositive/); // signal facultatif non câblé
  assert.match(c, /if \(!ax_Axe_X\.moveTo\(\(int32_t\)\(v_CibleX\(\)\), \(int32_t\)\(10\)\)\) go\(State::EMERGENCY\);/);
  // Prises d'origine : Z d'abord (ordre 1), puis X et Y ensemble (ordre 2).
  assert.ok(c.indexOf('// ordre 1 : Axe Z') < c.indexOf('// ordre 2 : Axe X, Axe Y'));
  const config = JSON.parse(g.files.find((f) => f.path === 'data/config.json').content);
  const names = config.children.map((s) => s.name);
  assert.ok(names.includes('Axe X registres lus') && names.includes('Axe Y registres lus') && names.includes('Axe Z etat'));
});

test('plusieurs axes : simulation du cycle, origines dans l’ordre, jog d’un seul axe', () => {
  const sim = new Simulator(multiAxisProject());
  sim.set('AU_OK', true);
  sim.run(5000);
  assert.equal(sim.mode, 'IDLE');
  const modes = sim.journal.filter((j) => j.kind === 'mode').map((j) => j.text);
  assert.deepEqual(modes, ['INDÉFINI → INITIALISATION', 'INITIALISATION → ARRÊT', "ARRÊT → PRISE D'ORIGINE", "PRISE D'ORIGINE → REPOS"]);
  assert.ok(sim.axes.every((a) => a.homeDone()));
  sim.set('Depart', true);
  sim.run(20);
  assert.deepEqual(sim.activeSteps(), [1]);
  sim.run(8000);
  assert.ok(sim.activeSteps().includes(2) || sim.activeSteps().includes(3) || sim.activeSteps().includes(0), sim.activeSteps().join());
  sim.set('Depart', false);
  sim.run(10000);
  assert.deepEqual(sim.activeSteps(), [0]);
  assert.equal(sim.axis('Axe_X').position(), 0);
  // Jog de Z seulement.
  sim.set('Manuel', true);
  sim.run(20);
  assert.equal(sim.mode, 'JOGGING');
  sim.set('JogZP', true);
  sim.run(500);
  assert.ok(sim.axis('Axe_Z').position() > 0);
  assert.equal(sim.axis('Axe_X').position(), 0);
});

test('plusieurs axes : alarme d’un axe -> urgence nommée, réarmement de tous les axes', () => {
  const sim = new Simulator(multiAxisProject());
  sim.set('AU_OK', true);
  sim.run(5000);
  sim.axis('Axe_Y').drive.raiseAlarm(0x0301);
  sim.run(20);
  assert.equal(sim.mode, 'EMERGENCY');
  assert.ok(sim.messages.some((m) => /Alarme Axe Y 0x0301/.test(m.text)));
  sim.set('Acquit', true);
  sim.run(5000);
  assert.equal(sim.mode, 'IDLE');
  assert.ok(sim.messages.some((m) => m.text === 'Réarmement des axes…'));
});

test('plusieurs axes : spec aller-retour (SERVO_MOVE[axe], modes par axe, signaux par équipement)', () => {
  const p = normalizeProject(multiAxisProject());
  const spec = specFromProject(p);
  assert.ok(spec.grafcet.steps[1].actions.includes('SERVO_MOVE[axe_x] CibleX @ 10'));
  assert.deepEqual(Object.keys(spec.modes.axes).sort(), ['axe_x', 'axe_y', 'axe_z']);
  assert.equal(spec.equipment.find((e) => e.id === 'axe_z').signals.step, 'plc.GPIO38');
  const r = buildFromSpec(JSON.parse(JSON.stringify(spec)));
  assert.deepEqual(r.errors, []);
  const a = generateFiles(p, ctx());
  const b = generateFiles(r.project, ctx());
  for (const f of a.files) assert.equal(b.files.find((x) => x.path === f.path).content, f.content, f.path);
});

test('validation : STEP sur un relais refusé, deux axes au même nom refusés', () => {
  const p = multiAxisProject();
  const step = p.variables.find((v) => v.system?.signal === 'step');
  step.binding = { eq: p.equipment[0].uid, group: 'do', channel: 12 };
  p.equipment.find((e) => e.label === 'Axe Y').label = 'Axe_X';
  const g = generateFiles(p, ctx());
  const msgs = g.issues.filter((i) => i.level === 'error').map((i) => i.message).join('\n');
  assert.match(msgs, /sortie GPIO directe/);
  assert.match(msgs, /même nom dans les expressions/);
});

test('ancien projet à un seul servo : bloc servo et références converties', () => {
  const old = pressProject();
  old.hmi = { auto: false, pages: [{ id: 'p', name: 'Servo', sections: [{ id: 's', name: 'S', items: [{ ref: 'servo:position', label: 'Pos', widget: 'rx-numeric' }] }] }] };
  const p = normalizeProject(old);
  const uid = p.equipment.find((e) => e.type === 'SureServo').uid;
  assert.equal(p.logic.blocks.servo, undefined);
  assert.equal(p.logic.blocks.axes[uid].jogMode, 'Jog');
  assert.equal(p.logic.steps[2].actions[0].axis, uid);
  assert.equal(p.hmi.pages[0].sections[0].items[0].ref, `axis:${uid}:position`);
});

test('axes Lichuan A6 et A5 : génération et simulation', () => {
  const p = multiAxisProject({ lichuan: true });
  const g = generateFiles(p, ctx());
  assert.deepEqual(g.issues.filter((i) => i.level === 'error'), []);
  const c = g.files.find((f) => f.path.endsWith('TableXYZ.cpp')).content;
  assert.match(c, /new LichuanServo\(LichuanFamily::A6, axNodes_Plateau, "Plateau"\)/);
  assert.match(c, /axDrive_Plateau->setAccelTime\(200\);/);
  assert.match(c, /new LichuanServo\(LichuanFamily::A5, axNodes_Convoyeur, "Convoyeur"\)/);
  const config = JSON.parse(g.files.find((f) => f.path === 'data/config.json').content);
  const sec = config.children.find((s) => s.name === 'Plateau registres ecrits');
  assert.equal(sec.hardware, 'Lichuan_A6');
  assert.equal(sec.address, 3);
  const sim = new Simulator(p);
  sim.set('AU_OK', true);
  sim.run(6000);
  assert.equal(sim.mode, 'IDLE');
  sim.set('Depart', true);
  sim.run(3000);
  assert.ok(sim.axis('Plateau').position() > 0);
});

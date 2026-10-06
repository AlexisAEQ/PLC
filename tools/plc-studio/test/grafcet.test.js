import { test } from 'node:test';
import assert from 'node:assert/strict';
import { compileLogic } from '../shared/grafcet.js';
import { generateFiles } from '../shared/generate.js';
import { patchMainCpp } from '../shared/cpp.js';
import { pressProject, hardwareFiles, mainCpp } from './fixtures.js';

test('le projet presse compile sans erreur', () => {
  const { ir, issues } = compileLogic(pressProject());
  assert.deepEqual(issues.filter((i) => i.level === 'error'), []);
  assert.equal(ir.steps.length, 6);
  assert.deepEqual(ir.transitions[1].to, [2, 3]); // divergence ET
  assert.deepEqual(ir.transitions[2].from, [2, 3]); // convergence ET
  assert.ok(ir.edges.some((e) => e.key === 'var:Depart'));
  assert.deepEqual(
    ir.driven.map((d) => d.sym).sort(),
    ['Mem', 'Soufflage', 'Verin', 'Voyant'].sort()
  );
});

test('erreurs de logique détectées', () => {
  const p = pressProject();
  p.logic.transitions[0].condition = 'Depart ET';
  p.logic.steps[1].actions.push({ type: 'N', target: 'Depart' }); // entrée : non écrivable
  p.logic.steps[4].actions.push({ type: 'N', target: 'Servo_servoOn' }); // signal système
  p.logic.steps[0].initial = false;
  const msgs = compileLogic(p)
    .issues.filter((i) => i.level === 'error')
    .map((i) => i.message)
    .join('\n');
  assert.match(msgs, /Transition 0 : Expression incomplète/);
  assert.match(msgs, /« Depart » ne peut pas être écrite/);
  assert.match(msgs, /« Servo_servoOn » ne peut pas être écrite/);
  assert.match(msgs, /étape initiale/);
});

test('génération complète : C++, main.cpp et données', () => {
  const r = generateFiles(pressProject(), { hardwareFiles: hardwareFiles(), mainCpp: mainCpp() });
  assert.deepEqual(r.issues.filter((i) => i.level === 'error'), []);
  const paths = r.files.map((f) => f.path);
  assert.ok(paths.includes('src/DemoPresse/DemoPresse.h'));
  assert.ok(paths.includes('src/DemoPresse/DemoPresse.cpp'));
  assert.ok(paths.includes('src/main.cpp'));
  assert.ok(r.installable);
  const h = r.files.find((f) => f.path.endsWith('.h')).content;
  const cfg = JSON.parse(r.files.find((f) => f.path === 'data/config.json').content);
  // Chaque constante d'identifiant du .h existe dans config.json.
  const hashes = new Set(cfg.children.flatMap((s) => s.children.map((c) => c.hash)));
  const consts = [...h.matchAll(/constexpr uint32_t \w+ = (\d+)u;/g)].map((m) => Number(m[1]));
  assert.ok(consts.length > 30);
  for (const c of consts) assert.ok(hashes.has(c), `hash ${c}`);
  assert.match(h, /class DemoPresse : public BusinessLogic/);
  assert.match(h, /#include "BusinessLogic\/BusinessLogic.inl"/);
});

test('main.cpp : bascule de RessortRoyal2 vers la classe générée, idempotente', () => {
  const main = mainCpp();
  const a = patchMainCpp(main, 'DemoPresse');
  assert.ok(a.ok && a.changed);
  assert.equal(a.oldClass, 'RessortRoyal2');
  assert.match(a.content, /#include "DemoPresse\/DemoPresse.h"/);
  assert.match(a.content, /plcApp = new DemoPresse\(\);/);
  assert.doesNotMatch(a.content, /\bRessortRoyal2\b|\bressortRoyal2\b/);
  const b = patchMainCpp(a.content, 'DemoPresse');
  assert.ok(b.ok && !b.changed);
  const c = patchMainCpp(a.content, 'AutreProjet');
  assert.match(c.content, /plcApp = new AutreProjet\(\);/);
});

test('sans servo : pas de SureServo dans le code généré', () => {
  const r = generateFiles(pressProject({ servo: false }), { hardwareFiles: hardwareFiles(), mainCpp: mainCpp() });
  assert.deepEqual(r.issues.filter((i) => i.level === 'error'), []);
  const h = r.files.find((f) => f.path.endsWith('.h')).content;
  assert.doesNotMatch(h, /SureServo/);
});

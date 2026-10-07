import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { buildFromSpec, specFromProject, parseAction, formatAction, layoutGrafcet } from '../shared/spec.js';
import { runScenario, runScenarios, parseDuration, modeKey } from '../shared/scenario.js';
import { generateFiles } from '../shared/generate.js';
import { normalizeProject } from '../shared/model.js';
import { parseJsonc } from '../shared/jsonc.js';
import { hardwareFiles, mainCpp, repo } from './fixtures.js';

const read = (p) => parseJsonc(readFileSync(repo + p, 'utf8'));
const example = () => read('.claude/skills/plc-projet/examples/tri-colis.spec.json');

test('actions en texte court : analyse et réécriture', () => {
  const cases = {
    'N Verin': { type: 'N', target: 'Verin' },
    'N Soufflage si Piece ET NON Defaut': { type: 'N', target: 'Soufflage', condition: 'Piece ET NON Defaut' },
    'S Mem': { type: 'S', target: 'Mem' },
    'INC Pieces': { type: 'INC', target: 'Pieces' },
    'SET Compteur := 0': { type: 'SET', target: 'Compteur', value: '0' },
    'NSET Consigne = Cible * 2': { type: 'NSET', target: 'Consigne', value: 'Cible * 2' },
    'SERVO_MOVE MIN(Cible, 5000) @ Vitesse': { type: 'SERVO_MOVE', position: 'MIN(Cible, 5000)', speed: 'Vitesse' },
    SERVO_HOME: { type: 'SERVO_HOME' },
    'MSG success: Pièce terminée': { type: 'MSG', level: 'success', text: 'Pièce terminée' },
    'MSG Bourrage : vérifier': { type: 'MSG', level: 'info', text: 'Bourrage : vérifier' },
  };
  for (const [src, want] of Object.entries(cases)) {
    assert.deepEqual(parseAction(src), want, src);
    assert.deepEqual(parseAction(formatAction(want)), want, `aller-retour ${src}`);
  }
  assert.throws(() => parseAction('FOO x'), /inconnu/);
  assert.throws(() => parseAction('SERVO_MOVE 100'), /@/);
});

test('projet de démonstration : projet -> spec -> projet produit des fichiers identiques', () => {
  const demo = normalizeProject(read('projets/DemoPresse.plc.json'));
  const spec = specFromProject(demo);
  const r = buildFromSpec(JSON.parse(JSON.stringify(spec)));
  assert.deepEqual(r.errors, []);
  const ctx = { hardwareFiles: hardwareFiles(), mainCpp: mainCpp() };
  const a = generateFiles(demo, ctx);
  const b = generateFiles(r.project, ctx);
  assert.deepEqual(b.files.map((f) => f.path), a.files.map((f) => f.path));
  for (const f of a.files) assert.equal(b.files.find((x) => x.path === f.path).content, f.content, f.path);
  // Positions du grafcet conservées.
  assert.deepEqual(r.project.logic.steps.map((s) => [s.x, s.y]), demo.logic.steps.map((s) => [s.x, s.y]));
});

test('exemple du skill : projet valide, installable, scénarios réussis', () => {
  const spec = example();
  const r = buildFromSpec(spec);
  assert.deepEqual(r.errors, []);
  assert.deepEqual(r.issues.filter((i) => i.level === 'error'), []);
  const g = generateFiles(r.project, { hardwareFiles: hardwareFiles(), mainCpp: mainCpp() });
  assert.deepEqual(g.issues.filter((i) => i.level === 'error'), []);
  assert.ok(g.installable);
  // Le projet produit est stable une fois relu par l'éditeur (normalizeProject).
  assert.deepEqual(normalizeProject(JSON.parse(JSON.stringify(r.project))), r.project);
  for (const res of runScenarios(r.project, spec.scenarios)) assert.ok(res.passed, `${res.name}\n${res.log.join('\n')}`);
  // Écrans explicites : références résolues, libellés et widgets complétés.
  assert.equal(r.project.hmi.auto, false);
  const items = r.project.hmi.pages.flatMap((p) => p.sections.flatMap((s) => s.items));
  assert.ok(items.every((i) => i.label && i.widget && i.ref));
  assert.ok(items.some((i) => i.ref === 'sys:state'));
  assert.equal(r.project.hmi.pages[1].password, '1234');
});

test('mise en page automatique : étapes sur la grille, branches sans chevauchement', () => {
  const r = buildFromSpec(example());
  const steps = r.project.logic.steps;
  const pos = new Set(steps.map((s) => `${s.x},${s.y}`));
  assert.equal(pos.size, steps.length);
  const byNum = Object.fromEntries(steps.map((s) => [s.num, s]));
  assert.ok(byNum[1].y > byNum[0].y && byNum[2].y > byNum[1].y && byNum[3].y > byNum[2].y);
  assert.equal(byNum[2].y, byNum[4].y); // branches OU au même niveau
  assert.ok(byNum[4].x - byNum[2].x >= 260);
  for (const t of r.project.logic.transitions) assert.ok(Number.isFinite(t.x) && Number.isFinite(t.y));
  // Divergence ET : étapes côte à côte, convergence en dessous.
  const logic = {
    steps: [0, 1, 2, 3].map((n) => ({ id: `s${n}`, num: n, initial: n === 0, actions: [] })),
    transitions: [
      { id: 't0', from: ['s0'], to: ['s1', 's2'], condition: 'a' },
      { id: 't1', from: ['s1', 's2'], to: ['s3'], condition: 'b' },
      { id: 't2', from: ['s3'], to: ['s0'], condition: 'c' },
    ],
  };
  layoutGrafcet(logic);
  const [s0, s1, s2, s3] = logic.steps;
  assert.equal(s1.y, s2.y);
  assert.ok(s2.x > s1.x);
  assert.ok(s3.y > s1.y && s0.y < s1.y);
});

test('erreurs de spec explicites', () => {
  const spec = example();
  spec.io[0].at = 'plc.X9';
  spec.io.push({ symbol: 'Moteur', label: 'Doublon', at: 'plc.Y5' });
  spec.grafcet.transitions.push({ from: 4, to: 42, condition: 'VRAI' });
  spec.hmi.pages[0].sections[0].items.push('Inconnu');
  const r = buildFromSpec(spec);
  const all = r.errors.join('\n');
  assert.match(all, /voie « X9 » absente/);
  assert.match(all, /symbole « Moteur » déjà utilisé/);
  assert.match(all, /étape aval 42 inexistante/);
  assert.match(all, /symbole « Inconnu » inconnu/);
});

test('scénarios : échec détaillé, durées et modes', () => {
  assert.equal(parseDuration('1,5s'), 1500);
  assert.equal(parseDuration('2min'), 120000);
  assert.equal(parseDuration(250), 250);
  assert.throws(() => parseDuration('5 secondes'));
  assert.equal(modeKey('Arrêt'), 'STOP');
  assert.equal(modeKey('PRISE D’ORIGINE'), 'HOMING');
  assert.equal(modeKey('RUNNING'), 'RUNNING');
  const { project } = buildFromSpec(example());
  const res = runScenario(project, {
    name: 'faux',
    steps: [{ set: { AU_OK: true } }, { until: { mode: 'CYCLE' }, timeout: '300ms' }, { expect: { Moteur: true } }, { expect: 'X0 ET Moteur' }],
  });
  assert.equal(res.passed, false);
  assert.equal(res.failures.length, 3);
  assert.match(res.failures[0].message, /délai de 300 ms dépassé/);
  assert.match(res.failures[1].message, /Moteur = false au lieu de true/);
  const bad = runScenario(project, { steps: [{ set: { Nope: 1 } }] });
  assert.match(bad.failures[0].message, /symbole inconnu/);
});

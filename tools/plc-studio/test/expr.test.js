import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parse, tryParse, evaluate, toCpp } from '../shared/expr.js';

const vars = {
  Depart: 'bool',
  Porte: 'bool',
  Compteur: 'int',
  Nb: 'int',
  Temp: 'float',
  Recette: 'text',
};
const ctx = {
  axes: new Map([
    ['Servo', { uid: 'eq_servo', symbol: 'Servo', label: 'Servo' }],
    ['Axe_X', { uid: 'eq_x', symbol: 'Axe_X', label: 'Axe X' }],
  ]),
  steps: new Set([0, 1, 3]),
  resolve: (n) => (vars[n] ? { symbol: n, type: vars[n] } : null),
};

function env(values, extra = {}) {
  return {
    get: (s) => values[s],
    step: (n) => !!extra.steps?.[n],
    stepTime: (n) => extra.times?.[n] ?? 0,
    axis: (uid, p) => extra.axes?.[uid]?.[p],
    prev: (k) => extra.prev?.[k],
  };
}

test('syntaxe française et symbolique équivalentes', () => {
  const a = parse('Depart ET NON Porte', ctx);
  const b = parse('Depart && !Porte', ctx);
  const c = parse('Depart and not Porte', ctx);
  assert.deepEqual(a, b);
  assert.deepEqual(b, c);
  assert.equal(evaluate(a, env({ Depart: true, Porte: false })), true);
  assert.equal(evaluate(a, env({ Depart: true, Porte: true })), false);
});

test('priorités : ET avant OU, comparaisons, arithmétique', () => {
  const e = parse('Depart OU Porte ET Compteur + 1 >= Nb * 2', ctx);
  assert.equal(evaluate(e, env({ Depart: false, Porte: true, Compteur: 3, Nb: 2 })), true);
  assert.equal(evaluate(e, env({ Depart: false, Porte: true, Compteur: 2, Nb: 2 })), false);
});

test('durées et temps d’étape', () => {
  const a = parse('X3.t >= 5s', ctx);
  const b = parse('t/X3/5s', ctx);
  assert.equal(evaluate(a, env({}, { times: { 3: 5000 } })), true);
  assert.equal(evaluate(b, env({}, { times: { 3: 4999 } })), false);
  assert.equal(parse('200ms', ctx).v, 200);
  assert.equal(parse('2min', ctx).v, 120000);
});

test('fronts montants et descendants', () => {
  const fm = parse('FM(Depart)', ctx);
  const fm2 = parse('↑Depart', ctx);
  assert.deepEqual(fm, fm2);
  assert.equal(evaluate(fm, env({ Depart: true }, { prev: { 'var:Depart': false } })), true);
  assert.equal(evaluate(fm, env({ Depart: true }, { prev: { 'var:Depart': true } })), false);
  const fd = parse('FD(Depart)', ctx);
  assert.equal(evaluate(fd, env({ Depart: false }, { prev: { 'var:Depart': true } })), true);
});

test('axes et textes', () => {
  assert.equal(parse('Servo.enPosition', ctx).type, 'bool');
  assert.equal(parse('Servo.position > 1000', ctx).type, 'bool');
  const x = parse('Axe_X.enPosition ET Servo.pret', ctx);
  assert.deepEqual(x.a, { k: 'axis', axis: 'eq_x', prop: 'enPosition', type: 'bool' });
  assert.equal(evaluate(x, env({}, { axes: { eq_x: { enPosition: true }, eq_servo: { pret: true } } })), true);
  assert.equal(evaluate(x, env({}, { axes: { eq_x: { enPosition: true }, eq_servo: { pret: false } } })), false);
  // « Servo » désigne l'axe unique d'un projet, quel que soit son nom (anciens projets).
  const single = { ...ctx, axes: new Map([['Table', { uid: 'eq_t', symbol: 'Table', label: 'Table' }]]), defaultAxis: 'eq_t' };
  assert.equal(parse('Servo.origineFaite', single).axis, 'eq_t');
  assert.equal(parse('table.origineFaite', single).axis, 'eq_t');
  const t = parse('Recette = "A"', ctx);
  assert.equal(evaluate(t, env({ Recette: 'A' })), true);
  assert.equal(toCpp(t, { var: (s) => `v_${s}()` }), '(strcmp(v_Recette(), "A") == 0)');
});

test('arithmétique entière comme en C++', () => {
  assert.equal(evaluate(parse('7 / 2', ctx), env({})), 3);
  assert.equal(evaluate(parse('-7 / 2', ctx), env({})), -3);
  assert.equal(evaluate(parse('7 / 0', ctx), env({})), 0);
  assert.equal(evaluate(parse('7.0 / 2', ctx), env({})), 3.5);
  assert.equal(evaluate(parse('MAX(3, Compteur) + ABS(-2)', ctx), env({ Compteur: 10 })), 12);
});

test('messages d’erreur en français', () => {
  assert.match(tryParse('Depart ET', ctx).error, /incomplète/);
  assert.match(tryParse('Inconnu', ctx).error, /Variable inconnue « Inconnu »/);
  assert.match(tryParse('Depart + 1', ctx).error, /numérique attendue/);
  assert.match(tryParse('Compteur ET Depart', ctx).error, /booléen attendu/);
  assert.match(tryParse('X9', ctx).error, /L’étape 9 n’existe pas/);
  assert.match(tryParse('Servo.vitesse', ctx).error, /Propriété d’axe inconnue/);
  assert.match(tryParse('Axe_Y.pret', ctx).error, /Axe inconnu « Axe_Y »/);
  assert.match(tryParse('Servo.pret', { ...ctx, axes: new Map() }).error, /Aucun axe/);
  assert.match(tryParse('(Depart', ctx).error, /Parenthèse/);
  assert.match(tryParse('FM(Compteur)', ctx).error, /FM s’applique/);
  assert.match(tryParse('Recette < "B"', ctx).error, /texte/);
});

test('traduction C++', () => {
  const names = {
    var: (s) => `v_${s}()`,
    step: (n) => `X[${n}]`,
    stepTime: (n) => `stepTime(${n})`,
    axis: (uid, p) => `${uid}.${p}()`,
    prev: (k) => `E["${k}"]`,
  };
  assert.equal(toCpp(parse('Depart ET NON Porte', ctx), names), '(v_Depart() && !v_Porte())');
  assert.equal(toCpp(parse('FM(Depart) OU X1', ctx), names), '((v_Depart() && !E["var:Depart"]) || X[1])');
  assert.equal(toCpp(parse('Compteur / Nb >= 2', ctx), names), '(plcDivI(v_Compteur(), v_Nb()) >= 2)');
  assert.equal(toCpp(parse('Temp > 1.5', ctx), names), '(v_Temp() > 1.5f)');
  assert.equal(toCpp(parse('X3.t >= 5s', ctx), names), '(stepTime(3) >= 5000)');
});

test('durée t/X/... : unité obligatoire ; débordement entier 32 bits comme en C++', () => {
  assert.match(tryParse('t/X3/5', ctx).error, /unité/);
  assert.equal(evaluate(parse('Compteur * Nb', ctx), env({ Compteur: 100000, Nb: 100000 })), Math.imul(100000, 100000));
  assert.equal(evaluate(parse('Compteur + 1', ctx), env({ Compteur: 2147483647 })), -2147483648);
});

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Simulator } from '../shared/sim.js';
import { pressProject } from './fixtures.js';

function boot(project = pressProject()) {
  const sim = new Simulator(project);
  sim.set('AU_OK', true);
  sim.set('Piece', true);
  sim.run(3000);
  return sim;
}

test('démarrage : initialisation du servo, prise d’origine puis repos', () => {
  const sim = boot();
  assert.equal(sim.mode, 'IDLE');
  assert.deepEqual(sim.activeSteps(), [0]);
  assert.equal(sim.get('Voyant'), true); // action N de l'étape initiale
  assert.ok(sim.servo.homeDone);
  const modes = sim.journal.filter((j) => j.kind === 'mode').map((j) => j.text);
  assert.deepEqual(modes, ['INDÉFINI → INITIALISATION', 'INITIALISATION → ARRÊT', "ARRÊT → PRISE D'ORIGINE", "PRISE D'ORIGINE → REPOS"]);
});

test('cycle complet : front montant, divergence ET, servo, compteur, retour', () => {
  const sim = boot();
  sim.set('Depart', true);
  sim.run(50);
  assert.deepEqual(sim.activeSteps(), [1]);
  assert.equal(sim.mode, 'RUNNING');
  assert.equal(sim.get('Verin'), true);
  assert.equal(sim.get('Mem'), true); // S Mem
  sim.set('Depart', false);
  sim.set('Verin_sorti', true);
  sim.run(50);
  assert.deepEqual(sim.activeSteps(), [2, 3]); // divergence ET
  assert.equal(sim.get('Soufflage'), true); // N si Piece
  sim.run(400);
  assert.ok(sim.servo.position > 0 && sim.servo.position <= 50000, `position ${sim.servo.position}`);
  sim.run(2000); // X3.t >= 1500 ms et Servo.enPosition
  assert.equal(sim.get('Pieces'), 1);
  assert.equal(sim.get('Mem'), false); // R Mem
  assert.ok(sim.messages.some((m) => m.text === 'Pièce terminée'));
  sim.set('Verin_sorti', false);
  sim.run(2000);
  assert.deepEqual(sim.activeSteps(), [0]);
  assert.equal(sim.mode, 'IDLE');
  assert.equal(sim.servo.position, 0);
});

test('front montant : un appui maintenu ne relance pas le cycle', () => {
  const sim = boot();
  sim.set('Depart', true);
  sim.set('Verin_sorti', true);
  sim.run(6000);
  // Cycle terminé une seule fois malgré Depart maintenu.
  assert.equal(sim.get('Pieces'), 1);
  assert.deepEqual(sim.activeSteps(), [0]);
});

test('arrêt d’urgence : repli des sorties, acquittement, réarmement', () => {
  const sim = boot();
  // Urgence au repos : le voyant (repli « maintenue ») garde sa valeur.
  assert.equal(sim.get('Voyant'), true);
  sim.set('AU_OK', false);
  sim.run(20);
  assert.equal(sim.mode, 'EMERGENCY');
  assert.equal(sim.get('Voyant'), true);
  sim.set('AU_OK', true);
  sim.set('Acquit', true);
  sim.run(3000);
  sim.set('Acquit', false);
  assert.equal(sim.mode, 'IDLE');
  // Urgence en cycle : le vérin (repli « à 0 ») retombe.
  sim.set('Depart', true);
  sim.run(50);
  assert.equal(sim.get('Verin'), true);
  sim.set('AU_OK', false);
  sim.run(20);
  assert.equal(sim.mode, 'EMERGENCY');
  assert.equal(sim.get('Verin'), false);
  assert.deepEqual(sim.activeSteps(), []);
  sim.set('AU_OK', true);
  sim.run(2000);
  assert.equal(sim.mode, 'EMERGENCY'); // attend l'acquittement
  sim.set('Acquit', true);
  sim.run(50);
  sim.set('Acquit', false);
  sim.run(3000);
  assert.equal(sim.mode, 'IDLE');
  assert.deepEqual(sim.activeSteps(), [0]);
});

test('alarme servo : urgence, puis reprise après acquittement', () => {
  const sim = boot();
  sim.servo.raiseAlarm(0x0301);
  sim.run(20);
  assert.equal(sim.mode, 'EMERGENCY');
  assert.ok(sim.messages.some((m) => /Alarme servo 0x0301/.test(m.text)));
  sim.set('Acquit', true);
  sim.run(3000);
  assert.equal(sim.servo.alarm, false);
  assert.equal(sim.mode, 'IDLE');
});

test('arrêt demandé puis remise en marche', () => {
  const sim = boot();
  sim.set('Depart', true);
  sim.run(50);
  sim.set('Arret', true);
  sim.run(20);
  assert.equal(sim.mode, 'STOP');
  assert.deepEqual(sim.activeSteps(), []);
  sim.set('Arret', false);
  sim.set('Depart', false);
  sim.run(50);
  assert.equal(sim.mode, 'IDLE');
});

test('jog : déplacement tant que le bouton est maintenu, butée basse après origine', () => {
  const sim = boot();
  sim.set('Jog', true);
  sim.run(20);
  assert.equal(sim.mode, 'JOGGING');
  sim.set('JogP', true);
  sim.run(1000);
  const p = sim.servo.position;
  assert.ok(p > 1000, `position ${p}`);
  sim.set('JogP', false);
  sim.run(50);
  const stopped = sim.servo.position;
  assert.ok(stopped >= p);
  sim.run(200);
  assert.equal(sim.servo.position, stopped);
  sim.set('JogM', true);
  sim.run(5000);
  assert.equal(sim.servo.position, 0); // butée basse
  sim.set('JogM', false);
  sim.set('Jog', false);
  sim.run(100);
  assert.equal(sim.mode, 'IDLE');
});

test('cible hors course : urgence au lieu du déplacement (CR-1)', () => {
  const sim = boot();
  sim.set('Cible', 5000000);
  sim.run(20);
  assert.equal(sim.get('Cible'), 900000); // borné par le max du paramètre
  const p = pressProject();
  p.variables.find((v) => v.symbol === 'Cible').max = '';
  const sim2 = boot(p);
  sim2.set('Cible', 5000000);
  sim2.set('Depart', true);
  sim2.set('Verin_sorti', true);
  sim2.run(200);
  assert.equal(sim2.mode, 'EMERGENCY');
  assert.ok(sim2.messages.some((m) => /hors course/.test(m.text)));
});

test('sans servo : démarrage direct en repos', () => {
  const sim = boot(pressProject({ servo: false }));
  assert.equal(sim.mode, 'IDLE');
  sim.set('Depart', true);
  sim.set('Verin_sorti', true);
  sim.run(2500); // t/X3/2s
  assert.deepEqual(sim.activeSteps(), [5]);
  sim.set('Depart', false);
  sim.run(50);
  assert.deepEqual(sim.activeSteps(), [0]);
  assert.equal(sim.get('Pieces'), 1);
});

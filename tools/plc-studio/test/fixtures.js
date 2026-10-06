// Projets d'exemple pour les tests.
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { newProject, addEquipment, normalizeVariable } from '../shared/model.js';
import { parseJsonc } from '../shared/jsonc.js';

export const repo = fileURLToPath(new URL('../../../', import.meta.url));

export function hardwareFiles() {
  return Object.fromEntries(
    readdirSync(repo + 'data/hardware')
      .filter((f) => f.endsWith('.json'))
      .map((f) => [f.slice(0, -5), parseJsonc(readFileSync(repo + 'data/hardware/' + f, 'utf8'))])
  );
}

export function mainCpp() {
  return readFileSync(repo + 'src/main.cpp', 'utf8');
}

const add = (p, v) => {
  const x = normalizeVariable(v);
  p.variables.push(x);
  return x;
};

// Presse : automate + module ES + servo, grafcet avec divergence ET et boucle.
export function pressProject({ servo = true } = {}) {
  const p = newProject('DemoPresse');
  p.wifi = [{ name: 'Atelier', ssid: 'atelier', pwd: 'secret123', dhcp: true }];
  const plc = addEquipment(p, 'Kincony_KC868_A8S');
  const io = addEquipment(p, 'Waveshare_8DIO');
  const sv = servo ? addEquipment(p, 'SureServo') : null;
  add(p, { label: 'AU OK', symbol: 'AU_OK', kind: 'input', dataType: 'bool', binding: { eq: plc.uid, group: 'di', channel: 1 } });
  add(p, { label: 'Départ cycle', symbol: 'Depart', kind: 'input', dataType: 'bool', binding: { eq: plc.uid, group: 'di', channel: 2 } });
  add(p, { label: 'Pièce présente', symbol: 'Piece', kind: 'input', dataType: 'bool', binding: { eq: io.uid, group: 'di', channel: 0 }, inverse: true });
  add(p, { label: 'Vérin sorti', symbol: 'Verin_sorti', kind: 'input', dataType: 'bool', binding: { eq: io.uid, group: 'di', channel: 1 } });
  add(p, { label: 'Vérin', symbol: 'Verin', kind: 'output', dataType: 'bool', binding: { eq: plc.uid, group: 'do', channel: 1 }, fallback: 'off' });
  add(p, { label: 'Soufflage', symbol: 'Soufflage', kind: 'output', dataType: 'bool', binding: { eq: io.uid, group: 'do', channel: 0 }, fallback: 'off' });
  add(p, { label: 'Voyant marche', symbol: 'Voyant', kind: 'output', dataType: 'bool', binding: { eq: plc.uid, group: 'gpio', channel: 1 }, fallback: 'hold' });
  add(p, { label: 'Acquitter', symbol: 'Acquit', kind: 'command', dataType: 'bool', hmi: { show: true, widget: 'tx-button-hold' } });
  add(p, { label: 'Arrêt', symbol: 'Arret', kind: 'command', dataType: 'bool', hmi: { show: true } });
  add(p, { label: 'Cible', symbol: 'Cible', kind: 'parameter', dataType: 'int', initial: 50000, min: 0, max: 900000 });
  add(p, { label: 'Vitesse', symbol: 'Vitesse', kind: 'parameter', dataType: 'int', initial: 8, min: 0, max: 15 });
  add(p, { label: 'Durée soufflage', symbol: 'Duree', kind: 'parameter', dataType: 'float', initial: 1.5 });
  add(p, { label: 'Recette', symbol: 'Recette', kind: 'parameter', dataType: 'text', initial: 'A' });
  add(p, { label: 'Pièces', symbol: 'Pieces', kind: 'indicator', dataType: 'int' });
  add(p, { label: 'Défaut', symbol: 'Defaut', kind: 'indicator', dataType: 'bool' });
  add(p, { label: 'Mémoire', symbol: 'Mem', kind: 'memory', dataType: 'bool', hmi: { show: false } });
  add(p, { label: 'Jog', symbol: 'Jog', kind: 'command', dataType: 'bool' });
  add(p, { label: 'Jog plus', symbol: 'JogP', kind: 'command', dataType: 'bool' });
  add(p, { label: 'Jog moins', symbol: 'JogM', kind: 'command', dataType: 'bool' });
  if (sv) {
    p.variables.filter((v) => v.system).forEach((v, i) => (v.binding = { eq: plc.uid, group: 'do', channel: 5 + i }));
  }
  p.logic = {
    steps: [
      { id: 's0', num: 0, label: 'Attente', initial: true, actions: [{ type: 'N', target: 'Voyant' }] },
      { id: 's1', num: 1, label: 'Serrage', actions: [{ type: 'N', target: 'Verin' }, { type: 'S', target: 'Mem' }] },
      { id: 's2', num: 2, label: 'Usinage', actions: servo ? [{ type: 'SERVO_MOVE', position: 'Cible', speed: 'Vitesse' }] : [{ type: 'NSET', target: 'Defaut', value: 'FAUX' }] },
      { id: 's3', num: 3, label: 'Soufflage', actions: [{ type: 'N', target: 'Soufflage', condition: 'Piece' }] },
      { id: 's4', num: 4, label: 'Fin', actions: [{ type: 'INC', target: 'Pieces' }, { type: 'R', target: 'Mem' }, { type: 'MSG', level: 'success', text: 'Pièce terminée' }] },
      { id: 's5', num: 5, label: 'Attente servo', actions: servo ? [{ type: 'SERVO_MOVE', position: '0', speed: '15' }] : [] },
    ],
    transitions: [
      { id: 't0', num: 0, from: ['s0'], to: ['s1'], condition: 'FM(Depart) ET Piece' },
      { id: 't1', num: 1, from: ['s1'], to: ['s2', 's3'], condition: 'Verin_sorti' },
      { id: 't2', num: 2, from: ['s2', 's3'], to: ['s4'], condition: servo ? 'Servo.enPosition ET X3.t >= 1500ms' : 't/X3/2s' },
      { id: 't3', num: 3, from: ['s4'], to: ['s5'], condition: 'VRAI' },
      { id: 't4', num: 4, from: ['s5'], to: ['s0'], condition: servo ? 'Servo.enPosition' : 'NON Depart' },
    ],
    blocks: {
      emergency: { condition: 'NON AU_OK', reset: 'Acquit' },
      run: { start: '', stop: 'Arret' },
      servo: { homing: 'auto', jogEnabled: true, jogMode: 'Jog', jogPlus: 'JogP', jogMinus: 'JogM', jogSpeed: '400', torque: '80' },
    },
  };
  return p;
}

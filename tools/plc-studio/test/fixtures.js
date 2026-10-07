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

// Table XYZ : automate KinCony KC868-A16v3, deux SureServo (X, Y) et un axe pas-à-pas (Z).
// lichuan : ajoute un plateau tournant Lichuan A6 et un convoyeur Lichuan A5.
export function multiAxisProject({ lichuan = false } = {}) {
  const p = newProject('TableXYZ');
  p.wifi = [{ name: 'Atelier', ssid: 'atelier', pwd: 'secret123', dhcp: true }];
  const plc = addEquipment(p, 'Kincony_KC868_A16v3');
  const x = addEquipment(p, 'SureServo', 'Axe X');
  const y = addEquipment(p, 'SureServo', 'Axe Y');
  const z = addEquipment(p, 'Stepper', 'Axe Z');
  y.address = 2;
  const r = lichuan ? addEquipment(p, 'LichuanA6', 'Plateau') : null;
  const c = lichuan ? addEquipment(p, 'LichuanA5', 'Convoyeur') : null;
  if (r) {
    r.address = 3;
    r.options.accelTime = 200;
    c.address = 4;
  }
  x.options.maxRange = 200000;
  y.options.maxRange = 100000;
  z.options.maxRange = 20000;
  add(p, { label: 'AU OK', symbol: 'AU_OK', kind: 'input', dataType: 'bool', binding: { eq: plc.uid, group: 'di', channel: 1 } });
  add(p, { label: 'Départ cycle', symbol: 'Depart', kind: 'input', dataType: 'bool', binding: { eq: plc.uid, group: 'di', channel: 2 } });
  add(p, { label: 'Pince', symbol: 'Pince', kind: 'output', dataType: 'bool', binding: { eq: plc.uid, group: 'do', channel: 10 }, fallback: 'off' });
  add(p, { label: 'Acquitter', symbol: 'Acquit', kind: 'command', dataType: 'bool' });
  add(p, { label: 'Manuel', symbol: 'Manuel', kind: 'command', dataType: 'bool' });
  add(p, { label: 'Jog Z +', symbol: 'JogZP', kind: 'command', dataType: 'bool' });
  add(p, { label: 'Jog Z -', symbol: 'JogZM', kind: 'command', dataType: 'bool' });
  add(p, { label: 'Cible X', symbol: 'CibleX', kind: 'parameter', dataType: 'int', initial: 50000 });
  add(p, { label: 'Cible Y', symbol: 'CibleY', kind: 'parameter', dataType: 'int', initial: 30000 });
  add(p, { label: 'Cible Z', symbol: 'CibleZ', kind: 'parameter', dataType: 'int', initial: 4000 });
  // Signaux des servos sur Y1-Y6, STEP/DIR du pas-à-pas sur GPIO38/GPIO39, ENABLE sur Y7.
  const sig = (eq, key) => p.variables.find((v) => v.system?.eq === eq.uid && v.system.signal === key);
  ['servoOn', 'immediateStop', 'alarmsReset'].forEach((k, i) => {
    sig(x, k).binding = { eq: plc.uid, group: 'do', channel: 1 + i };
    sig(y, k).binding = { eq: plc.uid, group: 'do', channel: 4 + i };
  });
  sig(z, 'step').binding = { eq: plc.uid, group: 'gpio', channel: 1 };
  sig(z, 'dir').binding = { eq: plc.uid, group: 'gpio', channel: 2 };
  sig(z, 'enable').binding = { eq: plc.uid, group: 'do', channel: 7 };
  sig(z, 'home').binding = { eq: plc.uid, group: 'di', channel: 3 };
  p.logic = {
    steps: [
      { id: 's0', num: 0, label: 'Attente', initial: true, actions: [] },
      {
        id: 's1',
        num: 1,
        label: 'Positionnement',
        actions: [
          { type: 'SERVO_MOVE', axis: x.uid, position: 'CibleX', speed: '10' },
          { type: 'SERVO_MOVE', axis: y.uid, position: 'CibleY', speed: '8' },
          { type: 'SERVO_MOVE', axis: z.uid, position: 'CibleZ', speed: '3000' },
          ...(r ? [{ type: 'SERVO_MOVE', axis: r.uid, position: '3600', speed: '300' }, { type: 'SERVO_HOME', axis: c.uid }] : []),
        ],
      },
      { id: 's2', num: 2, label: 'Prise', actions: [{ type: 'N', target: 'Pince' }] },
      {
        id: 's3',
        num: 3,
        label: 'Retour',
        actions: [
          { type: 'SERVO_MOVE', axis: x.uid, position: '0', speed: '15' },
          { type: 'SERVO_MOVE', axis: y.uid, position: '0', speed: '15' },
          { type: 'SERVO_MOVE', axis: z.uid, position: '0', speed: '5000' },
        ],
      },
    ],
    transitions: [
      { id: 't0', num: 0, from: ['s0'], to: ['s1'], condition: 'FM(Depart)' },
      { id: 't1', num: 1, from: ['s1'], to: ['s2'], condition: 'Axe_X.enPosition ET Axe_Y.enPosition ET Axe_Z.enPosition' },
      { id: 't2', num: 2, from: ['s2'], to: ['s3'], condition: 'X2.t >= 500ms' },
      { id: 't3', num: 3, from: ['s3'], to: ['s0'], condition: 'Axe_X.enPosition ET Axe_Y.enPosition ET Axe_Z.enPosition' },
    ],
    blocks: {
      emergency: { condition: 'NON AU_OK', reset: 'Acquit' },
      run: { start: '', stop: '' },
      axes: {
        [z.uid]: { homing: 'auto', homingOrder: 1, jogEnabled: true, jogMode: 'Manuel', jogPlus: 'JogZP', jogMinus: 'JogZM', jogSpeed: '2000' },
        [x.uid]: { homing: 'auto', homingOrder: 2 },
        [y.uid]: { homing: 'auto', homingOrder: 2, torque: '70' },
      },
    },
  };
  return p;
}

// Cellule robot : Waveshare ESP32-S3-POE (Ethernet en adresse fixe), robot piloté en
// Modbus TCP (équipement générique) et module ES derrière une passerelle Modbus TCP -> RTU.
export function robotCellProject() {
  const p = newProject('CelluleRobot');
  p.wifi = [{ name: 'Atelier', ssid: 'atelier', pwd: 'secret123', dhcp: true }];
  p.ethernet = { enabled: true, dhcp: false, ip: '192.168.10.20', gateway: '192.168.10.1', mask: '255.255.255.0', dns: '' };
  const plc = addEquipment(p, 'Waveshare_ESP32S3_POE_8DI8DO');
  const robot = addEquipment(p, 'ModbusGeneric', 'Robot');
  Object.assign(robot.options, { ip: '192.168.10.50', port: 502, oneBased: true, coilOutStart: 1, coilInStart: 101, regOutStart: 40001, regInStart: 40101 });
  const io = addEquipment(p, 'Waveshare_8DIO', 'ES distantes');
  Object.assign(io.options, { transport: 'tcp', ip: '192.168.10.60', port: 502 });
  io.address = 1;
  add(p, { label: 'AU OK', symbol: 'AU_OK', kind: 'input', dataType: 'bool', binding: { eq: plc.uid, group: 'di', channel: 1 } });
  add(p, { label: 'Départ cycle', symbol: 'Depart', kind: 'input', dataType: 'bool', binding: { eq: plc.uid, group: 'di', channel: 2 } });
  add(p, { label: 'Robot prêt', symbol: 'Robot_pret', kind: 'input', dataType: 'bool', binding: { eq: robot.uid, group: 'i', channel: 1 } });
  add(p, { label: 'Robot fini', symbol: 'Robot_fini', kind: 'input', dataType: 'bool', binding: { eq: robot.uid, group: 'i', channel: 2 } });
  add(p, { label: 'État robot', symbol: 'Etat_robot', kind: 'input', dataType: 'int', binding: { eq: robot.uid, group: 'r', channel: 1 } });
  add(p, { label: 'Départ robot', symbol: 'Depart_robot', kind: 'output', dataType: 'bool', binding: { eq: robot.uid, group: 'q', channel: 1 }, fallback: 'off' });
  add(p, { label: 'Programme robot', symbol: 'Programme', kind: 'output', dataType: 'int', binding: { eq: robot.uid, group: 'w', channel: 1 }, fallback: 'off' });
  add(p, { label: 'Vitesse robot', symbol: 'Vitesse_robot', kind: 'output', dataType: 'int', binding: { eq: robot.uid, group: 'w', channel: 2 }, fallback: 'hold' });
  add(p, { label: 'Pièce présente', symbol: 'Piece', kind: 'input', dataType: 'bool', binding: { eq: io.uid, group: 'di', channel: 0 } });
  add(p, { label: 'Voyant cycle', symbol: 'Voyant', kind: 'output', dataType: 'bool', binding: { eq: io.uid, group: 'do', channel: 0 }, fallback: 'off' });
  add(p, { label: 'Acquitter', symbol: 'Acquit', kind: 'command', dataType: 'bool' });
  add(p, { label: 'N° de programme', symbol: 'NumProg', kind: 'parameter', dataType: 'int', initial: 3, min: 1, max: 99 });
  p.logic = {
    steps: [
      { id: 's0', num: 0, label: 'Attente', initial: true, actions: [{ type: 'SET', target: 'Programme', value: '0' }] },
      {
        id: 's1',
        num: 1,
        label: 'Programme robot',
        actions: [
          { type: 'SET', target: 'Programme', value: 'NumProg' },
          { type: 'SET', target: 'Vitesse_robot', value: '80' },
          { type: 'N', target: 'Depart_robot' },
          { type: 'N', target: 'Voyant' },
        ],
      },
      { id: 's2', num: 2, label: 'Fin', actions: [] },
    ],
    transitions: [
      { id: 't0', num: 0, from: ['s0'], to: ['s1'], condition: 'FM(Depart) ET Robot_pret ET Piece ET Etat_robot = 0' },
      { id: 't1', num: 1, from: ['s1'], to: ['s2'], condition: 'Robot_fini' },
      { id: 't2', num: 2, from: ['s2'], to: ['s0'], condition: 'NON Robot_fini' },
    ],
    blocks: { emergency: { condition: 'NON AU_OK', reset: 'Acquit' }, run: { start: '', stop: '' } },
  };
  return p;
}

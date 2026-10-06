// Disposition des écrans opérateur (pages / sections / éléments) et valeurs par défaut des widgets.

import { CATALOG, widgetsFor } from './catalog.js';

let counter = 0;
const id = (p) => `${p}_${Date.now().toString(36)}${(counter++).toString(36)}`;

export function defaultWidget(v) {
  const allowed = widgetsFor(v);
  return allowed.includes(v.hmi?.widget) ? v.hmi.widget : allowed[0];
}

export function itemForVariable(v) {
  return { ref: `var:${v.uid}`, label: v.label, widget: defaultWidget(v) };
}

// Libellé et widget par défaut de toute référence affichable.
export function describeRef(project, layout, ref) {
  if (ref.startsWith('var:')) {
    const v = project.variables.find((x) => `var:${x.uid}` === ref);
    if (!v) return null;
    return { label: v.label, widgets: widgetsFor(v), variable: v };
  }
  const node = layout.nodes.get(ref);
  if (!node) return null;
  if (ref === 'sys:state' || ref === 'sys:clock') return { label: node.label, widgets: ['rx-label'] };
  if (ref.startsWith('servo:')) {
    if (node.cppClass === 'BooleanOutputNode') return { label: node.label, widgets: ['rx-indicator', 'rx-bool'] };
    return { label: node.label || node.name, widgets: ['rx-numeric', 'rx-level-bar'] };
  }
  if (ref.startsWith('chan:')) return { label: node.label, widgets: ['rx-indicator', 'rx-bool'] };
  return { label: node.name, widgets: ['rx-numeric'] };
}

// Références affichables qui ne sont placées sur aucune page.
export function unplacedRefs(project, layout) {
  const placed = new Set();
  for (const p of project.hmi.pages) for (const s of p.sections) for (const it of s.items) placed.add(it.ref);
  const refs = [];
  for (const v of project.variables) {
    const ref = `var:${v.uid}`;
    if (layout.nodes.has(ref) && !placed.has(ref)) refs.push(ref);
  }
  for (const [ref, node] of layout.nodes) {
    if (ref.startsWith('var:') || ref.startsWith('bank:') || ref.startsWith('mirror:') || placed.has(ref)) continue;
    // Registres internes du servo : seuls les états booléens, la position et les alarmes sont utiles à l'écran.
    if (ref.startsWith('servo:') && node.cppClass !== 'BooleanOutputNode' && !SERVO_NUMERIC.includes(node.field)) continue;
    refs.push(ref);
  }
  return refs;
}

const SERVO_NUMERIC = ['position', 'alarms', 'status'];
const SERVO_DEFAULT = ['servoReady', 'servoActivated', 'driveInitialised', 'homeDone', 'targetPositionReached', 'servoAlarm', 'modbusError'];

export function autoLayout(project, layout) {
  const shown = (v) => v.hmi?.show !== false && layout.nodes.has(`var:${v.uid}`);
  const vars = project.variables.filter(shown);
  const pick = (pred) => vars.filter(pred).map(itemForVariable);
  const pages = [];

  const main = { id: id('page'), name: 'Principal', sections: [] };
  if (layout.nodes.has('sys:state') || layout.nodes.has('sys:clock')) {
    const items = [];
    if (layout.nodes.has('sys:state')) items.push({ ref: 'sys:state', label: 'État machine', widget: 'rx-label' });
    if (layout.nodes.has('sys:clock')) items.push({ ref: 'sys:clock', label: 'Horloge', widget: 'rx-label' });
    main.sections.push({ id: id('sec'), name: 'État', orientation: 'horizontal', items });
  }
  const commands = pick((v) => v.kind === 'command');
  if (commands.length) main.sections.push({ id: id('sec'), name: 'Commandes', orientation: 'horizontal', items: commands });
  const lamps = pick((v) => (v.kind === 'indicator' || v.kind === 'memory') && v.dataType === 'bool');
  if (lamps.length) main.sections.push({ id: id('sec'), name: 'Voyants', orientation: 'horizontal', items: lamps });
  const values = pick((v) => (v.kind === 'indicator' || v.kind === 'memory') && v.dataType !== 'bool');
  if (values.length) main.sections.push({ id: id('sec'), name: 'Valeurs', orientation: 'horizontal', items: values });
  if (main.sections.length) pages.push(main);

  const io = { id: id('page'), name: 'Entrées sorties', sections: [] };
  const inputs = pick((v) => v.kind === 'input');
  if (inputs.length) io.sections.push({ id: id('sec'), name: 'Entrées', orientation: 'horizontal', items: inputs });
  const outputs = pick((v) => v.kind === 'output');
  if (outputs.length) io.sections.push({ id: id('sec'), name: 'Sorties', orientation: 'horizontal', items: outputs });
  if (io.sections.length) pages.push(io);

  const servo = project.equipment.find((e) => CATALOG[e.type]?.role === 'servo');
  if (servo) {
    const items = SERVO_DEFAULT.filter((f) => layout.nodes.has(`servo:${f}`)).map((f) => {
      const n = layout.nodes.get(`servo:${f}`);
      return { ref: `servo:${f}`, label: n.label, widget: 'rx-indicator', props: f === 'servoAlarm' || f === 'modbusError' ? { color: 'red' } : {} };
    });
    if (layout.nodes.has('servo:position')) items.push({ ref: 'servo:position', label: 'Position actuelle', widget: 'rx-numeric' });
    pages.push({ id: id('page'), name: 'Servo', sections: [{ id: id('sec'), name: 'État servo', orientation: 'horizontal', items }] });
  }

  const params = pick((v) => v.kind === 'parameter');
  if (params.length) {
    pages.push({ id: id('page'), name: 'Réglages', password: '', sections: [{ id: id('sec'), name: 'Paramètres', orientation: 'horizontal', items: params }] });
  }
  if (!pages.length) pages.push({ id: id('page'), name: 'Principal', sections: [] });
  return pages;
}

// Propriétés de widget déduites de la variable (bornes, couleurs...).
export function widgetProps(item, variable) {
  const props = {};
  const num = (x) => (x === undefined || x === null || x === '' ? undefined : Number(x));
  if (variable) {
    const min = num(variable.min);
    const max = num(variable.max);
    switch (item.widget) {
      case 'tx-numeric':
        if (min !== undefined) props.low = min;
        if (max !== undefined) props.high = max;
        break;
      case 'tx-slider':
        props.low = min ?? 0;
        props.high = max ?? 100;
        if (variable.step) props.step = Number(variable.step);
        break;
      case 'rx-level-bar':
        props.min = min ?? 0;
        props.max = max ?? 100;
        props.orientation = 'horizontal';
        if (variable.unit) props.unit = variable.unit;
        break;
      case 'tx-button-hold':
        props.color = 'primary';
        break;
      case 'rx-indicator':
        props.color = 'lime';
        break;
      case 'rx-label':
        props.state = 'neutral';
        break;
      case 'tx-dropdown':
        props.items = String(variable.choices || '')
          .split(/[;,\n]/)
          .map((s) => s.trim())
          .filter(Boolean);
        break;
      default:
        break;
    }
  }
  if (item.widget === 'rx-label' && !props.state) props.state = 'neutral';
  return { ...props, ...(item.props || {}) };
}

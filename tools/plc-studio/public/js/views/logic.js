// Étape 4 : éditeur de grafcet et modes de marche.

import { ACTION_TYPES, normalizeLogic, starterGrafcet, isWritable, compileLogic } from '/shared/grafcet.js';
import { SERVO_PROPS } from '/shared/expr.js';
import { h, input, checkbox, select, card, pageHead, button, confirmDialog, issueList } from '../ui.js';
import { exprInput } from '../exprinput.js';

const NS = 'http://www.w3.org/2000/svg';
const STEP_W = 56;
const STEP_H = 44;
const GRID = 10;

let tab = 'grafcet';
let selection = null; // { kind: 'step' | 'transition', id }
let zoom = 1;

const s = (tag, attrs = {}, ...children) => {
  const el = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'on') for (const [ev, fn] of Object.entries(v)) el.addEventListener(ev, fn);
    else el.setAttribute(k, v);
  }
  for (const c of children.flat()) if (c !== null && c !== undefined) el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return el;
};

const snap = (v) => Math.round(v / GRID) * GRID;
const newId = (p) => `${p}_${Math.random().toString(36).slice(2, 9)}`;

export function renderLogic(root, store) {
  const p = store.project;
  p.logic = { ...normalizeLogic(p.logic), ...p.logic, blocks: normalizeLogic(p.logic).blocks };
  p.logic.steps = p.logic.steps || [];
  p.logic.transitions = p.logic.transitions || [];

  const tabs = h(
    'div',
    { class: 'page-tabs' },
    [
      ['grafcet', 'Grafcet'],
      ['modes', 'Modes et sécurités'],
      ['help', 'Aide-mémoire'],
    ].map(([k, label]) =>
      h(
        'button',
        {
          class: `page-tab ${tab === k ? 'active' : ''}`,
          type: 'button',
          on: {
            click: () => {
              tab = k;
              store.rerender();
            },
          },
        },
        label
      )
    )
  );

  root.append(
    pageHead('Logique (grafcet)', 'Dessinez le cycle de production. Les modes de marche (arrêt d’urgence, prise d’origine, jog, arrêt) sont gérés par les blocs prédéfinis de l’onglet « Modes et sécurités ».', button('Câblage et variables', () => store.go('variables'), 'small')),
    logicIssues(store),
    tabs
  );

  if (tab === 'modes') root.append(modesPanel(store));
  else if (tab === 'help') root.append(helpPanel(store));
  else root.append(grafcetEditor(store));
}

function logicIssues(store) {
  const box = h('div', {});
  const draw = () => {
    const issues = store.issues.filter((i) => i.step === 'logic');
    box.replaceChildren(issues.length ? h('section', { class: 'card' }, h('h2', {}, 'À corriger'), issueList(issues)) : '');
  };
  draw();
  store.onChange(draw);
  return box;
}

// ===========================================================================
// Éditeur graphique
function grafcetEditor(store) {
  const logic = store.project.logic;
  const svg = s('svg', { class: 'grafcet-svg' });
  const canvas = h('div', { class: 'grafcet-canvas', tabindex: 0 }, svg);
  const props = h('div', { class: 'grafcet-props' });

  const redraw = () => drawGrafcet(svg, store, { onSelect: select, onDragEnd: () => store.changed() });
  const select = (sel) => {
    selection = sel;
    redraw();
    drawProps();
  };
  const drawProps = () => props.replaceChildren(propertiesPanel(store, { redraw, select, refresh: drawProps }));

  canvas.addEventListener('keydown', (e) => {
    if ((e.key === 'Delete' || e.key === 'Backspace') && selection && document.activeElement === canvas) {
      deleteSelection(store);
      select(null);
      e.preventDefault();
    }
    if (e.key === 'Escape') select(null);
  });

  const toolbar = h(
    'div',
    { class: 'btn-row grafcet-toolbar' },
    button('+ Étape', () => {
      const st = addStep(logic, freePosition(logic));
      store.changed();
      select({ kind: 'step', id: st.id });
    }, 'small'),
    button('+ Transition', () => {
      const pos = freePosition(logic);
      const t = { id: newId('t'), num: nextNum(logic.transitions), from: [], to: [], condition: 'VRAI', x: pos.x, y: pos.y };
      logic.transitions.push(t);
      store.changed();
      select({ kind: 'transition', id: t.id });
    }, 'small'),
    h('span', { class: 'spacer' }),
    button('−', () => ((zoom = Math.max(0.4, zoom - 0.1)), redraw()), 'small ghost', { title: 'Zoom arrière' }),
    button('+', () => ((zoom = Math.min(2, zoom + 0.1)), redraw()), 'small ghost', { title: 'Zoom avant' }),
    button('100 %', () => ((zoom = 1), redraw()), 'small ghost')
  );

  if (!logic.steps.length) {
    return card(
      'Grafcet vide',
      h('p', {}, 'Commencez par un grafcet de départ (étape initiale, une étape de cycle et le retour), puis complétez-le.'),
      h(
        'div',
        { class: 'btn-row' },
        button(
          'Créer un grafcet de départ',
          () => {
            const g = starterGrafcet();
            logic.steps = g.steps;
            logic.transitions = g.transitions;
            selection = null;
            store.changed({ render: true });
          },
          'primary'
        ),
        button('Étape initiale seule', () => {
          addStep(logic, { x: 160, y: 60 }, true);
          store.changed({ render: true });
        })
      )
    );
  }

  redraw();
  drawProps();
  store.onChange(redraw);
  return h('div', { class: 'grafcet-layout' }, h('section', { class: 'card grafcet-card' }, toolbar, canvas), props);
}

function nextNum(list) {
  return list.length ? Math.max(...list.map((x) => Number(x.num) || 0)) + 1 : 0;
}

function addStep(logic, pos, initial = false) {
  const st = { id: newId('s'), num: nextNum(logic.steps), label: '', initial, x: snap(pos.x), y: snap(pos.y), actions: [] };
  logic.steps.push(st);
  return st;
}

function freePosition(logic) {
  const maxY = Math.max(0, ...logic.steps.map((x) => x.y), ...logic.transitions.map((x) => x.y));
  return { x: 160, y: maxY + 100 };
}

function deleteSelection(store) {
  const logic = store.project.logic;
  if (!selection) return;
  if (selection.kind === 'step') {
    logic.steps = logic.steps.filter((x) => x.id !== selection.id);
    for (const t of logic.transitions) {
      t.from = t.from.filter((id) => id !== selection.id);
      t.to = t.to.filter((id) => id !== selection.id);
    }
  } else {
    logic.transitions = logic.transitions.filter((x) => x.id !== selection.id);
  }
  store.changed();
}

// ---------------------------------------------------------------------------
// Dessin
function actionText(a) {
  switch (a.type) {
    case 'N':
      return `N  ${a.target || '?'}${a.condition ? `  si ${a.condition}` : ''}`;
    case 'S':
      return `S  ${a.target || '?'}`;
    case 'R':
      return `R  ${a.target || '?'}`;
    case 'SET':
      return `${a.target || '?'} := ${a.value || '?'}`;
    case 'NSET':
      return `${a.target || '?'} = ${a.value || '?'}  (continu)`;
    case 'INC':
      return `${a.target || '?'} + 1`;
    case 'DEC':
      return `${a.target || '?'} − 1`;
    case 'SERVO_MOVE':
      return `Servo → ${a.position || '?'}  (vitesse ${a.speed || '?'})`;
    case 'SERVO_HOME':
      return 'Servo : prise d’origine';
    case 'SERVO_STOP':
      return 'Servo : arrêt';
    case 'MSG':
      return `Message : ${a.text || ''}`;
    default:
      return a.type;
  }
}

function drawGrafcet(svg, store, { onSelect, onDragEnd }) {
  const logic = store.project.logic;
  const steps = new Map(logic.steps.map((x) => [x.id, x]));
  const errorRefs = new Set(store.issues.filter((i) => i.step === 'logic' && i.level === 'error' && i.ref).map((i) => i.ref));
  while (svg.firstChild) svg.removeChild(svg.firstChild);

  // Bornes du dessin
  const xs = [...logic.steps.map((x) => x.x), ...logic.transitions.map((x) => x.x)];
  const ys = [...logic.steps.map((x) => x.y), ...logic.transitions.map((x) => x.y)];
  const actionWidth = (st) => (st.actions.length ? Math.max(...st.actions.map((a) => actionText(a).length)) * 7 + 24 : 0);
  const maxRight = Math.max(400, ...logic.steps.map((st) => st.x + STEP_W / 2 + 20 + actionWidth(st)), ...logic.transitions.map((t) => t.x + 40 + String(t.condition || '').length * 7.2));
  const minX = Math.min(0, ...xs, ...logic.steps.map((st) => st.x - (st.label ? st.label.length * 6.5 : 0))) - 160;
  const minY = Math.min(0, ...ys) - 60;
  const maxY = Math.max(300, ...ys) + 120;
  const width = maxRight - minX + 60;
  const height = maxY - minY;
  svg.setAttribute('viewBox', `${minX} ${minY} ${width} ${height}`);
  svg.setAttribute('width', width * zoom);
  svg.setAttribute('height', height * zoom);

  svg.append(
    s('defs', {}, s('pattern', { id: 'grid', width: GRID * 2, height: GRID * 2, patternUnits: 'userSpaceOnUse' }, s('circle', { cx: 1, cy: 1, r: 0.8, class: 'g-grid' }))),
    s('rect', {
      x: minX,
      y: minY,
      width,
      height,
      fill: 'url(#grid)',
      on: { mousedown: () => onSelect(null) },
    })
  );

  const links = s('g', { class: 'g-links' });
  svg.append(links);
  const path = (d, cls = '') => links.append(s('path', { d, class: `g-link ${cls}` }));
  const arrowUp = (x, y) => links.append(s('path', { d: `M ${x - 5} ${y + 6} L ${x} ${y - 4} L ${x + 5} ${y + 6} Z`, class: 'g-arrow' }));
  let loopIndex = 0;

  const top = (st) => st.y - STEP_H / 2;
  const bottom = (st) => st.y + STEP_H / 2;
  // Les retours vers le haut passent à gauche, au-delà des libellés d'étapes.
  const leftLimit = Math.min(...logic.steps.map((st) => st.x - Math.max(STEP_W / 2, st.label ? st.label.length * 6.5 + 8 : 0)), ...logic.transitions.map((t) => t.x - 50)) - 24;

  for (const t of logic.transitions) {
    const from = t.from.map((id) => steps.get(id)).filter(Boolean);
    const to = t.to.map((id) => steps.get(id)).filter(Boolean);

    // Amont
    if (from.length > 1) {
      const barY = t.y - 16;
      const lo = Math.min(t.x, ...from.map((x) => x.x)) - 12;
      const hi = Math.max(t.x, ...from.map((x) => x.x)) + 12;
      path(`M ${lo} ${barY - 3} H ${hi} M ${lo} ${barY + 3} H ${hi}`, 'g-bar');
      for (const st of from) path(`M ${st.x} ${bottom(st)} V ${barY - 3}`);
      path(`M ${t.x} ${barY + 3} V ${t.y}`);
    } else if (from.length === 1) {
      const st = from[0];
      if (bottom(st) <= t.y) path(st.x === t.x ? `M ${st.x} ${bottom(st)} V ${t.y}` : `M ${st.x} ${bottom(st)} V ${t.y - 14} H ${t.x} V ${t.y}`);
      else {
        const lx = Math.max(st.x, t.x) + 90 + 14 * loopIndex++;
        path(`M ${st.x} ${bottom(st)} V ${bottom(st) + 14} H ${lx} V ${t.y - 18} H ${t.x} V ${t.y}`);
        arrowUp(lx, (bottom(st) + t.y) / 2);
      }
    }

    // Aval
    const down = (st, startX, startY) => {
      if (top(st) >= startY) {
        path(st.x === startX ? `M ${startX} ${startY} V ${top(st)}` : `M ${startX} ${startY} V ${startY + 14} H ${st.x} V ${top(st)}`);
      } else {
        const lx = leftLimit - 14 * loopIndex++;
        path(`M ${startX} ${startY} V ${startY + 16} H ${lx} V ${top(st) - 16} H ${st.x} V ${top(st)}`);
        arrowUp(lx, (startY + top(st)) / 2);
      }
    };
    if (to.length > 1) {
      const barY = t.y + 16;
      const lo = Math.min(t.x, ...to.map((x) => x.x)) - 12;
      const hi = Math.max(t.x, ...to.map((x) => x.x)) + 12;
      path(`M ${t.x} ${t.y} V ${barY - 3}`);
      path(`M ${lo} ${barY - 3} H ${hi} M ${lo} ${barY + 3} H ${hi}`, 'g-bar');
      for (const st of to) down(st, st.x, barY + 3);
    } else if (to.length === 1) {
      down(to[0], t.x, t.y);
    }
  }

  // Transitions
  for (const t of logic.transitions) {
    const sel = selection?.kind === 'transition' && selection.id === t.id;
    const g = s('g', { class: `g-trans ${sel ? 'selected' : ''} ${errorRefs.has(t.id) ? 'has-error' : ''}`, transform: `translate(${t.x} ${t.y})` });
    g.append(
      s('rect', { x: -40, y: -10, width: 80, height: 20, class: 'g-hit' }),
      s('line', { x1: -16, y1: 0, x2: 16, y2: 0, class: 'g-tbar' }),
      s('text', { x: -24, y: 4, class: 'g-tnum', 'text-anchor': 'end' }, `T${t.num}`),
      s('text', { x: 24, y: 4, class: 'g-cond' }, t.condition || '(vide)')
    );
    makeDraggable(g, t, svg, { onSelect: () => onSelect({ kind: 'transition', id: t.id }), onDragEnd, redraw: () => drawGrafcet(svg, store, { onSelect, onDragEnd }) });
    svg.append(g);
  }

  // Étapes
  for (const st of logic.steps) {
    const sel = selection?.kind === 'step' && selection.id === st.id;
    const g = s('g', { class: `g-step ${sel ? 'selected' : ''} ${errorRefs.has(st.id) ? 'has-error' : ''}`, transform: `translate(${st.x} ${st.y})` });
    g.append(s('rect', { x: -STEP_W / 2, y: -STEP_H / 2, width: STEP_W, height: STEP_H, class: 'g-box' }));
    if (st.initial) g.append(s('rect', { x: -STEP_W / 2 + 4, y: -STEP_H / 2 + 4, width: STEP_W - 8, height: STEP_H - 8, class: 'g-box-inner' }));
    g.append(s('text', { x: 0, y: 5, 'text-anchor': 'middle', class: 'g-num' }, st.num));
    if (st.label) g.append(s('text', { x: -6, y: -STEP_H / 2 - 6, 'text-anchor': 'end', class: 'g-label' }, st.label));
    if (st.actions.length) {
      const w = actionWidth(st);
      const hgt = st.actions.length * 20 + 6;
      const ax = STEP_W / 2 + 20;
      g.append(
        s('line', { x1: STEP_W / 2, y1: 0, x2: ax, y2: 0, class: 'g-link' }),
        s('rect', { x: ax, y: -hgt / 2, width: w, height: hgt, class: 'g-actions' })
      );
      st.actions.forEach((a, i) => {
        if (i > 0) g.append(s('line', { x1: ax, y1: -hgt / 2 + i * 20 + 3, x2: ax + w, y2: -hgt / 2 + i * 20 + 3, class: 'g-sep' }));
        g.append(s('text', { x: ax + 10, y: -hgt / 2 + i * 20 + 17, class: 'g-action' }, actionText(a)));
      });
    }
    makeDraggable(g, st, svg, { onSelect: () => onSelect({ kind: 'step', id: st.id }), onDragEnd, redraw: () => drawGrafcet(svg, store, { onSelect, onDragEnd }) });
    svg.append(g);
  }
}

function makeDraggable(g, item, svg, { onSelect, onDragEnd, redraw }) {
  g.addEventListener('mousedown', (e) => {
    e.stopPropagation();
    e.preventDefault();
    svg.closest('.grafcet-canvas')?.focus();
    const start = svgPoint(svg, e);
    const orig = { x: item.x, y: item.y };
    let moved = false;
    const move = (ev) => {
      const p = svgPoint(svg, ev);
      const nx = snap(orig.x + p.x - start.x);
      const ny = snap(orig.y + p.y - start.y);
      if (nx !== item.x || ny !== item.y) {
        item.x = nx;
        item.y = ny;
        moved = true;
        g.setAttribute('transform', `translate(${nx} ${ny})`);
      }
    };
    const up = () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
      if (moved) {
        redraw();
        onDragEnd();
      }
      onSelect();
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
  });
}

function svgPoint(svg, e) {
  const pt = svg.createSVGPoint();
  pt.x = e.clientX;
  pt.y = e.clientY;
  return pt.matrixTransform(svg.getScreenCTM().inverse());
}

// ---------------------------------------------------------------------------
// Panneau de propriétés
function propertiesPanel(store, { redraw, select, refresh }) {
  const logic = store.project.logic;
  const changed = () => {
    store.changed();
    redraw();
  };
  if (!selection) {
    return card(
      'Propriétés',
      h('p', { class: 'muted' }, 'Cliquez sur une étape ou une transition pour la modifier. Glissez-les pour les déplacer.'),
      h(
        'ul',
        { class: 'muted', style: { paddingLeft: '18px', margin: 0 } },
        h('li', {}, 'Étape sélectionnée : « Étape suivante » crée la transition et l’étape en dessous.'),
        h('li', {}, '« Branche OU » ajoute un choix de séquence ; « Parallèle ET » sur une transition ajoute une étape simultanée.'),
        h('li', {}, 'Touche Suppr : supprime l’élément sélectionné.')
      )
    );
  }

  if (selection.kind === 'step') {
    const st = logic.steps.find((x) => x.id === selection.id);
    if (!st) return card('Propriétés', h('p', {}, 'Élément supprimé.'));
    const outgoing = logic.transitions.filter((t) => t.from.includes(st.id));
    return h(
      'div',
      {},
      card(
        `Étape ${st.num}`,
        h(
          'div',
          { class: 'grid', style: { gridTemplateColumns: '90px 1fr' } },
          h('label', { class: 'field' }, h('span', {}, 'Numéro'), input(st, 'num', { type: 'number', min: 0, parse: (v) => Math.trunc(Number(v) || 0), onInput: changed })),
          h('label', { class: 'field' }, h('span', {}, 'Libellé'), input(st, 'label', { onInput: changed }))
        ),
        h('div', { style: { margin: '10px 0' } }, checkbox(st, 'initial', 'Étape initiale', { onChange: changed })),
        h(
          'div',
          { class: 'btn-row' },
          button(
            'Étape suivante ↓',
            () => {
              const t = { id: newId('t'), num: nextNum(logic.transitions), from: [st.id], to: [], condition: 'VRAI', x: st.x, y: st.y + 90 };
              const next = addStep(logic, { x: st.x, y: st.y + 180 });
              t.to = [next.id];
              logic.transitions.push(t);
              store.changed();
              select({ kind: 'transition', id: t.id });
            },
            'small primary'
          ),
          button(
            'Branche OU →',
            () => {
              const dx = 260 * (outgoing.length || 1);
              const t = { id: newId('t'), num: nextNum(logic.transitions), from: [st.id], to: [], condition: 'VRAI', x: st.x + dx, y: st.y + 90 };
              const next = addStep(logic, { x: st.x + dx, y: st.y + 180 });
              t.to = [next.id];
              logic.transitions.push(t);
              store.changed();
              select({ kind: 'transition', id: t.id });
            },
            'small'
          ),
          button(
            'Supprimer',
            async () => {
              if (await confirmDialog('Supprimer l’étape', `Supprimer l’étape ${st.num} et ses actions ?`, 'Supprimer')) {
                deleteSelection(store);
                select(null);
              }
            },
            'small danger'
          )
        )
      ),
      actionsCard(store, st, { changed, refresh })
    );
  }

  const t = logic.transitions.find((x) => x.id === selection.id);
  if (!t) return card('Propriétés', h('p', {}, 'Élément supprimé.'));
  const stepOptions = [...logic.steps].sort((a, b) => a.num - b.num);
  const stepChecks = (listKey) =>
    h(
      'div',
      { class: 'step-checks' },
      stepOptions.map((st) =>
        h(
          'label',
          { class: 'check' },
          h('input', {
            type: 'checkbox',
            checked: t[listKey].includes(st.id),
            on: {
              change: (e) => {
                t[listKey] = e.target.checked ? [...t[listKey], st.id] : t[listKey].filter((id) => id !== st.id);
                changed();
              },
            },
          }),
          `X${st.num}${st.label ? ' ' + st.label : ''}`
        )
      )
    );
  return h(
    'div',
    {},
    card(
      `Transition T${t.num}`,
      h('label', { class: 'field' }, h('span', {}, 'Numéro'), input(t, 'num', { type: 'number', min: 0, parse: (v) => Math.trunc(Number(v) || 0), onInput: changed })),
      h(
        'label',
        { class: 'field', style: { marginTop: '10px' } },
        h('span', {}, 'Réceptivité'),
        exprInput(store, t, 'condition', { multiline: true, expect: 'bool', placeholder: 'ex. Depart ET NON Porte_ouverte', onInput: changed }),
        h('small', {}, 'ET, OU, NON, FM(x), FD(x), X3.t >= 5s, comparaisons. Tapez le début d’un nom pour les suggestions.')
      ),
      h('label', { class: 'field', style: { marginTop: '10px' } }, h('span', {}, 'Commentaire'), input(t, 'comment', { onInput: () => store.changed() })),
      h(
        'div',
        { class: 'btn-row', style: { marginTop: '12px' } },
        button(
          'Parallèle ET (+ étape)',
          () => {
            const target = logic.steps.find((x) => x.id === t.to[t.to.length - 1]);
            const pos = target ? { x: target.x + 260, y: target.y } : { x: t.x + 260, y: t.y + 90 };
            const st = addStep(logic, pos);
            t.to.push(st.id);
            store.changed();
            redraw();
            refresh();
          },
          'small'
        ),
        button(
          'Supprimer',
          () => {
            deleteSelection(store);
            select(null);
          },
          'small danger'
        )
      )
    ),
    card('Étapes amont', h('p', { class: 'hint' }, 'Plusieurs étapes = convergence ET (toutes doivent être actives).'), stepChecks('from')),
    card('Étapes aval', h('p', { class: 'hint' }, 'Plusieurs étapes = divergence ET (activées simultanément). Cochez une étape au-dessus pour reboucler.'), stepChecks('to'))
  );
}

function actionsCard(store, st, { changed, refresh }) {
  const p = store.project;
  const hasServo = p.equipment.some((e) => e.type === 'SureServo');
  const types = Object.entries(ACTION_TYPES)
    .filter(([, d]) => !d.servo || hasServo)
    .map(([k, d]) => ({ value: k, label: d.label }));
  const targetsFor = (type) => {
    const def = ACTION_TYPES[type];
    return p.variables
      .filter((v) => isWritable(v))
      .filter((v) => (def.targetType === 'bool' ? v.dataType === 'bool' : def.targetType === 'int' ? v.dataType === 'int' || v.dataType === 'float' : true))
      .map((v) => ({ value: v.symbol, label: `${v.symbol} — ${v.label}` }));
  };

  const list = h(
    'div',
    { class: 'action-list' },
    st.actions.map((a, i) => {
      const def = ACTION_TYPES[a.type];
      const fields = h('div', { class: 'action-fields' });
      if (def.needs.includes('target')) {
        const opts = [{ value: '', label: '— variable —' }, ...targetsFor(a.type)];
        if (a.target && !opts.some((o) => o.value === a.target)) opts.push({ value: a.target, label: `${a.target} (incompatible)` });
        fields.append(select(a, 'target', opts, { onChange: changed }));
      }
      if (def.needs.includes('value')) fields.append(exprInput(store, a, 'value', { placeholder: 'valeur ou expression', onInput: changed }));
      if (a.type === 'N') fields.append(exprInput(store, a, 'condition', { placeholder: 'condition (optionnelle)', expect: 'bool', allowEmpty: true, emptyHint: 'sans condition', onInput: changed }));
      if (a.type === 'SERVO_MOVE') {
        fields.append(
          h('label', { class: 'field' }, h('span', {}, 'Position (impulsions)'), exprInput(store, a, 'position', { expect: 'num', placeholder: 'ex. Cible', onInput: changed })),
          h('label', { class: 'field' }, h('span', {}, 'Vitesse (0 à 15)'), exprInput(store, a, 'speed', { expect: 'num', placeholder: 'ex. 8', onInput: changed }))
        );
      }
      if (a.type === 'MSG') {
        fields.append(
          select(
            a,
            'level',
            [
              { value: 'info', label: 'Information' },
              { value: 'success', label: 'Succès' },
              { value: 'warning', label: 'Avertissement' },
              { value: 'error', label: 'Erreur' },
            ],
            { onChange: changed }
          ),
          input(a, 'text', { placeholder: 'texte du message', onInput: changed })
        );
      }
      return h(
        'div',
        { class: 'action-item' },
        h(
          'div',
          { class: 'row', style: { gap: '6px', flexWrap: 'nowrap' } },
          select(a, 'type', types, {
            onChange: () => {
              changed();
              refresh();
            },
          }),
          button(
            '▲',
            () => {
              st.actions.splice(i - 1, 0, st.actions.splice(i, 1)[0]);
              changed();
              refresh();
            },
            'small ghost',
            { disabled: i === 0 }
          ),
          button(
            '✕',
            () => {
              st.actions.splice(i, 1);
              changed();
              refresh();
            },
            'small ghost',
            { title: 'Supprimer l’action' }
          )
        ),
        h('small', { class: 'muted' }, def.help),
        fields
      );
    })
  );
  return h(
    'section',
    { class: 'card' },
    h(
      'h2',
      {},
      'Actions',
      h('span', { class: 'spacer' }),
      button(
        '+ Action',
        () => {
          st.actions.push({ id: newId('a'), type: 'N', target: '' });
          changed();
          refresh();
        },
        'small'
      )
    ),
    st.actions.length ? list : h('p', { class: 'muted' }, 'Aucune action : l’étape sert d’attente.')
  );
}

// ===========================================================================
// Modes et sécurités
function modesPanel(store) {
  const p = store.project;
  const b = p.logic.blocks;
  const hasServo = p.equipment.some((e) => e.type === 'SureServo');
  const changed = () => store.changed();
  const exprField = (label, obj, key, opts = {}) => h('label', { class: 'field' }, h('span', {}, label), exprInput(store, obj, key, { expect: 'bool', allowEmpty: true, onInput: changed, ...opts }), opts.help ? h('small', {}, opts.help) : null);

  const cards = [
    h(
      'div',
      { class: 'callout info' },
      'Modes de la classe générée : INITIALISATION → ARRÊT → (PRISE D’ORIGINE) → REPOS ⇄ CYCLE. L’urgence est prioritaire dans tous les modes ; on en sort par l’acquittement puis le réarmement. Le grafcet ne tourne qu’en REPOS et en CYCLE ; « REPOS » = grafcet en situation initiale (les sauvegardes de paramètres n’ont lieu qu’au repos).'
    ),
    card(
      'Arrêt d’urgence',
      h(
        'div',
        { class: 'grid', style: { gridTemplateColumns: '1fr 1fr' } },
        exprField('Condition d’urgence', b.emergency, 'condition', { placeholder: 'ex. NON AU_OK', emptyHint: 'aucune (déconseillé)', help: 'Vraie = urgence. Les sorties prennent leur état de repli, le servo est stoppé, le grafcet est remis à zéro.' }),
        exprField('Acquittement', b.emergency, 'reset', { placeholder: 'ex. Acquitter', emptyHint: 'automatique 1 s après disparition', help: 'Nécessaire pour quitter l’urgence une fois la condition disparue.' })
      ),
      hasServo ? h('p', { class: 'hint', style: { marginTop: '10px' } }, 'Les alarmes du servo déclenchent aussi l’arrêt d’urgence.') : null
    ),
    card(
      'Marche / arrêt',
      h(
        'div',
        { class: 'grid', style: { gridTemplateColumns: '1fr 1fr' } },
        exprField('Condition de mise en marche', b.run, 'start', { placeholder: 'ex. Marche', emptyHint: 'automatique', help: 'Pour passer de ARRÊT à REPOS (le grafcet démarre en situation initiale).' }),
        exprField('Condition d’arrêt', b.run, 'stop', { placeholder: 'ex. Arret', emptyHint: 'aucune', help: 'Ramène en ARRÊT depuis tout mode de fonctionnement et remet le grafcet à zéro.' })
      )
    ),
  ];
  if (hasServo) {
    const sv = b.servo;
    const homingCond = h('div', {});
    const drawHoming = () => homingCond.replaceChildren(sv.homing === 'condition' ? exprField('Condition de prise d’origine', sv, 'homingCondition', { placeholder: 'ex. Origine' }) : '');
    drawHoming();
    const jogFields = h('div', {});
    const drawJog = () =>
      jogFields.replaceChildren(
        sv.jogEnabled
          ? h(
              'div',
              { class: 'grid', style: { gridTemplateColumns: '1fr 1fr', marginTop: '10px' } },
              exprField('Mode manuel actif', sv, 'jogMode', { placeholder: 'ex. Mode_jog' }),
              exprField('Vitesse de jog (1 à 3000)', sv, 'jogSpeed', { expect: 'num', allowEmpty: false, placeholder: 'ex. 500' }),
              exprField('Jog +', sv, 'jogPlus', { placeholder: 'ex. Jog_plus' }),
              exprField('Jog −', sv, 'jogMinus', { placeholder: 'ex. Jog_moins' })
            )
          : ''
      );
    drawJog();
    cards.push(
      card(
        'Servo',
        h(
          'div',
          { class: 'grid', style: { gridTemplateColumns: '1fr 1fr' } },
          h(
            'label',
            { class: 'field' },
            h('span', {}, 'Prise d’origine'),
            select(
              sv,
              'homing',
              [
                { value: 'auto', label: 'Automatique avant le premier cycle' },
                { value: 'condition', label: 'Sur condition' },
                { value: 'none', label: 'Aucune (grafcet seulement)' },
              ],
              {
                onChange: () => {
                  drawHoming();
                  changed();
                },
              }
            )
          ),
          exprField('Couple maximal (0 à 100 %)', sv, 'torque', { expect: 'num', placeholder: 'ex. Couple', emptyHint: 'non modifié' })
        ),
        homingCond,
        h(
          'div',
          { style: { marginTop: '12px' } },
          checkbox(sv, 'jogEnabled', 'Mode manuel (jog) depuis l’écran', {
            onChange: () => {
              drawJog();
              changed();
            },
          })
        ),
        jogFields,
        h('p', { class: 'hint', style: { marginTop: '10px' } }, 'Le jog n’est accepté qu’à l’arrêt ou au repos. Les commandes de jog sont envoyées à chaque cycle tant que le bouton est maintenu (contrôle de butée).')
      )
    );
  }
  return h('div', {}, ...cards);
}

// ===========================================================================
function helpPanel(store) {
  const servo = store.project.equipment.some((e) => e.type === 'SureServo');
  const row = (a, b) => h('tr', {}, h('td', { class: 'mono' }, a), h('td', {}, b));
  return h(
    'div',
    {},
    card(
      'Réceptivités et expressions',
      h(
        'table',
        { class: 'tbl' },
        h(
          'tbody',
          {},
          row('Depart ET NON Porte', 'opérateurs logiques (aussi AND/OR/NOT ou && || !)'),
          row('FM(Bouton)   ↑Bouton', 'front montant (FD / ↓ : front descendant)'),
          row('X3', 'étape 3 active'),
          row('X3.t >= 5s   ou   t/X3/5s', 'étape 3 active depuis 5 s (ms, s, min, h)'),
          row('Compteur >= Nb_pieces', 'comparaisons : =  <>  <  <=  >  >='),
          row('(Cible + 100) * 2', 'arithmétique : + − * / % (division entière entre entiers)'),
          row('MIN(a, b)  MAX(a, b)  ABS(a)', 'fonctions'),
          row('Recette = "A"', 'texte (= et <> uniquement)'),
          row('VRAI   FAUX', 'constantes')
        )
      )
    ),
    servo
      ? card(
          'Servo',
          h(
            'table',
            { class: 'tbl' },
            h(
              'tbody',
              {},
              Object.entries(SERVO_PROPS).map(([k, d]) => row(`Servo.${k}`, d.label))
            )
          )
        )
      : null,
    card(
      'Actions',
      h(
        'table',
        { class: 'tbl' },
        h(
          'tbody',
          {},
          Object.values(ACTION_TYPES)
            .filter((d) => !d.servo || servo)
            .map((d) => row(d.label, d.help))
        )
      ),
      h(
        'p',
        { class: 'hint', style: { marginTop: '10px' } },
        'Cycle d’exécution : actions à l’activation des étapes activées au cycle précédent → franchissement simultané des transitions validées → actions continues et sorties → mémorisation des fronts. Une sortie pilotée par N/S/R vaut le OU de toutes ses actions.'
      )
    )
  );
}

export { compileLogic };

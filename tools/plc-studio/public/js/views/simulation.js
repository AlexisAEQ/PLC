// Étape 6 : simulation du projet (même moteur que le code C++ généré).

import { Simulator, MODES } from '/shared/sim.js';
import { widgetsFor } from '/shared/catalog.js';
import { h, card, pageHead, button, issueList } from '../ui.js';
import { drawGrafcet } from './logic.js';

let sim = null;
let simKey = '';
let running = false;
let speed = 1;
let raf = 0;

const fmtTime = (ms) => {
  const s = ms / 1000;
  return s < 60 ? `${s.toFixed(2)} s` : `${Math.floor(s / 60)} min ${(s % 60).toFixed(1)} s`;
};

export function renderSimulation(root, store) {
  cancelAnimationFrame(raf);
  running = false;
  const p = store.project;
  const key = JSON.stringify([p.variables, p.logic, p.equipment]);
  const errors = store.issues.filter((i) => i.level === 'error' && (i.step === 'logic' || i.step === 'variables'));

  root.append(pageHead('Simulation', 'Le projet tourne ici exactement comme la classe C++ générée : même grafcet, mêmes modes de marche, même ordre de cycle (10 ms). Forcez les entrées, utilisez les commandes et observez les sorties.'));
  if (errors.length) {
    root.append(card('Simulation impossible', h('p', {}, 'Corrigez d’abord ces erreurs :'), issueList(errors, (step) => store.go(step))));
    return;
  }
  if (!sim || simKey !== key) {
    sim = new Simulator(p);
    simKey = key;
  }

  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('class', 'grafcet-svg');
  const canvas = h('div', { class: 'grafcet-canvas' }, svg);

  const modeBadge = h('span', { class: 'mode-badge' });
  const clock = h('span', { class: 'muted mono' });
  const stepsInfo = h('span', { class: 'muted' });
  const playBtn = button('▶ Lancer', () => toggle(), 'primary');
  const speedSel = h(
    'select',
    { on: { change: (e) => (speed = Number(e.target.value)) } },
    [0.25, 1, 5, 20].map((v) => h('option', { value: v, selected: v === speed }, `× ${v}`))
  );

  const bar = h(
    'div',
    { class: 'sim-bar' },
    playBtn,
    button('⏭ 1 cycle', () => {
      sim.scan();
      refresh();
    }),
    button('⏩ 1 s', () => {
      sim.run(1000);
      refresh();
    }),
    button('↺ Réinitialiser', () => {
      sim.reset();
      refresh();
    }),
    h('label', { class: 'row', style: { gap: '6px' } }, 'Vitesse', speedSel),
    h('span', { class: 'spacer' }),
    modeBadge,
    stepsInfo,
    clock
  );

  // Panneaux latéraux
  const side = h('div', { class: 'sim-side' });
  const updaters = [];

  const vars = p.variables;
  const inputs = vars.filter((v) => v.kind === 'input');
  const commands = vars.filter((v) => v.kind === 'command');
  const params = vars.filter((v) => v.kind === 'parameter');
  const outputs = vars.filter((v) => v.kind === 'output' && !v.system);
  const indicators = vars.filter((v) => v.kind === 'indicator' || v.kind === 'memory');

  const nameCell = (v) => h('div', { class: 'name' }, h('div', {}, v.label), h('div', { class: 'sym' }, v.symbol));

  const writable = (v) => {
    if (v.dataType === 'bool') {
      const momentary = v.kind === 'command' && widgetsFor(v)[0] === 'tx-button-hold' && (v.hmi?.widget || 'tx-button-hold') === 'tx-button-hold';
      if (momentary) {
        // Bouton maintenu (comme à l'écran) ; « bloquer » le garde enfoncé pour la simulation.
        const btn = h('button', { class: 'btn small momentary', type: 'button' }, 'Appuyer');
        const lock = h('input', { type: 'checkbox', title: 'Garder le bouton enfoncé' });
        const set = (val) => {
          sim.set(v.symbol, val);
          btn.classList.toggle('pressed', val);
        };
        btn.addEventListener('pointerdown', () => set(true));
        ['pointerup', 'pointerleave'].forEach((ev) => btn.addEventListener(ev, () => !lock.checked && sim.get(v.symbol) && set(false)));
        lock.addEventListener('change', () => set(lock.checked));
        updaters.push(() => btn.classList.toggle('pressed', !!sim.get(v.symbol)));
        return h('div', { class: 'io-row' }, nameCell(v), btn, h('label', { class: 'check muted', style: { fontSize: '11px' } }, lock, 'bloquer'));
      }
      const box = h('input', { type: 'checkbox', on: { change: (e) => (sim.set(v.symbol, e.target.checked), refresh()) } });
      updaters.push(() => (box.checked = !!sim.get(v.symbol)));
      return h('div', { class: 'io-row' }, nameCell(v), h('label', { class: 'switch' }, box, h('span')));
    }
    const field = h('input', { type: v.dataType === 'text' ? 'text' : 'number', step: v.dataType === 'float' ? 'any' : '1' });
    field.addEventListener('change', () => {
      sim.set(v.symbol, v.dataType === 'text' ? field.value : Number(field.value));
      refresh();
    });
    updaters.push(() => {
      if (document.activeElement !== field) field.value = sim.get(v.symbol);
    });
    return h('div', { class: 'io-row' }, nameCell(v), field);
  };

  const readOnly = (v, red = false) => {
    if (v.dataType === 'bool') {
      const led = h('span', { class: `led ${red ? 'red' : ''}` });
      updaters.push(() => led.classList.toggle('on', !!sim.get(v.symbol)));
      return h('div', { class: 'io-row' }, nameCell(v), led);
    }
    const val = h('span', { class: 'val' });
    updaters.push(() => {
      const x = sim.get(v.symbol);
      val.textContent = v.dataType === 'float' ? Number(x).toFixed(3) : String(x);
    });
    return h('div', { class: 'io-row' }, nameCell(v), val);
  };

  if (inputs.length) side.append(card('Entrées (forçage)', ...inputs.map(writable)));
  if (commands.length) side.append(card('Commandes de l’écran', ...commands.map(writable)));
  if (outputs.length) side.append(card('Sorties', ...outputs.map((v) => readOnly(v))));
  if (indicators.length) side.append(card('Indicateurs et mémoires', ...indicators.map((v) => readOnly(v, /defaut|alarme|erreur/i.test(v.symbol)))));
  if (params.length) side.append(card('Paramètres', ...params.map(writable)));

  if (sim.servo) {
    const s = sim.servo;
    const pos = h('div', { class: 'servo-pos' });
    const target = h('div', { class: 'servo-target' });
    const posText = h('span', { class: 'mono' });
    const flags = h('div', { class: 'row', style: { gap: '12px', fontSize: '12px', marginTop: '6px' } });
    const flag = (label, get, red) => {
      const led = h('span', { class: `led ${red ? 'red' : ''}` });
      updaters.push(() => led.classList.toggle('on', !!get()));
      return h('span', { class: 'row', style: { gap: '5px' } }, led, label);
    };
    flags.append(
      flag('Prêt', () => s.ready),
      flag('Origine faite', () => s.homeDone),
      flag('En mouvement', () => sim.servoIsMoving() || s.jogDir !== 0),
      flag('En position', () => sim.servoMoveDone),
      flag('Alarme', () => s.alarm, true)
    );
    const range = sim.maxRangePuu || 1000000;
    updaters.push(() => {
      pos.style.left = `${Math.max(0, Math.min(100, (s.position / range) * 100))}%`;
      target.style.left = `${Math.max(0, Math.min(100, (s.target / range) * 100))}%`;
      posText.textContent = `${s.position} / ${range} impulsions · vitesse ${s.speedIdx}`;
    });
    side.append(
      card(
        'Servo (modèle simplifié)',
        posText,
        h('div', { class: 'servo-track' }, target, pos),
        flags,
        h(
          'div',
          { class: 'btn-row', style: { marginTop: '10px' } },
          button('Déclencher une alarme', () => (s.raiseAlarm(0x0201), refresh()), 'small danger'),
          button('Perdre l’origine', () => ((s.homeDone = false), refresh()), 'small')
        )
      )
    );
  }

  const messages = h('div', { class: 'sim-log' });
  const journal = h('div', { class: 'sim-log' });
  updaters.push(() => {
    messages.replaceChildren(
      ...sim.messages
        .slice(-12)
        .reverse()
        .map((m) => {
          // Ces notifications sont filtrées par l'automate selon les options du projet.
          const hidden = (m.level === 'success' && !p.options.showSuccessMessages) || (m.level === 'info' && p.options.showInfoMessages === false);
          return h('div', { class: `msg-${m.level}` }, h('span', { class: 't' }, fmtTime(m.t)), m.text, hidden ? h('span', { class: 'muted' }, ' (non affiché sur l’automate)') : null);
        })
    );
    journal.replaceChildren(
      ...sim.journal
        .slice(-40)
        .reverse()
        .map((j) => h('div', {}, h('span', { class: 't' }, fmtTime(j.t)), j.text))
    );
  });

  root.append(
    bar,
    h(
      'div',
      { class: 'sim-layout' },
      h(
        'div',
        {},
        h('section', { class: 'card grafcet-card' }, canvas),
        h('div', { class: 'grid', style: { gridTemplateColumns: '1fr 1fr' } }, card('Messages à l’écran', messages), card('Journal', journal))
      ),
      side
    )
  );

  let lastSteps = '';
  function refresh() {
    const active = new Set(sim.ir.steps.filter((_, i) => sim.X[i]).map((s) => s.id));
    const ready = new Set(sim.transitionStates().filter((t) => t.enabled && t.cond).map((t) => t.id));
    const times = new Map(sim.ir.steps.map((s, i) => [s.id, sim.X[i] ? sim.now - sim.Xstart[i] : undefined]).filter(([, t]) => t !== undefined));
    const stepsKey = [...active].join(',') + '|' + [...ready].join(',') + '|' + Math.floor(sim.now / 100);
    if (stepsKey !== lastSteps) {
      drawGrafcet(svg, store, { onSelect: null, onDragEnd: null, sim: { active, ready, times } });
      lastSteps = stepsKey;
    }
    modeBadge.className = `mode-badge ${sim.mode}`;
    modeBadge.textContent = MODES[sim.mode];
    clock.textContent = fmtTime(sim.now);
    const act = sim.activeSteps();
    stepsInfo.textContent = act.length ? `Étapes actives : ${act.map((n) => 'X' + n).join(' ')}` : 'Aucune étape active';
    for (const u of updaters) u();
  }

  function toggle() {
    running = !running;
    playBtn.textContent = running ? '⏸ Pause' : '▶ Lancer';
    playBtn.classList.toggle('primary', !running);
    if (running) {
      let last = performance.now();
      let acc = 0;
      const loop = (t) => {
        if (!running || !document.body.contains(svg)) {
          running = false;
          return;
        }
        acc += Math.min(250, t - last) * speed;
        last = t;
        const n = Math.floor(acc / sim.scanMs);
        if (n > 0) {
          for (let i = 0; i < n; i++) sim.scan();
          acc -= n * sim.scanMs;
          refresh();
        }
        raf = requestAnimationFrame(loop);
      };
      raf = requestAnimationFrame(loop);
    } else cancelAnimationFrame(raf);
  }

  refresh();
}

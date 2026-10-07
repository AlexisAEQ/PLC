// Scénarios de test de la logique, exécutés avec le simulateur (même moteur que le C++ généré).
//
// Un scénario est une suite d'actions sur l'automate simulé et de vérifications :
//   { "set": { "AU_OK": true, "Cible": 5000 } }      forcer des entrées / écrire des variables
//   { "press": "Depart" }   { "press": { "symbol": "Depart", "ms": 300 } }
//                                                   appui (vrai puis faux, 100 ms par défaut)
//   { "wait": "2s" }                                laisser tourner (ms, s, min, h ; nombre = ms)
//   { "until": "X3 ET Verin", "timeout": "5s" }     tourner jusqu'à ce que la condition soit vraie
//   { "expect": { "mode": "REPOS", "X0": true, "Verin": false, "steps": [0] } }
//   { "expect": "Verin ET NON Soufflage" }          vérification (objet ou expression)
//   { "message": "Pièce terminée" }                 un message contenant ce texte a été émis
//   { "servo": "alarm" | "loseHome" }               défaut du servo simulé
//   { "comment": "..." }                            simple repère dans le rapport
//
// Dans « expect » / « until » (forme objet) : symboles de variables, "X<n>" (étape active),
// "steps" (liste exacte des étapes actives), "mode" (REPOS, CYCLE, ARRÊT, URGENCE, MANUEL,
// PRISE D'ORIGINE, INITIALISATION, RÉARMEMENT ou leur nom interne), "Servo.<prop>".

import { Simulator, MODES } from './sim.js';
import { tryParse, projectContext } from './expr.js';
import { normalizeLogic } from './grafcet.js';

const UNITS = { ms: 1, s: 1000, min: 60000, h: 3600000 };

export function parseDuration(d, fallback = 0) {
  if (d === undefined || d === null || d === '') return fallback;
  if (typeof d === 'number') return d;
  const m = /^\s*(\d+(?:[.,]\d+)?)\s*(ms|s|min|h)?\s*$/i.exec(String(d));
  if (!m) throw new Error(`durée invalide « ${d} » (ex. 500ms, 2s, 1min)`);
  return Math.round(Number(m[1].replace(',', '.')) * UNITS[(m[2] || 'ms').toLowerCase()]);
}

const plain = (s) =>
  String(s)
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .toUpperCase()
    .replace(/[^A-Z]/g, '');
const MODE_KEYS = new Map();
for (const [key, label] of Object.entries(MODES)) {
  MODE_KEYS.set(plain(key), key);
  MODE_KEYS.set(plain(label), key);
}
export function modeKey(name) {
  return MODE_KEYS.get(plain(name)) || null;
}

const fmt = (v) => (typeof v === 'string' ? `"${v}"` : Array.isArray(v) ? `[${v.join(', ')}]` : String(v));
const same = (a, b) => (typeof a === 'number' && typeof b === 'number' ? Math.abs(a - b) <= 1e-4 * Math.max(1, Math.abs(b)) : a === b);

// Exécute un scénario. Retourne { name, passed, failures, log, duration }.
export function runScenario(project, scenario, { scanMs = 10 } = {}) {
  const sim = new Simulator(project, { scanMs });
  const logic = normalizeLogic(project.logic);
  const ctx = projectContext(project, { steps: new Set(logic.steps.map((s) => s.num)) });
  const failures = [];
  const log = [];
  const t = () => `${(sim.now / 1000).toFixed(2)} s`;
  const state = () => `mode ${MODES[sim.mode]}, étapes actives [${sim.activeSteps().join(', ')}]`;
  const fail = (i, msg) => {
    failures.push({ index: i, time: sim.now, message: msg, state: state() });
    log.push(`  ✗ [${t()}] ${msg} — ${state()}`);
  };
  const ok = (msg) => log.push(`  ✓ [${t()}] ${msg}`);
  const msgStart = () => sim.messages.length;
  let messagesFrom = 0;

  const compile = (src) => {
    const r = tryParse(String(src), ctx);
    if (r.error) throw new Error(`expression « ${src} » : ${r.error}`);
    return r.ast;
  };

  // Vérifie un objet de conditions ; retourne la liste des écarts.
  const mismatches = (cond) => {
    if (typeof cond === 'string') return sim.raw(compile(cond)) ? [] : [`« ${cond} » est faux`];
    const out = [];
    for (const [k, want] of Object.entries(cond)) {
      let got;
      if (k === 'mode') {
        const key = modeKey(want);
        if (!key) throw new Error(`mode inconnu « ${want} »`);
        if (sim.mode !== key) out.push(`mode ${MODES[sim.mode]} au lieu de ${MODES[key]}`);
        continue;
      }
      if (k === 'steps') {
        const active = sim.activeSteps();
        const exp = [...want].map(Number).sort((a, b) => a - b);
        if (active.join(',') !== exp.join(',')) out.push(`étapes actives [${active.join(', ')}] au lieu de [${exp.join(', ')}]`);
        continue;
      }
      const step = /^X(\d+)$/i.exec(k);
      if (step) {
        const i = sim.stepIndex(Number(step[1]));
        if (i < 0) throw new Error(`étape ${step[1]} inexistante`);
        got = sim.X[i];
      } else if (/^Servo\./.test(k)) got = sim.servoProp(k.slice(6));
      else if (sim.vars.has(k)) got = sim.get(k);
      else throw new Error(`« ${k} » : symbole inconnu`);
      if (!same(got, want)) out.push(`${k} = ${fmt(got)} au lieu de ${fmt(want)}`);
    }
    return out;
  };
  const describe = (cond) => (typeof cond === 'string' ? cond : Object.entries(cond).map(([k, v]) => `${k}=${fmt(v)}`).join(', '));

  const setValues = (obj) => {
    for (const [k, v] of Object.entries(obj)) {
      if (!sim.vars.has(k)) throw new Error(`« ${k} » : symbole inconnu`);
      sim.set(k, v);
    }
  };

  const steps = Array.isArray(scenario.steps) ? scenario.steps : [];
  try {
    if (scenario.init) setValues(scenario.init);
    messagesFrom = msgStart();
    steps.forEach((st, i) => {
      if (st.comment) log.push(`  · ${st.comment}`);
      if (st.set) {
        setValues(st.set);
        log.push(`  → [${t()}] ${describe(st.set)}`);
      }
      if (st.press) {
        const p = typeof st.press === 'string' ? { symbol: st.press } : st.press;
        if (!sim.vars.has(p.symbol)) throw new Error(`« ${p.symbol} » : symbole inconnu`);
        const ms = parseDuration(p.ms, 100);
        log.push(`  → [${t()}] appui sur ${p.symbol} (${ms} ms)`);
        sim.set(p.symbol, true);
        sim.run(ms);
        sim.set(p.symbol, false);
        sim.scan();
      }
      if (st.servo) {
        if (!sim.servo) throw new Error('pas de servo dans le projet');
        if (st.servo === 'alarm') sim.servo.raiseAlarm(0x0201);
        else if (st.servo === 'loseHome') sim.servo.homeDone = false;
        else throw new Error(`action servo inconnue « ${st.servo} » (alarm, loseHome)`);
        log.push(`  → [${t()}] servo : ${st.servo}`);
      }
      if (st.wait !== undefined) sim.run(parseDuration(st.wait));
      if (st.until !== undefined) {
        const timeout = parseDuration(st.timeout, 10000);
        const t0 = sim.now;
        while (mismatches(st.until).length && sim.now - t0 < timeout) sim.scan();
        const left = mismatches(st.until);
        if (left.length) fail(i, `attente de ${describe(st.until)} : délai de ${timeout} ms dépassé (${left.join(' ; ')})`);
        else ok(`${describe(st.until)} atteint après ${sim.now - t0} ms`);
      }
      if (st.expect !== undefined) {
        const bad = mismatches(st.expect);
        if (bad.length) fail(i, bad.join(' ; '));
        else ok(describe(st.expect));
      }
      if (st.message !== undefined) {
        const found = sim.messages.slice(messagesFrom).some((m) => m.text.includes(st.message));
        if (found) ok(`message « ${st.message} »`);
        else fail(i, `aucun message contenant « ${st.message} » (messages : ${sim.messages.slice(messagesFrom).map((m) => m.text).join(' | ') || 'aucun'})`);
      }
    });
  } catch (e) {
    failures.push({ index: -1, time: sim.now, message: `scénario invalide : ${e.message}`, state: state() });
    log.push(`  ✗ scénario invalide : ${e.message}`);
  }
  return { name: scenario.name || 'Scénario', passed: failures.length === 0, failures, log, duration: sim.now, journal: sim.journal.slice(-30) };
}

export function runScenarios(project, scenarios, opts) {
  return (scenarios || []).map((s) => runScenario(project, s, opts));
}

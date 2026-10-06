// PLC Studio — application principale : état, navigation, enregistrement.

import { newProject, normalizeProject } from '/shared/model.js';
import { buildLayout } from '/shared/layout.js';
import { validateProject } from '/shared/validate.js';
import { effectivePages } from '/shared/generate.js';
import { h, clear, toast, modal, button, field, issueBadge } from './ui.js';
import { api } from './api.js';
import { renderProject } from './views/project.js';
import { renderEquipment } from './views/equipment.js';
import { renderVariables } from './views/variables.js';
import { renderLogic } from './views/logic.js';
import { renderHmi } from './views/hmi.js';
import { renderSimulation } from './views/simulation.js';
import { renderGenerate } from './views/generate.js';

export const STEPS = [
  { key: 'project', label: 'Projet', render: renderProject },
  { key: 'equipment', label: 'Équipements', render: renderEquipment },
  { key: 'variables', label: 'Câblage et variables', render: renderVariables },
  { key: 'logic', label: 'Logique (grafcet)', render: renderLogic },
  { key: 'hmi', label: 'Écrans opérateur', render: renderHmi },
  { key: 'simulation', label: 'Simulation', render: renderSimulation },
  { key: 'generate', label: 'Génération', render: renderGenerate },
];

const LAST_KEY = 'plc-studio:last-project';

export const store = {
  project: null,
  savedName: null,
  dirty: false,
  layout: null,
  pages: [],
  issues: [],
  step: 'project',
  info: null,
  saveTimer: null,
  hooks: [], // rafraîchissements "live" enregistrés par la vue courante

  onChange(fn) {
    this.hooks.push(fn);
  },

  // Appelé après toute modification du projet.
  // render=true : redessine la vue courante (changement de structure).
  changed({ render = false } = {}) {
    this.dirty = true;
    this.recompute();
    renderChrome();
    if (render) renderView();
    else for (const fn of this.hooks) fn();
    scheduleSave();
  },

  recompute() {
    this.layout = buildLayout(this.project);
    this.pages = effectivePages(this.project, this.layout);
    this.issues = validateProject({ ...this.project, hmi: { ...this.project.hmi, pages: this.pages } }, this.layout);
  },

  go(step) {
    this.step = step;
    location.hash = step;
    renderChrome();
    renderView();
    document.getElementById('content').scrollTop = 0;
  },

  rerender() {
    renderChrome();
    renderView();
  },
};

function renderChrome() {
  const p = store.project;
  const title = document.getElementById('project-title');
  clear(title).append(h('span', {}, 'Projet : '), h('strong', {}, p?.name || '—'), h('span', {}, p?.description ? ` — ${p.description}` : ''));

  const saveState = document.getElementById('save-state');
  saveState.className = `save-state ${store.dirty ? 'dirty' : ''}`;
  saveState.textContent = store.dirty ? 'Modifications non enregistrées' : store.savedName ? `Enregistré dans projets/${store.savedName}.plc.json` : '';

  const nav = clear(document.getElementById('steps'));
  STEPS.forEach((s, i) => {
    const issues = store.issues.filter((x) => x.step === s.key);
    nav.append(
      h(
        'button',
        { class: `step ${store.step === s.key ? 'active' : ''}`, type: 'button', on: { click: () => store.go(s.key) } },
        h('span', { class: 'num' }, i + 1),
        h('span', { class: 'label' }, s.label),
        issueBadge(issues)
      )
    );
  });
  const errors = store.issues.filter((i) => i.level === 'error').length;
  nav.append(
    h(
      'div',
      { class: 'steps-footer' },
      errors ? h('span', { class: 'badge err' }, `${errors} erreur(s)`) : h('span', { class: 'badge ok' }, 'Projet valide'),
      h('div', { style: { marginTop: '6px' } }, `${store.layout?.nodeCount ?? 0} nœuds`),
      store.info?.currentApp ? h('div', {}, `Application actuelle : ${store.info.currentApp}`) : null
    )
  );
}

function renderView() {
  const content = clear(document.getElementById('content'));
  store.hooks = [];
  const step = STEPS.find((s) => s.key === store.step) || STEPS[0];
  try {
    step.render(content, store);
  } catch (e) {
    console.error(e);
    content.append(h('div', { class: 'callout warn' }, `Erreur d'affichage : ${e.message}`));
  }
}

function scheduleSave() {
  clearTimeout(store.saveTimer);
  if (!store.savedName) return; // premier enregistrement : explicite
  store.saveTimer = setTimeout(() => save({ silent: true }), 1500);
}

export async function save({ silent = false } = {}) {
  clearTimeout(store.saveTimer);
  const p = store.project;
  if (!/^[A-Za-z][A-Za-z0-9_]{0,23}$/.test(p.name)) {
    if (!silent) toast('Nom de projet invalide : corrigez-le à l’étape Projet avant d’enregistrer.', 'error');
    return false;
  }
  try {
    await api.saveProject(p);
    if (store.savedName && store.savedName !== p.name && !silent) {
      toast(`Projet enregistré sous un nouveau nom. L'ancien fichier projets/${store.savedName}.plc.json est conservé.`);
    }
    store.savedName = p.name;
    store.dirty = false;
    localStorage.setItem(LAST_KEY, p.name);
    renderChrome();
    if (!silent) toast('Projet enregistré.');
    return true;
  } catch (e) {
    toast(`Échec de l'enregistrement : ${e.message}`, 'error');
    return false;
  }
}

export function loadProject(project, savedName = null) {
  store.project = normalizeProject(project);
  store.savedName = savedName;
  store.dirty = !savedName;
  store.recompute();
  if (savedName) localStorage.setItem(LAST_KEY, savedName);
  store.rerender();
}

function askNewProject() {
  modal((close) => {
    const model = { name: 'MonProjet' };
    const nameInput = h('input', { type: 'text', value: model.name, class: 'mono' });
    const create = async () => {
      const name = nameInput.value.trim();
      if (!/^[A-Za-z][A-Za-z0-9_]{0,23}$/.test(name)) {
        nameInput.classList.add('invalid');
        return;
      }
      close();
      loadProject(newProject(name));
      store.go('project');
      await save();
    };
    nameInput.addEventListener('keydown', (e) => e.key === 'Enter' && create());
    return h(
      'div',
      {},
      h('h2', {}, 'Nouveau projet'),
      field('Nom du projet', nameInput, 'Sert de nom de classe C++ (src/<Nom>/) et de fichier de paramètres. Lettres, chiffres et _ ; 24 caractères maximum.'),
      h('div', { class: 'actions' }, button('Annuler', close), button('Créer', create, 'primary'))
    );
  });
}

async function askOpenProject() {
  let list = [];
  try {
    list = await api.listProjects();
  } catch (e) {
    toast(e.message, 'error');
  }
  modal((close) =>
    h(
      'div',
      {},
      h('h2', {}, 'Ouvrir un projet'),
      list.length
        ? h(
            'div',
            { class: 'file-list' },
            list.map((p) =>
              h(
                'div',
                {
                  class: 'file-row',
                  on: {
                    click: async () => {
                      close();
                      await openProject(p.name);
                    },
                  },
                },
                h('span', { class: 'path' }, p.file),
                h('span', { class: 'muted' }, new Date(p.modified).toLocaleString('fr-FR'))
              )
            )
          )
        : h('p', { class: 'muted' }, 'Aucun projet dans le dossier projets/.'),
      h(
        'div',
        { class: 'actions' },
        button('Annuler', close),
        button(
          'Nouveau projet…',
          () => {
            close();
            askNewProject();
          },
          'primary'
        )
      )
    )
  );
}

async function openProject(name) {
  try {
    const p = await api.getProject(name);
    loadProject(p, name);
    store.go(store.step || 'project');
  } catch (e) {
    toast(`Impossible d'ouvrir ${name} : ${e.message}`, 'error');
  }
}

async function start() {
  document.getElementById('btn-save').addEventListener('click', () => save());
  document.getElementById('btn-new').addEventListener('click', askNewProject);
  document.getElementById('btn-open').addEventListener('click', askOpenProject);
  window.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
      e.preventDefault();
      save();
    }
  });
  window.addEventListener('beforeunload', (e) => {
    if (store.dirty) {
      e.preventDefault();
      e.returnValue = '';
    }
  });
  window.addEventListener('hashchange', () => {
    const step = location.hash.slice(1);
    if (step && step !== store.step && STEPS.some((s) => s.key === step)) store.go(step);
  });

  const fromHash = location.hash.slice(1);
  if (STEPS.some((s) => s.key === fromHash)) store.step = fromHash;

  try {
    store.info = await api.info();
  } catch (e) {
    toast(`Serveur injoignable : ${e.message}`, 'error');
  }

  const last = localStorage.getItem(LAST_KEY);
  let opened = false;
  if (last) {
    try {
      loadProject(await api.getProject(last), last);
      opened = true;
    } catch {
      /* projet supprimé */
    }
  }
  if (!opened) {
    loadProject(newProject('MonProjet'));
    store.savedName = null;
    renderChrome();
    const list = await api.listProjects().catch(() => []);
    if (list.length) askOpenProject();
    else askNewProject();
  }
}

start();

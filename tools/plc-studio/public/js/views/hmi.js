// Étape 5 : éditeur visuel des écrans opérateur (data/interface.json).
//
// Gauche : structure (pages > sections > widgets) et éléments disponibles, en glisser-déposer.
// Centre : l'interface web réelle du firmware (data/index.js), cliquable et réorganisable.
// Droite : propriétés de l'élément sélectionné (formulaires générés depuis component-manifest.json).

import { WIDGET_LABELS } from '/shared/catalog.js';
import { describeRef, unplacedRefs, widgetProps } from '/shared/hmi.js';
import { generateConfig, generateInterface } from '/shared/generate.js';
import { h, select, pageHead, button, confirmDialog, toast } from '../ui.js';
import { issueBlock } from './project.js';
import { HmiPreview } from '../hmipreview.js';

const clone = (x) => JSON.parse(JSON.stringify(x));
const newId = (p) => `${p}_${Math.random().toString(36).slice(2, 9)}`;

// État de l'éditeur (conservé entre deux affichages de l'étape).
const ui = { page: 0, sel: null, mode: 'edit', device: 'tablet', preview: null, previewKey: null };
let manifest = null;
let drag = null; // élément glissé depuis la structure ou la palette

const DEVICES = {
  phone: { label: 'Téléphone', width: 390 },
  tablet: { label: 'Tablette', width: 820 },
  desktop: { label: 'Écran', width: 1280 },
};

// Libellés français des propriétés de component-manifest.json.
const PROP_LABELS = {
  tooltip: 'Info-bulle',
  color: 'Couleur',
  low: 'Minimum',
  high: 'Maximum',
  step: 'Pas',
  unit: 'Unité',
  min: 'Échelle min',
  max: 'Échelle max',
  orientation: 'Orientation',
  barLength: 'Longueur de la barre',
  blink: 'Clignotant',
  inverse: 'Affichage inversé',
  state: 'Style',
  isBlinking: 'Clignotant',
  alignment: 'Alignement',
  nb_digits_ap: 'Décimales',
  buttonText: 'Texte du bouton',
  hideButton: 'Envoyer sans bouton',
  buttonColor: 'Couleur du bouton',
  multiline: 'Nombre de lignes',
  minLength: 'Longueur min',
  maxLength: 'Longueur max',
  items: 'Choix proposés',
  default: 'Choix par défaut',
  loadable: 'Attendre la fin de l’action (sablier)',
  disable: 'Désactivé',
  isLabelAlarmWarningOn: 'Afficher les seuils',
  consigne: 'Consigne',
  alarmHigh: 'Alarme haute (écart)',
  warningHigh: 'Avertissement haut (écart)',
  warningLow: 'Avertissement bas (écart)',
  alarmLow: 'Alarme basse (écart)',
};
// Aides (traduction des descriptions de component-manifest.json). Clé « widget.prop » prioritaire.
const PROP_HELP = {
  tooltip: 'Texte affiché au survol.',
  disable: 'Le widget est affiché mais ne peut pas être utilisé.',
  color: 'Couleur du bouton.',
  'rx-indicator.color': 'Couleur du voyant allumé (lime, red, blue, orange…).',
  'tx-qr.color': 'Couleur du bouton qui ouvre le lecteur.',
  low: 'Valeur minimale acceptée.',
  high: 'Valeur maximale acceptée.',
  step: 'Pas du curseur.',
  unit: 'Unité affichée.',
  min: 'Valeur affichée en bas de la barre.',
  max: 'Valeur affichée en haut de la barre.',
  orientation: 'Sens de la barre.',
  barLength: 'Longueur de la barre (ex. 200px).',
  blink: 'Le voyant clignote quand il est allumé.',
  inverse: 'Allumé quand la valeur est fausse.',
  state: 'Couleur du texte.',
  isBlinking: 'La valeur clignote.',
  alignment: 'Alignement de la valeur.',
  nb_digits_ap: 'Chiffres après la virgule pour les nombres non entiers.',
  buttonText: 'Texte du bouton d’envoi.',
  hideButton: 'Pas de bouton : la valeur est envoyée à chaque modification.',
  buttonColor: 'Couleur du bouton d’envoi.',
  multiline: 'Nombre de lignes (0 = une seule ligne).',
  minLength: 'Nombre minimal de caractères.',
  maxLength: 'Nombre maximal de caractères.',
  items: 'Valeurs proposées dans la liste, séparées par « ; ».',
  default: 'Valeur sélectionnée au départ (doit faire partie des choix).',
  loadable: 'Bouton à clic simple avec sablier au lieu d’un bouton maintenu.',
  isLabelAlarmWarningOn: 'Affiche les seuils d’alarme et d’avertissement.',
  consigne: 'Valeur de référence des seuils.',
  alarmHigh: 'Écart au-dessus de la consigne qui déclenche l’alarme haute.',
  warningHigh: 'Écart au-dessus de la consigne qui déclenche l’avertissement haut.',
  warningLow: 'Écart au-dessous de la consigne qui déclenche l’avertissement bas.',
  alarmLow: 'Écart au-dessous de la consigne qui déclenche l’alarme basse.',
};
const WIDGET_INFO = {
  'tx-string': 'Champ de saisie de texte avec bouton d’envoi.',
  'tx-slider': 'Curseur pour choisir une valeur numérique dans une plage.',
  'tx-qr': 'Bouton qui ouvre un lecteur de code QR (caméra ou fichier).',
  'tx-numeric': 'Champ de saisie numérique avec limites et bouton d’envoi.',
  'tx-modal': 'Bouton qui ouvre une fenêtre contenant d’autres widgets.',
  'tx-dropdown': 'Liste déroulante qui envoie la valeur choisie.',
  'tx-button-hold': 'Bouton : vrai tant qu’il est appuyé, faux au relâchement (ou clic simple avec sablier).',
  'tx-bool': 'Interrupteur qui envoie vrai/faux.',
  'rx-numeric': 'Affiche une valeur numérique.',
  'rx-level-bar': 'Affiche une valeur sous forme de barre avec seuils d’alarme.',
  'rx-label': 'Affiche un texte, avec couleur et clignotement.',
  'rx-indicator': 'Voyant de couleur, éventuellement clignotant.',
  'rx-bool': 'Affiche un booléen sous forme d’interrupteur (lecture seule).',
};

// Propriétés gérées ailleurs ou dangereuses dans l'éditeur.
const HIDDEN_PROPS = new Set(['name', 'value', 'nodes', 'getState']);
const COLOR_CHOICES = ['lime', 'green', 'red', 'orange', 'yellow', 'blue', 'cyan', 'white'];
const ENUM_LABELS = {
  primary: 'Principal (bleu)',
  success: 'Succès (vert)',
  warning: 'Attention (orange)',
  danger: 'Danger (rouge)',
  neutral: 'Neutre (gris)',
  vertical: 'Verticale',
  horizontal: 'Horizontale',
  left: 'Gauche',
  center: 'Centré',
  right: 'Droite',
};

async function loadManifest() {
  if (manifest) return manifest;
  try {
    manifest = await (await fetch('/hmi/component-manifest.json')).json();
  } catch {
    manifest = {};
  }
  return manifest;
}

// ---------------------------------------------------------------------------
// Modèle
function pages(store) {
  return store.project.hmi.auto !== false || !store.project.hmi.pages.length ? store.pages : store.project.hmi.pages;
}

// Toute modification transforme la disposition automatique en disposition personnalisée.
function editable(store) {
  const h = store.project.hmi;
  if (h.auto !== false || !h.pages.length) {
    h.pages = clone(store.pages);
    h.auto = false;
    toast('Disposition personnalisée : elle ne suivra plus automatiquement les nouvelles variables (elles restent dans « Éléments disponibles »).');
  }
  return h.pages;
}

// Éléments réellement envoyés à l'écran (même filtre que generateInterface).
function rendered(store, section) {
  return section.items.filter((it) => store.layout.nodes.has(it.ref));
}

function itemAt(store, sel) {
  const pg = pages(store)[sel.page];
  const sec = pg?.sections[sel.s];
  if (!sec) return null;
  const it = rendered(store, sec)[sel.i];
  return it ? { page: pg, section: sec, item: it, index: sec.items.indexOf(it) } : null;
}

function newItem(store, ref) {
  const d = describeRef(store.project, store.layout, ref);
  const widget = d?.variable?.hmi?.widget && d.widgets.includes(d.variable.hmi.widget) ? d.variable.hmi.widget : d?.widgets[0] || 'rx-label';
  return { ref, label: d?.label || ref, widget };
}

// Déplace ou insère un élément. source : {type:'item', page, s, i} | {type:'ref', ref}
function applyDrop(store, source, target) {
  const all = editable(store);
  const destSection = all[target.page]?.sections[target.s];
  if (!destSection) return null;
  const destRendered = rendered(store, destSection);
  const before = destRendered[target.i]; // insertion avant cet élément (ou à la fin)
  let item;
  if (source.type === 'item') {
    const src = all[source.page]?.sections[source.s];
    if (!src) return null;
    item = rendered(store, src)[source.i];
    if (!item) return null;
    if (item === before) return { page: target.page, s: target.s, i: target.i };
    src.items.splice(src.items.indexOf(item), 1);
  } else {
    item = newItem(store, source.ref);
  }
  const at = before ? destSection.items.indexOf(before) : destSection.items.length;
  destSection.items.splice(at < 0 ? destSection.items.length : at, 0, item);
  return { kind: 'item', page: target.page, s: target.s, i: rendered(store, destSection).indexOf(item) };
}

// ---------------------------------------------------------------------------
export function renderHmi(root, store) {
  const p = store.project;
  const all = pages(store);
  if (ui.page >= all.length) ui.page = Math.max(0, all.length - 1);
  if (ui.sel && ui.sel.page >= all.length) ui.sel = null;

  const treeBox = h('div', { class: 'hmi-tree' });
  const propsBox = h('div', { class: 'hmi-props' });
  const frameBox = h('div', { class: 'hmi-frame-box' });
  const frameScaler = h('div', { class: 'hmi-frame-scaler' });
  frameBox.append(frameScaler);

  // --- aperçu
  if (!ui.preview) {
    ui.preview = new HmiPreview({
      onSelect: (sel) => choose(sel),
      onDrop: (source, target) => drop(source, target),
      onPage: (index) => {
        ui.page = index;
        drawTree();
      },
      getDrag: () => drag,
      onReady: () => ui.preview.setSelection(ui.sel),
    });
  }
  const preview = ui.preview;
  preview.mode = ui.mode;
  preview.page = ui.page;

  const previewData = () => {
    const pgs = pages(store);
    const config = generateConfig(p, store.layout, pgs);
    config.security = { passwords: { pages: {} } }; // pas de mots de passe dans l'aperçu
    const iface = generateInterface(p, store.layout, pgs);
    const states = [];
    for (const [ref, node] of store.layout.nodes) {
      if (ref.startsWith('var:') && node.bankRef) continue;
      const v = node.variable ? p.variables.find((x) => x.uid === node.variable) : null;
      let value = false;
      if (ref === 'sys:state') value = 'REPOS';
      else if (ref === 'sys:clock') value = new Date().toLocaleString('fr-FR');
      else if (/text/.test(node.sectionType)) value = v?.kind === 'parameter' ? String(v.initial ?? '') : '—';
      else if (/bool/.test(node.sectionType) || /Coil|Status/.test(node.sectionType)) value = v?.kind === 'parameter' ? !!v.initial : false;
      else value = v?.kind === 'parameter' ? Number(v.initial) || 0 : 0;
      states.push({ hash: node.hash, value });
    }
    return { config, interface: iface, states };
  };

  let refreshTimer = null;
  const refreshPreview = (delay = 0) => {
    clearTimeout(refreshTimer);
    refreshTimer = setTimeout(() => {
      preview.setData(previewData());
      preview.refresh();
    }, delay);
  };

  // Après une modification : structure et aperçu ; propriétés seulement si demandé.
  const changed = ({ props = false, delay = 0 } = {}) => {
    store.changed();
    drawHead();
    drawTree();
    if (props) drawProps();
    refreshPreview(delay);
  };

  function choose(sel) {
    ui.sel = sel;
    if (sel && sel.page !== ui.page) {
      ui.page = sel.page;
      preview.showPage(ui.page);
    }
    preview.setSelection(sel);
    drawTree();
    drawProps();
  }

  function drop(source, target) {
    const sel = applyDrop(store, source, target);
    drag = null;
    if (sel) ui.sel = sel;
    preview.setSelection(ui.sel);
    changed({ props: true });
  }

  // --- structure (arbre) + palette
  function drawTree() {
    const pgs = pages(store);
    const rows = [];
    pgs.forEach((pg, pi) => {
      rows.push(
        treeRow({
          level: 0,
          label: pg.name || '(sans nom)',
          badge: pg.password ? '🔒' : '',
          active: ui.sel?.kind === 'page' && ui.sel.page === pi,
          current: ui.page === pi,
          onClick: () => {
            ui.page = pi;
            preview.showPage(pi);
            choose({ kind: 'page', page: pi });
          },
          dragData: { type: 'page', page: pi },
          onDrop: (src, pos) => {
            if (src.page === pi) return;
            const all = editable(store);
            if (src.type === 'page') {
              const [m] = all.splice(src.page, 1);
              all.splice(pos === 'after' ? (src.page < pi ? pi : pi + 1) : src.page < pi ? pi - 1 : pi, 0, m);
              ui.page = all.indexOf(m);
              ui.sel = { kind: 'page', page: ui.page };
              changed({ props: true });
              preview.showPage(ui.page);
            } else if (src.type === 'section') {
              const [sec] = all[src.page].sections.splice(src.s, 1);
              all[pi].sections.push(sec);
              ui.sel = { kind: 'section', page: pi, s: all[pi].sections.length - 1 };
              ui.page = pi;
              changed({ props: true });
            }
          },
          accepts: (src) => src.type === 'page' || src.type === 'section',
        })
      );
      pg.sections.forEach((sec, si) => {
        rows.push(
          treeRow({
            level: 1,
            label: sec.name || '(sans nom)',
            badge: sec.password ? '🔒' : '',
            active: ui.sel?.kind === 'section' && ui.sel.page === pi && ui.sel.s === si,
            onClick: () => choose({ kind: 'section', page: pi, s: si }),
            dragData: { type: 'section', page: pi, s: si },
            onDrop: (src, pos) => {
              if (src.type === 'section' && src.page === pi && src.s === si) return;
              const all = editable(store);
              if (src.type === 'section') {
                const [m] = all[src.page].sections.splice(src.s, 1);
                let at = all[pi].sections.indexOf(sec);
                if (pos === 'after') at++;
                all[pi].sections.splice(at, 0, m);
                ui.sel = { kind: 'section', page: pi, s: all[pi].sections.indexOf(m) };
                changed({ props: true });
              } else if (src.type === 'item' || src.type === 'ref') {
                drop(src, { page: pi, s: si, i: rendered(store, sec).length });
              }
            },
            accepts: (src) => src.type === 'section' || src.type === 'item' || src.type === 'ref',
          })
        );
        rendered(store, sec).forEach((it, ii) => {
          rows.push(
            treeRow({
              level: 2,
              label: it.label || '(sans libellé)',
              hint: WIDGET_LABELS[it.widget] || it.widget,
              active: ui.sel?.kind === 'item' && ui.sel.page === pi && ui.sel.s === si && ui.sel.i === ii,
              onClick: () => choose({ kind: 'item', page: pi, s: si, i: ii }),
              dragData: { type: 'item', page: pi, s: si, i: ii },
              onDrop: (src, pos) => drop(src, { page: pi, s: si, i: pos === 'after' ? ii + 1 : ii }),
              accepts: (src) => src.type === 'item' || src.type === 'ref',
            })
          );
        });
      });
    });

    const avail = unplacedRefs(p, store.layout, pgs);
    treeBox.replaceChildren(
      h(
        'div',
        { class: 'hmi-tree-head' },
        h('strong', {}, 'Structure'),
        h('span', { class: 'spacer' }),
        button(
          '+ Page',
          () => {
            const all = editable(store);
            all.push({ id: newId('page'), name: `Page ${all.length + 1}`, sections: [{ id: newId('sec'), name: 'Section 1', orientation: 'horizontal', items: [] }] });
            ui.page = all.length - 1;
            ui.sel = { kind: 'page', page: ui.page };
            changed({ props: true });
            preview.showPage(ui.page);
          },
          'small'
        ),
        button(
          '+ Section',
          () => {
            const all = editable(store);
            const pg = all[ui.page];
            if (!pg) return;
            pg.sections.push({ id: newId('sec'), name: `Section ${pg.sections.length + 1}`, orientation: 'horizontal', items: [] });
            ui.sel = { kind: 'section', page: ui.page, s: pg.sections.length - 1 };
            changed({ props: true });
          },
          'small'
        )
      ),
      h('div', { class: 'hmi-tree-rows' }, rows.length ? rows : h('p', { class: 'muted' }, 'Aucune page.')),
      h('div', { class: 'hmi-tree-head', style: { marginTop: '14px' } }, h('strong', {}, 'Éléments disponibles'), h('span', { class: 'badge neutral' }, avail.length)),
      h('p', { class: 'hint', style: { margin: '0 0 6px' } }, 'Glissez-les sur l’aperçu ou dans la structure.'),
      h(
        'div',
        { class: 'hmi-palette' },
        avail.length
          ? avail.map((ref) => {
              const d = describeRef(p, store.layout, ref);
              if (!d) return null;
              const el = h('div', { class: 'hmi-palette-item', draggable: 'true', title: 'Glisser vers l’écran' }, h('span', {}, d.label), h('span', { class: 'muted' }, d.variable?.symbol || store.layout.nodes.get(ref)?.section || ''));
              el.addEventListener('dragstart', (e) => {
                drag = { type: 'ref', ref };
                e.dataTransfer.effectAllowed = 'copy';
                e.dataTransfer.setData('text/plain', ref);
              });
              el.addEventListener('dragend', () => setTimeout(() => (drag = null), 50));
              el.addEventListener('dblclick', () => {
                const pg = editable(store)[ui.page];
                const sec = pg?.sections[ui.sel?.kind !== 'page' && ui.sel?.page === ui.page ? ui.sel.s : 0] || pg?.sections[0];
                if (!sec) return toast('Créez d’abord une section.', 'error');
                drop({ type: 'ref', ref }, { page: ui.page, s: pg.sections.indexOf(sec), i: rendered(store, sec).length });
              });
              return el;
            })
          : h('p', { class: 'muted' }, 'Tout est placé.')
      )
    );
  }

  function treeRow({ level, label, hint, badge, active, current, onClick, dragData, onDrop, accepts }) {
    const row = h(
      'div',
      { class: `hmi-tree-row level-${level} ${active ? 'active' : ''} ${current ? 'current' : ''}`, draggable: 'true' },
      h('span', { class: 'grip' }, '⠿'),
      h('span', { class: 'lbl' }, label),
      badge ? h('span', {}, badge) : null,
      hint ? h('span', { class: 'hint-type' }, hint) : null
    );
    row.addEventListener('click', onClick);
    row.addEventListener('dragstart', (e) => {
      drag = dragData;
      e.dataTransfer.effectAllowed = 'move';
      e.dataTransfer.setData('text/plain', 'plc');
      e.stopPropagation();
    });
    row.addEventListener('dragend', () => setTimeout(() => (drag = null), 50));
    row.addEventListener('dragover', (e) => {
      if (!drag || !accepts(drag)) return;
      e.preventDefault();
      const r = row.getBoundingClientRect();
      row.classList.toggle('drop-before', e.clientY < r.top + r.height / 2);
      row.classList.toggle('drop-after', e.clientY >= r.top + r.height / 2);
    });
    row.addEventListener('dragleave', () => row.classList.remove('drop-before', 'drop-after'));
    row.addEventListener('drop', (e) => {
      if (!drag || !accepts(drag)) return;
      e.preventDefault();
      const r = row.getBoundingClientRect();
      const pos = e.clientY < r.top + r.height / 2 ? 'before' : 'after';
      row.classList.remove('drop-before', 'drop-after');
      const src = drag;
      drag = null;
      onDrop(src, pos);
    });
    return row;
  }

  // --- propriétés
  function drawProps() {
    const sel = ui.sel;
    const pgs = pages(store);
    propsBox.replaceChildren();
    if (!sel) {
      propsBox.append(
        h('div', { class: 'card' }, h('h2', {}, 'Propriétés'), h('p', { class: 'muted' }, 'Cliquez sur un widget ou une section dans l’aperçu, ou sur un élément de la structure.'), h('ul', { class: 'muted', style: { paddingLeft: '18px', margin: 0 } }, h('li', {}, 'Glissez les widgets pour les réorganiser.'), h('li', {}, 'Glissez les éléments disponibles sur une section pour les ajouter.'), h('li', {}, 'Mode « Essai » : les widgets réagissent comme sur l’automate.')))
      );
      return;
    }
    if (sel.kind === 'page') {
      const pg = pgs[sel.page];
      if (!pg) return;
      propsBox.append(
        h(
          'div',
          { class: 'card' },
          h('h2', {}, 'Page'),
          labeled('Nom', liveInput(() => pages(store)[sel.page], 'name')),
          labeled('Mot de passe', liveInput(() => pages(store)[sel.page], 'password'), 'Demandé à l’ouverture de la page (désactivé dans l’aperçu).'),
          h(
            'div',
            { class: 'btn-row', style: { marginTop: '12px' } },
            button(
              'Supprimer la page',
              async () => {
                if (!(await confirmDialog('Supprimer la page', `Supprimer « ${pg.name} » ? Ses éléments redeviennent disponibles.`, 'Supprimer'))) return;
                const all = editable(store);
                all.splice(sel.page, 1);
                ui.page = Math.max(0, sel.page - 1);
                ui.sel = null;
                changed({ props: true });
                preview.showPage(ui.page);
              },
              'small danger'
            )
          )
        )
      );
      return;
    }
    const pg = pgs[sel.page];
    const sec = pg?.sections[sel.s];
    if (!sec) return;
    if (sel.kind === 'section') {
      propsBox.append(
        h(
          'div',
          { class: 'card' },
          h('h2', {}, 'Section'),
          labeled('Titre', liveInput(() => pages(store)[sel.page]?.sections[sel.s], 'name')),
          labeled(
            'Disposition',
            select(
              sec,
              'orientation',
              [
                { value: 'horizontal', label: 'Horizontale (en ligne, retour automatique)' },
                { value: 'vertical', label: 'Verticale (en colonne)' },
              ],
              { onChange: () => (editable(store), changed()) }
            )
          ),
          labeled('Mot de passe', liveInput(() => pages(store)[sel.page]?.sections[sel.s], 'password'), 'Section verrouillée jusqu’à la saisie du mot de passe (désactivé dans l’aperçu).'),
          h(
            'div',
            { class: 'btn-row', style: { marginTop: '12px' } },
            button(
              '▲',
              () => {
                const all = editable(store);
                const list = all[sel.page].sections;
                if (sel.s === 0) return;
                list.splice(sel.s - 1, 0, list.splice(sel.s, 1)[0]);
                ui.sel = { ...sel, s: sel.s - 1 };
                changed({ props: true });
              },
              'small',
              { title: 'Monter', disabled: sel.s === 0 }
            ),
            button(
              '▼',
              () => {
                const all = editable(store);
                const list = all[sel.page].sections;
                if (sel.s >= list.length - 1) return;
                list.splice(sel.s + 1, 0, list.splice(sel.s, 1)[0]);
                ui.sel = { ...sel, s: sel.s + 1 };
                changed({ props: true });
              },
              'small',
              { title: 'Descendre', disabled: sel.s >= pg.sections.length - 1 }
            ),
            button(
              'Supprimer la section',
              () => {
                editable(store)[sel.page].sections.splice(sel.s, 1);
                ui.sel = null;
                changed({ props: true });
              },
              'small danger'
            )
          )
        )
      );
      return;
    }
    // Widget
    const found = itemAt(store, sel);
    if (!found) return;
    const it = found.item;
    const desc = describeRef(p, store.layout, it.ref);
    const variable = desc?.variable || null;
    const widgets = (desc?.widgets || [it.widget]).map((w) => ({ value: w, label: WIDGET_LABELS[w] || w }));
    const effective = widgetProps(it, variable);
    const def = manifest?.[it.widget];
    const fields = h('div', { class: 'hmi-prop-fields' });
    const drawFields = () => {
      fields.replaceChildren();
      const eff = widgetProps(it, variable);
      for (const [key, spec] of Object.entries(def?.props || {})) {
        if (HIDDEN_PROPS.has(key)) continue;
        fields.append(propField(key, spec, eff));
      }
    };
    const propField = (key, spec, eff) => {
      const overridden = it.props && Object.prototype.hasOwnProperty.call(it.props, key);
      const current = eff[key] !== undefined ? eff[key] : spec.default;
      const setProp = (val) => {
        editable(store);
        const target = itemAt(store, sel).item; // l'objet peut avoir été cloné par editable()
        target.props = { ...(target.props || {}) };
        if (val === undefined || val === '' || val === null) delete target.props[key];
        else target.props[key] = val;
        changed({ delay: 250 });
        resetBtn.disabled = !(target.props && key in target.props);
      };
      let control;
      if (spec.type === 'boolean') {
        const box = h('input', { type: 'checkbox', checked: !!current });
        box.addEventListener('change', () => setProp(box.checked));
        control = h('label', { class: 'check' }, box, PROP_LABELS[key] || key);
      } else if (spec.enum) {
        const sel2 = h('select', {}, spec.enum.map((v) => h('option', { value: v, selected: v === current }, ENUM_LABELS[v] || v)));
        sel2.addEventListener('change', () => setProp(sel2.value));
        control = sel2;
      } else if (key === 'color' && it.widget === 'rx-indicator') {
        const list = `colors_${key}`;
        const inp = h('input', { type: 'text', value: current ?? '', list });
        inp.addEventListener('input', () => setProp(inp.value || undefined));
        control = h('div', {}, inp, h('datalist', { id: list }, COLOR_CHOICES.map((c) => h('option', { value: c }))));
      } else if (spec.type === 'array') {
        const inp = h('input', { type: 'text', value: Array.isArray(current) ? current.join('; ') : '', placeholder: 'choix séparés par ;' });
        inp.addEventListener('input', () =>
          setProp(
            inp.value
              .split(/[;\n]/)
              .map((x) => x.trim())
              .filter(Boolean)
          )
        );
        control = inp;
      } else if (spec.type === 'number') {
        const inp = h('input', { type: 'number', value: current ?? '', step: 'any' });
        inp.addEventListener('input', () => setProp(inp.value === '' ? undefined : Number(inp.value)));
        control = inp;
      } else {
        const inp = h('input', { type: 'text', value: current ?? '' });
        inp.addEventListener('input', () => setProp(inp.value === '' ? undefined : inp.value));
        control = inp;
      }
      const resetBtn = button(
        '↺',
        () => {
          editable(store);
          const target = itemAt(store, sel).item;
          if (target.props) delete target.props[key];
          changed();
          drawFields();
        },
        'small ghost',
        { title: 'Revenir à la valeur par défaut', disabled: !overridden }
      );
      return h(
        'div',
        { class: 'hmi-prop' },
        spec.type === 'boolean' ? null : h('span', { class: 'prop-label' }, PROP_LABELS[key] || key),
        h('div', { class: 'row', style: { gap: '6px', flexWrap: 'nowrap' } }, h('div', { style: { flex: 1, minWidth: 0 } }, control), resetBtn),
        PROP_HELP[`${it.widget}.${key}`] || PROP_HELP[key] ? h('small', { class: 'muted' }, PROP_HELP[`${it.widget}.${key}`] || PROP_HELP[key]) : null
      );
    };
    drawFields();
    void effective;

    propsBox.append(
      h(
        'div',
        { class: 'card' },
        h('h2', {}, 'Widget'),
        h('p', { class: 'hint' }, variable ? `${variable.symbol} — ${variable.label}` : nodeTitle(it.ref)),
        it.ref.startsWith('chan:') ? h('p', { class: 'hint' }, 'Voie libre : nommez-la à l’étape « Câblage et variables » pour l’utiliser dans la logique.') : null,
        labeled('Libellé', liveInput(() => itemAt(store, sel)?.item, 'label')),
        labeled(
          'Type de widget',
          select(it, 'widget', widgets, {
            onChange: (v) => {
              editable(store);
              itemAt(store, sel).item.widget = v;
              changed({ props: true });
            },
          })
        ),
        WIDGET_INFO[it.widget] ? h('p', { class: 'hint', style: { marginTop: '6px' } }, WIDGET_INFO[it.widget]) : null,
        h(
          'div',
          { class: 'btn-row', style: { marginTop: '10px' } },
          button(
            'Retirer de l’écran',
            () => {
              editable(store);
              const f = itemAt(store, sel);
              f.section.items.splice(f.index, 1);
              ui.sel = { kind: 'section', page: sel.page, s: sel.s };
              changed({ props: true });
            },
            'small danger'
          )
        )
      ),
      h('div', { class: 'card' }, h('h2', {}, 'Apparence et comportement'), fields)
    );
  }

  // Champ lié à l'objet courant : editable() remplace les objets (copie), la cible est relue à chaque saisie.
  function liveInput(get, key, delay = 300) {
    const el = h('input', { type: 'text', value: get()?.[key] ?? '' });
    el.addEventListener('input', () => {
      editable(store);
      const target = get();
      if (!target) return;
      if (el.value === '' && key === 'password') delete target[key];
      else target[key] = el.value;
      changed({ delay });
    });
    return el;
  }

  function nodeTitle(ref) {
    const node = store.layout.nodes.get(ref);
    return node ? `${node.section} › ${node.name}` : ref;
  }

  function labeled(label, control, help) {
    return h('label', { class: 'field', style: { marginBottom: '10px' } }, h('span', {}, label), control, help ? h('small', {}, help) : null);
  }

  // --- barre d'outils de l'aperçu
  const scaleFrame = () => {
    const dev = DEVICES[ui.device];
    const avail = Math.max(320, frameBox.clientWidth - 2);
    const scale = Math.min(1, avail / dev.width);
    preview.iframe.style.width = `${dev.width}px`;
    preview.iframe.style.height = `${Math.round(760 / scale)}px`;
    frameScaler.style.transform = `scale(${scale})`;
    frameScaler.style.width = `${dev.width}px`;
    frameScaler.style.marginLeft = `${Math.max(0, Math.floor((avail - dev.width * scale) / 2))}px`;
    frameBox.style.height = `${760 + 2}px`;
  };
  const toolbar = h(
    'div',
    { class: 'btn-row hmi-toolbar' },
    segmented(
      [
        { value: 'edit', label: '✎ Édition' },
        { value: 'test', label: '▶ Essai' },
      ],
      ui.mode,
      (v) => {
        ui.mode = v;
        preview.setMode(v);
      }
    ),
    h('span', { class: 'spacer' }),
    segmented(
      Object.entries(DEVICES).map(([k, d]) => ({ value: k, label: d.label })),
      ui.device,
      (v) => {
        ui.device = v;
        scaleFrame();
      }
    ),
    button('↻', () => refreshPreview(), 'small ghost', { title: 'Recharger l’aperçu' })
  );

  function segmented(options, value, onChange) {
    const wrap = h('div', { class: 'segmented' });
    const draw = (val) =>
      wrap.replaceChildren(
        ...options.map((o) =>
          h(
            'button',
            {
              type: 'button',
              class: o.value === val ? 'on' : '',
              on: {
                click: () => {
                  draw(o.value);
                  onChange(o.value);
                },
              },
            },
            o.label
          )
        )
      );
    draw(value);
    return wrap;
  }

  const headBox = h('div');
  const drawHead = () =>
    headBox.replaceChildren(
      pageHead(
      'Écrans opérateur',
      'L’aperçu est l’interface web réelle de l’automate (data/index.js), alimentée par un automate simulé. Cliquez pour sélectionner, glissez pour réorganiser, réglez les propriétés à droite.',
      p.hmi.auto !== false
        ? h('span', { class: 'badge neutral', title: 'Les nouvelles variables sont placées automatiquement' }, 'Disposition automatique')
        : button('Revenir à la disposition automatique', async () => {
            if (!(await confirmDialog('Disposition automatique', 'Abandonner la disposition personnalisée et revenir à la disposition calculée automatiquement ?', 'Revenir'))) return;
            p.hmi.auto = true;
            p.hmi.pages = [];
            ui.sel = null;
            ui.page = 0;
            changed({ props: true });
          })
      )
    );
  root.append(headBox, issueBlock(store, 'hmi'), h('div', { class: 'hmi-editor' }, treeBox, h('section', { class: 'card hmi-center' }, toolbar, frameBox), propsBox));

  drawHead();
  drawTree();
  loadManifest().then(() => drawProps());
  preview.setData(previewData());
  preview.setSelection(ui.sel);
  // Rattacher l'iframe recrée son document : il sera rechargé (onLoad) avec les données à jour.
  preview.ready = false;
  if (!preview.iframe.getAttribute('src')) preview.iframe.src = '/hmi/preview.html';
  frameScaler.append(preview.iframe);
  requestAnimationFrame(scaleFrame);
  if (ui.onResize) window.removeEventListener('resize', ui.onResize);
  ui.onResize = scaleFrame;
  window.addEventListener('resize', scaleFrame);
}

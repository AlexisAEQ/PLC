// Étape 5 : écrans opérateur (data/interface.json).

import { WIDGET_LABELS } from '/shared/catalog.js';
import { describeRef, unplacedRefs } from '/shared/hmi.js';
import { h, input, select, card, pageHead, button, confirmDialog } from '../ui.js';
import { issueBlock } from './project.js';

let currentPage = 0;
const clone = (x) => JSON.parse(JSON.stringify(x));
const newId = (p) => `${p}_${Math.random().toString(36).slice(2, 9)}`;

export function renderHmi(root, store) {
  const p = store.project;
  const auto = p.hmi.auto !== false || !p.hmi.pages.length;

  root.append(
    pageHead(
      'Écrans opérateur',
      'Pages, sections et widgets de l’interface web de l’automate. La disposition automatique se met à jour quand vous ajoutez des variables ; personnalisez-la pour la modifier à la main.',
      auto
        ? button(
            'Personnaliser la disposition',
            () => {
              p.hmi.pages = clone(store.pages);
              p.hmi.auto = false;
              currentPage = 0;
              store.changed({ render: true });
            },
            'primary'
          )
        : button('Revenir à l’automatique', async () => {
            if (await confirmDialog('Disposition automatique', 'Abandonner la disposition personnalisée ?', 'Revenir')) {
              p.hmi.auto = true;
              p.hmi.pages = [];
              currentPage = 0;
              store.changed({ render: true });
            }
          })
    ),
    issueBlock(store, 'hmi')
  );

  if (auto) {
    root.append(card('Aperçu de la disposition automatique', preview(store, store.pages)));
    return;
  }
  root.append(editor(store));
}

// ---------------------------------------------------------------------------
function preview(store, pages) {
  if (!pages.length || pages.every((pg) => !pg.sections.length)) {
    return h('div', { class: 'empty-state' }, 'Rien à afficher : créez des variables visibles à l’écran.');
  }
  const tabs = h('div', { class: 'page-tabs' });
  const body = h('div', {});
  const draw = (i) => {
    tabs.replaceChildren(...pages.map((pg, k) => h('button', { class: `page-tab ${k === i ? 'active' : ''}`, type: 'button', on: { click: () => draw(k) } }, pg.name, pg.password ? ' 🔒' : '')));
    const pg = pages[i];
    body.replaceChildren(
      h(
        'div',
        { class: 'preview-page' },
        pg.sections.map((s) =>
          h(
            'div',
            { class: 'preview-section' },
            h('h4', {}, s.name),
            h(
              'div',
              { class: 'preview-widgets' },
              s.items.map((it) => h('div', { class: 'pw' }, h('div', {}, it.label), h('div', { class: 'pw-kind' }, WIDGET_LABELS[it.widget] || it.widget)))
            )
          )
        )
      )
    );
  };
  draw(Math.min(currentPage, pages.length - 1));
  return h('div', {}, tabs, body);
}

// ---------------------------------------------------------------------------
function editor(store) {
  const p = store.project;
  const pages = p.hmi.pages;
  if (currentPage >= pages.length) currentPage = Math.max(0, pages.length - 1);
  const page = pages[currentPage];

  const tabs = h(
    'div',
    { class: 'page-tabs' },
    pages.map((pg, i) =>
      h(
        'button',
        {
          class: `page-tab ${i === currentPage ? 'active' : ''}`,
          type: 'button',
          on: {
            click: () => {
              currentPage = i;
              store.rerender();
            },
          },
        },
        pg.name || '(sans nom)',
        pg.password ? ' 🔒' : ''
      )
    ),
    button(
      '+ Page',
      () => {
        pages.push({ id: newId('page'), name: `Page ${pages.length + 1}`, sections: [] });
        currentPage = pages.length - 1;
        store.changed({ render: true });
      },
      'small ghost'
    )
  );

  const pageEditor = page
    ? h(
        'div',
        {},
        h(
          'div',
          { class: 'row', style: { marginBottom: '12px' } },
          h('label', { class: 'field', style: { flex: '1 1 200px' } }, h('span', {}, 'Nom de la page'), input(page, 'name', { onInput: () => store.changed() })),
          h('label', { class: 'field', style: { flex: '1 1 160px' } }, h('span', {}, 'Mot de passe (optionnel)'), input(page, 'password', { onInput: () => store.changed() })),
          h(
            'div',
            { class: 'btn-row', style: { alignSelf: 'flex-end' } },
            button('◀', () => movePage(-1), 'small ghost', { title: 'Déplacer à gauche', disabled: currentPage === 0 }),
            button('▶', () => movePage(1), 'small ghost', { title: 'Déplacer à droite', disabled: currentPage === pages.length - 1 }),
            button(
              'Supprimer la page',
              async () => {
                if (await confirmDialog('Supprimer la page', `Supprimer la page « ${page.name} » ?`, 'Supprimer')) {
                  pages.splice(currentPage, 1);
                  currentPage = Math.max(0, currentPage - 1);
                  store.changed({ render: true });
                }
              },
              'small danger'
            )
          )
        ),
        page.sections.map((s, si) => sectionEditor(store, page, s, si)),
        button(
          '+ Section',
          () => {
            page.sections.push({ id: newId('sec'), name: `Section ${page.sections.length + 1}`, orientation: 'horizontal', items: [] });
            store.changed({ render: true });
          },
          'small'
        )
      )
    : h('div', { class: 'empty-state' }, 'Aucune page.');

  function movePage(d) {
    const [pg] = pages.splice(currentPage, 1);
    currentPage += d;
    pages.splice(currentPage, 0, pg);
    store.changed({ render: true });
  }

  return h('div', { class: 'hmi-layout' }, h('section', { class: 'card' }, tabs, pageEditor), palette(store, page));
}

function sectionEditor(store, page, s, si) {
  const allSections = store.project.hmi.pages.flatMap((pg) => pg.sections.map((sec) => ({ pg, sec })));
  const moveItem = (from, to) => {
    const [it] = s.items.splice(from, 1);
    s.items.splice(to, 0, it);
    store.changed({ render: true });
  };
  return h(
    'div',
    { class: 'hmi-section' },
    h(
      'div',
      { class: 'hmi-section-head' },
      input(s, 'name', { onInput: () => store.changed() }),
      select(
        s,
        'orientation',
        [
          { value: 'horizontal', label: 'Horizontale' },
          { value: 'vertical', label: 'Verticale' },
        ],
        { onChange: () => store.changed() }
      ),
      button(
        '▲',
        () => {
          const [x] = page.sections.splice(si, 1);
          page.sections.splice(si - 1, 0, x);
          store.changed({ render: true });
        },
        'small ghost',
        { disabled: si === 0, title: 'Monter la section' }
      ),
      button(
        '▼',
        () => {
          const [x] = page.sections.splice(si, 1);
          page.sections.splice(si + 1, 0, x);
          store.changed({ render: true });
        },
        'small ghost',
        { disabled: si === page.sections.length - 1, title: 'Descendre la section' }
      ),
      button(
        '✕',
        () => {
          page.sections.splice(si, 1);
          store.changed({ render: true });
        },
        'small ghost',
        { title: 'Supprimer la section (ses éléments redeviennent disponibles)' }
      )
    ),
    h(
      'div',
      { class: 'hmi-items' },
      s.items.length ? null : h('span', { class: 'muted' }, 'Section vide : ajoutez des éléments depuis la colonne de droite.'),
      s.items.map((it, ii) => {
        const desc = describeRef(store.project, store.layout, it.ref);
        const widgets = (desc?.widgets || [it.widget]).map((w) => ({ value: w, label: WIDGET_LABELS[w] || w }));
        const moveTo = h(
          'select',
          {
            title: 'Déplacer vers une autre section',
            on: {
              change: (e) => {
                const target = allSections[Number(e.target.value)];
                if (!target) return;
                s.items.splice(ii, 1);
                target.sec.items.push(it);
                store.changed({ render: true });
              },
            },
          },
          h('option', { value: '' }, 'Déplacer…'),
          allSections.map((x, k) => (x.sec === s ? null : h('option', { value: k }, `${x.pg.name} › ${x.sec.name}`)))
        );
        return h(
          'div',
          { class: 'hmi-item' },
          h('div', {}, input(it, 'label', { onInput: () => store.changed() }), h('div', { class: 'ref' }, desc ? desc.variable?.symbol || it.ref : `introuvable : ${it.ref}`)),
          select(it, 'widget', widgets, { onChange: () => store.changed() }),
          h(
            'div',
            { class: 'btn-row', style: { flexWrap: 'nowrap' } },
            button('▲', () => moveItem(ii, ii - 1), 'small ghost', { disabled: ii === 0 }),
            button('▼', () => moveItem(ii, ii + 1), 'small ghost', { disabled: ii === s.items.length - 1 }),
            moveTo,
            button(
              '✕',
              () => {
                s.items.splice(ii, 1);
                store.changed({ render: true });
              },
              'small ghost',
              { title: 'Retirer de l’écran' }
            )
          )
        );
      })
    )
  );
}

function palette(store, page) {
  const refs = unplacedRefs(store.project, store.layout);
  const sections = page?.sections || [];
  return h(
    'section',
    { class: 'card palette' },
    h('h2', {}, 'Éléments non placés'),
    !refs.length
      ? h('p', { class: 'muted' }, 'Tout est placé.')
      : !sections.length
        ? h('p', { class: 'muted' }, 'Créez une section sur cette page pour y ajouter des éléments.')
        : refs.map((ref) => {
            const desc = describeRef(store.project, store.layout, ref);
            if (!desc) return null;
            return h(
              'div',
              { class: 'palette-item' },
              h('span', {}, desc.label),
              h(
                'select',
                {
                  on: {
                    change: (e) => {
                      const sec = sections[Number(e.target.value)];
                      if (!sec) return;
                      sec.items.push({ ref, label: desc.label, widget: desc.variable?.hmi?.widget && desc.widgets.includes(desc.variable.hmi.widget) ? desc.variable.hmi.widget : desc.widgets[0] });
                      store.changed({ render: true });
                    },
                  },
                },
                h('option', { value: '' }, 'Ajouter à…'),
                sections.map((s, k) => h('option', { value: k }, s.name))
              )
            );
          })
  );
}

// Étape 3 : câblage des voies et variables internes.

import { CATALOG, VARIABLE_KINDS, DATA_TYPE_LABELS, WIDGET_LABELS, widgetsFor } from '/shared/catalog.js';
import { normalizeVariable, toSymbol, listChannels } from '/shared/model.js';
import { h, input, checkbox, select, card, pageHead, button, confirmDialog } from '../ui.js';
import { issueBlock } from './project.js';

const FALLBACKS = [
  { value: 'off', label: 'Forcée à 0' },
  { value: 'on', label: 'Forcée à 1' },
  { value: 'hold', label: 'Maintenue' },
];

export function uniqueSymbol(project, base, except) {
  const taken = new Set(project.variables.filter((v) => v !== except).map((v) => v.symbol.toLowerCase()));
  let s = toSymbol(base);
  if (!taken.has(s.toLowerCase())) return s;
  for (let i = 2; ; i++) {
    const c = `${s.slice(0, 36)}_${i}`;
    if (!taken.has(c.toLowerCase())) return c;
  }
}

export function renderVariables(root, store) {
  const p = store.project;
  root.append(
    pageHead('Câblage et variables', 'Indiquez ce qui est branché sur chaque voie, puis créez les variables internes dont la logique a besoin. Vous pouvez revenir ici à tout moment, y compris pendant l’écriture de la logique.'),
    issueBlock(store, 'variables')
  );

  if (!p.equipment.length) {
    root.append(card('Câblage', h('div', { class: 'empty-state' }, 'Aucun équipement : ajoutez-en à l’étape « Équipements ».', h('br'), button('Aller aux équipements', () => store.go('equipment'), 'small'))));
  }

  const signals = p.variables.filter((v) => v.system);
  if (signals.length) root.append(signalsCard(store, signals));

  for (const eq of p.equipment) {
    const entry = CATALOG[eq.type];
    if (!entry?.groups.length) continue;
    root.append(wiringCard(store, eq));
  }

  const orphans = p.variables.filter((v) => (v.kind === 'input' || v.kind === 'output') && !v.system && !v.binding);
  if (orphans.length) root.append(orphansCard(store, orphans));

  root.append(internalCard(store));
}

// ---------------------------------------------------------------------------
// Câblage d'un équipement
function wiringCard(store, eq) {
  const p = store.project;
  const entry = CATALOG[eq.type];
  const channels = listChannels(p).filter((c) => c.eq.uid === eq.uid);
  const body = h('div', {});

  for (const g of entry.groups) {
    const rows = channels.filter((c) => c.group.key === g.key);
    const isOut = g.dir === 'out';
    body.append(
      h('h3', { style: { fontSize: '13px', margin: '14px 0 4px' } }, `${g.label}`, h('span', { class: 'muted', style: { fontWeight: 400 } }, ` — ${g.dir === 'in' ? 'entrées' : 'sorties'} ${DATA_TYPE_LABELS[g.dataType].toLowerCase()}s`)),
      h(
        'table',
        { class: 'tbl' },
        h(
          'thead',
          {},
          h(
            'tr',
            {},
            h('th', {}, 'Voie'),
            h('th', {}, 'Ce qui est branché'),
            h('th', { class: 'w-m' }, 'Symbole'),
            h('th', { class: 'w-s' }, isOut ? 'En urgence' : g.supportsInverse ? 'Inversée' : ''),
            h('th', { class: 'w-xs' }, 'Écran'),
            h('th', { class: 'w-xs' }, '')
          )
        ),
        h(
          'tbody',
          {},
          rows.map((c) => channelRow(store, eq, g, c))
        )
      )
    );
  }
  return h('section', { class: 'card' }, h('h2', {}, eq.label, h('span', { class: 'muted', style: { fontWeight: 400, fontSize: '13px' } }, entry.label)), body);
}

function channelRow(store, eq, g, c) {
  const p = store.project;
  const v = c.variable;
  const isOut = g.dir === 'out';

  if (v?.system) {
    return h(
      'tr',
      {},
      h('td', { class: 'chan' }, c.name),
      h('td', {}, h('strong', {}, v.label), h('span', { class: 'muted' }, ' (signal)')),
      h('td', { class: 'mono' }, v.symbol),
      h('td', {}, FALLBACKS.find((f) => f.value === v.fallback)?.label || ''),
      h('td', {}),
      h('td', {}, button('Libérer', () => (delete v.binding, store.changed({ render: true })), 'small ghost'))
    );
  }

  const model = { label: v?.label || '' };
  const symbolInput = v ? input(v, 'symbol', { cls: 'mono', onInput: () => ((v.symbolTouched = true), store.changed()) }) : h('span', { class: 'muted' }, '');
  const labelInput = input(model, 'label', {
    placeholder: '— libre —',
    onInput: (val) => {
      if (v) {
        v.label = val;
        if (!v.symbolTouched) {
          v.symbol = uniqueSymbol(p, val, v);
          symbolInput.value = v.symbol;
        }
        store.changed();
      }
    },
    onChange: (val) => {
      if (!v && val.trim()) {
        p.variables.push(
          normalizeVariable({
            label: val.trim(),
            symbol: uniqueSymbol(p, val),
            kind: isOut ? 'output' : 'input',
            dataType: g.dataType,
            binding: { eq: eq.uid, group: g.key, channel: c.channel },
            hmi: { show: true },
          })
        );
        store.changed({ render: true });
      }
    },
  });

  let option = h('span');
  if (v && isOut) option = select(v, 'fallback', FALLBACKS, { onChange: () => store.changed() });
  else if (v && g.supportsInverse) option = checkbox(v, 'inverse', undefined, { onChange: () => store.changed() });

  return h(
    'tr',
    { class: v ? '' : 'empty' },
    h('td', { class: 'chan' }, c.name),
    h('td', {}, labelInput),
    h('td', {}, symbolInput),
    h('td', {}, option),
    h('td', {}, v ? checkbox(v.hmi, 'show', undefined, { onChange: () => store.changed() }) : ''),
    h(
      'td',
      {},
      v
        ? button(
            '✕',
            async () => {
              if (await confirmDialog('Supprimer la variable', `Supprimer « ${v.label} » (${v.symbol}) ?`, 'Supprimer')) {
                p.variables = p.variables.filter((x) => x !== v);
                store.changed({ render: true });
              }
            },
            'small ghost',
            { title: 'Supprimer la variable' }
          )
        : ''
    )
  );
}

// ---------------------------------------------------------------------------
// Signaux d'équipements (ex. servo) à câbler sur une sortie libre
function signalsCard(store, signals) {
  const p = store.project;
  const channels = listChannels(p).filter((c) => c.group.dir === 'out' && c.group.dataType === 'bool');
  return card(
    'Signaux à câbler',
    h('p', { class: 'hint' }, 'Ces signaux sont pilotés automatiquement (bloc servo). Choisissez la sortie sur laquelle chacun est branché.'),
    h(
      'table',
      { class: 'tbl' },
      h('thead', {}, h('tr', {}, h('th', {}, 'Signal'), h('th', {}, 'Sortie'), h('th', { class: 'w-m' }, 'Symbole'))),
      h(
        'tbody',
        {},
        signals.map((v) => {
          const current = v.binding ? `${v.binding.eq}/${v.binding.group}/${v.binding.channel}` : '';
          const options = [{ value: '', label: '— non câblé —' }].concat(
            channels
              .filter((c) => c.eq.uid !== v.system.eq && (!c.variable || c.variable === v))
              .map((c) => ({ value: c.key, label: `${c.eq.label} · ${c.group.label} · ${c.name}` }))
          );
          const sel = h(
            'select',
            {
              on: {
                change: (e) => {
                  const val = e.target.value;
                  if (!val) delete v.binding;
                  else {
                    const [eqUid, group, channel] = val.split('/');
                    v.binding = { eq: eqUid, group, channel: Number(channel) };
                  }
                  store.changed({ render: true });
                },
              },
            },
            options.map((o) => h('option', { value: o.value, selected: o.value === current }, o.label))
          );
          return h('tr', { class: v.binding ? '' : 'has-error' }, h('td', {}, v.label), h('td', {}, sel), h('td', { class: 'mono' }, v.symbol));
        })
      )
    )
  );
}

function orphansCard(store, orphans) {
  const p = store.project;
  return card(
    'Variables non câblées',
    h('p', { class: 'hint' }, 'Ces entrées/sorties ne sont plus reliées à une voie (équipement retiré ?). Câblez-les en tapant leur nom sur une voie libre, ou supprimez-les.'),
    h(
      'table',
      { class: 'tbl' },
      h(
        'tbody',
        {},
        orphans.map((v) =>
          h(
            'tr',
            {},
            h('td', {}, v.label),
            h('td', { class: 'mono' }, v.symbol),
            h('td', {}, VARIABLE_KINDS[v.kind].label),
            h('td', {}, rebindSelect(store, v)),
            h(
              'td',
              {},
              button(
                'Supprimer',
                () => {
                  p.variables = p.variables.filter((x) => x !== v);
                  store.changed({ render: true });
                },
                'small danger'
              )
            )
          )
        )
      )
    )
  );
}

function rebindSelect(store, v) {
  const dir = v.kind === 'input' ? 'in' : 'out';
  const free = listChannels(store.project).filter((c) => !c.variable && c.group.dir === dir && c.group.dataType === v.dataType);
  const sel = h(
    'select',
    {
      on: {
        change: (e) => {
          if (!e.target.value) return;
          const [eq, group, channel] = e.target.value.split('/');
          v.binding = { eq, group, channel: Number(channel) };
          store.changed({ render: true });
        },
      },
    },
    h('option', { value: '' }, 'Câbler sur…'),
    free.map((c) => h('option', { value: c.key }, `${c.eq.label} · ${c.group.label} · ${c.name}`))
  );
  return sel;
}

// ---------------------------------------------------------------------------
// Variables internes
const INTERNAL_KINDS = ['command', 'parameter', 'indicator', 'memory'];

function internalCard(store) {
  const p = store.project;
  const vars = p.variables.filter((v) => INTERNAL_KINDS.includes(v.kind));
  const add = (kind) => {
    const dataType = kind === 'parameter' ? 'int' : 'bool';
    const base = { command: 'Commande', parameter: 'Parametre', indicator: 'Indicateur', memory: 'Memoire' }[kind];
    const v = normalizeVariable({ kind, dataType, label: `${VARIABLE_KINDS[kind].label} ${vars.filter((x) => x.kind === kind).length + 1}`, symbol: uniqueSymbol(p, base), hmi: { show: kind !== 'memory' } });
    p.variables.push(v);
    store.changed({ render: true });
    setTimeout(() => document.querySelector(`[data-var="${v.uid}"] input`)?.focus(), 0);
  };

  const table = h(
    'table',
    { class: 'tbl' },
    h(
      'thead',
      {},
      h(
        'tr',
        {},
        h('th', { style: { minWidth: '200px' } }, 'Libellé'),
        h('th', { class: 'w-m' }, 'Symbole'),
        h('th', { class: 'w-s' }, 'Nature'),
        h('th', { class: 'w-s' }, 'Type'),
        h('th', {}, 'Valeurs'),
        h('th', { class: 'w-m' }, 'Écran'),
        h('th', { class: 'w-xs' }, '')
      )
    ),
    h('tbody', {}, vars.length ? vars.map((v) => internalRow(store, v)) : h('tr', { class: 'empty' }, h('td', { colspan: 7 }, 'Aucune variable interne.')))
  );

  return h(
    'section',
    { class: 'card' },
    h(
      'h2',
      {},
      'Variables internes',
      h('span', { class: 'spacer' }),
      ...INTERNAL_KINDS.map((k) => button(`+ ${VARIABLE_KINDS[k].label}`, () => add(k), 'small'))
    ),
    h(
      'p',
      { class: 'hint' },
      'Commande : bouton ou interrupteur à l’écran. Paramètre : consigne réglable, sauvegardée dans data/<Nom>.json. Indicateur : valeur affichée, écrite par la logique. Mémoire : variable de travail sans affichage (compteur, mémoire…).'
    ),
    table
  );
}

function internalRow(store, v) {
  const p = store.project;
  const kinds = INTERNAL_KINDS.map((k) => ({ value: k, label: VARIABLE_KINDS[k].label }));
  const types = VARIABLE_KINDS[v.kind].dataTypes.map((t) => ({ value: t, label: DATA_TYPE_LABELS[t] }));
  const symbol = input(v, 'symbol', { cls: 'mono', onInput: () => ((v.symbolTouched = true), store.changed()) });
  const label = input(v, 'label', {
    onInput: (val) => {
      if (!v.symbolTouched) {
        v.symbol = uniqueSymbol(p, val, v);
        symbol.value = v.symbol;
      }
      store.changed();
    },
  });

  const values = h('div', { class: 'row', style: { gap: '6px', flexWrap: 'nowrap' } });
  if (v.kind === 'parameter') {
    if (v.dataType === 'bool') values.append(checkbox(v, 'initial', 'Valeur initiale', { onChange: () => store.changed() }));
    else values.append(input(v, 'initial', { type: v.dataType === 'text' ? 'text' : 'number', placeholder: 'initiale', title: 'Valeur initiale', onInput: () => store.changed() }));
  }
  if (v.dataType === 'int' || v.dataType === 'float') {
    values.append(
      input(v, 'min', { type: 'number', placeholder: 'min', title: 'Minimum', onInput: () => store.changed() }),
      input(v, 'max', { type: 'number', placeholder: 'max', title: 'Maximum', onInput: () => store.changed() }),
      input(v, 'unit', { placeholder: 'unité', title: 'Unité', onInput: () => store.changed() })
    );
  }
  if (v.kind === 'parameter' && v.dataType === 'text') {
    values.append(input(v, 'choices', { placeholder: 'choix ; séparés', title: 'Choix proposés (liste déroulante)', onInput: () => store.changed() }));
  }

  const widgets = widgetsFor(v).map((w) => ({ value: w, label: WIDGET_LABELS[w] || w }));
  if (!v.hmi) v.hmi = { show: true };
  if (!widgets.some((w) => w.value === v.hmi.widget)) v.hmi.widget = widgets[0].value;
  const screen = h(
    'div',
    { class: 'row', style: { gap: '6px', flexWrap: 'nowrap' } },
    checkbox(v.hmi, 'show', undefined, { onChange: () => store.changed({ render: true }) }),
    v.hmi.show ? select(v.hmi, 'widget', widgets, { onChange: () => store.changed() }) : h('span', { class: 'muted' }, 'masquée')
  );

  return h(
    'tr',
    { dataset: { var: v.uid } },
    h('td', {}, label),
    h('td', {}, symbol),
    h(
      'td',
      {},
      select(v, 'kind', kinds, {
        onChange: () => {
          if (!VARIABLE_KINDS[v.kind].dataTypes.includes(v.dataType)) v.dataType = VARIABLE_KINDS[v.kind].dataTypes[0];
          if (v.kind === 'memory') v.hmi.show = false;
          store.changed({ render: true });
        },
      })
    ),
    h('td', {}, select(v, 'dataType', types, { onChange: () => store.changed({ render: true }) })),
    h('td', {}, values),
    h('td', {}, screen),
    h(
      'td',
      {},
      button(
        '✕',
        async () => {
          if (await confirmDialog('Supprimer la variable', `Supprimer « ${v.label} » (${v.symbol}) ?`, 'Supprimer')) {
            p.variables = p.variables.filter((x) => x !== v);
            store.changed({ render: true });
          }
        },
        'small ghost',
        { title: 'Supprimer' }
      )
    )
  );
}

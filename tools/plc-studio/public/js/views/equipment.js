// Étape 2 : choix des équipements.

import { CATALOG } from '/shared/catalog.js';
import { addEquipment, removeEquipment } from '/shared/model.js';
import { h, input, checkbox, select, field, card, pageHead, button, confirmDialog } from '../ui.js';
import { issueBlock } from './project.js';

const ROLE_LABEL = { controller: 'Automate', 'modbus-io': 'E/S Modbus', servo: 'Servo Modbus', axis: 'Axe' };

export function renderEquipment(root, store) {
  const p = store.project;

  const catalog = h(
    'div',
    { class: 'catalog' },
    Object.entries(CATALOG).map(([type, entry]) => {
      const count = p.equipment.filter((e) => e.type === type).length;
      const otherController = entry.role === 'controller' && !count && p.equipment.some((e) => CATALOG[e.type]?.role === 'controller');
      const full = (entry.max && count >= entry.max) || otherController;
      const channels = entry.groups.map((g) => `${g.bank ? g.bank.bits : g.ids.length} ${g.label.toLowerCase()}`);
      return h(
        'div',
        { class: 'cat-item' },
        h('h3', {}, entry.label),
        h('div', { class: 'tags' }, h('span', { class: 'tag' }, ROLE_LABEL[entry.role] || entry.role), entry.modbus ? h('span', { class: 'tag' }, 'RS485') : null),
        h('p', {}, entry.description),
        channels.length ? h('small', { class: 'muted' }, channels.join(' · ')) : null,
        h(
          'div',
          { class: 'btn-row' },
          button(
            otherController ? 'Un automate est déjà présent' : full ? 'Déjà ajouté' : '+ Ajouter',
            () => {
              addEquipment(p, type);
              store.changed({ render: true });
            },
            'small primary',
            { disabled: full }
          ),
          count ? h('span', { class: 'muted' }, `${count} dans le projet`) : null
        )
      );
    })
  );

  const list = h('div', {});
  if (!p.equipment.length) {
    list.append(h('div', { class: 'empty-state' }, 'Aucun équipement. Commencez par ajouter l’automate (KinCony, Waveshare ESP32-S3, M5Stack StamPLC, Homemaster MiniPLC…).'));
  }
  const ordered = [...p.equipment].sort((a, b) => (CATALOG[a.type]?.rs485 ? 0 : 1) - (CATALOG[b.type]?.rs485 ? 0 : 1));
  for (const eq of ordered) list.append(equipmentCard(store, eq));

  root.append(
    pageHead('Équipements', 'Sélectionnez les équipements avec lesquels le projet interagit. Vous câblerez leurs voies à l’étape suivante.'),
    issueBlock(store, 'equipment'),
    card('Équipements du projet', h('p', { class: 'hint' }, 'L’automate est toujours déclaré en premier dans config.json (il porte le bus RS485).'), list),
    card('Catalogue', catalog)
  );
}

function equipmentCard(store, eq) {
  const p = store.project;
  const entry = CATALOG[eq.type];
  const used = p.variables.filter((v) => v.binding?.eq === eq.uid).length;
  const fields = h(
    'div',
    { class: 'grid' },
    field('Nom', input(eq, 'label', { onInput: () => store.changed(), onChange: () => store.changed({ render: true }) }), 'Sert à nommer les sections de config.json.')
  );
  if (entry?.modbus) {
    fields.append(field('Adresse Modbus', input(eq, 'address', { type: 'number', min: 1, max: 247, onInput: () => store.changed() }), 'Entre 1 et 247, unique sur le bus.'));
  }
  for (const opt of entry?.options || []) {
    let control;
    if (opt.type === 'bool') control = checkbox(eq.options, opt.key, opt.label, { onChange: () => store.changed() });
    else if (opt.type === 'select') control = field(opt.label, select(eq.options, opt.key, opt.choices, { onChange: () => store.changed() }), opt.help);
    else control = field(opt.label, input(eq.options, opt.key, { type: opt.type === 'number' ? 'number' : 'text', onInput: () => store.changed() }), opt.help);
    fields.append(opt.type === 'bool' ? h('div', { class: 'field' }, h('span', {}, ' '), control, opt.help ? h('small', {}, opt.help) : null) : control);
  }

  const summary = [];
  for (const g of entry?.groups || []) summary.push(`${g.label} : ${g.bank ? g.bank.bits : g.ids.length}`);
  if (entry?.signals) summary.push(`Signaux à câbler : ${entry.signals.map((s) => s.label).join(', ')}`);
  if (entry?.sections) summary.push(`${entry.sections.reduce((a, s) => a + s.nodes.length, 0)} nœuds créés automatiquement`);
  if (entry?.pioEnv) summary.push(`Compilation : environnement PlatformIO « ${entry.pioEnv} »`);

  return h(
    'div',
    { class: 'card', style: { background: 'var(--panel-2)', boxShadow: 'none' } },
    h(
      'div',
      { class: 'eq-head', style: { marginBottom: '10px' } },
      h('strong', {}, eq.label),
      h('span', { class: 'eq-type' }, entry?.label || eq.type),
      h('span', { class: 'tag' }, ROLE_LABEL[entry?.role] || '?'),
      h('span', { class: 'spacer' }),
      used ? h('span', { class: 'muted' }, `${used} voie(s) câblée(s)`) : null,
      button(
        'Câbler…',
        () => {
          store.go('variables');
        },
        'small'
      ),
      button(
        'Retirer',
        async () => {
          const ok = await confirmDialog('Retirer l’équipement', `Retirer « ${eq.label} » ? Les variables câblées dessus seront conservées mais décâblées.`, 'Retirer');
          if (!ok) return;
          removeEquipment(p, eq.uid);
          store.changed({ render: true });
        },
        'small danger'
      )
    ),
    fields,
    h('p', { class: 'muted', style: { margin: '10px 0 0', fontSize: '12px' } }, summary.join(' · '))
  );
}

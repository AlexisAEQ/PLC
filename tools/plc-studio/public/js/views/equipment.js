// Étape 2 : choix des équipements.

import { CATALOG, marquesAutomates } from '/shared/catalog.js';
import { addEquipment, removeEquipment } from '/shared/model.js';
import { siteDuProjet, champsImposes, appliquerSite } from '/shared/site.js';
import { genericHardwareName } from '/shared/layout.js';
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
        h('div', { class: 'tags' }, h('span', { class: 'tag' }, ROLE_LABEL[entry.role] || entry.role), entry.modbus ? h('span', { class: 'tag' }, entry.defaultTransport === 'tcp' ? 'Modbus TCP / RTU' : 'Modbus RTU / TCP') : null),
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
    list.append(h('div', { class: 'empty-state' }, `Aucun équipement. Commencez par ajouter l’automate (${marquesAutomates().join(', ')}…).`));
  }
  const ordered = [...p.equipment].sort((a, b) => (CATALOG[a.type]?.rs485 ? 0 : 1) - (CATALOG[b.type]?.rs485 ? 0 : 1));
  for (const eq of ordered) list.append(equipmentCard(store, eq));

  root.append(
    pageHead('Équipements', 'Sélectionnez les équipements avec lesquels le projet interagit. Vous câblerez leurs voies à l’étape suivante.'),
    issueBlock(store, 'equipment'),
    card('Équipements du projet', h('p', { class: 'hint' }, 'L’automate est toujours déclaré en premier dans config.json (il porte le bus RS485). Chaque équipement Modbus passe par le RS485 (RTU) ou par le réseau WiFi / Ethernet (TCP).'), list),
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
  // Appareil du site (référentiel d'infrastructure) : ses adresses remplacent celles du projet.
  // Affiché aussi pour un équipement encore lié sans site (site retiré du référentiel) : on peut le délier.
  const site = siteDuProjet(p);
  if (site || eq.appareil) {
    // Automate : appareils de son type, ou appareils sans type qui ne décrivent pas un équipement Modbus.
    const convient = (a) =>
      entry?.role === 'controller'
        ? a.type === eq.type || (!a.type && !a.ip && !a.modbusTcp && !a.rs485 && !a.logiciel)
        : !a.type || a.type === eq.type;
    const appareils = Object.entries(site?.appareils || {}).filter(([, a]) => convient(a));
    const choix = [{ value: '', label: '— non lié : adresses saisies ici —' }, ...appareils.map(([nom, a]) => ({ value: nom, label: `${nom}${a.ip ? ` (${a.ip})` : a.ethernet ? ` (${a.ethernet.ip})` : a.rs485 ? ` (RS485 n° ${a.rs485.adresse})` : ''}` }))];
    if (eq.appareil && !appareils.some(([nom]) => nom === eq.appareil)) choix.push({ value: eq.appareil, label: `${eq.appareil} (absent du site ou d’un autre type)` });
    const etat = { appareil: eq.appareil || '' };
    fields.append(
      field(
        site ? `Appareil du site ${p.site}` : 'Appareil (aucun site choisi)',
        select(etat, 'appareil', choix, {
          onChange: (v) => {
            if (v) eq.appareil = v;
            else delete eq.appareil;
            appliquerSite(p); // recopie les adresses de l'appareil
            store.changed({ render: true });
          },
        }),
        'Lié : IP, port, n° d’unité ou adresse RS485 viennent de infrastructure.jsonc.'
      )
    );
  }
  const imposes = champsImposes(p, eq);
  const verrou = (key) => (key in imposes ? `Fixé par le site ${p.site} (appareil « ${eq.appareil} ») dans infrastructure.jsonc.` : null);
  const tcp = entry?.modbus && eq.options?.transport === 'tcp';
  if (entry?.modbus) {
    const adresse = tcp
      ? field('N° d’unité Modbus', input(eq, 'address', { type: 'number', min: 0, max: 255, onInput: () => store.changed() }), verrou('address') || 'Entre 0 et 255 (souvent 1 ; adresse de l’esclave derrière une passerelle TCP → RTU).')
      : field('Adresse Modbus', input(eq, 'address', { type: 'number', min: 1, max: 247, onInput: () => store.changed() }), verrou('address') || 'Entre 1 et 247, unique sur le bus RS485.');
    if (verrou('address')) adresse.querySelector('input').disabled = true;
    fields.append(adresse);
  }
  for (const opt of entry?.options || []) {
    // Adresse IP et port : seulement en Modbus TCP.
    if (entry.modbus && !tcp && (opt.key === 'ip' || opt.key === 'port')) continue;
    if (verrou(opt.key)) {
      const valeur = opt.choices ? opt.choices.find((c) => String(c.value) === String(eq.options[opt.key]))?.label ?? eq.options[opt.key] : eq.options[opt.key];
      fields.append(field(opt.label, h('input', { type: 'text', value: String(valeur ?? ''), disabled: true }), verrou(opt.key)));
      continue;
    }
    let control;
    if (opt.type === 'bool') control = checkbox(eq.options, opt.key, opt.label, { onChange: () => store.changed() });
    else if (opt.type === 'select') control = field(opt.label, select(eq.options, opt.key, opt.choices, { onChange: () => store.changed({ render: opt.key === 'transport' }) }), opt.help);
    else control = field(opt.label, input(eq.options, opt.key, { type: opt.type === 'number' ? 'number' : 'text', onInput: () => store.changed() }), opt.help);
    fields.append(opt.type === 'bool' ? h('div', { class: 'field' }, h('span', {}, ' '), control, opt.help ? h('small', {}, opt.help) : null) : control);
  }

  const summary = [];
  for (const g of entry?.groups || []) summary.push(`${g.label} : ${g.bank ? g.bank.bits : g.ids.length}`);
  if (entry?.signals) summary.push(`Signaux à câbler : ${entry.signals.map((s) => s.label).join(', ')}`);
  if (entry?.sections) summary.push(`${entry.sections.reduce((a, s) => a + s.nodes.length, 0)} nœuds créés automatiquement`);
  if (entry?.pioEnv) summary.push(`Compilation : environnement PlatformIO « ${entry.pioEnv} »`);
  if (entry?.ethernet) summary.push('Port Ethernet utilisable (à activer à l’étape Projet)');
  if (entry?.generic) summary.push(`Fichier matériel généré : data/hardware/${genericHardwareName(eq)}.json`);

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

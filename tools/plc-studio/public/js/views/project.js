// Étape 1 : réglages généraux du projet.

import { toHostname } from '/shared/model.js';
import { CATALOG } from '/shared/catalog.js';
import { h, input, checkbox, select, field, card, pageHead, button, issueList } from '../ui.js';

export function renderProject(root, store) {
  const p = store.project;
  const changed = () => store.changed();

  const hostname = input(p, 'hostname', { cls: 'mono', onInput: changed });
  const name = input(p, 'name', {
    cls: 'mono',
    onInput: (v) => {
      if (!p.hostnameTouched) {
        p.hostname = toHostname(v);
        hostname.value = p.hostname;
      }
      changed();
    },
  });
  hostname.addEventListener('input', () => (p.hostnameTouched = true));

  root.append(
    pageHead('Projet', 'Identité du projet, mode de fonctionnement et accès réseau de l’automate.'),
    issueBlock(store, 'project'),
    card(
      'Identité',
      h(
        'div',
        { class: 'grid' },
        field('Nom du projet', name, 'Nom de la classe C++ générée (src/<Nom>/) et du fichier data/<Nom>.json.'),
        field('Nom réseau (mDNS)', hostname, 'Clé « name » de config.json, sans espace. L’automate répond sur http://<nom>.local'),
        field('Description', input(p, 'description', { onInput: changed }))
      )
    ),
    card(
      'Mode d’exécution',
      h('p', { class: 'hint' }, 'Indique si la logique doit attendre qu’un écran (navigateur) soit connecté pour tourner.'),
      modeChooser(p, changed),
      h(
        'div',
        { class: 'callout warn', style: { marginTop: '12px', marginBottom: 0 } },
        'Ce réglage est écrit dans config.json (clé « execution_mode »), mais le firmware actuel ne le lit pas encore : aujourd’hui, la logique ne tourne que si un écran est connecté (audit CR-2). La correction de main.cpp est prévue plus tard.'
      )
    ),
    card(
      'Options',
      h(
        'div',
        { class: 'grid' },
        checkbox(p.options, 'stateDisplay', 'Afficher l’état machine à l’écran', { onChange: () => store.changed({ render: false }) }),
        checkbox(p.options, 'clockDisplay', 'Afficher l’horloge à l’écran', { onChange: changed }),
        checkbox(p.options, 'logDiagnostic', 'Journal de diagnostic (fichier)', { onChange: changed }),
        checkbox(p.options, 'deleteDiagnosticAtStartup', 'Effacer le journal au démarrage', { onChange: changed }),
        checkbox(p.options, 'showSuccessMessages', 'Notifications de succès à l’écran', { onChange: changed }),
        checkbox(p.options, 'showInfoMessages', 'Notifications d’information à l’écran', { onChange: changed }),
        checkbox(p.options, 'logHistory', 'Historique des transitions dans le journal', { onChange: changed })
      ),
      h(
        'div',
        { class: 'grid', style: { marginTop: '12px' } },
        field('Mot de passe de l’éditeur de fichiers', input(p, 'editorPassword', { onInput: changed }), 'Vérifié uniquement par le navigateur.'),
        field('URL de mise à jour (OTA)', input(p, 'otaUrl', { onInput: changed }), '79 caractères maximum.')
      )
    ),
    wifiCard(store),
    ethernetCard(store)
  );
}

// Ethernet filaire (cartes W5500 / LAN8720), en plus du WiFi.
function ethernetCard(store) {
  const p = store.project;
  const controller = p.equipment.find((e) => CATALOG[e.type]?.role === 'controller');
  const capable = !!CATALOG[controller?.type]?.ethernet;
  const body = h('div', {});
  const draw = () => {
    const fields = h('div', { class: 'grid' });
    fields.append(
      field(
        'Ethernet',
        select(
          p.ethernet,
          'enabled',
          [
            { value: false, label: 'Désactivé' },
            { value: true, label: 'Activé' },
          ],
          { onChange: () => (draw(), store.changed()) }
        )
      )
    );
    if (p.ethernet.enabled) {
      fields.append(
        field(
          'Adressage',
          select(
            p.ethernet,
            'dhcp',
            [
              { value: true, label: 'Automatique (DHCP)' },
              { value: false, label: 'Adresse fixe' },
            ],
            { onChange: () => (draw(), store.changed()) }
          )
        )
      );
      if (p.ethernet.dhcp === false) {
        for (const [k, label] of [
          ['ip', 'Adresse IP'],
          ['mask', 'Masque'],
          ['gateway', 'Passerelle'],
          ['dns', 'DNS (optionnel)'],
        ]) {
          fields.append(field(label, input(p.ethernet, k, { cls: 'mono', onInput: () => store.changed() })));
        }
      }
    }
    body.replaceChildren(
      h(
        'p',
        { class: 'hint' },
        capable
          ? 'Port Ethernet de l’automate, utilisable en même temps que le WiFi (écran, mDNS, Modbus TCP). Évitez de mettre le WiFi client et l’Ethernet sur le même sous-réseau.'
          : 'L’automate choisi n’a pas de port Ethernet utilisable : seul le WiFi est disponible.'
      ),
      fields
    );
  };
  draw();
  return card('Réseau filaire (Ethernet)', body);
}

function modeChooser(p, changed) {
  const options = [
    { value: 'hmi', title: 'Avec écran (HMI)', text: 'La logique ne tourne que lorsqu’un navigateur est connecté. C’est le comportement actuel du firmware.' },
    { value: 'headless', title: 'Autonome (headless)', text: 'La logique tourne en permanence, avec ou sans écran connecté.' },
  ];
  const wrap = h('div', { class: 'radio-cards' });
  const draw = () => {
    wrap.replaceChildren(
      ...options.map((o) =>
        h(
          'label',
          { class: `radio-card ${p.execution.mode === o.value ? 'selected' : ''}` },
          h('input', {
            type: 'radio',
            name: 'exec-mode',
            checked: p.execution.mode === o.value,
            on: {
              change: () => {
                p.execution.mode = o.value;
                draw();
                changed();
              },
            },
          }),
          h('div', {}, h('strong', {}, o.title), h('span', {}, o.text))
        )
      )
    );
  };
  draw();
  return wrap;
}

function wifiCard(store) {
  const p = store.project;
  const body = h('div', {});
  const draw = () => {
    body.replaceChildren();
    if (!p.wifi.length) body.append(h('p', { class: 'muted' }, 'Aucun réseau configuré.'));
    p.wifi.forEach((w, i) => {
      w.mode = w.mode || 'station';
      const fields = h('div', { class: 'grid' });
      const drawFields = () => {
        fields.replaceChildren(
          field('Nom', input(w, 'name', { onInput: () => store.changed() })),
          field(
            'Mode',
            select(
              w,
              'mode',
              [
                { value: 'station', label: 'Client (se connecte à un réseau)' },
                { value: 'access_point', label: 'Point d’accès (crée un réseau)' },
              ],
              {
                onChange: () => {
                  drawFields();
                  store.changed();
                },
              }
            )
          )
        );
        if (w.mode === 'access_point') {
          fields.append(
            field('Nom du réseau (SSID)', input(w, 'ap_name', { onInput: () => store.changed() })),
            field('Mot de passe', input(w, 'ap_password', { onInput: () => store.changed() }), '8 caractères minimum.'),
            field('Adresse IP', input(w, 'ap_ip', { cls: 'mono', placeholder: '192.168.4.1', onInput: () => store.changed() })),
            field('Canal', input(w, 'ap_channel', { type: 'number', min: 1, max: 13, onInput: () => store.changed() }))
          );
        } else {
          if (w.dhcp === undefined) w.dhcp = true;
          fields.append(
            field('SSID', input(w, 'ssid', { onInput: () => store.changed() })),
            field('Mot de passe', input(w, 'pwd', { onInput: () => store.changed() })),
            field(
              'Adressage',
              select(
                w,
                'dhcp',
                [
                  { value: true, label: 'Automatique (DHCP)' },
                  { value: false, label: 'Adresse fixe' },
                ],
                {
                  onChange: () => {
                    drawFields();
                    store.changed();
                  },
                }
              )
            )
          );
          if (w.dhcp === false) {
            for (const [k, label] of [
              ['ip', 'Adresse IP'],
              ['mask', 'Masque'],
              ['gateway', 'Passerelle'],
              ['dns', 'DNS (optionnel)'],
            ]) {
              fields.append(field(label, input(w, k, { cls: 'mono', onInput: () => store.changed() })));
            }
          }
        }
      };
      drawFields();
      body.append(
        h(
          'div',
          { class: 'card', style: { background: 'var(--panel-2)', boxShadow: 'none' } },
          h(
            'div',
            { class: 'row', style: { marginBottom: '10px' } },
            h('strong', {}, `Réseau ${i + 1}`),
            h('span', { class: 'spacer' }),
            button('▲', () => move(i, -1), 'small ghost', { title: 'Monter', disabled: i === 0 }),
            button('▼', () => move(i, 1), 'small ghost', { title: 'Descendre', disabled: i === p.wifi.length - 1 }),
            button(
              'Supprimer',
              () => {
                p.wifi.splice(i, 1);
                draw();
                store.changed();
              },
              'small danger'
            )
          ),
          fields
        )
      );
    });
  };
  const move = (i, d) => {
    const [w] = p.wifi.splice(i, 1);
    p.wifi.splice(i + d, 0, w);
    draw();
    store.changed();
  };
  draw();
  return h(
    'section',
    { class: 'card' },
    h(
      'h2',
      {},
      'Réseaux WiFi',
      h('span', { class: 'spacer' }),
      button(
        '+ Réseau client',
        () => {
          p.wifi.push({ name: `Réseau ${p.wifi.length + 1}`, mode: 'station', ssid: '', pwd: '', dhcp: true });
          draw();
          store.changed();
        },
        'small'
      ),
      button(
        '+ Point d’accès',
        () => {
          p.wifi.push({ name: 'Point d’accès', mode: 'access_point', ap_name: p.hostname, ap_password: '', ap_ip: '192.168.4.1', ap_channel: 6 });
          draw();
          store.changed();
        },
        'small'
      )
    ),
    h('p', { class: 'hint' }, 'Essayés dans l’ordre (10 maximum).'),
    body
  );
}

// Liste des problèmes de l'étape, rafraîchie à chaque modification.
export function issueBlock(store, step) {
  const box = h('div', {});
  const draw = () => {
    const issues = store.issues.filter((i) => i.step === step);
    box.replaceChildren(issues.length ? h('section', { class: 'card' }, h('h2', {}, 'À corriger'), issueList(issues)) : '');
  };
  draw();
  store.onChange(draw);
  return box;
}

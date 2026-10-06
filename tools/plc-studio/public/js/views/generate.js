// Étape 7 : vérification et génération des fichiers du firmware.

import { h, card, pageHead, button, issueList, toast, confirmDialog } from '../ui.js';
import { api } from '../api.js';
import { lineDiff } from '../diff.js';
import { save } from '../app.js';

let last = null; // dernier aperçu
let selected = null;
let showDiff = true;

export function renderGenerate(root, store) {
  const errors = store.issues.filter((i) => i.level === 'error');
  const head = pageHead(
    'Génération',
    'Vérifiez le projet et prévisualisez les fichiers produits. « Exporter » les écrit dans le dossier du projet ; « Installer » les écrit dans le dépôt (les fichiers remplacés sont sauvegardés dans tools/plc-studio/.sauvegardes/).',
    button('Prévisualiser', () => runPreview(root, store), 'primary')
  );
  root.append(
    head,
    card(
      errors.length ? `Vérification : ${errors.length} erreur(s)` : 'Vérification',
      issueList(store.issues, (step) => store.go(step))
    ),
    h('div', { id: 'gen-output' })
  );
  if (last && last.projectName === store.project.name) drawPreview(root, store);
}

async function runPreview(root, store) {
  try {
    const result = await api.preview(store.project);
    last = { ...result, projectName: store.project.name };
    if (!selected || !result.files.some((f) => f.path === selected)) selected = result.files.find((f) => f.status !== 'identique')?.path || result.files[0]?.path;
    drawPreview(root, store);
  } catch (e) {
    toast(`Prévisualisation impossible : ${e.message}`, 'error');
  }
}

function drawPreview(root, store) {
  const out = root.querySelector('#gen-output');
  if (!out) return;
  const errors = last.issues.filter((i) => i.level === 'error');
  const changed = last.files.filter((f) => f.status !== 'identique');
  const file = last.files.find((f) => f.path === selected);

  const statusBadge = (s) => h('span', { class: `badge ${s === 'nouveau' ? 'ok' : s === 'modifié' ? 'warn' : 'neutral'}` }, s);

  out.replaceChildren(
    card(
      'Fichiers produits',
      !last.installable
        ? h(
            'div',
            { class: 'callout warn' },
            `Le code C++ n’est pas encore généré (il arrive avec l’éditeur grafcet). Installer ces fichiers dans le dépôt casserait l’application actuelle${last.currentApp ? ` (${last.currentApp})` : ''} : utilisez « Exporter », qui écrit dans ${last.exportDir}/ sans toucher au firmware.`
          )
        : null,
      h(
        'div',
        { class: 'file-list' },
        last.files.map((f) =>
          h(
            'div',
            {
              class: `file-row ${f.path === selected ? 'selected' : ''}`,
              on: {
                click: () => {
                  selected = f.path;
                  drawPreview(root, store);
                },
              },
            },
            h('span', { class: 'path' }, f.path),
            statusBadge(f.status)
          )
        )
      ),
      h(
        'div',
        { class: 'btn-row', style: { marginTop: '14px' } },
        button(`Exporter dans ${last.exportDir}/`, () => applyFiles(root, store, 'export'), last.installable ? '' : 'primary', {
          disabled: errors.length > 0,
          title: errors.length ? 'Corrigez d’abord les erreurs.' : '',
        }),
        button(
          changed.length ? `Installer ${changed.length} fichier(s) dans le dépôt` : 'Dépôt à jour',
          () => applyFiles(root, store, 'install'),
          last.installable ? 'primary' : '',
          {
            disabled: !last.installable || !changed.length || errors.length > 0,
            title: !last.installable ? 'Disponible quand le code C++ sera généré.' : errors.length ? 'Corrigez d’abord les erreurs.' : '',
          }
        ),
        errors.length ? h('span', { class: 'badge err' }, 'Corrigez les erreurs avant d’écrire') : null,
        h('span', { class: 'spacer' }),
        h('span', { class: 'muted' }, `${last.nodeCount} nœuds dans config.json`)
      )
    ),
    file ? fileCard(root, store, file) : null
  );
}

function fileCard(root, store, f) {
  const canDiff = f.current !== null && f.status === 'modifié';
  const lines = canDiff && showDiff ? lineDiff(f.current, f.content) : f.content.split('\n').map((text, i) => ({ type: 'same', text, ln: i + 1 }));
  return h(
    'section',
    { class: 'card' },
    h(
      'h2',
      {},
      h('span', { class: 'mono' }, f.path),
      h('span', { class: 'spacer' }),
      canDiff
        ? button(
            showDiff ? 'Voir le fichier complet' : 'Voir les différences',
            () => {
              showDiff = !showDiff;
              drawPreview(root, store);
            },
            'small'
          )
        : null
    ),
    h(
      'pre',
      { class: 'code' },
      lines.map((l) =>
        h('span', { class: `ln ${l.type === 'add' ? 'add' : l.type === 'del' ? 'del' : l.type === 'hunk' ? 'hunk' : ''}` }, `${l.type === 'add' ? '+ ' : l.type === 'del' ? '- ' : l.type === 'hunk' ? '' : '  '}${l.text}`)
      )
    )
  );
}

async function applyFiles(root, store, target) {
  const changed = last.files.filter((f) => f.status !== 'identique');
  if (target === 'install') {
    const ok = await confirmDialog(
      'Installer dans le dépôt',
      `${changed.length} fichier(s) vont être écrits dans le dépôt (${changed.map((f) => f.path).join(', ')}). Les versions actuelles seront sauvegardées dans tools/plc-studio/.sauvegardes/.`,
      'Installer'
    );
    if (!ok) return;
  }
  if (store.dirty) await save({ silent: true });
  try {
    const res = await api.apply(store.project, target === 'install' ? changed.map((f) => f.path) : null, target);
    if (!res.ok) {
      toast(res.message || 'Génération refusée.', 'error');
      return;
    }
    toast(
      target === 'install'
        ? `${res.written.length} fichier(s) installé(s).${res.backup ? ` Sauvegarde : ${res.backup}` : ''}`
        : `${res.written.length} fichier(s) exporté(s) dans ${last.exportDir}/.`
    );
    await runPreview(root, store);
  } catch (e) {
    toast(`Écriture impossible : ${e.message}`, 'error');
  }
}

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { newProject, addEquipment, normalizeVariable } from '../shared/model.js';
import { buildLayout } from '../shared/layout.js';
import { autoLayout, unplacedRefs, widgetProps } from '../shared/hmi.js';
import { generateConfig, generateInterface } from '../shared/generate.js';

function project() {
  const p = newProject('Ecrans');
  const plc = addEquipment(p, 'Kincony_KC868_A8S');
  p.variables.push(normalizeVariable({ label: 'AU OK', symbol: 'AU_OK', kind: 'input', dataType: 'bool', inverse: true, binding: { eq: plc.uid, group: 'di', channel: 1 }, hmi: { show: true } }));
  p.variables.push(normalizeVariable({ label: 'Départ', symbol: 'Depart', kind: 'command', dataType: 'bool', comment: 'Lance un cycle' }));
  p.variables.push(normalizeVariable({ label: 'Défaut', symbol: 'Defaut', kind: 'indicator', dataType: 'bool' }));
  return p;
}
const custom = (p, layout) => {
  p.hmi.auto = false;
  p.hmi.pages = JSON.parse(JSON.stringify(autoLayout(p, layout)));
  return p.hmi.pages;
};
const allNodes = (ui) => ui.pages.flatMap((pg) => pg.sections.flatMap((s) => s.NodesList));

test('éléments disponibles : calculés sur la disposition affichée (automatique ou personnalisée)', () => {
  const p = project();
  const layout = buildLayout(p);
  const pages = autoLayout(p, layout);
  const placed = new Set(pages.flatMap((pg) => pg.sections.flatMap((s) => s.items.map((i) => i.ref))));
  const avail = unplacedRefs(p, layout, pages);
  assert.ok(avail.length > 0);
  for (const ref of avail) assert.ok(!placed.has(ref), ref);
  // Retirer un widget le rend disponible.
  const mine = custom(p, layout);
  const sec = mine.find((pg) => pg.sections.some((s) => s.items.length)).sections.find((s) => s.items.length);
  const [removed] = sec.items.splice(0, 1);
  assert.ok(unplacedRefs(p, layout).includes(removed.ref));
});

test('propriétés de widget : valeurs par défaut, info-bulle du commentaire, surcharge de l’éditeur', () => {
  const p = project();
  const depart = p.variables.find((v) => v.symbol === 'Depart');
  const item = { ref: `var:${depart.uid}`, label: 'Départ', widget: 'tx-button-hold' };
  assert.deepEqual(widgetProps(item, depart), { color: 'primary', tooltip: 'Lance un cycle' });
  item.props = { color: 'success', tooltip: 'Démarrer', loadable: true };
  assert.deepEqual(widgetProps(item, depart), { color: 'success', tooltip: 'Démarrer', loadable: true });
});

test('interface.json : propriétés personnalisées écrites, « inverse » annulé sauf choix explicite', () => {
  const p = project();
  const layout = buildLayout(p);
  const pages = custom(p, layout);
  const items = pages.flatMap((pg) => pg.sections.flatMap((s) => s.items));
  const au = items.find((i) => i.label === 'AU OK');
  const defaut = items.find((i) => i.label === 'Défaut');
  defaut.props = { color: 'red', blink: true };
  let ui = generateInterface(p, layout, pages);
  assert.equal(allNodes(ui).find((n) => n.name === 'AU OK').inverse, false);
  const d = allNodes(ui).find((n) => n.name === 'Défaut');
  assert.equal(d.color, 'red');
  assert.equal(d.blink, true);
  au.props = { inverse: true };
  ui = generateInterface(p, layout, pages);
  assert.equal(allNodes(ui).find((n) => n.name === 'AU OK').inverse, true);
});

test('interface.json : sections vides conservées (l’ordre correspond à l’éditeur)', () => {
  const p = project();
  const layout = buildLayout(p);
  const pages = custom(p, layout);
  pages[0].sections.unshift({ id: 'vide', name: 'Vide', orientation: 'vertical', items: [] });
  const ui = generateInterface(p, layout, pages);
  assert.equal(ui.pages[0].sections.length, pages[0].sections.length);
  assert.deepEqual(ui.pages[0].sections[0], { SectionName: 'Vide', orientation: 'vertical', NodesList: [] });
});

test('config.json : mots de passe de page et de section', () => {
  const p = project();
  const layout = buildLayout(p);
  const pages = custom(p, layout);
  pages[0].password = '1234';
  pages[0].sections[0].password = 'abcd';
  const cfg = generateConfig(p, layout, pages);
  assert.equal(cfg.security.passwords.pages[pages[0].name], '1234');
  assert.equal(cfg.security.passwords.sections[pages[0].sections[0].name], 'abcd');
});

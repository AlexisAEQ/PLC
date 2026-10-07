#!/usr/bin/env node
// PLC Studio en ligne de commande : créer un projet depuis un cahier des charges (spec),
// le vérifier, le simuler et le décrire, sans ouvrir l'interface. Le projet produit
// (projets/<Nom>.plc.json) s'ouvre ensuite dans PLC Studio (« Ouvrir… »).
//
//   node tools/plc-studio/cli/plc.mjs <commande> [arguments]
//
//   catalog                         équipements et noms des voies (plc.X1, io.DI3…)
//   build <spec.json> [--force]     spec -> projets/<Nom>.plc.json (+ spec et scénarios dans
//                                   projets/<Nom>/), puis vérification et scénarios
//   check <projet> [--cpp]          vérification complète (+ syntaxe C++ avec g++)
//   sim <projet> [--scenarios f]    exécute les scénarios (défaut projets/<Nom>/scenarios.json)
//   describe <projet> [--md]        résumé lisible (E/S, variables, grafcet, modes, écrans)
//   export-spec <projet> [--out f]  projet -> spec (pour modifier un projet existant)
//
// <projet> : chemin d'un .plc.json ou simple nom (projets/<Nom>.plc.json).
// Options communes : --root <dépôt> (défaut : dépôt qui contient l'outil), --json.

import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { CATALOG } from '../shared/catalog.js';
import { normalizeProject, listChannels, channelName } from '../shared/model.js';
import { parseJsonc } from '../shared/jsonc.js';
import { buildFromSpec, specFromProject } from '../shared/spec.js';
import { generateFiles } from '../shared/generate.js';
import { runScenarios } from '../shared/scenario.js';
import { actionText, normalizeLogic, axisBlock } from '../shared/grafcet.js';
import { axisList } from '../shared/axes.js';
import { buildLayout } from '../shared/layout.js';
import { effectivePages } from '../shared/generate.js';
import { checkCppSyntax } from './cppcheck.mjs';

const toolDir = path.dirname(path.dirname(fileURLToPath(import.meta.url)));

// ---------------------------------------------------------------------------
const argv = process.argv.slice(2);
const flags = {};
const args = [];
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a.startsWith('--')) {
    const key = a.slice(2);
    const next = argv[i + 1];
    if (['root', 'out', 'scenarios'].includes(key) && next !== undefined) {
      flags[key] = next;
      i++;
    } else flags[key] = true;
  } else args.push(a);
}
const ROOT = path.resolve(flags.root || path.join(toolDir, '..', '..'));
const PROJECTS = path.join(ROOT, 'projets');
const cmd = args.shift();

const readJson = (file) => parseJsonc(readFileSync(file, 'utf8'));
const rel = (p) => {
  const r = path.relative(process.cwd(), p);
  return !r ? '.' : r.startsWith('..') ? p : r;
};

function hardwareFiles() {
  const dir = path.join(ROOT, 'data', 'hardware');
  if (!existsSync(dir)) return {};
  return Object.fromEntries(
    readdirSync(dir)
      .filter((f) => f.endsWith('.json'))
      .map((f) => [f.slice(0, -5), readJson(path.join(dir, f))])
  );
}
function mainCpp() {
  const f = path.join(ROOT, 'src', 'main.cpp');
  return existsSync(f) ? readFileSync(f, 'utf8') : undefined;
}
function projectPath(arg) {
  if (!arg) die('Indiquez le projet (chemin ou nom).');
  if (arg.endsWith('.json') || arg.includes('/') || arg.includes('\\')) return path.resolve(arg);
  return path.join(PROJECTS, `${arg}.plc.json`);
}
function loadProject(arg) {
  const file = projectPath(arg);
  if (!existsSync(file)) die(`Projet introuvable : ${rel(file)}`);
  return { file, project: normalizeProject(readJson(file)) };
}
function die(msg) {
  console.error(msg);
  process.exit(2);
}

const STEP_LABELS = { project: 'Projet', equipment: 'Équipements', variables: 'Câblage et variables', logic: 'Logique', hmi: 'Écrans', generate: 'Génération', simulation: 'Simulation' };
function printIssues(issues) {
  const errors = issues.filter((i) => i.level === 'error');
  const warnings = issues.filter((i) => i.level !== 'error');
  for (const i of errors) console.log(`  ERREUR    [${STEP_LABELS[i.step] || i.step}] ${i.message}`);
  for (const i of warnings) console.log(`  attention [${STEP_LABELS[i.step] || i.step}] ${i.message}`);
  return errors.length;
}

function checkProject(project, { cpp = false, quiet = false } = {}) {
  const r = generateFiles(project, { hardwareFiles: hardwareFiles(), mainCpp: mainCpp() });
  const errors = r.issues.filter((i) => i.level === 'error').length;
  const warnings = r.issues.length - errors;
  if (!quiet || r.issues.length) {
    console.log(`Vérification : ${errors} erreur(s), ${warnings} avertissement(s) — ${r.layout.nodeCount} nœuds`);
    printIssues(r.issues);
  }
  if (!quiet) {
    console.log(`Fichiers que « Installer » produirait : ${r.files.map((f) => f.path).join(', ')}`);
    console.log(r.installable ? 'Installable : oui.' : 'Installable : non (corriger les erreurs).');
  }
  let cppOk = true;
  if (cpp && !errors) {
    const res = checkCppSyntax([{ label: project.name, project }], { repo: ROOT, hardwareFiles: hardwareFiles(), mainCpp: mainCpp() });
    if (!res) console.log('C++ : g++ introuvable, vérification ignorée.');
    else
      for (const x of res) {
        console.log(`C++ (g++ -fsyntax-only) : ${x.ok ? 'OK' : 'ÉCHEC'}`);
        for (const m of x.messages) console.log('  ' + m);
        cppOk = cppOk && x.ok;
      }
  }
  return { errors, warnings, cppOk, result: r };
}

function simulate(project, scenarios, { verbose = false } = {}) {
  const results = runScenarios(project, scenarios);
  let failed = 0;
  for (const r of results) {
    if (!r.passed) failed++;
    console.log(`${r.passed ? 'RÉUSSI' : 'ÉCHEC '}  ${r.name}  (${(r.duration / 1000).toFixed(2)} s simulées)`);
    if (verbose || !r.passed) console.log(r.log.join('\n'));
    if (!r.passed) {
      console.log('  Journal (fin) :');
      for (const j of r.journal.slice(-12)) console.log(`    ${(j.t / 1000).toFixed(2)} s  ${j.kind}  ${j.text}`);
    }
  }
  console.log(`Scénarios : ${results.length - failed}/${results.length} réussis.`);
  return failed;
}

// ---------------------------------------------------------------------------
function cmdCatalog() {
  if (flags.json) {
    console.log(JSON.stringify(CATALOG, null, 2));
    return 0;
  }
  console.log('Équipements (type pour « equipment[].type ») et voies (« <id>.<voie> » dans « io[].at »)\n');
  for (const [type, e] of Object.entries(CATALOG)) {
    console.log(`${type} — ${e.label}${e.max ? ` (${e.max} max.)` : ''}`);
    console.log(`  ${e.description}`);
    if (e.modbus) console.log(`  Modbus : adresse par défaut ${e.defaultAddress} (1 à 247, unique)`);
    for (const g of e.groups) {
      const names = g.bank ? Array.from({ length: g.bank.bits }, (_, i) => channelName(g, i)) : g.ids.map((id) => channelName(g, id));
      console.log(`  ${g.dir === 'in' ? 'entrées' : 'sorties'} ${g.dataType.padEnd(4)} ${g.label} : ${names.join(' ')}${g.supportsInverse ? '  (inverse possible)' : ''}`);
    }
    for (const o of e.options || []) console.log(`  option ${o.key} (défaut ${JSON.stringify(o.default)}) : ${o.label}${o.choices ? ' — ' + o.choices.map((c) => c.value).join(' | ') : ''}`);
    if (e.axis) console.log(`  axe : positions en ${e.axis.positionUnit}, vitesse de déplacement en ${e.axis.speedUnit} (${e.axis.speedMin} à ${e.axis.speedMax}, défaut ${e.axis.defaultSpeed}), jog en ${e.axis.jogSpeedUnit}`);
    if (e.pioEnv) console.log(`  compilation : environnement PlatformIO « ${e.pioEnv} »`);
    for (const s of e.signals || []) {
      const where = s.gpio ? "sortie GPIO directe de l'automate" : s.dir === 'in' ? "entrée de l'automate" : "sortie de l'automate";
      console.log(`  signal${s.optional ? ' facultatif' : ''} (equipment[].signals.${s.key}) sur une ${where} : ${s.label}`);
    }
    console.log('');
  }
  return 0;
}

function cmdBuild() {
  const specFile = args[0];
  if (!specFile) die('Usage : build <spec.json> [--force]');
  const spec = readJson(path.resolve(specFile));
  // Reconstruction d'un projet existant : identifiants internes et ordre des champs conservés.
  const target = flags.out ? path.resolve(flags.out) : spec.name ? path.join(PROJECTS, `${spec.name}.plc.json`) : null;
  const previous = flags.force && target && existsSync(target) ? readJson(target) : null;
  const { project, errors, warnings, issues } = buildFromSpec(spec, { previous });
  for (const w of warnings) console.log(`  attention [spec] ${w}`);
  if (errors.length) {
    console.log(`Spec invalide : ${errors.length} erreur(s), rien n'a été écrit.`);
    for (const e of errors) console.log(`  ERREUR    [spec] ${e}`);
    return 1;
  }
  const out = flags.out ? path.resolve(flags.out) : path.join(PROJECTS, `${project.name}.plc.json`);
  if (existsSync(out) && !flags.force) {
    console.log(`${rel(out)} existe déjà (peut-être retouché dans PLC Studio) : relancez avec --force pour le remplacer,`);
    console.log(`ou partez du projet existant (export-spec ${project.name}).`);
    return 1;
  }
  mkdirSync(path.dirname(out), { recursive: true });
  writeFileSync(out, JSON.stringify(project, null, 2) + '\n');
  const dir = path.join(path.dirname(out), project.name);
  mkdirSync(dir, { recursive: true });
  const { scenarios, ...specOnly } = spec;
  writeFileSync(path.join(dir, 'spec.json'), JSON.stringify(specOnly, null, 2) + '\n');
  if (Array.isArray(scenarios)) writeFileSync(path.join(dir, 'scenarios.json'), JSON.stringify(scenarios, null, 2) + '\n');
  console.log(`Projet écrit : ${rel(out)}  (spec : ${rel(path.join(dir, 'spec.json'))}${Array.isArray(scenarios) ? `, scénarios : ${rel(path.join(dir, 'scenarios.json'))}` : ''})`);
  void issues;
  const c = checkProject(project, { cpp: !!flags.cpp });
  let failed = 0;
  if (Array.isArray(scenarios) && scenarios.length && !c.errors) failed = simulate(project, scenarios, { verbose: !!flags.verbose });
  else if (!Array.isArray(scenarios) || !scenarios.length) console.log('Aucun scénario : ajoutez « scenarios » à la spec pour valider la logique.');
  console.log(`Ouvrir dans PLC Studio : node tools/plc-studio/server/server.js, puis « Ouvrir… » → ${project.name}`);
  return c.errors || failed || !c.cppOk ? 1 : 0;
}

function cmdCheck() {
  const { file, project } = loadProject(args[0]);
  console.log(`Projet : ${rel(file)}`);
  const c = checkProject(project, { cpp: !!flags.cpp });
  return c.errors || !c.cppOk ? 1 : 0;
}

function cmdSim() {
  const { file, project } = loadProject(args[0]);
  const scFile = flags.scenarios ? path.resolve(flags.scenarios) : path.join(path.dirname(file), project.name, 'scenarios.json');
  if (!existsSync(scFile)) die(`Scénarios introuvables : ${rel(scFile)} (option --scenarios <fichier>)`);
  const raw = readJson(scFile);
  const scenarios = Array.isArray(raw) ? raw : raw.scenarios;
  const c = checkProject(project, { quiet: true });
  if (c.errors) {
    console.log('Le projet contient des erreurs : corrigez-les avant de simuler.');
    return 1;
  }
  return simulate(project, scenarios, { verbose: !!flags.verbose }) ? 1 : 0;
}

function cmdExportSpec() {
  const { file, project } = loadProject(args[0]);
  const spec = specFromProject(project);
  // Les scénarios existants font partie de la spec exportée (build les réécrit).
  const scFile = path.join(path.dirname(file), project.name, 'scenarios.json');
  if (existsSync(scFile)) {
    const raw = readJson(scFile);
    spec.scenarios = Array.isArray(raw) ? raw : raw.scenarios || [];
  }
  const text = JSON.stringify(spec, null, 2) + '\n';
  if (flags.out) {
    writeFileSync(path.resolve(flags.out), text);
    console.log(`Spec écrite : ${rel(path.resolve(flags.out))}`);
  } else process.stdout.write(text);
  return 0;
}

function cmdDescribe() {
  const { file, project } = loadProject(args[0]);
  const md = !!flags.md;
  const layout = buildLayout(project);
  const pages = effectivePages(project, layout);
  const channels = listChannels(project);
  const out = [];
  const h = (t) => out.push(md ? `\n## ${t}\n` : `\n== ${t}`);
  const table = (head, rows) => {
    if (!rows.length) return out.push(md ? '_aucun_' : '  (aucun)');
    if (md) {
      out.push(`| ${head.join(' | ')} |`, `|${head.map(() => '---').join('|')}|`);
      for (const r of rows) out.push(`| ${r.map((c) => String(c ?? '').replace(/\|/g, '\\|')).join(' | ')} |`);
    } else for (const r of rows) out.push('  ' + r.map((c) => String(c ?? '')).join('  |  '));
  };
  const chName = (b) => {
    const ch = channels.find((c) => c.eq.uid === b?.eq && c.group.key === b?.group && c.channel === b?.channel);
    return ch ? `${ch.eq.label} · ${ch.group.label} · ${ch.name}` : '— non câblée —';
  };
  out.push(md ? `# ${project.name}` : `Projet ${project.name} (${rel(file)})`);
  if (project.description) out.push(md ? `\n${project.description}` : project.description);
  out.push(`${md ? '\n' : ''}Mode d'exécution : ${project.execution?.mode === 'headless' ? 'autonome (headless)' : 'avec écran (HMI)'} · nom réseau : ${project.hostname} · ${layout.nodeCount} nœuds`);

  h('Équipements');
  table(['Équipement', 'Type', 'Adresse Modbus', 'Options'], project.equipment.map((e) => [e.label, CATALOG[e.type]?.label || e.type, CATALOG[e.type]?.modbus ? e.address : '—', Object.entries(e.options || {}).map(([k, v]) => `${k}=${v}`).join(', ')]));

  const fb = { off: 'à 0', on: 'à 1', hold: 'maintenue' };
  h('Entrées / sorties');
  table(
    ['Symbole', 'Libellé', 'Sens', 'Voie', 'Remarque'],
    project.variables.filter((v) => ['input', 'output'].includes(v.kind)).map((v) => [v.symbol, v.label, v.kind === 'input' ? 'entrée' : 'sortie', chName(v.binding), [v.inverse ? 'inversée' : '', v.kind === 'output' ? `repli ${fb[v.fallback] || v.fallback}` : '', v.system ? "signal d'axe" : '', v.comment].filter(Boolean).join(', ')])
  );
  const KIND = { command: 'Commande', parameter: 'Paramètre', indicator: 'Indicateur', memory: 'Mémoire' };
  h('Variables internes');
  table(
    ['Symbole', 'Libellé', 'Nature', 'Type', 'Valeur initiale / bornes', 'Remarque'],
    project.variables
      .filter((v) => KIND[v.kind])
      .map((v) => [v.symbol, v.label, KIND[v.kind], v.dataType, [v.initial !== undefined && v.kind === 'parameter' ? `${v.initial}` : '', v.min !== undefined && v.min !== '' ? `min ${v.min}` : '', v.max !== undefined && v.max !== '' ? `max ${v.max}` : '', v.unit || ''].filter(Boolean).join(' · '), v.comment])
  );

  const logic = normalizeLogic(project.logic, project);
  const axes = axisList(project);
  const axisName = (uid) => axes.find((a) => a.uid === uid)?.label || null;
  const numOf = new Map(logic.steps.map((s) => [s.id, s.num]));
  h('Grafcet — étapes');
  const act = (a) => actionText(a, axisName).replace(/\s{2,}/g, ' ');
  table(['Étape', 'Libellé', 'Actions'], [...logic.steps].sort((a, b) => a.num - b.num).map((s) => [`${s.initial ? '((' : ''}${s.num}${s.initial ? '))' : ''}`, s.label, s.actions.map(act).join(' ; ') || '—']));
  h('Grafcet — transitions');
  table(['Transition', 'De', 'Vers', 'Réceptivité'], [...logic.transitions].sort((a, b) => a.num - b.num).map((t) => [`T${t.num}`, t.from.map((id) => numOf.get(id)).join(' + '), t.to.map((id) => numOf.get(id)).join(' + '), t.condition]));
  const b = logic.blocks;
  h('Modes et sécurités');
  table(
    ['Bloc', 'Réglage'],
    [
      ["Condition d'arrêt d'urgence", b.emergency.condition || '— aucune —'],
      ['Acquittement', b.emergency.reset || '— aucun (sortie automatique après 1 s) —'],
      ['Mise en marche', b.run.start || 'automatique'],
      ["Condition d'arrêt", b.run.stop || '— aucune —'],
      ...axes.flatMap((a) => {
        const x = axisBlock(logic, a.uid);
        return [
          [`${a.label} — prise d'origine`, x.homing + (x.homingCondition ? ` (${x.homingCondition})` : '') + (x.homing !== 'none' ? `, ordre ${x.homingOrder}` : '')],
          [`${a.label} — jog`, x.jogEnabled ? `mode ${x.jogMode || '?'}, + ${x.jogPlus || '?'}, − ${x.jogMinus || '?'}, vitesse ${x.jogSpeed || 'défaut'}` : 'désactivé'],
          [`${a.label} — couple maximal`, x.torque ? `${x.torque} %` : 'défaut'],
        ];
      }),
    ]
  );
  h(`Écrans (${project.hmi?.auto === false ? 'disposition personnalisée' : 'disposition automatique'})`);
  for (const p of pages) {
    out.push(md ? `- **${p.name}**${p.password ? ' (mot de passe)' : ''}` : `  ${p.name}${p.password ? ' (mot de passe)' : ''}`);
    for (const s of p.sections) out.push(`${md ? '  - ' : '    '}${s.name} : ${s.items.map((i) => i.label).join(', ') || '—'}`);
  }
  console.log(out.join('\n'));
  return 0;
}

// ---------------------------------------------------------------------------
const COMMANDS = { catalog: cmdCatalog, build: cmdBuild, check: cmdCheck, sim: cmdSim, describe: cmdDescribe, 'export-spec': cmdExportSpec };
if (!COMMANDS[cmd]) {
  const lines = readFileSync(fileURLToPath(import.meta.url), 'utf8').split('\n').slice(1);
  console.log(lines.slice(0, lines.findIndex((l) => !l.startsWith('//'))).map((l) => l.replace(/^\/\/ ?/, '')).join('\n'));
  process.exit(cmd ? 2 : 0);
}
process.exit(COMMANDS[cmd]());

// Génération des fichiers du projet firmware.

import { buildLayout } from './layout.js';
import { validateProject } from './validate.js';
import { autoLayout, widgetProps } from './hmi.js';
import { utf8Length } from './hash.js';
import { paramsFileName } from './model.js';
import { compileLogic } from './grafcet.js';
import { generateCpp, patchMainCpp } from './cpp.js';

export { paramsFileName };
export const GENERATOR_TAG = 'généré par PLC Studio';

// Pages effectives : disposition automatique ou disposition éditée par l'utilisateur.
export function effectivePages(project, layout) {
  return project.hmi?.auto !== false || !project.hmi?.pages?.length ? autoLayout(project, layout) : project.hmi.pages;
}

export function generateConfig(project, layout, pages) {
  const o = project.options || {};
  const passwords = { pages: {} };
  for (const p of pages) if (p.password) passwords.pages[p.name] = String(p.password);
  if (project.editorPassword) passwords.editor = String(project.editorPassword);

  const wifi = (project.wifi || []).map((w) => {
    if ((w.mode || 'station') === 'access_point') {
      const ap = { name: w.name, mode: 'access_point' };
      for (const k of ['ap_name', 'ap_password', 'ap_ip']) if (w[k]) ap[k] = w[k];
      if (w.ap_channel !== undefined && w.ap_channel !== '') ap.ap_channel = Number(w.ap_channel);
      return ap;
    }
    const st = { name: w.name, ssid: w.ssid, pwd: w.pwd, dhcp: w.dhcp !== false };
    if (w.dhcp === false) {
      for (const k of ['ip', 'gateway', 'mask']) st[k] = w[k];
      if (w.dns) st.dns = w.dns;
    }
    return st;
  });

  const children = layout.sections.map((s) => {
    const section = { name: s.name, hardware: s.hardware, type: s.type };
    if (s.address !== undefined) section.address = s.address;
    section.children = s.nodes.map((n) => ({ id: n.id, name: n.name, hash: n.hash, ...(n.extra || {}) }));
    return section;
  });

  return {
    name: project.hostname,
    type: 2,
    // Nouvelle clé : "hmi" = la logique attend qu'un écran soit connecté (comportement
    // actuel du firmware), "headless" = la logique tourne sans écran. Le firmware ne la
    // lit pas encore (voir README de PLC Studio).
    execution_mode: project.execution?.mode || 'hmi',
    'log on diagnostic file': o.logDiagnostic !== false,
    'delete diagnostic file at startup': !!o.deleteDiagnosticAtStartup,
    'show success messages': !!o.showSuccessMessages,
    'show info messages': o.showInfoMessages !== false,
    'log historry and transition': !!o.logHistory,
    filteredMessages: Array.isArray(o.filteredMessages) ? o.filteredMessages : [],
    editable_files: ['*.json'],
    ignore_files: ['index.html', 'index.js', 'renderers.json'],
    default_editor_file: 'interface.json',
    with_plc_enable_interface: true,
    security: { passwords },
    config: [{ Wifi: wifi }, { parameters: { ota_url: project.otaUrl || '' } }],
    children,
  };
}

export function generateInterface(project, layout, pages) {
  const varsByUid = new Map(project.variables.map((v) => [v.uid, v]));
  return {
    pages: pages.map((p) => ({
      PageName: p.name,
      sections: p.sections.map((s) => ({
        SectionName: s.name,
        orientation: s.orientation || 'horizontal',
        NodesList: s.items
          .filter((it) => layout.nodes.has(it.ref))
          .map((it) => {
            const node = layout.nodes.get(it.ref);
            const variable = node.variable ? varsByUid.get(node.variable) : null;
            const entry = { name: it.label, component: it.widget, hash: node.hash, ...widgetProps(it, variable) };
            // Un "inverse" présent dans config.json se propagerait au voyant : on l'annule.
            if (node.extra?.inverse === true || Array.isArray(node.extra?.inverse)) entry.inverse = false;
            if (variable?.comment) entry.tooltip = variable.comment;
            return entry;
          }),
      })),
    })),
  };
}

export function generateParams(project) {
  const out = { 'machine name': project.name };
  for (const v of project.variables) {
    if (v.kind !== 'parameter') continue;
    let value = v.initial;
    if (v.dataType === 'bool') value = value === true || value === 'true';
    else if (v.dataType === 'int') value = Math.trunc(Number(value) || 0);
    else if (v.dataType === 'float') value = Number(value) || 0;
    else value = String(value ?? '');
    out[v.symbol] = value;
  }
  return out;
}

export function generateHardwareVariant(variant, baseDoc) {
  const doc = JSON.parse(JSON.stringify(baseDoc));
  doc.hardware = variant.name;
  if (variant.removeRtc && doc.I2C) delete doc.I2C.RTC;
  return doc;
}

const json = (obj) => JSON.stringify(obj, null, 2) + '\n';

// ctx.hardwareFiles : { <nom>: document JSON } lus dans data/hardware/
export function generateFiles(project, ctx = {}) {
  const layout = buildLayout(project);
  const pages = effectivePages(project, layout);
  const issues = validateProject({ ...project, hmi: { ...project.hmi, pages } }, layout);
  const files = [];

  files.push({ path: 'data/config.json', content: json(generateConfig(project, layout, pages)), kind: 'config' });
  files.push({ path: 'data/interface.json', content: json(generateInterface(project, layout, pages)), kind: 'interface' });
  files.push({ path: `data/${paramsFileName(project)}`, content: json(generateParams(project)), kind: 'params' });

  for (const variant of layout.hardwareVariants) {
    const base = ctx.hardwareFiles?.[variant.base];
    if (!base) {
      issues.push({ level: 'error', step: 'equipment', message: `Fichier data/hardware/${variant.base}.json introuvable.` });
      continue;
    }
    files.push({ path: `data/hardware/${variant.name}.json`, content: json(generateHardwareVariant(variant, base)), kind: 'hardware' });
  }

  // Vérification croisée avec les fichiers matériels réels : chaque id doit exister.
  if (ctx.hardwareFiles) {
    for (const s of layout.sections) {
      if (s.hardware === 'virtual') continue;
      const baseName = layout.hardwareVariants.find((v) => v.name === s.hardware)?.base || s.hardware;
      const doc = ctx.hardwareFiles[baseName];
      if (!doc) {
        issues.push({ level: 'error', step: 'equipment', message: `Fichier data/hardware/${baseName}.json introuvable.` });
        continue;
      }
      const entries = doc[s.type];
      if (!Array.isArray(entries)) {
        issues.push({ level: 'error', step: 'equipment', message: `data/hardware/${baseName}.json ne décrit pas « ${s.type} ».` });
        continue;
      }
      for (const n of s.nodes) {
        if (!entries.some((e) => e.id === n.id)) {
          issues.push({ level: 'error', step: 'equipment', message: `data/hardware/${baseName}.json : id ${n.id} absent de « ${s.type} ».` });
        }
      }
    }
    const firstHw = layout.sections.find((s) => s.hardware !== 'virtual');
    if (firstHw) {
      const baseName = layout.hardwareVariants.find((v) => v.name === firstHw.hardware)?.base || firstHw.hardware;
      if (ctx.hardwareFiles[baseName] && !ctx.hardwareFiles[baseName].RS485) {
        issues.push({ level: 'error', step: 'equipment', message: `La première section matérielle (${firstHw.name}) doit utiliser un fichier avec un bloc RS485.` });
      }
    }
  }

  // Logique et code C++
  const { ir, issues: logicIssues } = compileLogic(project);
  issues.push(...logicIssues);
  if (!logicIssues.some((i) => i.level === 'error') && !issues.some((i) => i.level === 'error' && i.step !== 'logic')) {
    const cpp = generateCpp(project, layout, ir);
    files.push({ path: `src/${project.name}/${project.name}.h`, content: cpp.header, kind: 'cpp' });
    files.push({ path: `src/${project.name}/${project.name}.cpp`, content: cpp.source, kind: 'cpp' });
    if (ctx.mainCpp) {
      const patch = patchMainCpp(ctx.mainCpp, project.name);
      if (!patch.ok) issues.push({ level: 'error', step: 'generate', message: patch.message });
      else if (patch.changed) files.push({ path: 'src/main.cpp', content: patch.content, kind: 'main', replaces: patch.oldClass });
    }
  }

  const paramsPath = `/${paramsFileName(project)}`;
  if (utf8Length(paramsPath) >= 32) issues.push({ level: 'error', step: 'project', message: `Le chemin ${paramsPath} doit faire moins de 32 caractères.` });

  // L'installation dans le dépôt n'est possible que si la classe C++ et main.cpp sont générés :
  // sinon l'application actuelle ne trouverait plus ses nœuds et l'automate bloquerait au démarrage.
  const installable = files.some((f) => f.kind === 'cpp');
  return { files, issues, layout, pages, installable, ir };
}

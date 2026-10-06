#!/usr/bin/env node
// PLC Studio — serveur local (aucune dépendance npm).
//
//   node tools/plc-studio/server/server.js [--port 8090] [--root <dépôt PLC>]
//
// Sert l'interface (public/), les modules partagés (shared/) et une petite API
// pour lire/écrire les projets (projets/*.plc.json) et générer les fichiers firmware.

import http from 'node:http';
import { readFile, writeFile, mkdir, readdir, stat, copyFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { generateFiles } from '../shared/generate.js';
import { normalizeProject } from '../shared/model.js';
import { parseJsonc } from '../shared/jsonc.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const toolDir = path.resolve(here, '..');

function arg(name, def) {
  const i = process.argv.indexOf(`--${name}`);
  return i > 0 && process.argv[i + 1] ? process.argv[i + 1] : def;
}

const PORT = Number(arg('port', process.env.PLC_STUDIO_PORT || 8090));
const HOST = arg('host', '127.0.0.1');
const ROOT = path.resolve(arg('root', path.resolve(toolDir, '..', '..')));
const PROJECTS_DIR = path.join(ROOT, 'projets');
const BACKUP_DIR = path.join(toolDir, '.sauvegardes');

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
};

function send(res, status, body, type = 'application/json; charset=utf-8') {
  const data = typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body);
  res.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store' });
  res.end(data);
}

async function readBody(req) {
  const chunks = [];
  let size = 0;
  for await (const c of req) {
    size += c.length;
    if (size > 20 * 1024 * 1024) throw new Error('Requête trop volumineuse');
    chunks.push(c);
  }
  const text = Buffer.concat(chunks).toString('utf8');
  return text ? JSON.parse(text) : {};
}

// Empêche toute sortie du dossier autorisé.
function safeJoin(base, rel) {
  const target = path.resolve(base, rel);
  if (target !== base && !target.startsWith(base + path.sep)) throw new Error(`Chemin refusé : ${rel}`);
  return target;
}

const PROJECT_NAME = /^[A-Za-z][A-Za-z0-9_]{0,23}$/;

async function readHardwareFiles() {
  const dir = path.join(ROOT, 'data', 'hardware');
  const out = {};
  if (!existsSync(dir)) return out;
  for (const f of await readdir(dir)) {
    if (!f.endsWith('.json')) continue;
    try {
      out[f.slice(0, -5)] = parseJsonc(await readFile(path.join(dir, f), 'utf8'));
    } catch (e) {
      out[f.slice(0, -5)] = { __error: e.message };
    }
  }
  return out;
}

// Classe d'application actuellement utilisée par src/main.cpp.
async function detectCurrentApp() {
  try {
    const main = await readFile(path.join(ROOT, 'src', 'main.cpp'), 'utf8');
    const m = main.match(/=\s*new\s+([A-Za-z_]\w*)\s*\(\s*\)\s*;[^\n]*\n[\s\S]*?setBusinessLogic/);
    return m ? m[1] : null;
  } catch {
    return null;
  }
}

async function generationContext() {
  const ctx = { hardwareFiles: await readHardwareFiles(), currentApp: await detectCurrentApp() };
  try {
    ctx.mainCpp = await readFile(path.join(ROOT, 'src', 'main.cpp'), 'utf8');
  } catch {
    ctx.mainCpp = null;
  }
  return ctx;
}

// Fichiers que la génération a le droit d'écrire.
function allowedOutput(rel, project) {
  const p = rel.replace(/\\/g, '/');
  return (
    p === 'data/config.json' ||
    p === 'data/interface.json' ||
    p === `data/${project.name}.json` ||
    /^data\/hardware\/[A-Za-z0-9_]+\.json$/.test(p) ||
    p.startsWith(`src/${project.name}/`) ||
    p === 'src/main.cpp'
  );
}

async function preview(project) {
  const ctx = await generationContext();
  const result = generateFiles(project, ctx);
  const files = [];
  for (const f of result.files) {
    const abs = safeJoin(ROOT, f.path);
    let current = null;
    if (existsSync(abs)) current = await readFile(abs, 'utf8');
    files.push({ ...f, current, status: current === null ? 'nouveau' : current === f.content ? 'identique' : 'modifié' });
    // Ne jamais écraser une classe écrite à la main (ex. src/RessortRoyal2/).
    if (f.kind === 'cpp' && current !== null && !current.includes('généré par PLC Studio')) {
      result.issues.push({
        level: 'error',
        step: 'project',
        message: `${f.path} existe déjà et n'a pas été généré par PLC Studio : choisissez un autre nom de projet.`,
      });
    }
  }
  return {
    files,
    issues: result.issues,
    nodeCount: result.layout.nodeCount,
    currentApp: ctx.currentApp,
    installable: result.installable,
    exportDir: path.relative(ROOT, exportDir(project)),
  };
}

function exportDir(project) {
  return path.join(PROJECTS_DIR, project.name, 'generation');
}

// target = "export" : copie dans projets/<Nom>/generation/ (sans toucher au firmware)
// target = "install" : écriture dans le dépôt, avec sauvegarde des fichiers remplacés
async function apply(project, only, target = 'export') {
  const { files, issues, installable } = await preview(project);
  if (issues.some((i) => i.level === 'error')) {
    return { ok: false, issues, message: 'Le projet contient des erreurs : génération annulée.' };
  }
  if (target === 'export') {
    const base = exportDir(project);
    const written = [];
    for (const f of files) {
      if (only && !only.includes(f.path)) continue;
      const abs = safeJoin(base, f.path);
      await mkdir(path.dirname(abs), { recursive: true });
      await writeFile(abs, f.content, 'utf8');
      written.push(path.relative(ROOT, abs));
    }
    return { ok: true, written, backup: null, issues, target };
  }
  if (!installable) {
    return { ok: false, issues, message: "Installation impossible : le code C++ du projet n'est pas encore généré (l'automate ne démarrerait plus)." };
  }
  const stamp = new Date().toISOString().replace(/[:.]/g, '-');
  const backupRoot = path.join(BACKUP_DIR, stamp);
  const written = [];
  for (const f of files) {
    if (only && !only.includes(f.path)) continue;
    if (!allowedOutput(f.path, project)) throw new Error(`Écriture refusée : ${f.path}`);
    if (f.status === 'identique') continue;
    const abs = safeJoin(ROOT, f.path);
    if (f.current !== null) {
      const backup = safeJoin(backupRoot, f.path);
      await mkdir(path.dirname(backup), { recursive: true });
      await copyFile(abs, backup);
    }
    await mkdir(path.dirname(abs), { recursive: true });
    await writeFile(abs, f.content, 'utf8');
    written.push(f.path);
  }
  return { ok: true, written, backup: written.length ? path.relative(ROOT, backupRoot) : null, issues, target };
}

async function listProjects() {
  if (!existsSync(PROJECTS_DIR)) return [];
  const out = [];
  for (const f of await readdir(PROJECTS_DIR)) {
    if (!f.endsWith('.plc.json')) continue;
    const s = await stat(path.join(PROJECTS_DIR, f));
    out.push({ name: f.slice(0, -'.plc.json'.length), file: `projets/${f}`, modified: s.mtime.toISOString() });
  }
  return out.sort((a, b) => b.modified.localeCompare(a.modified));
}

async function handleApi(req, res, url) {
  const parts = url.pathname.split('/').filter(Boolean).slice(1); // sans "api"
  const method = req.method;

  if (parts[0] === 'info' && method === 'GET') {
    const hw = await readHardwareFiles();
    return send(res, 200, {
      root: ROOT,
      currentApp: await detectCurrentApp(),
      hardware: Object.keys(hw),
      projectsDir: path.relative(ROOT, PROJECTS_DIR),
    });
  }
  if (parts[0] === 'hardware' && method === 'GET') return send(res, 200, await readHardwareFiles());

  if (parts[0] === 'projects') {
    if (parts.length === 1 && method === 'GET') return send(res, 200, await listProjects());
    const name = decodeURIComponent(parts[1] || '');
    if (!PROJECT_NAME.test(name)) return send(res, 400, { error: 'Nom de projet invalide.' });
    const file = path.join(PROJECTS_DIR, `${name}.plc.json`);
    if (method === 'GET') {
      if (!existsSync(file)) return send(res, 404, { error: 'Projet introuvable.' });
      return send(res, 200, normalizeProject(JSON.parse(await readFile(file, 'utf8'))));
    }
    if (method === 'PUT') {
      const body = await readBody(req);
      const project = normalizeProject(body);
      if (project.name !== name) return send(res, 400, { error: 'Le nom du projet ne correspond pas au fichier.' });
      await mkdir(PROJECTS_DIR, { recursive: true });
      await writeFile(file, JSON.stringify(project, null, 2) + '\n', 'utf8');
      return send(res, 200, { ok: true, file: path.relative(ROOT, file) });
    }
  }

  if (parts[0] === 'preview' && method === 'POST') {
    const { project } = await readBody(req);
    return send(res, 200, await preview(normalizeProject(project)));
  }
  if (parts[0] === 'apply' && method === 'POST') {
    const { project, files, target } = await readBody(req);
    return send(res, 200, await apply(normalizeProject(project), files, target === 'install' ? 'install' : 'export'));
  }
  return send(res, 404, { error: 'Route inconnue.' });
}

async function handleStatic(req, res, url) {
  let rel = decodeURIComponent(url.pathname);
  let base = path.join(toolDir, 'public');
  if (rel.startsWith('/shared/')) {
    base = path.join(toolDir, 'shared');
    rel = rel.slice('/shared/'.length);
  } else if (rel === '/' || rel === '') {
    rel = 'index.html';
  }
  const file = safeJoin(base, rel.replace(/^\/+/, ''));
  if (!existsSync(file) || !(await stat(file)).isFile()) return send(res, 404, 'Introuvable', 'text/plain; charset=utf-8');
  send(res, 200, await readFile(file), MIME[path.extname(file)] || 'application/octet-stream');
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  try {
    if (url.pathname.startsWith('/api/')) await handleApi(req, res, url);
    else await handleStatic(req, res, url);
  } catch (e) {
    console.error(e);
    send(res, 500, { error: e.message });
  }
});

server.listen(PORT, HOST, () => {
  console.log(`PLC Studio : http://${HOST === '0.0.0.0' ? 'localhost' : HOST}:${PORT}`);
  console.log(`Dépôt PLC  : ${ROOT}`);
});

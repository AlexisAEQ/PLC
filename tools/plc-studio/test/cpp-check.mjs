#!/usr/bin/env node
// Vérification de syntaxe du C++ généré (g++ -fsyntax-only) contre les vrais en-têtes du
// dépôt et des en-têtes Arduino/ESP32 simplifiés (test/cpp-stubs).
//
//   npm run check:cpp
//
// Ce n'est pas une compilation ESP32 complète : elle détecte les erreurs de types, de noms de
// méthodes et de syntaxe dans les classes générées. Pour une validation complète, compiler avec
// PlatformIO (pio run -e quatre_mb_huge) après installation du projet.

import { execFileSync, spawnSync } from 'node:child_process';
import { cpSync, existsSync, mkdtempSync, mkdirSync, readdirSync, readFileSync, rmSync, symlinkSync, writeFileSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { generateFiles } from '../shared/generate.js';
import { pressProject, hardwareFiles, mainCpp, repo } from './fixtures.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const compiler = process.env.CXX || 'g++';
try {
  execFileSync(compiler, ['--version'], { stdio: 'ignore' });
} catch {
  console.log(`${compiler} introuvable : vérification C++ ignorée.`);
  process.exit(0);
}

// En-tête ArduinoJson : bibliothèque PlatformIO du dépôt, sinon téléchargement de la version 7.
function findArduinoJson(dir) {
  const libdeps = path.join(repo, '.pio', 'libdeps');
  if (existsSync(libdeps)) {
    for (const env of readdirSync(libdeps)) {
      const p = path.join(libdeps, env, 'ArduinoJson', 'src');
      if (existsSync(path.join(p, 'ArduinoJson.h'))) return p;
    }
  }
  const out = path.join(dir, 'libs');
  mkdirSync(out, { recursive: true });
  const url = 'https://github.com/bblanchon/ArduinoJson/releases/download/v7.4.2/ArduinoJson-v7.4.2.h';
  execFileSync('curl', ['-sSL', '-o', path.join(out, 'ArduinoJson.h'), url]);
  if (!readFileSync(path.join(out, 'ArduinoJson.h'), 'utf8').includes('ArduinoJson')) throw new Error('Téléchargement ArduinoJson impossible');
  return out;
}

// Les includes du dépôt ne respectent pas toujours la casse des fichiers (audit MA-29) :
// on crée des liens dans la copie temporaire pour compiler sous Linux.
function fixIncludeCase(src) {
  const includes = new Set();
  const walk = (d) => {
    for (const f of readdirSync(d)) {
      const p = path.join(d, f);
      if (statSync(p).isDirectory()) walk(p);
      else if (/\.(h|hpp|cpp|inl)$/.test(f)) for (const m of readFileSync(p, 'utf8').matchAll(/#include\s+"([^"]+\/[^"]+)"/g)) includes.add(m[1]);
    }
  };
  walk(src);
  for (const inc of includes) {
    if (existsSync(path.join(src, inc))) continue;
    const [dir, file] = [path.dirname(inc), path.basename(inc)];
    const realDir = readdirSync(src).find((d) => d.toLowerCase() === dir.toLowerCase());
    if (!realDir) continue;
    const realFile = readdirSync(path.join(src, realDir)).find((f) => f.toLowerCase() === file.toLowerCase());
    if (!realFile) continue;
    if (!existsSync(path.join(src, dir))) symlinkSync(realDir, path.join(src, dir));
    if (!existsSync(path.join(src, dir, file))) symlinkSync(realFile, path.join(src, dir, file));
  }
}

const work = mkdtempSync(path.join(tmpdir(), 'plc-studio-cpp-'));
let failed = false;
try {
  const ajDir = findArduinoJson(work);
  for (const variant of [{ servo: true }, { servo: false }]) {
    const dir = path.join(work, variant.servo ? 'servo' : 'sans-servo');
    cpSync(path.join(repo, 'src'), path.join(dir, 'src'), { recursive: true });
    fixIncludeCase(path.join(dir, 'src'));
    const project = pressProject(variant);
    const r = generateFiles(project, { hardwareFiles: hardwareFiles(), mainCpp: mainCpp() });
    const errors = r.issues.filter((i) => i.level === 'error');
    if (errors.length) throw new Error('Projet exemple invalide : ' + errors.map((e) => e.message).join('; '));
    for (const f of r.files.filter((x) => x.kind === 'cpp')) {
      mkdirSync(path.dirname(path.join(dir, f.path)), { recursive: true });
      writeFileSync(path.join(dir, f.path), f.content);
    }
    const cpp = path.join('src', project.name, `${project.name}.cpp`);
    const run = spawnSync(
      compiler,
      ['-std=gnu++17', '-fsyntax-only', '-frtti', '-Wall', '-Wextra', '-DARDUINOJSON_ENABLE_COMMENTS=1', '-I', 'src', '-I', path.join(here, 'cpp-stubs'), '-I', ajDir, cpp],
      { cwd: dir, encoding: 'utf8' }
    );
    const output = (run.stdout || '') + (run.stderr || '');
    if (run.status !== 0) failed = true;
    // Seuls les messages qui concernent le code généré nous intéressent.
    const own = output.split('\n').filter((l) => l.startsWith(`src/${project.name}/`) && /(error|warning):/.test(l));
    if (own.length) failed = true;
    console.log(`${variant.servo ? 'Avec servo' : 'Sans servo'} : ${own.length ? 'ÉCHEC' : 'OK'}`);
    for (const l of own) console.log('  ' + l);
    if (failed && !own.length) console.log(output.split('\n').filter((l) => /error/.test(l)).slice(0, 20).join('\n'));
  }
} finally {
  rmSync(work, { recursive: true, force: true });
}
process.exit(failed ? 1 : 0);

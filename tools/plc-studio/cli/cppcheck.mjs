// Vérification de syntaxe du C++ généré (g++ -fsyntax-only) contre les vrais en-têtes du
// dépôt et des en-têtes Arduino/ESP32 simplifiés (test/cpp-stubs).
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

const toolDir = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const stubs = path.join(toolDir, 'test', 'cpp-stubs');

export function compilerAvailable(compiler = process.env.CXX || 'g++') {
  try {
    execFileSync(compiler, ['--version'], { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
}

// En-tête ArduinoJson : bibliothèque PlatformIO du dépôt, sinon téléchargement de la version 7.
function findArduinoJson(repo, dir) {
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

// items : [{ label, project }]. Retourne null si aucun compilateur, sinon [{ label, ok, messages }].
export function checkCppSyntax(items, { repo, hardwareFiles, mainCpp, compiler = process.env.CXX || 'g++' } = {}) {
  if (!compilerAvailable(compiler)) return null;
  const work = mkdtempSync(path.join(tmpdir(), 'plc-studio-cpp-'));
  const results = [];
  try {
    const ajDir = findArduinoJson(repo, work);
    for (const { label, project } of items) {
      const dir = path.join(work, String(label).replace(/\W+/g, '-') || 'projet');
      cpSync(path.join(repo, 'src'), path.join(dir, 'src'), { recursive: true });
      fixIncludeCase(path.join(dir, 'src'));
      const r = generateFiles(project, { hardwareFiles, mainCpp });
      const errors = r.issues.filter((i) => i.level === 'error');
      if (errors.length) {
        results.push({ label, ok: false, messages: errors.map((e) => `projet invalide : ${e.message}`) });
        continue;
      }
      for (const f of r.files.filter((x) => x.kind === 'cpp')) {
        mkdirSync(path.dirname(path.join(dir, f.path)), { recursive: true });
        writeFileSync(path.join(dir, f.path), f.content);
      }
      const cpp = path.join('src', project.name, `${project.name}.cpp`);
      const run = spawnSync(
        compiler,
        ['-std=gnu++17', '-fsyntax-only', '-frtti', '-Wall', '-Wextra', '-DARDUINOJSON_ENABLE_COMMENTS=1', '-I', 'src', '-I', stubs, '-I', ajDir, cpp],
        { cwd: dir, encoding: 'utf8' }
      );
      const output = (run.stdout || '') + (run.stderr || '');
      // Seuls les messages qui concernent le code généré nous intéressent.
      const own = output.split('\n').filter((l) => l.startsWith(`src/${project.name}/`) && /(error|warning):/.test(l));
      const messages = own.length ? own : run.status !== 0 ? output.split('\n').filter((l) => /error/.test(l)).slice(0, 20) : [];
      results.push({ label, ok: run.status === 0 && !own.length, messages });
    }
  } finally {
    rmSync(work, { recursive: true, force: true });
  }
  return results;
}

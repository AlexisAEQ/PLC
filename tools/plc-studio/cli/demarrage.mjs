// À importer EN PREMIER par les points d'entrée (server.js, plc.mjs), avant tout module de shared/ :
//
// 1. Référentiel du dépôt choisi par --root : si <root>/infrastructure.jsonc existe (et que
//    PLC_REFERENTIEL n'est pas déjà fixé), c'est lui que lisent le générateur, la validation, la
//    CLI et le serveur. Sinon, celui du dépôt qui contient l'outil (un avertissement le dit).
// 2. Un référentiel invalide arrête le programme avec un message lisible (fichier:ligne, sans
//    pile JavaScript) et le code de sortie 3. Les autres erreurs gardent l'affichage de Node.
import { existsSync } from 'node:fs';
import path from 'node:path';

const i = process.argv.indexOf('--root');
if (i > 0 && process.argv[i + 1] && !process.env.PLC_REFERENTIEL) {
  const candidat = path.join(path.resolve(process.argv[i + 1]), 'infrastructure.jsonc');
  if (existsSync(candidat)) process.env.PLC_REFERENTIEL = candidat;
  else process.stderr.write(`Attention : ${candidat} absent ; référentiel de l'outil utilisé.\n`);
}

process.on('uncaughtExceptionMonitor', (e) => {
  if (e?.name === 'ErreurReferentiel') {
    process.stderr.write(`${e.message}\n`);
    process.exit(3);
  }
});

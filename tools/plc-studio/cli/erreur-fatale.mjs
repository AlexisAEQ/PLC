// À importer EN PREMIER par les points d'entrée (server.js, plc.mjs) : un référentiel
// d'infrastructure invalide (infrastructure.jsonc) arrête le programme avec un message lisible
// (fichier:ligne, sans pile JavaScript) et le code de sortie 3.
process.on('uncaughtException', (e) => {
  if (e?.name === 'ErreurReferentiel') {
    process.stderr.write(`${e.message}\n`);
    process.exit(3);
  }
  process.stderr.write(`${e?.stack || e}\n`);
  process.exit(1);
});

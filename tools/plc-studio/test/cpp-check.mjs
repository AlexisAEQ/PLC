#!/usr/bin/env node
// Vérification de syntaxe du C++ généré (g++ -fsyntax-only) pour des projets d'exemple.
//
//   npm run check:cpp
//
// Voir cli/cppcheck.mjs. Ce n'est pas une compilation ESP32 complète : pour une validation
// complète, compiler avec PlatformIO (pio run -e quatre_mb_huge) après installation du projet.

import { checkCppSyntax } from '../cli/cppcheck.mjs';
import { pressProject, multiAxisProject, robotCellProject, hardwareFiles, mainCpp, repo } from './fixtures.js';

// Variante « noms piégeux » : symbole qui est une macro du framework, course non entière.
const tricky = (p) => {
  p.variables.find((v) => v.symbol === 'Pieces').symbol = 'STATUS';
  p.logic.steps[4].actions[0].target = 'STATUS';
  p.equipment.find((e) => e.type === 'SureServo').options.maxRange = 24.5;
  return p;
};

const results = checkCppSyntax(
  [
    { label: 'Avec servo', project: pressProject({ servo: true }) },
    { label: 'Sans servo', project: pressProject({ servo: false }) },
    { label: 'Noms piégeux', project: tricky(pressProject({ servo: true })) },
    { label: 'Axes multiples (2 SureServo + pas-à-pas)', project: multiAxisProject() },
    { label: 'Axes multiples + Lichuan A6 et A5', project: multiAxisProject({ lichuan: true }) },
    { label: 'Cellule robot (Modbus TCP, Ethernet)', project: robotCellProject() },
  ],
  { repo, hardwareFiles: hardwareFiles(), mainCpp: mainCpp() }
);
if (!results) {
  console.log('g++ introuvable : vérification C++ ignorée.');
  process.exit(0);
}
let failed = false;
for (const r of results) {
  console.log(`${r.label} : ${r.ok ? 'OK' : 'ÉCHEC'}`);
  for (const m of r.messages) console.log('  ' + m);
  if (!r.ok) failed = true;
}
process.exit(failed ? 1 : 0);

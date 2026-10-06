// Étape 6 : simulation (prévue en phase 3).

import { h, card, pageHead } from '../ui.js';

export function renderSimulation(root) {
  root.append(
    pageHead('Simulation', 'Exécution du grafcet dans le navigateur : forçage des entrées, visualisation des sorties, des étapes actives et du servo.'),
    card('En cours de développement', h('p', {}, 'Le simulateur arrive après l’éditeur grafcet.'))
  );
}

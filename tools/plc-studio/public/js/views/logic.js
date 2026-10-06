// Étape 4 : logique grafcet (éditeur prévu en phase 2).

import { h, card, pageHead, button } from '../ui.js';

export function renderLogic(root, store) {
  root.append(
    pageHead('Logique (grafcet)', 'Éditeur visuel de grafcet : étapes, transitions, divergences ET/OU, actions et blocs prédéfinis (arrêt d’urgence, servo, prise d’origine, jog).'),
    card(
      'En cours de développement',
      h('p', {}, 'L’éditeur grafcet et la génération du code C++ arrivent dans la prochaine livraison.'),
      h('p', { class: 'muted' }, `Variables disponibles pour la logique : ${store.project.variables.length}.`),
      button('Compléter les variables', () => store.go('variables'), 'small')
    )
  );
}

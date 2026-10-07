---
name: plc-projet
description: Crée ou modifie un projet d'automate conforme pour le framework PLC ESP32 de ce dépôt (BorneUniverselle, automate Kincony KC868-A8S, modules Waveshare, servo SureServo) à partir du comportement souhaité d'une machine — E/S câblées, variables, grafcet, arrêt d'urgence et modes de marche, écrans opérateur — et le valide par des scénarios de simulation. Produit projets/<Nom>.plc.json, qui s'ouvre dans PLC Studio pour peaufiner les écrans et rejouer la logique. Utiliser dès que l'utilisateur décrit une machine, un cycle, une séquence ou un automatisme à programmer pour cet automate (« crée-moi un projet pour… », « fais la logique de… », « grafcet pour… », « une ensacheuse / un convoyeur / une presse qui… »), veut générer config.json / interface.json ou une classe d'application qui remplace RessortRoyal2, ou demande de modifier la logique, le câblage ou les écrans d'un projet de projets/ — même sans nommer PLC Studio.
---

# Projet PLC Studio à partir d'un comportement

Ce skill transforme une description de machine en projet **PLC Studio** prêt à ouvrir
(`projets/<Nom>.plc.json`), avec la logique déjà validée en simulation. L'utilisateur
l'ouvre ensuite dans PLC Studio pour retoucher les écrans, rejouer la simulation et, quand
il est satisfait, « Installer » puis compiler.

On n'écrit pas le `.plc.json` à la main : on écrit un **cahier des charges compact (spec)**
et la ligne de commande de PLC Studio le convertit avec le même code que l'interface. C'est
ce qui garantit un projet conforme (identifiants de nœuds, sections, règles du firmware,
C++ généré compilable) et un grafcet bien placé dans l'éditeur.

## Outils

Depuis la racine du dépôt (Node.js ≥ 18, aucune dépendance) :

| Commande `node tools/plc-studio/cli/plc.mjs …` | Rôle |
|---|---|
| `catalog` | Équipements disponibles et noms exacts des voies (`plc.X1`, `io.DI3`…). |
| `build <spec.json> [--cpp] [--force] [--verbose]` | Spec → `projets/<Nom>.plc.json` + `projets/<Nom>/spec.json` + `projets/<Nom>/scenarios.json`, puis vérification complète, syntaxe C++ (`--cpp`) et scénarios. Refuse d'écraser un projet existant sans `--force`. |
| `check <Nom> [--cpp]` | Vérification d'un projet (y compris retouché dans PLC Studio). |
| `sim <Nom> [--verbose]` | Rejoue `projets/<Nom>/scenarios.json`. |
| `describe <Nom> [--md]` | Résumé lisible : E/S, variables, grafcet, modes, écrans. |
| `export-spec <Nom> --out <f>` | Projet existant → spec (garde écrans et positions du grafcet). |

Références à lire selon le besoin :
- `references/spec.md` — format complet de la spec et des scénarios (à lire avant d'en écrire une) ;
- `references/logique.md` — modes de marche, cycle, langage, servo, patrons (arrêt
  d'urgence, marche/arrêt, défauts, temporisations…) ;
- `examples/tri-colis.spec.json` (convoyeur, sans servo, écrans explicites, défaut de
  bourrage) et `examples/presse-servo.spec.json` (servo, divergence ET).

## Démarche

### 1. Comprendre le comportement

Relever dans la demande : équipements, ce qui est câblé (capteurs, boutons, actionneurs),
commandes et réglages à l'écran, séquence normale, cas particuliers (pièce absente, rebut,
défaut, temporisations, comptages), sécurité (arrêt d'urgence, acquittement), mise en
marche / arrêt, servo (positions, vitesses, prise d'origine, jog).

Ce qui manque se complète par des choix raisonnables plutôt que par une rafale de
questions : voies affectées dans l'ordre (entrées `X1…`, relais `Y1…`, puis un module
Waveshare 8DIO si plus de 8 E/S ; signaux du servo sur les derniers relais), acquittement
par une commande d'écran, temporisations en paramètres réglables, écrans automatiques.
Chaque choix fait à la place de l'utilisateur est noté pour le rapport. Ne poser une
question que si elle change la logique de sécurité ou le principe du cycle et qu'aucune
hypothèse n'est raisonnable — et alors une seule fois, en regroupant.

### 2. Écrire la spec

1. `node tools/plc-studio/cli/plc.mjs catalog` pour les noms de voies.
2. Lire `references/spec.md` ; partir de l'exemple le plus proche.
3. Écrire la spec dans le dossier temporaire de la session (le `build` en range une copie
   dans `projets/<Nom>/spec.json`). Points d'attention :
   - un symbole court et parlant par signal (`Cel_entree`, `Verin_sorti`), un libellé
     lisible par l'opérateur ;
   - contacts NF (arrêt d'urgence, bouton arrêt) : la valeur logique doit être « vraie =
     situation normale » (`AU_OK`) ou `inverse: true` pour « vraie = actionné » ;
   - `fallback` des sorties pensé pour l'urgence ;
   - réceptivités exclusives dans les divergences OU, fronts (`FM`) pour les boutons ;
   - défauts process gérés dans le grafcet, URGENCE réservée à la sécurité
     (`references/logique.md`, « Patrons »).
4. Ajouter les **scénarios** : cycle nominal complet, chaque branche, arrêt d'urgence +
   acquittement, arrêt, chaque exigence particulière (temporisation, comptage, défaut,
   servo). Un scénario vérifie ce que la demande exige, pas ce que la logique fait.

### 3. Construire et valider

```bash
node tools/plc-studio/cli/plc.mjs build <spec.json> --cpp
```

- Corriger toutes les **erreurs** (spec ou PLC Studio) et relancer avec `--force`. Les
  **avertissements** se corrigent ou se justifient dans le rapport.
- Un scénario en échec : relire l'exigence. Si la logique est fausse, corriger la logique ;
  si le scénario se trompe (délai trop court, mauvaise hypothèse sur les modes), corriger
  le scénario en le justifiant. Ne jamais affaiblir un scénario juste pour qu'il passe.
- `--cpp` vérifie la syntaxe du C++ généré avec g++ (si disponible).

### 4. Documenter

Écrire `projets/<Nom>/LISEZMOI.md` :
- la sortie de `plc.mjs describe <Nom> --md` (équipements, câblage, variables, grafcet, modes, écrans) ;
- une section **Hypothèses** (tous les choix faits à la place de l'utilisateur) ;
- une section **Scénarios validés** (nom et ce que chacun prouve) ;
- une section **À vérifier sur la machine** (sens des capteurs, temporisations réelles,
  positions du servo, limites du framework concernées — voir `references/logique.md`).

### 5. Remettre le projet

Ne pas « installer » (écrire dans `data/` ou `src/`) : l'utilisateur valide d'abord dans
PLC Studio. Dans un dépôt git, committer `projets/<Nom>.plc.json` et `projets/<Nom>/`
(et pousser si c'est le mode de travail de la session).

Rapport à l'utilisateur, court :
- ce que fait le projet (3 à 5 lignes), les E/S principales ;
- les hypothèses à confirmer ;
- le résultat : vérification (erreurs/avertissements), C++, scénarios (n/n réussis) ;
- pour l'ouvrir : `node tools/plc-studio/server/server.js`, `http://localhost:8090`,
  **Ouvrir… → <Nom>** ; étape 5 « Écrans opérateur » pour peaufiner l'IHM, étape 6
  « Simulation » pour rejouer la logique, étape 7 « Génération » → Installer, puis
  `pio run -e quatre_mb_huge -t upload` et `-t uploadfs`.

## Modifier un projet existant

Le projet a pu être retouché dans PLC Studio (écrans, positions du grafcet) : ne jamais le
reconstruire depuis une ancienne spec.

1. `plc.mjs export-spec <Nom> --out <tmp>/spec.json` (reflète l'état actuel, écrans
   personnalisés et positions compris) ; `plc.mjs describe <Nom>` pour comprendre.
2. Modifier la spec. Les nouvelles étapes sans `x`/`y` déclenchent la remise en page de
   tout le grafcet : soit leur donner des positions cohérentes (grille de 10, 180 px entre
   deux étapes, transition à mi-hauteur), soit accepter la remise en page et le signaler.
3. `build <spec> --force --cpp`, compléter les scénarios (`projets/<Nom>/scenarios.json`),
   `sim <Nom>`, mettre à jour `LISEZMOI.md`.

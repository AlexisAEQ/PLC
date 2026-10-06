# PLC Studio

Outil de configuration et de génération de projets pour le framework PLC ESP32
(BorneUniverselle / BusinessLogic). Il remplace l'écriture à la main de
`data/config.json`, `data/interface.json` et de la classe d'application
(aujourd'hui `src/RessortRoyal2/`).

## Lancer l'outil

Prérequis : [Node.js](https://nodejs.org) 18 ou plus récent. Aucune dépendance à installer.

```bash
node tools/plc-studio/server/server.js
```

Puis ouvrir <http://localhost:8090> dans un navigateur.

Options :

| Option | Défaut | Rôle |
|---|---|---|
| `--port <n>` | `8090` | port HTTP |
| `--root <dossier>` | racine du dépôt | dépôt PLC à lire/écrire |
| `--host <adresse>` | `127.0.0.1` | interface d'écoute (locale uniquement par défaut) |

Les projets sont enregistrés dans `projets/<Nom>.plc.json` (à la racine du dépôt).
L'enregistrement est automatique après le premier enregistrement (Ctrl+S).

## Étapes

1. **Projet** : nom (classe C++ et fichier de paramètres), nom réseau, mode
   d'exécution, réseaux WiFi, options du journal.
2. **Équipements** : automate Kincony KC868-A8S, modules Waveshare (8DIO, 16DO,
   8AI), servo SureServo 2. L'automate est toujours déclaré en premier (il porte le
   bus RS485, sinon le firmware bloque au démarrage).
3. **Câblage et variables** : on tape ce qui est branché sur chaque voie ; on crée
   les variables internes (commandes, paramètres sauvegardés, indicateurs,
   mémoires). Les signaux du servo (servo ON, arrêt immédiat, reset alarmes) se
   câblent sur des sorties de l'automate.
4. **Logique (grafcet)** : éditeur visuel (étapes, transitions, divergences et
   convergences ET/OU, reprises), actions (continue N, S/R, affectation, compteur,
   servo, message) et onglet « Modes et sécurités » (arrêt d'urgence et
   acquittement, marche/arrêt, prise d'origine, jog, couple du servo).
5. **Écrans opérateur** : disposition automatique ou personnalisée des pages,
   sections et widgets (`interface.json`).
6. **Simulation** : *en développement*.
7. **Génération** : vérification, aperçu des fichiers avec différences, export.

## Fichiers produits

| Fichier | Contenu |
|---|---|
| `data/config.json` | réseau, options, sections et nœuds (identifiants calculés) |
| `data/interface.json` | pages, sections et widgets de l'écran opérateur |
| `data/<Nom>.json` | valeurs initiales des paramètres sauvegardés |
| `data/hardware/Kincony_KC868_A8S_noRTC.json` | variante sans horloge, si l'option est décochée |
| `src/<Nom>/<Nom>.h` et `.cpp` | classe d'application dérivée de `BusinessLogic` (modes de marche + grafcet) |
| `src/main.cpp` | bascule de la classe actuelle (ex. `RessortRoyal2`) vers `<Nom>` |

Deux modes d'écriture :

- **Exporter** : écrit dans `projets/<Nom>/generation/`, sans toucher au firmware.
- **Installer** : écrit dans le dépôt et sauvegarde les fichiers remplacés dans
  `tools/plc-studio/.sauvegardes/<date>/`. Disponible seulement quand le code C++
  est généré sans erreur : installer un `config.json` sans la classe correspondante
  empêcherait l'application actuelle de démarrer. L'outil refuse d'écraser un dossier
  `src/<Nom>/` qu'il n'a pas généré (par exemple `src/RessortRoyal2/`).

## Code C++ généré

La classe générée dérive de `BusinessLogic`. Ses états sont les modes de marche :

```
INITIALISATION → ARRÊT → (PRISE D'ORIGINE) → REPOS ⇄ CYCLE
            urgence (prioritaire partout) → acquittement → RÉARMEMENT → ARRÊT
ARRÊT / REPOS → MANUEL (jog) → ARRÊT
```

- Le grafcet ne tourne qu'en REPOS et en CYCLE. REPOS signifie « grafcet en
  situation initiale » : c'est le seul moment où `isIdle()` est vrai, donc où les
  paramètres sont sauvegardés.
- Un cycle d'automate : actions à l'activation des étapes activées au cycle
  précédent → franchissement simultané des transitions validées → actions continues
  et sorties → mémorisation des fronts. Le simulateur suit exactement le même ordre.
- En urgence, chaque sortie prend son état de repli (à 0, à 1 ou maintenue), le servo
  est arrêté et le grafcet remis à zéro.
- Servo : toute cible est comparée à la course maximale (audit CR-1) ; le jog est
  appelé à chaque cycle tant que le bouton est maintenu (audit CR-3).
- Les paramètres sont relus au démarrage depuis `data/<Nom>.json` (valeurs par défaut
  si le fichier ou une clé manque), bornés par leurs min/max et sauvegardés quand ils
  changent.

## Règles du firmware appliquées automatiquement

- Identifiant de nœud = CRC-32 (zlib) de `"<id><nom de section>"` (`src/Node/Node.cpp`).
- Chaque identifiant de `interface.json` existe dans `config.json` (sinon blocage au démarrage).
- Section + nom de nœud < 79 octets UTF-8 ; nom réseau sans espace ; 10 WiFi maximum.
- Une seule instance SureServo (variables statiques dans `SureServo.cpp`).
- Un « inverse » sur une entrée est annulé côté écran (sinon le voyant est inversé deux fois).

## Mode d'exécution (headless / HMI)

Le projet déclare `"execution_mode": "hmi"` ou `"headless"` dans `config.json`.
**Le firmware actuel ne lit pas encore cette clé** : `main.cpp` n'exécute la logique
que lorsqu'un navigateur est connecté (audit CR-2). La correction est prévue.

## Tests

```bash
cd tools/plc-studio && npm test
```

Les tests vérifient notamment que la formule d'identifiant retrouve les 58 nœuds de
`data/config.json` et les `#define` de `RessortRoyal2.h`, le langage des réceptivités,
la compilation du grafcet et la bascule de `main.cpp`.

```bash
npm run check:cpp
```

Vérifie la syntaxe du C++ généré pour deux projets d'exemple (avec et sans servo) avec
`g++ -fsyntax-only`, contre les vrais en-têtes du dépôt et des en-têtes Arduino
simplifiés (`test/cpp-stubs/`). Ce n'est pas une compilation ESP32 complète : après
installation, compilez avec PlatformIO (`pio run -e quatre_mb_huge`).

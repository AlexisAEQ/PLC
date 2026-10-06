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

## Démarrage rapide

1. **Nouveau** → nom du projet (ex. `Presse`), puis réseaux WiFi à l'étape Projet.
2. **Équipements** : ajoutez l'automate Kincony, puis les modules et le servo.
3. **Câblage et variables** : sur chaque voie, tapez ce qui est branché (« Arrêt
   d'urgence OK », « Vérin sorti »…) ; le symbole utilisé dans la logique est créé
   automatiquement. Ajoutez les commandes (boutons à l'écran), paramètres, indicateurs.
4. **Logique** : « Créer un grafcet de départ », puis « Étape suivante », « Branche
   OU », « Parallèle ET » ; écrivez les réceptivités (suggestions de symboles en
   tapant) et les actions. Renseignez l'onglet « Modes et sécurités » (au minimum la
   condition d'arrêt d'urgence).
5. **Simulation** : forcez les entrées et vérifiez le comportement.
6. **Génération** : « Prévisualiser », puis « Installer », puis compilez et téléversez
   avec PlatformIO (`pio run -e quatre_mb_huge -t upload` et `-t uploadfs` pour `data/`).

Vous pouvez revenir à n'importe quelle étape : tout est recalculé et revérifié en direct
(la barre latérale indique le nombre d'erreurs par étape).

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
6. **Simulation** : le projet tourne dans le navigateur avec le même moteur que le
   code C++ généré (cycle de 10 ms) : grafcet animé (étapes actives et leur temps),
   forçage des entrées, commandes de l'écran, paramètres, sorties, modèle simplifié du
   servo (déplacement, prise d'origine, jog, alarmes), messages et journal. Lecture
   en continu (× 0,25 à × 20), cycle par cycle ou par seconde.
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
  situation initiale » : `isIdle()` (qui autorise les sauvegardes de paramètres) est
  vrai en REPOS, ARRÊT, URGENCE et INITIALISATION, jamais en CYCLE.
- Un cycle d'automate : capture des opérandes des fronts → franchissement simultané
  des transitions validées, répété tant que possible dans le même cycle (recherche de
  stabilité, 16 itérations au plus) avec exécution des actions à l'activation →
  actions continues et sorties. Une étape franchie dans le même cycle n'active pas ses
  actions continues ; un front ne vaut qu'une fois par cycle. Le simulateur suit
  exactement le même ordre.
- Les voies des modules Waveshare (un seul nœud par module) sont recopiées dans des
  nœuds « Miroirs ES » pour être affichées correctement à l'écran.
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

## Ajouter un équipement au catalogue

1. Créez (ou réutilisez) le fichier `data/hardware/<Nom>.json` lu par le firmware.
2. Ajoutez une entrée dans `tools/plc-studio/shared/catalog.js` : `hardware` (nom du
   fichier), `role`, `modbus`/`defaultAddress` et des `groups` dont `type` est le type
   de section `config.json` et `ids` les identifiants du tableau correspondant dans le
   fichier matériel (ou `bank` pour un nœud qui regroupe plusieurs voies).
3. `npm test` vérifie que chaque id généré existe dans le fichier matériel.

## Limites connues

- **Mode headless** : la clé `execution_mode` est écrite mais pas encore lue par le
  firmware ; la logique ne tourne qu'avec un écran connecté (audit CR-2).
- **Compilation ESP32** : le C++ généré est vérifié par `g++ -fsyntax-only` contre les
  en-têtes du dépôt ; la compilation complète se fait avec PlatformIO sur le poste.
- **Ancienne application** : après installation, `src/RessortRoyal2/` reste compilé
  (inutilisé) ; supprimez-le ou excluez-le si la place en flash manque.
- **Servo** : un seul SureServo par projet ; le simulateur utilise un modèle simplifié
  (vitesses et temps indicatifs).
- **Import** : un projet existant écrit à la main (config.json + classe C++) ne peut pas
  être importé dans PLC Studio.
- Sur la carte Kincony A8S, GPIO2 (buzzer) est remis à 0 par `main.cpp` à chaque
  connexion WiFi.

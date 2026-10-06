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
4. **Logique (grafcet)** : *en développement*.
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

Deux modes d'écriture :

- **Exporter** : écrit dans `projets/<Nom>/generation/`, sans toucher au firmware.
- **Installer** : écrit dans le dépôt et sauvegarde les fichiers remplacés dans
  `tools/plc-studio/.sauvegardes/<date>/`. Disponible seulement quand le code C++
  est généré : installer un `config.json` sans la classe correspondante empêcherait
  l'application actuelle de démarrer.

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
`data/config.json` et les `#define` de `RessortRoyal2.h`.

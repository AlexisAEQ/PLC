# Référentiel d'infrastructure (`infrastructure.jsonc`)

`infrastructure.jsonc`, à la racine du dépôt, décrit toute l'infrastructure que PLC Studio sait
piloter :

- les cartes automates, les modules d'E/S, les robots et les variateurs ;
- leurs voies, leurs registres et leurs bornes ;
- les limites du firmware et les environnements de compilation ;
- **les sites réellement installés** : IP, adresses, logiciel relevé sur chaque contrôleur.

PLC Studio le lit au démarrage : interface, ligne de commande (`plc.mjs`), générateur, validation
et simulateur. Quand l'installation change, **c'est le seul fichier à modifier** dans PLC Studio.

Le firmware ne le lit jamais. Il lit `config.json` et `data/hardware/*.json`, qui restent sa
vérité (broches, registres, puces). Le référentiel les indexe, et `plc infra verifier` contrôle
qu'il reste d'accord avec eux.

## Après chaque modification

```bash
node tools/plc-studio/cli/plc.mjs infra verifier   # contre data/hardware, platformio.ini et le firmware
cd tools/plc-studio && npm test                    # identifiants gelés, génération de chaque équipement
```

Ensuite, redémarrez PLC Studio s'il tourne : le serveur lit le fichier à son démarrage, et un
bandeau prévient si le fichier a changé depuis.

Une erreur de saisie bloque le démarrage avec un message précis, par exemple :

```
Référentiel d'infrastructure invalide : 1 erreur(s)
  infrastructure.jsonc:159:205  valeur inattendue « vrai » (écrire true ou false)
```

Pour essayer un brouillon sans toucher au fichier du dépôt :
`PLC_REFERENTIEL=brouillon.jsonc node tools/plc-studio/cli/plc.mjs infra verifier`.

## Contenu

Les sections sont rangées de la plus souvent modifiée à la plus rare. L'en-tête du fichier
explique les notations : gabarits, plages d'ids `"1-8"`, `"broches"`, `"noms"`…

| Section | Contenu | Contrôlé contre |
|---|---|---|
| `sites` | appareils installés : IP, `modbusTcp`, `rs485`, Ethernet et bus de l'automate, logiciel relevé | cohérence interne (IP uniques, sous-réseau, logiciel compatible) |
| `logiciels` | logiciels de contrôleur connus (ARCS, AUBOPE, FAIRINO) et présence d'un serveur Modbus TCP | — |
| `automates` | une entrée par fichier `data/hardware` : env PlatformIO, Ethernet, horloge, voies, marque | `data/hardware/<carte>.json`, `platformio.ini` |
| `robots`, `modules` | profils Modbus : voies, adresse et transport par défaut, logiciel requis (`logicielRequis`) | `data/hardware/<profil>.json` |
| `axes` | modèles de variateur : pilote, famille, bornes (rampe, courant crête par modèle), format série imposé, plage d'adresses | `data/hardware`, valeurs par défaut des pilotes C++ |
| `compilation` | environnements PlatformIO (carte, flash, partitions, cœur Arduino) | `platformio.ini`, fichiers de partitions |
| `firmware` | version, limites (noms, nœuds, WiFi, Modbus TCP…), types de section et classes C++, puces gérées, noms réservés | en-têtes C++ (`#define`, `constexpr`, classes) |
| `pilotes` | types d'axe que le **code** sait générer : classe, structure de nœuds, unités, écran, signaux, registres | structures `*Nodes` des pilotes C++ |
| `gabarits` | morceaux réutilisables (voies d'automate, options, état de variateur, écran d'axe) | — |
| `alias` | anciens noms acceptés (types, voies, options, sites, appareils) | — |
| `ecartsConnus` | écarts acceptés avec le firmware, signalés en avertissement | « cliquet » : un écart corrigé doit être retiré |

## Lier un projet à un site

À l'étape **Projet**, la carte « Site » liste les sites du référentiel et leurs appareils. À
l'étape **Équipements**, le champ « Appareil du site » lie chaque équipement à un appareil.

Une fois lié, un équipement tire du site :

- son IP, son port et son n° d'unité Modbus TCP, ou son adresse RS485 ;
- pour l'automate : son Ethernet et le réglage de son bus.

Ces champs apparaissent grisés, avec leur provenance. À chaque ouverture, les valeurs du site
remplacent celles enregistrées dans le projet. Changer l'IP d'un robot dans le référentiel
suffit donc : il reste à regénérer, installer, puis téléverser `data/` (`uploadfs`).

Un projet sans site n'est jamais modifié.

Dans une spec (`plc.mjs build`), on écrit `"site": "banc-aubo"` en tête et
`"appareil": "robot"` sur l'équipement. Recopier en plus l'adresse ou l'IP est refusé, pour
éviter deux sources de vérité.

Le logiciel relevé est comparé au logiciel exigé par le profil :

- un profil `Robot_Aubo` (ARCS seulement) lié à un robot relevé sous AUBOPE 4.5.13 est une
  **erreur**, et la génération est bloquée ;
- un `ModbusGeneric` lié à un appareil sans `modbusTcp` ni `rs485` est aussi une erreur.

C'est exactement le piège du banc AUBO i10 (`docs/robots/diagnostic-aubo-i10.md`). Un robot
dont le profil exige un logiciel, mais qui n'est lié à aucun site, reçoit un avertissement
« logiciel du contrôleur non vérifié ».

## Procédures

Légende des colonnes :

- **R** : seul `infrastructure.jsonc` change ;
- **R+F** : un fichier lu par le firmware ou par PlatformIO change aussi (`data/hardware`,
  `platformio.ini`), ce qui est inévitable puisqu'ils ne lisent pas le référentiel ;
- **C** : du code doit changer.

| Changement | Où | Étapes |
|---|---|---|
| **IP ou n° d'unité d'un robot, d'une passerelle Modbus TCP** | R | 1. `sites.<site>.appareils.<appareil>.ip` (ou `modbusTcp.unite`). 2. `plc infra verifier`. 3. `plc infra impact <site>.<appareil>` liste les projets concernés. 4. Pour chacun : Générer, Installer, téléverser `data/`. |
| **Logiciel du robot relevé** (« À propos » du pendant) | R | `logiciel: { "nom": "AUBOPE", "version": "4.5.13", "releveLe": "AAAA-MM-JJ" }` et `"modbusTcp": null` s'il n'y a pas de serveur Modbus TCP. PLC Studio refuse alors un profil incompatible. Il faut câbler le robot en E/S « linkage » (`docs/robots/aubo.md` § B). |
| **Remplacer la carte d'un banc par une carte connue** | R + projet | 1. `sites.*.appareils.automate.type` (et son IP). 2. Les projets liés reçoivent l'avertissement « le site indique … ». 3. Changer l'automate du projet (étape Équipements) et recâbler les voies signalées. 4. `plc check` affiche la nouvelle commande `pio run -e …`. |
| **Ajouter une carte** dont les puces sont déjà gérées | R+F | 1. `data/hardware/<Carte>.json` (vérité du firmware, voir `docs/cartes.md`). 2. Entrée `automates.<Carte>` d'une dizaine de lignes : gabarits `entrees` / `sorties` / `gpio` / `buzzer`, `horloge`, `envPlatformIO`, `marque`. 3. `plc infra verifier` (ids, broches, buzzer, ETH, horloge, environnement) et `plc infra montrer <Carte>`. 4. `npm test`. Il faut du code (**C**) seulement pour une nouvelle puce d'extension, d'horloge ou d'Ethernet (`IoExpander`, `DS3231_RTC`, `EthernetManager`), à reporter ensuite dans `firmware.pucesExtension` / `pucesHorloge`. |
| **Révision de carte** (GPIO déplacé) | R+F | Modifier `data/hardware/<Carte>.json` **et** `"broches"` : `infra verifier` contrôle qu'ils concordent. |
| **Bus RS485 d'un site** (vitesse, format) | R | `appareils.automate.bus = { "vitesse": 19200, "format": "SERIAL_8N1" }` : PLC Studio génère la variante `<carte>_19200_8N1.json`. Un variateur à format imposé (Lichuan A6 : 8E1) est refusé sur un autre format. Il reste à régler physiquement chaque équipement. |
| **Proposer une nouvelle vitesse RS485** (4800, 230400…) | R | Ajouter la valeur à `gabarits.options.rs485Speed.choix` (le firmware accepte tout entier). |
| **Le firmware relève une limite** (ex. `MAX_SLAVES` 16 → 32) | C, puis R | 1. Modifier le C++ (et `BORNE_UNIVERSELLE_VERSION`). 2. `infra verifier` signale l'écart avec le symbole et le fichier. 3. Reporter `"valeur"` (et `firmware.version`). La validation applique aussitôt le nouveau seuil. |
| **Nouvel environnement PlatformIO** | F, puis R | 1. `[env:…]` et son fichier de partitions dans `platformio.ini`. 2. `compilation.envs.<env>` (mcu, carte, flash, arduino, partitions). 3. `envPlatformIO` des cartes concernées. 4. `infra verifier` (environnement présent, carte, flash, partitions, Ethernet ⇒ Arduino 3). |
| **Nouveau modèle d'une série de variateur connue** | R (+F) | Entrée `axes.<Modèle>` : pilote, famille, bornes, nœuds « par modèle », et son `data/hardware` s'il diffère. `infra verifier` contrôle les champs et les bornes. Une nouvelle valeur d'enum C++ (ex. DM860RS) demande d'abord le firmware (**C**). |
| **Nouveau type d'axe** | C + F + R | Classe C++, `case` dans `shared/cpp.js`, modèle dans `shared/sim.js`, `KINDS_GERES` (`shared/referentiel.js`), puis `pilotes.<type>` et `axes.<Modèle>`. Sans le code, le chargeur refuse le pilote, et le dit. |
| **Renommer** un type, une voie, une option, un site ou un appareil | R | Renommer, puis ajouter l'ancien nom dans `alias` : les projets et les specs restent valides et sont réécrits à l'enregistrement. Les suffixes de section, ids et noms de nœuds entrent dans le hash : on ne peut pas les aliaser. Après un tel renommage voulu, il faut réinstaller les projets puis lancer `plc infra figer`. |

## Vérifications

**`plc infra verifier`** contrôle le référentiel contre la vérité du firmware :

- **fichiers matériels** :
  - le fichier existe ;
  - son champ `hardware` est égal à son nom ;
  - chaque id de chaque voie et de chaque section est présent ;
  - la taille des bancs est égale à `nb_values` ;
  - les broches GPIO et la broche du buzzer concordent ;
  - le bloc `ETH` est présent si et seulement si `ethernet` est vrai ;
  - le bus RS485 est cohérent, et l'horloge aussi ;
- **`platformio.ini`** : environnements, carte, flash, cœur Arduino, fichiers de partitions
  (casse comprise), `default_envs` ;
- **firmware** :
  - la version ;
  - les limites (`#define`, `constexpr`) ;
  - les types de section et les classes C++ ;
  - les puces gérées ;
  - les structures de nœuds des pilotes (chaque `champ`) ;
  - les bornes des variateurs (`defaultsFor`).

Un symbole introuvable est une erreur nommée, jamais un contrôle sauté en silence.

Les **écarts connus** (`ecartsConnus`) sont signalés en avertissement. À la création du
référentiel, ce sont :

- **E02** : Lichuan A5 annoncé à 6000 tr/min, le firmware borne à 3000 ;
- **E07** : fichier de la KC868-A8S à 115000 bauds ;
- **E14** : `partitions_16MB.csv` cité, alors que le fichier s'appelle `partitions_16Mb.csv` ;
- **E15** (simple note) : environnement de l'A8S à trancher.

Quand un écart est corrigé dans le firmware, le vérificateur demande de le retirer.

**`npm test`** ajoute des contrôles :

- les identifiants gelés (`test/snapshots/identifiants.json`) : un retrait sans alias échoue,
  avec l'explication ;
- la génération de chaque équipement du référentiel, toutes voies câblées, avec les vrais
  fichiers matériels ;
- les messages d'erreur des fautes de saisie courantes ;
- la concordance des pilotes avec le code (`cpp.js`, `sim.js`) ;
- les sites (propagation, compatibilité logicielle, alias).

## Ce qui reste hors du référentiel

- Les secrets (mots de passe, SSID) : ils restent dans le projet, et le chargeur refuse ces clés.
- Le comportement :
  - génération C++ par type d'axe ;
  - simulation ;
  - langage des réceptivités ;
  - contrat `main.cpp` ;
  - conventions de nommage des sections, hachées.
- Les fichiers lus par le firmware et par PlatformIO (`data/hardware/*.json`, `platformio.ini`) :
  le référentiel les indexe et les vérifie, sans les remplacer.

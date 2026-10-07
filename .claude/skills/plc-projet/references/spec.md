# Format du cahier des charges (spec)

Fichier JSON (les commentaires `//` sont acceptés) lu par
`node tools/plc-studio/cli/plc.mjs build <spec.json>`. Exemples complets :
`examples/tri-colis.spec.json` (sans servo, écrans explicites, défaut de bourrage) et
`examples/presse-servo.spec.json` (servo, divergence ET, écrans automatiques).

Sommaire : [Projet](#projet) · [Équipements](#équipements) · [E/S câblées](#es-câblées) ·
[Variables internes](#variables-internes) · [Grafcet](#grafcet) · [Actions](#actions) ·
[Modes et sécurités](#modes-et-sécurités) · [Écrans](#écrans) · [Scénarios](#scénarios)

## Projet

| Clé | Obligatoire | Rôle |
|---|---|---|
| `name` | oui | Nom du projet = nom de la classe C++ et de `data/<name>.json`. Lettre en premier, puis lettres, chiffres ou `_`, 24 caractères max., pas de nom réservé (`config`, `interface`, `main`…). Ex. `TriColis`. |
| `description` | non | Une phrase : affichée dans PLC Studio. |
| `hostname` | non | Nom réseau mDNS (défaut : dérivé du nom, minuscules, sans espace). |
| `execution` | non | `"hmi"` (défaut : la logique tourne quand un navigateur est connecté — comportement actuel du firmware) ou `"headless"` (pas encore lu par le firmware, voir references/logique.md). |
| `wifi` | non | Tableau de réseaux. Défaut : un point d'accès `{ "name": "Point d'accès", "mode": "access_point", "ap_name": <hostname>, "ap_password": "ChangeMoi123", "ap_ip": "192.168.4.1", "ap_channel": 6 }`. Client : `{ "name": "Atelier", "mode": "station", "ssid": "…", "pwd": "…", "dhcp": true }` (sinon `ip`, `gateway`, `mask`, `dns`). 10 réseaux max. |
| `options` | non | `{ "stateDisplay": true, "clockDisplay": false, "logDiagnostic": true, "logHistory": false, "showInfoMessages": true, "showSuccessMessages": false, "deleteDiagnosticAtStartup": false }` (valeurs par défaut). `stateDisplay` affiche l'état machine à l'écran, `clockDisplay` l'horloge (nécessite l'horloge DS3231). |
| `otaUrl`, `editorPassword` | non | URL de mise à jour, mot de passe de l'éditeur de fichiers de l'interface web. |

## Équipements

```json
"equipment": [
  { "id": "plc", "type": "Kincony_KC868_A8S", "label": "Automate", "options": { "rtc": true } },
  { "id": "io", "type": "Waveshare_8DIO", "label": "Module ES", "address": 1 },
  { "id": "relais", "type": "Waveshare_16DO", "label": "Module relais", "address": 2 },
  { "id": "ana", "type": "Waveshare_8AI", "label": "Module analogique", "address": 3 },
  { "id": "servo", "type": "SureServo", "label": "Servo", "options": { "maxRange": 500000 } }
]
```

- `id` : nom court, local à la spec, utilisé dans les voies (`plc.X1`). `type` : voir
  `plc.mjs catalog`. `label` : nom affiché et préfixe des sections du firmware — court,
  unique, sans `/`, ne commence pas par un chiffre.
- L'automate **Kincony est obligatoire** (une seule fois) ; il est automatiquement placé en
  premier (il porte le bus RS485).
- `address` : adresse Modbus 1 à 247, **unique** (défauts : 8DIO 1, 16DO 2, 8AI 3, servo 127).
- `options.rtc: false` si l'horloge DS3231 n'est pas montée (sinon blocage au démarrage).
- Servo (1 max.) : `options.pulsesPerUnit` (défaut 1), `options.maxRange` (course max. en
  unités, toute cible au-delà est refusée), `options.homePolicy`
  (`NEGATIVE_INVALIDATES_HOME` | `NEGATIVE_TOLERATED`).

## E/S câblées

```json
"io": [
  { "symbol": "AU_OK", "label": "Arrêt d'urgence OK", "at": "plc.X1" },
  { "symbol": "BP_Arret", "label": "Bouton arrêt", "at": "plc.X3", "inverse": true, "comment": "Contact NF" },
  { "symbol": "Niveau", "label": "Niveau cuve", "at": "ana.AI1" },
  { "symbol": "Moteur", "label": "Moteur convoyeur", "at": "plc.Y1", "fallback": "off" }
],
"servoSignals": { "servoOn": "plc.Y6", "immediateStop": "plc.Y7", "alarmsReset": "plc.Y8" }
```

- `at` : `<id équipement>.<voie>`. Noms de voies (`plc.mjs catalog`) :
  Kincony entrées `X1`…`X8`, relais `Y1`…`Y8`, `GPIO2` (déconseillé : c'est le buzzer, remis
  à 0 à chaque connexion WiFi) ; Waveshare 8DIO `DI1`…`DI8` / `DO1`…`DO8` ; 16DO `DO1`…`DO16` ;
  8AI `AI1`…`AI8` (entiers 16 bits).
- Le sens (entrée/sortie) et le type (`bool`, `int` pour AI) viennent de la voie.
- `symbol` : nom utilisé dans la logique — lettres, chiffres, `_`, 40 caractères max., pas
  de mot du langage (`ET`, `OU`, `NON`, `FM`, `FD`, `VRAI`, `FAUX`, `MIN`, `MAX`, `ABS`,
  `SERVO`…), pas de `X<nombre>`, pas de mot-clé C++. Défaut : dérivé du libellé.
- `label` : texte affiché à l'écran (court : section + libellé < 77 octets).
- `inverse: true` (entrées Kincony et Waveshare) : la valeur logique est inversée — pour un
  contact NF, `true` = actionné.
- `fallback` (sorties) : état en arrêt d'urgence, `"off"` (défaut), `"on"` ou `"hold"`.
- `show: false` : ne pas proposer à l'écran ; `widget` : widget par défaut (voir Écrans).
- Sans `at`, donner `"kind": "input"|"output"` (et `dataType`) : la variable est créée non
  câblée (avertissement) et se câble ensuite dans PLC Studio.
- `servoSignals` (obligatoire avec un servo) : les trois signaux TOR du variateur, sur des
  **relais de l'automate** (pas sur un module Waveshare). Symboles créés :
  `Servo_servoOn`, `Servo_immediateStop`, `Servo_alarmsReset` (pilotés par le bloc servo, la
  logique ne peut pas les écrire).

## Variables internes

```json
"variables": [
  { "symbol": "Acquit", "label": "Acquitter", "kind": "command", "dataType": "bool" },
  { "symbol": "Cible", "label": "Position cible", "kind": "parameter", "dataType": "int", "initial": 5000, "min": 0, "max": 90000, "unit": "imp" },
  { "symbol": "Recette", "label": "Recette", "kind": "parameter", "dataType": "text", "initial": "A", "choices": "A;B;C", "widget": "tx-dropdown" },
  { "symbol": "Pieces", "label": "Pièces produites", "kind": "indicator", "dataType": "int" },
  { "symbol": "Mem", "label": "Mémoire", "kind": "memory", "dataType": "bool" }
]
```

| `kind` | `dataType` possibles | Usage |
|---|---|---|
| `command` | bool, int, float | Bouton/saisie à l'écran lue par la logique (non conservée). |
| `parameter` | int, float, text, bool | Réglage à l'écran, **sauvegardé** dans `data/<name>.json` (au repos), borné par `min`/`max`. `initial` = valeur par défaut. |
| `indicator` | bool, int, float, text | Calculée par la logique, affichée. |
| `memory` | bool, int, float | Variable de travail, non affichée (`show: true` pour l'afficher). |

Champs facultatifs : `comment` (devient l'info-bulle), `initial` (**paramètres
seulement** ; les autres variables démarrent à 0 / faux / ""), `min`, `max`, `step`
(curseur), `unit` (affichée seulement par `rx-level-bar` : sinon la mettre dans le
libellé), `choices` (liste « ; » pour `tx-dropdown`), `show`, `widget`.

## Grafcet

```json
"grafcet": {
  "steps": [
    { "num": 0, "label": "Attente", "initial": true, "actions": ["N Voyant"] },
    { "num": 1, "label": "Serrage", "actions": ["N Verin", "S Mem"] }
  ],
  "transitions": [
    { "from": 0, "to": 1, "condition": "FM(Depart) ET Piece" },
    { "from": 1, "to": [2, 3], "condition": "Verin_sorti" },
    { "from": [2, 3], "to": 4, "condition": "Servo.enPosition ET X3.t >= 1500ms" },
    { "from": 4, "to": 0, "condition": "VRAI" }
  ]
}
```

- Étapes : `num` entier unique (0 à 9999), `label` court, `initial: true` pour au moins une
  étape, `actions` (voir ci-dessous). 250 étapes max.
- Transitions : `from` / `to` = numéro d'étape ou liste. Plusieurs `to` = divergence ET
  (étapes simultanées) ; plusieurs `from` = convergence ET (toutes actives). Plusieurs
  transitions qui partent d'une même étape = divergence OU (rendre les réceptivités
  exclusives). Une transition vers une étape plus haute = reprise (boucle). `num` facultatif
  (T0, T1… dans l'ordre). `condition` : voir references/logique.md ; défaut `VRAI`.
- Positions : sans `x`/`y`, le grafcet est placé automatiquement sur la grille de l'éditeur
  (suite principale à gauche, branches à droite, reprises proches de la colonne
  principale et dessinées à gauche, grafcets indépendants les uns sous les autres). Si
  **toutes** les étapes et transitions ont `x`/`y` (spec exportée d'un projet retouché, ou
  placement à la main), elles sont conservées ; sinon tout est replacé. Repères pour un
  placement à la main : centre des étapes, grille de 10 ; 180 px entre deux étapes
  successives et transition à mi-hauteur ; branches espacées d'au moins 260 px (plus si
  les actions ou réceptivités sont longues) ; transitions d'une même convergence ET au
  même `y` (une seule double barre) ; deux reprises d'une même rangée décalées de 20 px.

## Actions

Texte court ou objet (format du projet, ex. `{ "type": "N", "target": "Verin", "condition": "Piece" }`).

| Texte court | Effet |
|---|---|
| `N Verin` · `N Soufflage si Piece` | Vrai tant que l'étape est active (et la condition vraie). |
| `S Mem` · `R Mem` | Mise à 1 / à 0 mémorisée à l'activation (booléens). |
| `SET Compteur := 0` · `NSET Consigne := Cible * 2` | Affectation à l'activation / à chaque cycle. |
| `INC Pieces` · `DEC Stock` | +1 / −1 à l'activation (entiers). |
| `SERVO_MOVE Cible @ Vitesse` | Déplacement du servo : position (impulsions) `@` vitesse 0-15. |
| `SERVO_HOME` · `SERVO_STOP` | Prise d'origine / arrêt du mouvement à l'activation. |
| `MSG success: Pièce terminée` | Message à l'écran à l'activation (`info` par défaut, `warning`, `error`, `success`). |

Cibles : sorties câblées, indicateurs, mémoires, paramètres, commandes (ex. `SET Depart_ecran := FAUX`) ;
jamais les entrées ni les signaux du servo. N/S/R : booléens ; INC/DEC : numériques.

- Les valeurs (`SET`, `NSET`, `SERVO_MOVE` position et vitesse) et les conditions de `N`
  sont des expressions complètes : variables, `X3`, `Servo.position`, arithmétique,
  `MIN`/`MAX`…
- Ordre : les actions à l'activation s'exécutent dans l'ordre de la liste ; un `SET` est
  visible par les actions suivantes et par les réceptivités évaluées dans le même cycle.
- Messages : `error` et `warning` toujours affichés ; `info` si `options.showInfoMessages`
  (défaut vrai) ; `success` masqué tant que `options.showSuccessMessages` est faux (défaut)
  — le simulateur les enregistre tous.
Une même variable ne doit pas être à la fois pilotée par N/S/R et affectée par SET/INC.

## Modes et sécurités

```json
"modes": {
  "emergency": { "condition": "NON AU_OK", "reset": "Acquit" },
  "run": { "start": "BP_Marche", "stop": "BP_Arret" },
  "servo": { "homing": "auto", "homingCondition": "", "jogEnabled": true, "jogMode": "Jog", "jogPlus": "JogP", "jogMinus": "JogM", "jogSpeed": "500", "torque": "80" }
}
```

Expressions booléennes (vides = non utilisées). Sémantique dans references/logique.md.
`servo.homing` : `"auto"` (avant le premier cycle), `"condition"` (+ `homingCondition`) ou
`"none"` (actions `SERVO_HOME` du grafcet). `jogSpeed` 1-3000, `torque` 0-100 % (expressions
numériques, vides = non modifié). Bloc `servo` ignoré sans servo.

## Écrans

`"hmi": "auto"` (défaut) : PLC Studio range lui-même les variables (`show` ≠ false) en pages
Principal / Entrées sorties / Servo / Réglages, et suivra les nouvelles variables.

Écrans explicites (disposition personnalisée, retouchable dans l'éditeur visuel) :

```json
"hmi": { "pages": [
  { "name": "Production", "sections": [
    { "name": "État", "items": ["@etat", "Moteur"] },
    { "name": "Commandes", "orientation": "horizontal", "items": [ { "ref": "Acquit", "label": "Acquitter", "widget": "tx-button-hold", "props": { "color": "warning" } } ] }
  ]},
  { "name": "Réglages", "password": "1234", "sections": [ { "name": "Cycle", "password": "abcd", "items": ["Cible", "Vitesse"] } ] }
]}
```

- Élément : symbole, `"@etat"` (état machine), `"@horloge"`, `"Servo.<alias>"` (`pret`,
  `active`, `initialise`, `origineFaite`, `enPosition`, `alarme`, `erreurModbus`,
  `position`, `codeAlarme`, `etat`) ; ou objet `{ ref, label, widget, props }`.
- `label` (défaut : libellé de la variable), `widget` (défaut selon la nature) :
  commandes `tx-button-hold` | `tx-bool` (bool), `tx-numeric` | `tx-slider` ;
  paramètres `tx-bool`, `tx-numeric` | `tx-slider`, `tx-string` | `tx-dropdown` ;
  indicateurs/E/S `rx-indicator` | `rx-bool` (bool), `rx-numeric` | `rx-level-bar`,
  `rx-label` (texte).
- `props` : propriétés de `data/component-manifest.json` — `color` (`tx-button-hold` :
  primary/success/warning/danger/neutral ; `rx-indicator` : lime, red, orange…), `blink`,
  `inverse`, `state` (`rx-label` : neutral/success/warning/danger), `isBlinking`,
  `alignment`, `nb_digits_ap`, `unit`, `min`, `max`, `orientation`, `low`, `high`, `step`,
  `items`, `default`, `loadable`, `tooltip`, `disable`… Les bornes min/max des variables et
  leur commentaire (info-bulle) sont repris automatiquement.
- Section + libellé < 77 octets UTF-8 ; noms de pages uniques. `orientation` :
  `horizontal` (défaut) ou `vertical`. `password` : page ou section verrouillée.

## Scénarios

Tableau `scenarios` de la spec (enregistré dans `projets/<name>/scenarios.json`, rejoué par
`plc.mjs sim <name>` même après retouche du projet dans PLC Studio). Chaque scénario démarre
automate à l'arrêt complet (mode INITIALISATION, toutes les entrées à faux, paramètres à
leur valeur initiale) et enchaîne des pas :

| Pas | Effet |
|---|---|
| `{ "set": { "AU_OK": true, "Cible": 5000 } }` | Force des entrées / écrit des variables (restent forcées). Valeur **logique** : pour une entrée `inverse`, `true` = actionnée. |
| `{ "press": "Depart" }` · `{ "press": { "symbol": "Depart", "ms": 300 } }` | Appui : vrai pendant 100 ms (défaut), puis faux. |
| `{ "wait": "2s" }` | Laisse tourner (`ms`, `s`, `min`, `h` ; nombre = ms ; `1,5s` accepté). |
| `{ "until": <condition>, "timeout": "5s" }` | Tourne jusqu'à ce que la condition soit vraie ; échec au-delà du délai (défaut 10 s). |
| `{ "expect": <condition> }` | Vérifie immédiatement. |
| `{ "message": "Pièce terminée" }` | Un message contenant ce texte a été émis depuis le début. |
| `{ "servo": "alarm" }` · `{ "servo": "loseHome" }` | Défaut du servo simulé. |
| `{ "comment": "…" }` | Repère dans le rapport. |

Condition : expression du langage (`"X3 ET Verin"`, sans fronts) ou objet dont toutes les
clés doivent correspondre : symbole → valeur, `"X3": true`, `"steps": [2, 3]` (liste exacte
des étapes actives), `"mode": "REPOS"` (INITIALISATION, ARRÊT, PRISE D'ORIGINE, REPOS,
CYCLE, MANUEL, URGENCE, RÉARMEMENT), `"Servo.origineFaite": true`, `"Servo.position": 0`.

Temps typiques du simulateur (cycle 10 ms) : sans servo, INITIALISATION → ARRÊT en ~0,1 s ;
avec servo, initialisation du variateur + prise d'origine automatique ≈ 1 à 2 s ;
réarmement après acquittement ≈ 1 s. Utiliser `until` avec un délai large plutôt que des
`wait` exacts.

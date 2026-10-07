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
| `ethernet` | non | Port Ethernet de l'automate (cartes à Ethernet seulement, voir `docs/cartes.md`), en plus du WiFi : `{ "enabled": true, "dhcp": true }` ou `{ "enabled": true, "dhcp": false, "ip": "192.168.10.20", "gateway": "192.168.10.1", "mask": "255.255.255.0", "dns": "" }`. Défaut : désactivé. |
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

Plusieurs axes (types mélangeables) : chaque axe a ses propres `options`, son adresse
Modbus et ses `signals` (voies de l'automate) :

```json
"equipment": [
  { "id": "plc", "type": "Kincony_KC868_A16v3", "label": "Automate" },
  { "id": "axe_x", "type": "SureServo", "label": "Axe X", "address": 1,
    "signals": { "servoOn": "plc.Y1", "immediateStop": "plc.Y2", "alarmsReset": "plc.Y3" } },
  { "id": "plateau", "type": "LichuanA6", "label": "Plateau", "address": 3 },
  { "id": "axe_z", "type": "Stepper", "label": "Axe Z", "options": { "maxRange": 20000 },
    "signals": { "step": "plc.GPIO38", "dir": "plc.GPIO39", "enable": "plc.Y7", "home": "plc.X3" } }
]
```

- `id` : nom court, local à la spec, utilisé dans les voies (`plc.X1`). `type` : voir
  `plc.mjs catalog`. `label` : nom affiché et préfixe des sections du firmware — court,
  unique, sans `/`, ne commence pas par un chiffre.
- **Un automate, obligatoire** ; il est automatiquement placé en premier (il porte le bus
  RS485). Types : `Kincony_KC868_A8S`, `Kincony_KC868_A16`, `Kincony_KC868_A16v3`,
  `Kincony_KC868_A8v3`, `Waveshare_ESP32S3_POE_8DI8DO`, `Waveshare_ESP32S3_RS485_WLED`,
  `M5Stack_StamPLC`, `Homemaster_MiniPLC` (voies : `plc.mjs catalog`, qui donne aussi
  l'environnement PlatformIO à compiler).
- `address` : adresse Modbus 1 à 247, **unique** sur le bus RS485 (défauts : 8DIO 1, 16DO 2,
  8AI 3, SureServo 127, Lichuan 1).
- Liaison de **tout équipement Modbus** : `options.transport` = `"rtu"` (défaut, bus RS485)
  ou `"tcp"` (réseau WiFi/Ethernet) avec `options.ip` (ex. `"192.168.10.50"`) et
  `options.port` (502). En TCP, `address` est le n° d'unité 0 à 255 (souvent 1 ; adresse de
  l'esclave derrière une passerelle TCP → RTU), unique par IP/port. Au plus 8 IP, 16
  équipements TCP, un seul port par IP. Voir `docs/modbus-tcp.md`.
- `ModbusGeneric` (robot, automate tiers, passerelle ; TCP par défaut) : voies `Q1`…`Q16`
  (bobines écrites), `I1`…`I16` (bobines lues), `W1`…`W16` (registres écrits, **sorties
  entières** 0-65535), `R1`…`R16` (registres lus), `IR1`…`IR16` (registres d'entrée).
  Options : `coilOutStart`, `coilInStart`, `regOutStart`, `regInStart`, `inputRegStart`
  (adresse de la voie n°1) et `oneBased: true` si la documentation compte à partir de 1
  (40001 accepté). Exemple :
  `{ "id": "robot", "type": "ModbusGeneric", "label": "Robot", "address": 1, "options": { "ip": "192.168.10.50", "regOutStart": 100, "regInStart": 200 } }`.
- Automate : `options.rtc: false` si l'horloge n'est pas montée ; `options.rs485Speed`
  (`"9600"` … `"115200"`) et `options.rs485Config` (`"SERIAL_8N1"`, `"SERIAL_8E1"`…) pour
  régler le bus sur les équipements (vide = valeur du fichier matériel).
- Axes (autant que nécessaire, `label` différents) :
  - `SureServo` : `pulsesPerUnit` (défaut 1), `maxRange` (course max. en unités, toute cible
    au-delà est refusée), `homePolicy` (`NEGATIVE_INVALIDATES_HOME` | `NEGATIVE_TOLERATED`) ;
    vitesse des déplacements = index 0-15, jog en tr/min.
  - `LichuanA6` / `LichuanA5` : `pulsesPerUnit`, `maxRange` (0 = pas de butée haute),
    `accelTime` (ms, 0 = réglage du variateur) ; vitesses en tr/min. Paramétrage du
    variateur : `docs/axes/lichuan.md`.
  - `Stepper` (STEP/DIR sur GPIO) : `stepsPerUnit`, `maxRange`, `acceleration` (pas/s²),
    `homingDirection` (`"-1"`/`"1"`), `homingSpeed`, `homingBackoff`, `invertDirection`,
    `enableActiveLow` ; vitesses en pas/s. Voir `docs/axes/stepper.md`.

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
(`servoSignals` : ancienne forme pour un axe unique ; sinon `signals` dans l'équipement.)

- `at` : `<id équipement>.<voie>`. Noms de voies (`plc.mjs catalog`) :
  KinCony entrées `X1`…, sorties `Y1`…, GPIO libres `GPIO<n>` ; Waveshare et M5Stack
  `DI1`… / `DO1`… ; `BUZZER` (remis au repos à chaque connexion WiFi) ; KC868-A8S `GPIO2`
  (c'est le buzzer) ; modules Waveshare 8DIO `DI1`…`DI8` / `DO1`…`DO8` ; 16DO `DO1`…`DO16` ;
  8AI `AI1`…`AI8` (entiers 16 bits) ; `ModbusGeneric` `Q1`, `I1`, `W1`, `R1`, `IR1`….
- Le sens (entrée/sortie) et le type (`bool`, `int` pour AI et registres) viennent de la
  voie. Une sortie entière (registre `W<n>`) s'écrit avec `SET` / `NSET`.
- `symbol` : nom utilisé dans la logique — lettres, chiffres, `_`, 40 caractères max., pas
  de mot du langage (`ET`, `OU`, `NON`, `FM`, `FD`, `VRAI`, `FAUX`, `MIN`, `MAX`, `ABS`,
  `SERVO`…), pas de `X<nombre>`, pas de mot-clé C++. Défaut : dérivé du libellé.
- `label` : texte affiché à l'écran (court : section + libellé < 77 octets).
- `inverse: true` (entrées Kincony et Waveshare) : la valeur logique est inversée — pour un
  contact NF, `true` = actionné.
- `fallback` (sorties) : état en arrêt d'urgence, `"off"` (défaut, 0 pour un registre),
  `"on"` ou `"hold"` (valeur maintenue).
- `show: false` : ne pas proposer à l'écran ; `widget` : widget par défaut (voir Écrans).
- Sans `at`, donner `"kind": "input"|"output"` (et `dataType`) : la variable est créée non
  câblée (avertissement) et se câble ensuite dans PLC Studio.
- `signals` d'un axe (sur des **voies de l'automate**, pas sur un module Waveshare) :
  SureServo `servoOn`, `immediateStop`, `alarmsReset` (obligatoires) ; Stepper `step` et
  `dir` (obligatoires, **sorties GPIO directes** `GPIO<n>`), `enable`, `alarmsReset`
  (facultatives), `home`, `limitPlus`, `limitMinus`, `driverAlarm` (entrées facultatives).
  Symboles créés : `<Axe>_<signal>` (ex. `Axe_Z_home`), lisibles par la logique, jamais
  écrits par elle. Les Lichuan n'ont pas de signal (tout passe par Modbus).

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
| `SERVO_MOVE[axe_x] Cible @ Vitesse` | Déplacement d'un axe : position `@` vitesse, dans les unités de l'axe (SureServo : impulsions, index 0-15). `[axe]` = `id` de l'équipement ou nom de l'axe ; facultatif avec un seul axe. |
| `SERVO_HOME[axe_x]` · `SERVO_STOP[axe_x]` | Prise d'origine / arrêt du mouvement de l'axe à l'activation. |
| `MSG success: Pièce terminée` | Message à l'écran à l'activation (`info` par défaut, `warning`, `error`, `success`). |

Cibles : sorties câblées, indicateurs, mémoires, paramètres, commandes (ex. `SET Depart_ecran := FAUX`) ;
jamais les entrées ni les signaux des axes. N/S/R : booléens ; INC/DEC : numériques.

- Les valeurs (`SET`, `NSET`, `SERVO_MOVE` position et vitesse) et les conditions de `N`
  sont des expressions complètes : variables, `X3`, `Axe_X.position`, arithmétique,
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
  "axes": {
    "axe_z": { "homing": "auto", "homingOrder": 1, "jogEnabled": true, "jogMode": "Jog", "jogPlus": "JogP", "jogMinus": "JogM", "jogSpeed": "2000" },
    "axe_x": { "homing": "auto", "homingOrder": 2, "torque": "80" }
  }
}
```
(`"servo": { … }` : ancienne forme, bloc de l'axe unique.)

Expressions booléennes (vides = non utilisées). Sémantique dans references/logique.md.
Un bloc par axe (clé = `id` de l'équipement ; axe absent = prise d'origine automatique, pas
de jog). `homing` : `"auto"` (avant le premier cycle), `"condition"` (+ `homingCondition`) ou
`"none"` (actions `SERVO_HOME` du grafcet). `homingOrder` : les axes d'ordre 1 prennent leur
origine d'abord, puis 2… (même ordre = en même temps ; ex. Z dégagé avant X/Y). `jogSpeed`
dans l'unité de jog de l'axe (vide = défaut du type), `torque` 0-100 % (SureServo, Lichuan ;
expressions numériques, vides = non modifié). Le jog d'un axe est actif quand son `jogMode`
est vrai ; MANUEL dure tant qu'au moins un `jogMode` est vrai.

## Écrans

`"hmi": "auto"` (défaut) : PLC Studio range lui-même les variables (`show` ≠ false) en pages
Principal / Entrées sorties / <axe> (un axe) ou Axes / Réglages, et suivra les nouvelles variables.

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

- Élément : symbole, `"@etat"` (état machine), `"@horloge"`, `"<axe>.<alias>"` (axe = `id`
  de l'équipement ou nom de l'axe ; `Servo.` pour l'axe unique) : `pret`, `initialise`,
  `origineFaite`, `enPosition`, `alarme`, `position` pour tous ; `active`, `erreurModbus`,
  `codeAlarme`, `etat` (SureServo) ; `erreurModbus`, `codeAlarme`, `vitesse`, `etat`
  (Lichuan) ; `active`, `finDeCourse` (Stepper) ; ou objet `{ ref, label, widget, props }`.
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
| `{ "servo": "alarm", "axis": "axe_x" }` · `{ "servo": "loseHome", "axis": "Axe_X" }` | Défaut d'un axe simulé (`axis` facultatif avec un seul axe). |
| `{ "comment": "…" }` | Repère dans le rapport. |

Condition : expression du langage (`"X3 ET Verin"`, sans fronts) ou objet dont toutes les
clés doivent correspondre : symbole → valeur, `"X3": true`, `"steps": [2, 3]` (liste exacte
des étapes actives), `"mode": "REPOS"` (INITIALISATION, ARRÊT, PRISE D'ORIGINE, REPOS,
CYCLE, MANUEL, URGENCE, RÉARMEMENT), `"Axe_X.origineFaite": true`, `"Servo.position": 0`.
Les messages se comparent sans tenir compte des majuscules.

Temps typiques du simulateur (cycle 10 ms) : sans servo, INITIALISATION → ARRÊT en ~0,1 s ;
avec axes, initialisation des variateurs (en parallèle) + prises d'origine ≈ 1 à 3 s ;
réarmement après acquittement ≈ 1 s. Utiliser `until` avec un délai large plutôt que des
`wait` exacts.

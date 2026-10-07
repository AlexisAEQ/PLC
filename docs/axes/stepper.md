# Axe pas-à-pas STEP/DIR (`StepperMotor`)

`StepperMotor` (`src/StepperMotor/StepperMotor.h`, `Steppermotor.cpp`) pilote un driver de moteur
pas-à-pas par impulsions **STEP**, sens **DIR** et activation **ENABLE** (option). La classe implémente
l'interface `ServoDrive` : une application pilote un axe pas-à-pas exactement comme un servo
SureServo, et peut mélanger plusieurs axes pas-à-pas et des servos.

Les impulsions sont générées par la bibliothèque **FastAccelStepper** sur les périphériques matériels
de l'ESP32 (MCPWM/PCNT, RMT, I2S) : aucune impulsion n'est produite par la boucle logicielle, la rampe
d'accélération est calculée par une tâche FreeRTOS de la bibliothèque.

---

## 1. Principe

| Élément | Rôle |
|---|---|
| `StepperMotor::sharedEngine()` | Un seul `FastAccelStepperEngine` pour tout le programme, `init()` appelé une fois (au premier axe connecté). |
| Un `StepperMotor` par axe | Obtient son propre `FastAccelStepper*` (`stepperConnectToPin`) lors de l'initialisation. |
| Registre interne | Refuse deux axes sur la même GPIO STEP ; réutilise le générateur si un axe est détruit puis recréé sur la même broche (rechargement de configuration). |
| État | Entièrement porté par l'instance : plusieurs axes ne se gênent pas (plus de variables `static` de fonction). |
| Messages | Tous les messages série et IHM sont préfixés par l'identifiant de l'axe : `[AXE_X] …`. |

Constructeur :

```cpp
StepperMotor(uint32_t stepsPerUnit,          // pas par unité mécanique (information, 0 -> 1)
             const StepperNodes& nodes,      // nœuds de l'axe (STEP et DIR obligatoires)
             uint32_t maxRangeSteps = 0,     // course : butées logicielles [0, maxRangeSteps] après origine
             const char* servoId = "STEPPER",
             uint32_t accelerationStepsPerS2 = 5000);
```

---

## 2. Câblage

### 2.1 Signaux et nœuds

| Champ `StepperNodes` | Signal | Type de nœud | |
|---|---|---|---|
| `stepPin` | STEP / PUL | `HardwareBooleanOutputNode` (GPIO `tx-bool`) | **obligatoire** |
| `dirPin` | DIR | `HardwareBooleanOutputNode` (GPIO `tx-bool`) | **obligatoire** |
| `enable` | ENA / EN | GPIO `tx-bool` → piloté par FastAccelStepper ; tout autre `BooleanOutputNode` (relais, sortie d'expandeur, bobine Modbus, nœud virtuel) → écrit par la classe (`setValue`) | option |
| `alarmsReset` | entrée d'acquittement du driver | `BooleanOutputNode` : impulsion de 200 ms pendant le réarmement | option |
| `homeSwitch` | capteur d'origine | `BooleanInputNode` (vrai = actif) | option |
| `limitSwitchPositive` / `limitSwitchNegative` | fins de course + / − | `BooleanInputNode` | option |
| `hardwareStepperAlarm` | sortie ALM / FAULT du driver | `BooleanInputNode` (vrai = alarme) | option |
| `position`, `targetPositionReached`, `stepperReady`, `stepperActivated`, `homeDone`, `limitError`, `stepperAlarm`, `zeroSpeed`, `driveInitialised` | recopies d'état pour l'IHM | `BooleanOutputNode` / `Uint32OutputNode` | option |
| `maxSpeed`, `jogSpeed`, `acceleration` | réglages IHM (pas/s, pas/s²), appliqués quand la valeur change, 0 ignoré | `Uint32InputNode` | option |

Règles :

- **STEP et DIR doivent être des GPIO directes** de l'automate (section `tx-bool` du fichier
  matériel). Une sortie relais ou d'expandeur I2C est bien trop lente (et un relais s'userait en quelques
  minutes).
- La classe **n'écrit jamais** les nœuds STEP/DIR : FastAccelStepper possède ces broches. Dans
  `config.json`, déclarer ces nœuds avec `"component": "no-show"` pour que l'IHM ne puisse pas les
  écrire, et ne pas les utiliser dans la logique.
- Les entrées (capteurs, ALM) acceptent n'importe quel nœud booléen. Pour le capteur d'origine, préférer
  une **entrée GPIO directe** (`rx-bool`) : une entrée d'expandeur I2C ajoute sa période de scrutation à
  l'imprécision de l'origine (voir §5). Inverser la logique d'un capteur NF avec l'option `inverted` du
  nœud (ou `activeLow` dans le fichier matériel), pas dans le code.
- La position (`position`) est un `int32` signé stocké dans un `Uint32OutputNode` en complément à deux :
  la lire avec `getValueAsInt32()`. Elle est rafraîchie au plus toutes les 100 ms en mouvement.

### 2.2 Niveaux : sorties 3,3 V de l'ESP32

Les GPIO de l'ESP32 sortent du **3,3 V** (quelques mA conseillés, 20 mA max raisonnable).

| Type de driver | Entrées | Raccordement conseillé |
|---|---|---|
| Modules logiques (A4988, DRV8825, TMC2208/2209, TMC5160 en STEP/DIR…) | logique 3,3 V / 5 V | direct, masse commune ; EN actif bas. |
| Drivers industriels à optocoupleurs (TB6600, DM542, DM556, DM860, hybrides boucle fermée…) | LED d'optocoupleur, prévues pour 5 V (souvent 24 V avec résistance série), 7 à 16 mA | **tampon 5 V** : 74HCT245 / 74AHCT125 alimenté en 5 V (entrées compatibles 3,3 V), ou transistor/MOSFET rapide. |
| Drivers à entrées différentielles, câbles longs (> 2–3 m), environnement bruité | RS422 | émetteur de ligne (AM26LS31, MAX3030…) : PUL+/PUL−, DIR+/DIR−. |

Câblage recommandé pour un driver à optocoupleurs : **cathode commune** — PUL−, DIR−, ENA− au 0 V
du tampon, PUL+, DIR+, ENA+ sur les sorties du tampon 5 V. Les impulsions restent alors actives à
l'état haut. En anode commune (PUL+ au +5 V, PUL− tiré à la masse par un étage collecteur ouvert),
l'étage inverse le signal : le déclarer `activeLow` dans le fichier matériel (voir §2.3).

Sans tampon, une sortie 3,3 V sur une entrée 5 V à résistance intégrée (≈ 270 Ω) ne fournit
qu'environ 7 mA : cela fonctionne parfois, sans marge. À éviter en production.

Autres points :

- **Masse commune** logique entre l'automate et l'étage d'entrée du driver (ou isolation complète par
  les optocoupleurs, alimentation 5 V dédiée côté driver). Ne jamais faire passer le courant moteur
  par la masse logique.
- Câble blindé ou paires torsadées pour STEP/DIR, éloignés des câbles moteur.
- Largeur d'impulsion (FastAccelStepper 2.0 sur ESP32) : environ la moitié de la période (plafonnée
  à quelques µs en RMT), soit ≈ 2,5 µs à 200 kpas/s, davantage en dessous. Le changement de sens est
  précédé d'une pause par la bibliothèque. Vérifier à l'oscilloscope si le driver exige plus.
- Les drivers « boucle fermée » (moteurs hybrides avec codeur) signalent les erreurs de poursuite sur
  leur sortie ALM : la câbler sur `hardwareStepperAlarm`.

### 2.3 Polarités

| Réglage | Défaut | Effet |
|---|---|---|
| `setEnableActiveLow(bool)` | `true` | Driver activé quand la sortie ENABLE est **au repos** (GPIO basse). Convient aux A4988/DRV8825/TMC (EN actif bas) et aux drivers à optocoupleurs (ENA alimenté = moteur libre). Mettre `false` pour un driver activé par un niveau haut. Même convention pour un ENABLE non GPIO : la classe écrit `false` pour activer, `true` pour désactiver. |
| `setDirectionInverted(bool)` | `false` | Inverse le sens : DIR haute = sens négatif. |
| `"activeLow": true` sur une sortie du fichier matériel | — | Étage de sortie inverseur de la carte. Comme FastAccelStepper écrit la GPIO sans passer par le nœud, la classe reporte cette inversion sur DIR et ENABLE ; pour STEP, un avertissement est tracé (impulsions inversées à la borne, en général toléré par les drivers). |

À appeler avant l'initialisation (avant le premier `handleInitializing()`). `setDirectionInverted()`
reste applicable à l'arrêt.

---

## 3. GPIO utilisables

### 3.1 Règles générales

**ESP32 (classique)**

- Interdites : 6 à 11 (mémoire flash SPI).
- Entrées seules : 34, 35, 36, 39 → utilisables pour capteurs / ALM (`rx-bool`), jamais pour STEP/DIR/ENABLE.
- Broches de démarrage (*strapping*), à éviter pour STEP/DIR (un niveau imposé au reset peut empêcher
  le démarrage, ou générer des impulsions parasites au boot) : 0, 2, 5, 12, 15.
- 1 et 3 : UART0 (console série).
- Généralement libres si la carte ne les utilise pas : 4, 13, 14, 16–19, 21–23, 25–27, 32, 33.

**ESP32-S3**

- Interdites : 26 à 32 (flash SPI) ; 33 à 37 sur les modules à PSRAM octale (références R8/R16).
- 19 et 20 : USB natif (D−/D+) ; 43 et 44 : UART0 (console).
- Broches de démarrage : 0, 3, 45, 46.
- 47 et 48 sont alimentées par VDD_SPI : sur les modules à mémoire 1,8 V (références en « V »), elles
  sortent du 1,8 V.
- Généralement libres si la carte ne les utilise pas : 1, 2, 4–18, 21, 38–42.

Les broches réellement libres dépendent de la carte (Ethernet, RS485, I2C, LED…). Elles sont déclarées
par le fichier matériel de la carte (`data/hardware/<carte>.json`), section `tx-bool` pour les sorties
et `rx-bool` pour les entrées.

### 3.2 Par carte (à compléter par les fichiers matériel)

État des fichiers `data/hardware/*.json` au moment de la rédaction. Une GPIO déclarée `tx-bool` peut
aussi piloter un organe de la carte (LED, buzzer, sortie transistor) : vérifier le schéma de la carte
avant d'y câbler un driver, puis documenter dans le fichier matériel les GPIO réservées aux axes.

| Carte | Puce | Utilisées par la carte (fichier matériel) | GPIO déclarées `tx-bool` | Remarques |
|---|---|---|---|---|
| Kincony KC868-A8S | ESP32 | I2C 4/5, RS485 32/33 | 2 | 2 est une broche de démarrage ; très peu de GPIO libres sur cette carte : à compléter d'après son schéma. |
| Kincony KC868-A16 | ESP32 | I2C 4/5, RS485 16/13 | 32, 33, 14 | |
| Kincony KC868-A16 v3 | ESP32-S3 | I2C 9/10, RS485 17/16 | 38, 39, 40, 41, 47, 48 | 47/48 : vérifier la tension (VDD_SPI). |
| Kincony KC868-A8 v3 | ESP32-S3 | I2C 8/18, RS485 38/39 | 13, 14, 40, 48 | |
| Homemaster MiniPLC | ESP32 | I2C 32/33, RS485 16/17, entrées 34/35/36/39 | 5, 4, 2 | 2 et 5 : broches de démarrage. |
| M5Stack StamPLC | ESP32-S3 | I2C 13/15, RS485 39/0 (DE 46) | 4, 5, 1, 2, 44 | 44 = UART0 RX. |
| Waveshare ESP32-S3 POE 8DI 8DO | ESP32-S3 | I2C 42/41, RS485 18/17 (DE 21), entrées 4–11 | 40, 1, 47, 48, 46 | 46 : broche de démarrage. |

Exemple de déclaration dans le fichier matériel (une entrée par GPIO, `id` libre et unique) :

```jsonc
"tx-bool": [
    {"id": 1, "pin": 13},                     // STEP axe X
    {"id": 2, "pin": 14},                     // DIR  axe X
    {"id": 3, "pin": 16, "activeLow": false}  // ENABLE axe X
]
```

Puis dans `config.json`, une section `tx-bool` de la carte référence ces `id` (nœuds STEP/DIR en
`"component": "no-show"`).

---

## 4. Unités, vitesse, accélération

Toutes les grandeurs sont en **pas** (micro-pas du driver) : positions `int32` signées, vitesses en
**pas/s**, accélération en **pas/s²**. `stepsPerUnit` sert à la conversion côté application (ex. 1600
pas/mm), la classe ne l'applique pas.

| Méthode | Unité | Bornes / remarques |
|---|---|---|
| `setMoveSpeed(int32_t)` | pas/s | bornée à [1, 200 000] ; prise en compte au **prochain** déplacement (défaut 2000). |
| `setJogSpeed(uint32_t)` | pas/s | 0 refusé, borné à 200 000 ; un jog en cours prend la nouvelle vitesse au prochain appel (défaut 1000). |
| `setAcceleration(uint32_t)` | pas/s² | bornée à [1, 10 000 000] ; constructeur : défaut 5000. Sert aussi à la décélération. |
| `setSpeedAndRamp(speed, ramp)` | — | **compatibilité SureServo** : vitesse = `speed` × 1000 pas/s, `ramp` ignoré. Préférer `setMoveSpeed()`. |
| `setAccelerationProfile(v, a)` | pas/s, pas/s² | ancienne API : vitesse des déplacements + accélération. |
| `getActualPosition()` / `getPosition()` | pas | position du compteur FastAccelStepper, sans trace série. |
| `getCurrentSpeed()` | pas/s | signée (négative en sens −). |

Exemple : moteur 200 pas/tour en 1/8 de pas = 1600 pas/tour, vis au pas de 5 mm → 320 pas/mm ;
100 mm/s = 32 000 pas/s.

Le délai d'un déplacement (`startGoToPosition(pos, sync, timeout)`, 0 = `setMoveTimeout()`, 60 s par
défaut) est automatiquement allongé à 1,5 × la durée estimée du trajet + 2 s : un long déplacement lent
n'échoue pas par faux délai. `waitForSync` est ignoré (la position d'un pas-à-pas est toujours celle du
compteur).

---

## 5. Prise d'origine

Réglages : `setHomingParameters(direction, vitesse, dégagement)` et `setHomingTimeout(ms)` (30 s par
défaut). `handleHoming(status, vitesse, délai)` peut imposer une vitesse (pas/s) et un délai pour une
prise d'origine donnée.

**Avec capteur (`homeSwitch`)** :

1. si l'axe est déjà sur le capteur, il s'en dégage dans le sens opposé, puis s'arrête ;
2. il avance vers le capteur dans le sens `direction` (−1 par défaut) à la vitesse d'origine (défaut :
   vitesse de jog) ;
3. à la détection, la position est mémorisée et l'axe s'arrête en décélérant (pas de pas perdus) ;
4. le **zéro est le point de détection** : la distance de freinage (v²/2a) n'influe pas sur l'origine ;
5. l'axe revient en `−direction × dégagement` (0 sans dégagement : retour sur le point de détection) ;
6. `getHomeDone()` devient vrai.

Si le capteur n'est pas vu avant le délai, ou si le mouvement est interrompu, la prise d'origine passe
en `DRIVE_FAILED` (message `[AXE] Prise d'origine : délai dépassé …`).

Précision : l'origine est connue à **vitesse × latence de lecture du capteur** près (période du cycle
logique + rafraîchissement du nœud). À 1000 pas/s avec 10 ms de latence : ±10 pas. Pour une origine
précise, prendre une vitesse d'origine faible et un capteur sur GPIO directe.

Un capteur commun origine / fin de course est possible (même nœud dans `homeSwitch` et
`limitSwitchNegative`) : il n'est pas traité comme une alarme pendant la prise d'origine. Prévoir alors
un dégagement suffisant pour libérer le capteur.

**Sans capteur** : la prise d'origine définit immédiatement la position courante comme zéro
(`setCurrentPosition(0)`), sans mouvement. À réserver aux axes positionnés à la main ou contre une butée.

**Perte de l'origine** (`getHomeDone()` repasse à faux, nouvelle prise d'origine nécessaire) : driver
désactivé (`setServoEnabled(false)`, `emergencyStop()`), arrêt brutal en mouvement (fin de course),
alarme du driver, prise d'origine annulée. `setHomeLostOnDisable(false)` conserve l'origine sur
coupure de l'ENABLE et arrêt brutal (axe irréversible, sans risque de glissement) ; l'alarme driver
l'invalide toujours.

---

## 6. Fins de course, butées logicielles, alarmes

- **Fin de course matérielle** : si l'axe se déplace vers une fin de course active, il est arrêté
  immédiatement (`forceStop`), l'opération en cours passe en `DRIVE_FAILED` et une **alarme mémorisée**
  est levée (`ALARM_LIMIT_POSITIVE` = 2 ou `ALARM_LIMIT_NEGATIVE` = 3, nœud `limitError`). Un
  déplacement ou un jog *vers* une fin de course active est refusé ; on peut toujours s'en éloigner
  (après réarmement).
- **Butées logicielles** : `maxRangeSteps` (constructeur) donne [0, maxRangeSteps] ; `setSoftLimits(min,
  max)` / `clearSoftLimits()`. Appliquées **après la prise d'origine** seulement : une cible hors course
  est refusée, le jog s'arrête exactement sur la butée (déplacement vers la limite plutôt que marche
  libre).
- **Alarme driver** (`hardwareStepperAlarm`) : sur front montant, arrêt immédiat, alarme mémorisée
  `ALARM_DRIVER` = 1. Elle reste active même si l'entrée retombe, jusqu'au réarmement.
- `getHasAlarms()`, `getAlarmCode()`, `getAlarmDescription()` ; le rappel `setOnAlarmCallback()` est
  appelé une fois, sur la première alarme, avec un message IHM `[AXE] ALARME AL00x : …`.
- **Réarmement** (`startReset()`) : arrêt de tout mouvement, impulsion `alarmsReset` (200 ms) et
  coupure de l'ENABLE si une alarme driver est en cause (acquittement des drivers boucle fermée),
  réactivation, attente du délai ENABLE ; échoue (`DRIVE_FAILED`) si l'entrée ALM est toujours active
  après 5 s. En cas de succès : alarmes effacées, états `FAILED` remis à `IDLE`, verrou d'arrêt levé,
  axe réinitialisé si besoin.

---

## 7. Contrat `ServoDrive` (utilisation par l'application)

| Appel | Comportement pas-à-pas |
|---|---|
| `process()` | à chaque cycle, pour chaque axe. |
| `handleInitializing(status)` | connexion à FastAccelStepper (broches valides, générateur libre), activation du driver, attente du délai ENABLE (`setEnableDelay`, 100 ms par défaut) ; `COMPLETED` en ~100 ms. Échec si broches invalides, générateur indisponible ou entrée ALM active. |
| `setMoveSpeed(pas/s)` puis `startGoToPosition(pas)` | `getMoveState()` passe immédiatement à `DRIVE_IN_PROGRESS`, puis `DRIVE_IDLE` à l'arrivée exacte sur la cible, `DRIVE_FAILED` en cas de délai, d'interruption ou d'alarme. Refusé (retour `false`, raison dans `getLastError()`) : axe non prêt, alarme, mouvement/origine/jog en cours, erreur précédente non réarmée, arrêt en cours, cible hors course, fin de course dans le sens du trajet. |
| `startHoming()` / `handleHoming(status)` | voir §5. Après un `stopMovement()` pendant la prise d'origine, l'appel suivant à `handleHoming()` rend `ABORTED` sans relancer de mouvement (comme SureServo). |
| `jogForward(limite, zoneLente, vitesseLente)` / `jogReverse(…)` | à appeler **à chaque cycle** tant que le bouton est maintenu ; `INT32_MAX` / `INT32_MIN` = sans limite ; limites, zone lente (pas) et vitesse lente (pas/s) appliquées après l'origine. Retourne `false` en butée ou si refusé. Sécurité « homme mort » : sans appel pendant 500 ms (`setJogWatchdog`), le jog s'arrête. |
| `jogStop()` | arrêt décéléré du jog (sans effet sur un déplacement automatique). |
| `stopMovement()` | arrêt **décéléré** ; déplacement et prise d'origine en cours repassent à `DRIVE_IDLE`. `isImmediateStopActive()` reste vrai tant que l'axe décélère (au moins 100 ms) : attendre qu'il retombe avant `startGoToPosition()`. |
| `emergencyStop()` | arrêt **immédiat** (`forceStop`), driver désactivé, tous les états à `DRIVE_FAILED`, verrou 500 ms, origine perdue. Sortie par `startReset()`. |
| `startReset()` / `getResetState()` | voir §6. |
| `setServoEnabled(bool)` | active/désactive le driver ; désactiver en mouvement arrête brutalement l'axe et fait échouer l'opération. |
| `clearMovementLock()` | remet à `IDLE` les états en échec (sans effacer les alarmes) et réactive le driver. |
| `getIsReady()` | initialisé, driver activé, sans alarme. |
| `setMaxTorque()`, `saveParameters()` | sans objet (retournent `true`) ; le courant se règle sur le driver. |

---

## 8. Nombre d'axes et ressources

Chaque axe occupe un générateur d'impulsions matériel de l'ESP32 :

| Puce | FastAccelStepper 2.0, ESP-IDF ≥ 5.3 (Arduino-ESP32 ≥ 3.1) | FastAccelStepper 0.33, ESP-IDF 5 | ESP-IDF 4.4 (Arduino-ESP32 2.x) |
|---|---|---|---|
| ESP32 | 6 MCPWM/PCNT + 8 RMT (+ I2S) | 8 RMT | 6 MCPWM/PCNT + 8 RMT |
| ESP32-S3 | 4 MCPWM/PCNT + 4 RMT (+ I2S) | 4 RMT | 4 MCPWM/PCNT + 4 RMT |

- Le **RMT est partagé** avec la LED RGB WS2812 des cartes (`rgbLedWrite`/`neopixelWrite`) et tout
  autre usage RMT (télécommande IR…). Par défaut (`DriverType::AUTO`), la classe demande d'abord un
  générateur **MCPWM/PCNT**, puis RMT, puis tout autre (I2S direct) : les premiers axes laissent le RMT
  libre. `setDriverType(DriverType::RMT)` ou `MCPWM_PCNT` impose un type. Avec FastAccelStepper 0.33 sur
  ESP-IDF 5, seul le RMT existe (choix ignoré).
- Le PCNT est aussi utilisé par les compteurs d'impulsions : une application qui en utilise réduit
  d'autant les axes MCPWM/PCNT.
- Fréquence maximale : 200 000 pas/s par axe (MCPWM/PCNT et RMT). La tâche de FastAccelStepper calcule
  les rampes toutes les ~4 ms ; avec beaucoup d'axes rapides, vérifier la charge CPU.
- Débordement : plus de générateur disponible → l'initialisation de l'axe échoue avec
  `FastAccelStepper refuse la GPIO STEP n (…plus de générateur libre)`.

---

## 9. Version de FastAccelStepper

Le code n'utilise que l'API commune à **0.33.x** et **2.0.x** (vérifié par compilation contre les deux,
cibles ESP32 et ESP32-S3, ESP-IDF 4.4 et 5.5). Version recommandée : **2.0.0**
(`gin66/FastAccelStepper@^2.0.0` dans `platformio.ini`), car sur Arduino-ESP32 3.x (ESP-IDF 5) :

- 0.33.x n'a **que le RMT** sur ESP-IDF 5 (8 axes sur ESP32, 4 sur ESP32-S3, en concurrence avec la LED
  WS2812) ; 2.0.0 ajoute MCPWM/PCNT (6 + 8 sur ESP32, 4 + 4 sur ESP32-S3) ;
- le journal de 2.0.0 corrige précisément le multi-axes sur ESP-IDF 5/6 : générateur MCPWM/PCNT qui
  s'emballait dès le deuxième axe (« every stepper after the first emitted continuously and never
  stopped »), changement de sens RMT appliqué avant la fin du segment précédent (pas émis dans le
  mauvais sens), pas supplémentaire sporadique en RMT (corrigé en 1.4.0) ;
- les deux versions exigent ESP-IDF ≥ 5.3 (Arduino-ESP32 ≥ 3.1) sur ESP-IDF 5.

---

## 10. Dans PLC Studio

Un axe pas-à-pas se présente comme un équipement dont les **signaux** se câblent sur les voies de
l'automate, comme les signaux TOR du SureServo :

| Signal de l'axe | Voies proposées | Champ `StepperNodes` |
|---|---|---|
| STEP | voies **GPIO** (`tx-bool`) du contrôleur uniquement | `stepPin` |
| DIR | voies **GPIO** (`tx-bool`) du contrôleur uniquement | `dirPin` |
| ENABLE (option) | GPIO de préférence ; toute sortie TOR possible (relais, expandeur, Modbus) | `enable` |
| Origine, fins de course, alarme driver (options) | toute entrée TOR ; GPIO `rx-bool` conseillée pour l'origine | `homeSwitch`, `limitSwitch*`, `hardwareStepperAlarm` |

Le générateur produit les nœuds STEP/DIR avec `"component": "no-show"`, renseigne `StepperNodes`
dans la classe générée et construit l'axe avec les options de l'équipement : pas par unité, course
(butées logicielles), accélération, sens/vitesse/dégagement d'origine. Les vitesses de consigne des
déplacements sont transmises par `setMoveSpeed()` en pas/s (et non par l'index 0–15 du SureServo).

---

## 11. Exemple

```cpp
#include "StepperMotor/StepperMotor.h"

// Dans initNodePtr() de la classe métier : nœuds GPIO déclarés dans config.json
StepperNodes nx;
nx.stepPin    = stepX;        // HardwareBooleanOutputNode*  (tx-bool, no-show)
nx.dirPin     = dirX;         // HardwareBooleanOutputNode*  (tx-bool, no-show)
nx.enable     = enableX;      // HardwareBooleanOutputNode* ou tout BooleanOutputNode*
nx.homeSwitch = homeX;        // BooleanInputNode*
nx.limitSwitchPositive = limXp;
nx.homeDone   = homeDoneX;    // recopie IHM (option)

axisX = new StepperMotor(320 /* pas/mm */, nx, 64000 /* course en pas */, "AXE_X", 20000 /* pas/s² */);
axisX->setHomingParameters(-1, 2000 /* pas/s */, 400 /* pas */);
axisX->setJogSpeed(4000);

// Deuxième axe sur d'autres GPIO : même code, autre instance, même moteur FastAccelStepper.
axisY = new StepperMotor(320, ny, 32000, "AXE_Y", 20000);

// À chaque cycle
axisX->process();
axisY->process();

// Déplacement
axisX->setMoveSpeed(16000);                 // pas/s
if (axisX->startGoToPosition(32000)) { /* attendre getMoveState() == DRIVE_IDLE */ }
```

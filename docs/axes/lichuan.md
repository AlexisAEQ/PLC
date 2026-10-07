# Axes Lichuan (A6 / A5) en Modbus RTU — `LichuanServo`

Classe : `src/LichuanServo/LichuanServo.h` / `.cpp` — implémente l'interface `ServoDrive`
(mêmes contrats que `SureServo`).
Fichiers matériels : `data/hardware/Lichuan_A6.json`, `data/hardware/Lichuan_A5.json`.

| Famille | Modèles | Paramètres | Adresse Modbus |
|---|---|---|---|
| `LichuanFamily::A6` | LCDA6 / A6 (et A4, même jeu de paramètres) | `PA_xxx` | numéro hexadécimal du paramètre : `PA_1A4` → `0x01A4` = 420 |
| `LichuanFamily::A5` | LCDA630P / A5 / LC10E | `Pgg-nn` | `(gg << 8) \| nn`, `gg` en hexadécimal, `nn` en **décimal** : `P31-00` → `0x3100`, `P11-12` → `0x110C` |

Les adresses ne sont **pas** dans le code C++ : elles sont dans les fichiers matériels, comme
pour SureServo. Le firmware crée les nodes Modbus à partir de `config.json`, la classe métier
remplit une structure `LichuanNodes` avec les pointeurs de nodes et construit le
`LichuanServo`.

---

## 1. Principe

Le variateur est paramétré **une fois** à la mise en service (section 4 et 5). Ensuite
l'automate n'écrit que des registres de fonctionnement, en RAM :

* **mot d'entrées virtuelles** (`PA_1A4` / `P31-00`) : un bit par fonction (servo ON,
  acquittement, départ de déplacement, départ de prise d'origine, arrêt, jog) ;
* **consigne** : position cible 32 bits (`PA_168` / `P11-12`), vitesse en tr/min
  (`PA_190` / `P11-14`), rampe optionnelle ;
* lecture du **mot d'état** (sorties DO `PA_1D3` / sorties virtuelles VDO `P17-32`), du
  **code d'alarme** et de la **position**.

Aucune écriture EEPROM n'est faite à l'exécution (`saveParameters()` renvoie `false`).

### Écritures différées

Un `setValue()` ne part sur le bus qu'au prochain `BorneUniverselle::refresh()`, dans
l'ordre des nodes (pas dans l'ordre des appels). La classe ne lève donc le bit de départ
qu'après avoir constaté que cible et vitesse sont passées (`getLastRefresh()` postérieur à
l'écriture). Toutes les consignes sont ré-écrites de force à chaque déplacement (même si la
valeur n'a pas changé) : un variateur redémarré entre-temps ne repart pas avec une ancienne
cible.

### Unités

* Positions : **unités de commande** du variateur (impulsions par tour réglées par
  `PA_04A` sur A6, `P05-02` ou le rapport électronique `P05-07/P05-09` sur A5).
  Exemple : 10000 impulsions/tour → `startGoToPosition(25000)` = 2,5 tours.
* Vitesses : **tr/min**. `setMoveSpeed()` et `setJogSpeed()` bornent à
  `1..LichuanOptions::maxSpeedRpm` (3000 par défaut ; registre jusqu'à 3000 sur A6, 6000 sur A5).
* Rampe (`setAccelTime(ms)`, optionnelle) : A6 = ms pour 0 → 1000 tr/min (`PA_058`/`PA_059`,
  0..2500) ; A5 = ms du segment 1 (`P11-15`). Tant que `setAccelTime()` n'est pas appelée, la
  rampe réglée dans le variateur est conservée.
* Couple (`setMaxTorque(%)`, optionnel) : écrit `% × 10` (0,1 % du couple nominal) dans
  `PA_05E` (A6) ou `P07-09`/`P07-10` (A5).

---

## 2. Nodes et ids des fichiers matériels

Les ids sont stables (référencés par le catalogue PLC Studio). Même id = même rôle dans les
deux familles ; un id absent n'existe pas pour la famille.

### Lecture 16 bits — `ModbusReadHoldingRegister`

| id | Champ `LichuanNodes` | A6 | A5 | Obligatoire |
|---|---|---|---|---|
| 1 | `status` | `PA_1D3` (467) état des DO | `P17-32` (5920) état des VDO | oui |
| 2 | `alarmCode` | `PA_1C9` (457) code d'erreur | `P0B-34` (2850) code du défaut | oui |
| 3 | `actualSpeed` | `PA_1C1` (449) tr/min | `P0B-00` (2816) tr/min | non |
| 4 | `virtualInputsReadback` | `PA_1A4` (420) relu | `P31-00` (12544) relu | non (recommandé) |
| 5 | — (diagnostic IHM) | `PA_1D2` (466) état des DI | `P0B-03` (2819) état des DI | non |
| 6 | — (diagnostic IHM) | — | `P0B-05` (2821) état des DO | non |

### Écriture 16 bits — `ModbusWriteHoldingRegister` (FC 0x06)

| id | Champ | A6 | A5 | Obligatoire |
|---|---|---|---|---|
| 1 | `virtualInputs` | `PA_1A4` (420) | `P31-00` (12544) | oui |
| 2 | `moveSpeed` | `PA_190` (400) | `P11-14` (4366) | oui |
| 3 | `accelTime` | `PA_058` (88) | `P11-15` (4367) | non |
| 4 | `decelTime` | `PA_059` (89) | — | non |
| 5 | `jogSpeed` | — | `P06-04` (1540) | non (A5 : requis pour le jog natif) |
| 6 | `torqueLimit` | `PA_05E` (94) | `P07-09` (1801) | non |
| 7 | `torqueLimitReverse` | — | `P07-10` (1802) | non |
| 8 | `faultReset` | — | `P0D-01` (3329) | non |

### Lecture 32 bits — `ModbusReadDobbleHoldingRegister` (mot bas d'abord)

| id | Champ | A6 | A5 | Obligatoire |
|---|---|---|---|---|
| 1 | `position` | `PA_1B8/1B9` (440) position **commande** | `P0B-07` (2823) compteur de position absolue | oui |
| 2 | `feedbackPosition` | `PA_1BC/1BD` (444) retour codeur (unités codeur) | `P0B-17` (2833) impulsions de retour (unités codeur) | non (diagnostic) |

### Écriture 32 bits — `ModbusWriteDobbleHoldingRegister` (FC 0x10, mot bas d'abord)

| id | Champ | A6 | A5 | Obligatoire |
|---|---|---|---|---|
| 1 | `targetPosition` | `PA_168/169` (360) position interne 0 | `P11-12` (4364) déplacement du segment 1 | oui |

`registersOffset` vaut **0** dans les deux fichiers : les adresses Lichuan sont déjà les
adresses brutes du protocole (le manuel prévient que certains automates ajoutent 1).

Sorties virtuelles optionnelles (section `tx-bool` virtuelle, pour l'IHM) :
`servoReady`, `inPosition`, `zeroSpeed`, `homeDone`, `servoAlarm`, `driveInitialised`,
`modbusError` (`BooleanOutputNode*`).

### Exemple de sections `config.json` (axe A6, adresse 2)

Les sections Lichuan doivent venir **après** la section de la carte automate (qui initialise
le RS485 et le Modbus).

```json
{ "name": "Axe X lecture", "hardware": "Lichuan_A6", "component": "no-show",
  "type": "ModbusReadHoldingRegister", "address": 2,
  "children": [
    { "id": 1, "name": "axeX status", "refreshInterval": 200 },
    { "id": 2, "name": "axeX alarm", "refreshInterval": 1000 },
    { "id": 4, "name": "axeX vi readback", "refreshInterval": 1000 } ] },
{ "name": "Axe X écriture", "hardware": "Lichuan_A6", "component": "no-show",
  "type": "ModbusWriteHoldingRegister", "address": 2,
  "children": [ { "id": 1, "name": "axeX vi" }, { "id": 2, "name": "axeX speed" } ] },
{ "name": "Axe X position", "hardware": "Lichuan_A6", "component": "no-show",
  "type": "ModbusReadDobbleHoldingRegister", "address": 2,
  "children": [ { "id": 1, "name": "axeX position", "refreshInterval": 200 } ] },
{ "name": "Axe X consigne", "hardware": "Lichuan_A6", "component": "no-show",
  "type": "ModbusWriteDobbleHoldingRegister", "address": 2,
  "children": [ { "id": 1, "name": "axeX target" } ] }
```

### Exemple côté C++

```cpp
#include "LichuanServo/LichuanServo.h"

LichuanNodes n;                       // pointeurs obtenus comme pour ServoNodes (hash / initNodePtr)
n.virtualInputs  = viNode;            // Uint16OutputNode*  (W1)
n.moveSpeed      = speedNode;         // Uint16OutputNode*  (W2)
n.targetPosition = targetNode;        // Uint32OutputNode*  (DW1)
n.position       = positionNode;      // Uint32InputNode*   (DR1)
n.status         = statusNode;        // Uint16InputNode*   (R1)
n.alarmCode      = alarmNode;         // Uint16InputNode*   (R2)
n.virtualInputsReadback = viReadNode; // optionnel (R4)
if (!n.validateRequired()) BorneUniverselle::setPlcBroken("Axe X : nodes manquants");

axeX.reset(new LichuanServo(LichuanFamily::A6, n, "AXE_X"));
// Options non standard (bits, tolérance...) :
LichuanOptions o = LichuanOptions::defaultsFor(LichuanFamily::A5);
o.positionTolerance = 50;
axeY.reset(new LichuanServo(LichuanFamily::A5, nY, o, "AXE_Y"));
```

Aucun état statique : plusieurs axes Lichuan (et SureServo) peuvent coexister.

---

## 3. Bits par défaut et options (`LichuanOptions`)

`LichuanOptions::defaultsFor(famille)` donne les valeurs ci-dessous ; elles correspondent
au paramétrage des sections 4 et 5. Un bit à `LichuanOptions::NONE` (0xFF) n'est pas utilisé.

### Mot d'entrées virtuelles

| Option | Rôle | A6 (`PA_1A4`, bit n = DIn) | A5 (`P31-00`, bit n = VDI n+1) |
|---|---|---|---|
| `viServoOn` | servo ON | bit 0 = DI0, fonction 0 | bit 0 = VDI1, FunIN.1 S-ON |
| `viAlarmReset` | acquittement | bit 1 = DI1, fonction 1 | bit 3 = VDI4, FunIN.2 ALM-RST |
| `viStartMove` | départ déplacement | bit 5 = DI5, fonction 20 POS_LOAD | bit 1 = VDI2, FunIN.28 PosInSen |
| `viHomingStart` | départ prise d'origine | bit 7 = DI7, fonction 16 | bit 2 = VDI3, FunIN.32 HomingStart |
| `viStop` | arrêt (vitesse nulle + maintien) | NONE | bit 4 = VDI5, FunIN.34 |
| `viJogPlus` / `viJogMinus` | jog natif | NONE | bit 5 = VDI6 FunIN.18 / bit 6 = VDI7 FunIN.19 |

### Mot d'état

| Option | Rôle | A6 (`PA_1D3`, bit n = DOn) | A5 (`P17-32`, bit n = VDO n+1) |
|---|---|---|---|
| `stReady` | servo prêt | bit 0 = DO0, fonction 0 S-RDY | bit 0 = VDO1, FunOUT.1 S-RDY |
| `stAlarm` | alarme | bit 1 = DO1, fonction 1 ALM | bit 1 = VDO2, FunOUT.11 ALM |
| `stInPosition` | positionnement terminé | bit 2 = DO2, fonction 2 COIN | bit 2 = VDO3, FunOUT.5 COIN |
| `stZeroSpeed` | vitesse nulle | bit 4 = DO4, fonction 4 ZSP | bit 4 = VDO5, FunOUT.3 ZERO |
| `stHomeDone` | origine faite | bit 5 = DO5, fonction 11 ORG_FOUND | bit 5 = VDO6, FunOUT.16 HomeAttain |

Autres options : `statusInvertMask` (XOR sur le mot d'état), `holdStartBit` (A5 : `true`,
le bit PosInSen reste à 1 pendant le déplacement ; A6 : `false`, impulsion de 100 ms),
`alarmFromCode` (A6 : `true`, code ≠ 0 = alarme ; A5 : `false`, seul le bit ALM compte,
les avertissements Er.9xx ne bloquent pas), `servoOffDuringReset`, `fastPollingDuringMotion`,
`maxSpeedRpm` (3000), `maxAccelMs`, `defaultMoveSpeedRpm` (100), `defaultJogSpeedRpm` (60),
`positionTolerance` (100 unités de commande), `jogWindow` (100000), `keepAliveMs` (2000).

---

## 4. Mise en service — Lichuan A6 (liste de contrôle)

À faire au panneau ou avec le logiciel Lichuan, puis **sauvegarder et redémarrer** le
variateur. (Par Modbus : écrire les paramètres puis `PA_1A7 = 2049` (0x0801) une seule fois —
jamais en fonctionnement.)

**Communication**

| Paramètre | Valeur | Rôle |
|---|---|---|
| `PA_000` | adresse (1..32, unique) | = `"address"` des sections de `config.json` |
| `PA_00D` | **6** (115200 bit/s) | même vitesse que le bus de l'automate (`"speed"` du RS485 de la carte). Format fixe 8 bits, parité paire, 1 stop = `SERIAL_8E1`. Effectif après redémarrage |

**Mode position par communication**

| Paramètre | Valeur | Rôle |
|---|---|---|
| `PA_002` | **0** | mode position |
| `PA_090` | **1** | mode étendu (commande par communication) |
| `PA_091` | **0** | position interne 0 (`PA_168/169`, vitesse `PA_190`) |
| `PA_094` | **0** | position absolue |
| `PA_096` | **2** | chargement de la position sur front montant |
| `PA_08F` | **0** | servo ON par l'entrée (pas d'activation automatique à la mise sous tension) |
| `PA_04A` | ex. 10000 | impulsions de commande par tour (définit l'unité des positions) |
| `PA_058` / `PA_059` | ex. 100 | rampes (ms pour 1000 tr/min) si `setAccelTime()` n'est pas utilisée |

**Entrées DI (fonctions)** — deux DI ne doivent jamais avoir la même fonction (alarme 2)

| Paramètre | Valeur | DI | Source |
|---|---|---|---|
| `PA_080` | **0** servo ON | DI0 | communication (bit 0) |
| `PA_081` | **1** acquittement alarme | DI1 | communication (bit 1) |
| `PA_082` | 2 fin de course horaire | DI2 | câblée |
| `PA_083` | 3 fin de course anti-horaire | DI3 | câblée |
| `PA_084` | 21 arrêt d'urgence (EMG) | DI4 | câblée |
| `PA_085` | **20** chargement de position | DI5 | communication (bit 5) |
| `PA_086` | 17 capteur d'origine | DI6 | câblée |
| `PA_087` | **16** départ retour à zéro | DI7 | communication (bit 7) |
| `PA_1A0` | **163** (0xA3) | — | bits 0, 1, 5, 7 pilotés par `PA_1A4` |
| `PA_1A4` | **0** | — | valeur sauvegardée = toutes les commandes au repos |
| `PA_08E` | 0 | — | pas d'inversion de polarité |

**Sorties DO (fonctions)** — lues dans `PA_1D3`

| Paramètre | Valeur | Bit de `PA_1D3` |
|---|---|---|
| `PA_088` | 0 servo prêt | bit 0 |
| `PA_089` | 1 alarme | bit 1 |
| `PA_08A` | 2 positionnement terminé (COIN) | bit 2 |
| `PA_08B` | 3 frein (inchangé) | bit 3 |
| `PA_08C` | 4 vitesse nulle (ZSP) | bit 4 |
| `PA_08D` | **11** origine trouvée (au lieu de 5) | bit 5 |
| `PA_060` | ≤ `positionTolerance` (défaut 100) | plage de positionnement terminé |
| `PA_063` | **2** (recommandé) | COIN seulement sans commande, à vitesse nulle et dans la plage |
| `PA_061` | 10 (défaut) | seuil de vitesse nulle, tr/min |

**Prise d'origine**

| Paramètre | Valeur | Rôle |
|---|---|---|
| `PA_0A0` | **0** | départ sur l'entrée DI7 (niveau : la prise d'origine s'arrête si le bit retombe) |
| `PA_0A1` | 0..15 (défaut 12) | mode (capteur, fin de course, top Z…), voir annexe du manuel |
| `PA_0A2` / `PA_0A3` | 300 / 50 (défaut) | vitesses de recherche rapide / lente, tr/min |
| `PA_0A4` | 100 | rampe pendant la recherche |
| `PA_0A5` | 0 | décalage de l'origine |
| `PA_0A6` | 0 ou ×100 ms | timeout interne (alarme 6) ; la classe a son propre timeout (100 s par défaut) |

## 5. Mise en service — Lichuan A5 / LCDA630P (liste de contrôle)

À faire au panneau ou avec le logiciel. Les paramètres « arrêt » ne s'écrivent que servo OFF ;
ceux marqués « prise d'effet à la remise sous tension » (VDI/VDO/DI) exigent un redémarrage
(sinon avertissement Er.941).

**Communication**

| Paramètre | Valeur | Rôle |
|---|---|---|
| `P0C-00` | adresse (1..247, unique) | = `"address"` de `config.json` |
| `P0C-02` | **6** (115200 bit/s) | même vitesse que le bus de l'automate. Si le variateur ne propose que 0..5 (57600 max), régler **tout le bus** à 57600 (`"speed"` de la carte automate) et `P0C-02 = 5` |
| `P0C-03` | **1** | parité paire, 1 stop (`SERIAL_8E1`) |
| `P0C-26` | **1** (défaut) | 32 bits : mot bas d'abord (format des nodes 32 bits) |
| `P0C-13` | **0** | **indispensable** : les écritures Modbus ne vont pas en EEPROM (sinon usure et Er.942). À régler en dernier, de préférence au panneau |
| `P0C-09` | **1** | entrées virtuelles VDI par communication |
| `P0C-10` | 0 | VDI à 0 à la mise sous tension |
| `P0C-11` | **1** | sorties virtuelles VDO |
| `P0B-33` | 0 (défaut) | `P0B-34` affiche le défaut courant |

**Mode position multi-segment (segment 1 piloté par communication)**

| Paramètre | Valeur | Rôle |
|---|---|---|
| `P02-00` | **1** | mode position |
| `P05-00` | **2** | consigne multi-segment |
| `P05-02` | ex. 10000 | impulsions de commande par tour (0 = rapport `P05-07/P05-09`) |
| `P11-00` | **2** | exécution sur front de PosInSen, segment choisi par DI (aucune DI CMD → segment 1) |
| `P11-01` | 1 | un seul segment |
| `P11-04` | **1** | déplacement absolu |
| `P11-16` | 0 | pas d'attente après le segment 1 |
| `P05-20` | **1** (recommandé) | COIN seulement quand la consigne filtrée est nulle |
| `P05-21` | ≤ `positionTolerance` | seuil de positionnement terminé |
| `P06-19` | 10 (défaut) | seuil de vitesse nulle |

**Entrées physiques en conflit** (défauts usine à libérer, sinon Er.130 « fonction DI en double »)

| Paramètre | Valeur | Remarque |
|---|---|---|
| `P03-02` | **0** | DI1 (défaut S-ON) |
| `P03-08` | **0** | DI4 (défaut ALM-RST) |
| `P03-12` | **0** | DI6 (défaut FunIN.34 arrêt d'urgence). Pour garder un arrêt câblé sur DI6, laisser 34, ne pas affecter VDI5 et passer `viStop = NONE` |
| `P03-16` | **0** | DI8 (défaut HomingStart) |
| `P03-04` / `P03-06` / `P03-14` | 14 / 15 / 31 | fins de course P-OT / N-OT et capteur d'origine câblés (inchangés) |

**Entrées virtuelles VDI** (`P31-00`, bit n = VDI n+1)

| Fonction | Logique | Bit |
|---|---|---|
| `P17-00` = **1** (S-ON) | `P17-01` = 0 (niveau) | 0 |
| `P17-02` = **28** (PosInSen) | `P17-03` = 0 | 1 |
| `P17-04` = **32** (HomingStart) | `P17-05` = 0 | 2 |
| `P17-06` = **2** (ALM-RST) | `P17-07` = 1 (front) | 3 |
| `P17-08` = **34** (arrêt, vitesse nulle + maintien) | `P17-09` = 0 | 4 |
| `P17-10` = **18** (JOGCMD+) | `P17-11` = 0 | 5 |
| `P17-12` = **19** (JOGCMD-) | `P17-13` = 0 | 6 |

**Sorties virtuelles VDO** (`P17-32`, bit n = VDO n+1)

| Fonction | Logique | Bit |
|---|---|---|
| `P17-33` = **1** (S-RDY) | `P17-34` = 0 | 0 |
| `P17-35` = **11** (ALM) | `P17-36` = 0 | 1 |
| `P17-37` = **5** (COIN) | `P17-38` = 0 | 2 |
| `P17-39` = 0 (libre) | — | 3 |
| `P17-41` = **3** (ZERO) | `P17-42` = 0 | 4 |
| `P17-43` = **16** (HomeAttain) | `P17-44` = 0 | 5 |

**Prise d'origine**

| Paramètre | Valeur | Rôle |
|---|---|---|
| `P05-30` | **1** | prise d'origine lancée par HomingStart (VDI3) |
| `P05-31` | 0..13 | mode (sens, capteur, top Z, butée mécanique…) |
| `P05-32` / `P05-33` | 100 / 10 (défaut) | vitesses rapide / lente, tr/min |
| `P05-34` | 1000 | rampe de recherche, ms |
| `P05-35` | ex. 30000 | durée max de recherche, ms (Er.601 au-delà) |
| `P05-36` | 0 | décalage de l'origine |

---

## 6. Séquences (contrats identiques à SureServo)

* `process()` à **chaque cycle** (lecture des registres déjà rafraîchis, machines à états).
* **Initialisation** (`handleInitializing()` jusqu'à `COMPLETED`) : attend que l'esclave
  réponde (le délai de 15 s est gelé tant qu'il est « mort » pour MyModbus), exige une
  première lecture de l'état et de la position ; alarme présente → acquittement automatique
  (`startReset()`) ; met toutes les commandes à 0 puis le servo ON ; attend 300 ms après
  l'écriture et une lecture « prêt, sans alarme ».
* **Reset** (`startReset()` / `getResetState()`) : acquitte aussi les défauts logiciels ;
  si une alarme est active, servo OFF pendant l'acquittement (requis sur A5 pour les défauts
  n°1/n°2) ; impulsion de 200 ms sur le bit d'acquittement (+ `P0D-01 = 1` si le node existe),
  500 ms de stabilisation, servo ON rétabli, mot d'entrées ré-écrit de force, puis 200 ms
  stables sans alarme (2 s au plus, timeout global 5 s). Succès → `clearMovementLock()`
  (relance l'initialisation après `emergencyStop()`).
* **Déplacement** : `setMoveSpeed(tr/min)` puis, au cycle suivant,
  `startGoToPosition(cible)` ; `getMoveState()` = `DRIVE_IDLE` quand c'est fini.
  Vitesse (+ rampe) et cible sont écrites de force ; quand elles sont confirmées sur le bus,
  le bit de départ est mis à 0 (si besoin), puis à 1 (front montant) : impulsion de 100 ms sur
  A6, maintenu jusqu'à la fin sur A5. Fin = lectures postérieures au départ, |position − cible|
  ≤ `positionTolerance`, bit COIN et vitesse nulle (+ 3 lectures stables si `waitForSync`).
  Timeout (60 s par défaut) → `DRIVE_TIMEOUT` ; alarme / perte de communication →
  `DRIVE_FAILED` (RESET obligatoire, comme SureServo).
* **Prise d'origine** (`handleHoming()`) : bit de départ maintenu jusqu'à la fin (A6 : départ
  sur niveau). Terminé quand le bit « origine faite » est vrai, la vitesse nulle et la position
  stable (3 lectures), après avoir vu le bit retomber ou l'axe bouger. Si le bit reste vrai sans
  aucun mouvement pendant 2 s, l'origine du variateur est considérée déjà valide. Les vitesses
  de recherche sont celles du variateur (`homingSpeed` est ignoré).
* **Jog** (`jogForward()` / `jogReverse()` appelés à chaque cycle tant que le bouton est
  maintenu, `jogStop()` au relâchement ; mêmes règles de butée et de zone lente que SureServo) :
  * A5 : jog natif — vitesse écrite dans `P06-04`, puis bit JOGCMD+ ou JOGCMD- ; arrêt = bit à 0
    (décélération du variateur) ;
  * A6 (pas de jog bidirectionnel par communication documenté) : déplacement absolu vers la
    butée si elle s'applique, sinon vers une fenêtre glissante de `jogWindow` impulsions
    prolongée en marche (en cas de perte de communication l'axe ne dépasse pas la fenêtre) ;
    arrêt par re-consigne (ci-dessous).
* **`stopMovement()`** : retire les commandes (départ, homing, jog), annule le mouvement et la
  prise d'origine en `DRIVE_IDLE` (pas `FAILED`), bloque les nouveaux mouvements au moins
  500 ms **et** jusqu'à la vitesse nulle (`isImmediateStopActive()`).
  * A5 : bit d'arrêt FunIN.34 (« vitesse nulle puis maintien en position ») tenu pendant le
    verrou, puis relâché avec une impulsion d'acquittement (avertissement Er.900 éventuel) ;
  * A6 : **arrêt par re-consigne** — nouvelle cible = position courante extrapolée (âge de la
    lecture compensé, plus la distance de décélération si la rampe est connue), bornée entre la
    position et la cible interrompue, chargée par un nouveau front. Le variateur décélère avec
    sa rampe ; une légère marche arrière vers ce point est possible à grande vitesse ;
  * si l'axe tourne encore après 3 s + la durée de décélération estimée, la classe lève un
    **défaut** (« Arrêt non exécuté ») : la classe métier passe en urgence et appelle
    `emergencyStop()`.
* **`emergencyStop()`** : mot d'entrées à 0 (servo OFF, toutes commandes retirées), écriture
  forcée ; tous les états `DRIVE_FAILED`, `getIsInitialized()` = false ; verrou de 500 ms.
* **Surveillance permanente** :
  * perte de communication (esclave « mort » pour MyModbus ou état non lu depuis 5 s) :
    mouvement / homing en échec, `getHasAlarms()` vrai (« Perte de communication Modbus »),
    message toutes les 10 s ; au retour, le mot d'entrées est ré-écrit ;
  * redémarrage du variateur : relecture de `PA_1A4`/`P31-00` différente du mot écrit (node
    id 4), ou bit « origine faite » perdu après avoir été stable 2 s → défaut jusqu'au reset ;
  * mot d'entrées ré-écrit toutes les 2 s au repos (`keepAliveMs`) ;
  * lecture rapide de l'état et de la position pendant les mouvements, et du code d'alarme dès
    que le bit d'alarme monte (`getHasAlarms()` attend ce code 500 ms au plus pour que le
    libellé soit juste).

---

## 7. Alarmes

`getAlarmCode()` = registre brut (`PA_1C9` / `P0B-34`). `getAlarmDescription()` :

* A6 (code décimal du manuel) : 1 erreur système, 2 configuration DI (fonction en double),
  3 communication Modbus, 4 alimentation de contrôle, 5 FPGA, 6 timeout de prise d'origine,
  12 surtension, 13 sous-tension, 14 surintensité / terre, 15 surchauffe, 16 surcharge,
  18 résistance de freinage, 21 codeur, 24 écart de position excessif, 26 survitesse,
  27 rapport électronique, 29 débordement du compteur d'écart, 36 EEPROM, 38 fins de course,
  39 consigne analogique, 40 batterie codeur absolu ;
* A5 (code « Er.xyz », comparé en hexadécimal sur 12 bits) : 0x101…0xB03 (table complète dans
  le .cpp : Er.130 DI en double, Er.410 sous-tension, Er.601 échec homing, Er.610/620
  surcharges, Er.630 moteur bloqué, Er.942 écritures EEPROM trop fréquentes, Er.B00 écart de
  position…) ;
* inconnu : `Alarme Lichuan 0x%04X (décimal)` ; sans code : « Alarme variateur (code non
  disponible) » ; défauts logiciels : « Perte de communication Modbus avec le variateur »,
  « Variateur redémarré… », « Origine perdue… », « Arrêt non exécuté… ».

---

## 8. Vérifié / supposé

**Vérifié dans les sources** (manuel A6, note Lichuan « A4 position mode communication
control », manuel LCDA630P et chapitre Modbus, note « A5 485 串口案例 », code open source
`Servo_Modbus_RTU_lib` et `Servo_Tool`) :

* adressage des deux familles, FC 03/06/0x10, FC 0x06 interdit sur 32 bits et 0x10 interdit
  sur 16 bits (A5) — les nodes 16 bits du firmware utilisent bien FC 0x06 et les nodes 32 bits
  FC 0x10 mot bas d'abord (modbus-esp8266) ;
* A6 : `PA_1A0`/`PA_1A4` (bit n = DIn), fonctions DI 0, 1, 16, 17, 20, 21, `PA_090`, `PA_094`,
  `PA_096` = 2 (front montant), `PA_168/169` + `PA_190` puis bit 5 (« à remettre à 0 »), bit 7
  = retour à zéro et position remise à 0, `PA_1D3` bit 0 = servo prêt, fonctions DO (11 =
  ORG_FOUND), `PA_1B8`/`PA_1BC`, `PA_1C9`, `PA_0A0` = 0 (arrêt si le niveau retombe), table des
  alarmes, 8E1 fixe, `PA_00D` = 6 → 115200, `PA_1A7` = 0x0801 pour sauvegarder. Le code open
  source utilise à tort la fonction 18 (verrouillage) pour le retour à zéro : c'est **16** ;
* A5 : `P31-00` bit n = VDI n+1 (exemple constructeur 1 → 3 / 1 → 5), `P17-xx`, `P0C-09/11/13/26`,
  `P11-00/04/12/14/15`, `P05-30` = 1 + VDI HomingStart, `P0D-01` = 1, fonctions FunIN 1, 2, 18,
  19, 28, 32, 34 et FunOUT 1, 3, 5, 11, 16, adresses (`Servo_Tool`), défauts usine des DI/DO,
  `P07-09/10` en 0,1 %, `P06-04` = vitesse de jog, liste des défauts Er.xxx.

**Supposé** (à confirmer à la mise en service) :

* A6 : un nouveau front de chargement en marche remplace la cible (arrêt par re-consigne,
  jog) — si ce n'est pas le cas, l'arrêt n'est pas exécuté et la classe lève un défaut ;
  `PA_1C9` contient le numéro d'alarme en décimal et revient à 0 après acquittement ;
  `PA_058/059` s'appliquent au mode position interne (indiqué par la note A4, le manuel A6 les
  marque « S ») ; `PA_1B8` est en unités de commande ; ORG_FOUND reste à 1 après la prise
  d'origine (sinon la classe garde l'origine en mémoire) ; `PA_05E` en 0,1 % ;
* A5 : `P17-32` bit n = VDO n+1 (comme pour les VDI) ; `P0B-34` contient le code « Er » en
  hexadécimal et 0 sans défaut (de toute façon seul le bit ALM déclenche l'alarme) ;
  FunIN.34 n'arrête pas le variateur en défaut et l'avertissement Er.900 s'acquitte par
  ALM-RST ; le jog JOGCMD fonctionne en mode position ; `P0C-02 = 6` (115200) n'existe pas
  sur tous les firmwares (le manuel liste 0..5) ;
* les deux : le mot d'entrées virtuelles et les consignes écrits par Modbus restent en RAM
  (A6 : seul `PA_1A7` sauvegarde ; A5 : avec `P0C-13 = 0`) et le variateur les remet à 0 au
  redémarrage.

---

## 9. Essais de mise en service

1. Lire l'état (`printStatus()`) : servo prêt, aucune alarme, mot d'état cohérent
   (bits 0/2/4 à 1 à l'arrêt).
2. Initialisation : le moteur se bloque (servo ON) ; vérifier la relecture des entrées
   virtuelles (node id 4) égale au mot écrit.
3. Petit déplacement lent (`setMoveSpeed(60)`, ±1 tour) puis retour ; vérifier le sens et
   l'unité.
4. Arrêt : lancer un long déplacement et appeler `stopMovement()` — l'axe doit décélérer et
   s'arrêter sans alarme. Sur A6 c'est l'essai qui valide l'arrêt par re-consigne.
5. Prise d'origine, puis vérifier « origine faite » après 5 s (mémorisation du bit).
6. Jog dans les deux sens, butées logicielles, zone lente.
7. Provoquer une alarme (ex. fin de course) et vérifier libellé + reset.
8. Couper le variateur quelques secondes : perte de communication signalée, puis défaut
   « redémarré » au retour ; RESET puis nouvelle prise d'origine.

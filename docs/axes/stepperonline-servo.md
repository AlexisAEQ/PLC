# Servos StepperOnline A6-RS et T6 (RS485) en Modbus RTU — `StepperOnlineServo`

Classe : `src/StepperOnlineServo/StepperOnlineServo.h` / `.cpp` — implémente l'interface
`ServoDrive` (mêmes contrats que `LichuanServo` et `SureServo`, pilotée par `AxisController`).
Fichiers matériels : `data/hardware/StepperOnline_A6RS.json`, `data/hardware/StepperOnline_T6.json`.

| Famille | Modèles | Paramètres | Adresse Modbus |
|---|---|---|---|
| `StepperOnlineFamily::A6RS` | A6-RS (ex. A6-RS400H2A1-M17, A6-RS750H2A1-M17, A6-RS1000H2B1-M17) | `Cgg.nn` ; surveillance `U40.nn` | `(gg << 8) \| nn`, `gg` et `nn` en **hexadécimal** : `C04.11` → `0x0411`, `C11.06` → `0x1106`, `U40.16` → `0x4016` |
| `StepperOnlineFamily::T6` | T6 version RS485 (ex. T6-1000RS) | `Prx.yy` (type Leadshine) ; mode PR `Pr8.yy` / `Pr9.yy` | `Prx.yy` → `x * 0x100 + 2 * yy + 1` (`Pr3.04` → `0x0309`, `Pr4.00` → `0x0401`) ; `Pr8.yy` → `0x6000 + yy` ; `Pr9.yy` → `0x6200 + yy` |

Les adresses ne sont **pas** dans le code C++ : elles sont dans les fichiers matériels. Le
firmware crée les nodes Modbus à partir de `config.json`, la classe métier (ou le code généré
par PLC Studio) remplit un `StepperOnlineServoNodes` et construit le `StepperOnlineServo`.

---

## 1. Compatibilité : ni Lichuan, ni un seul jeu de registres

* **A6-RS ≠ Lichuan A6** : le Lichuan A6 utilise des paramètres `PA_xxx` (adresse = numéro
  hexadécimal, mot d'entrées virtuelles `PA_1A4`) ; l'A6-RS utilise des groupes `Cgg.nn`
  (communication `C0A`, entrées `C04`, planification de positions `C11`, surveillance `U40`).
* **A6-RS ≠ Lichuan A5 / LCDA630P** : le Lichuan A5 suit l'organisation Inovance
  (`P0C` communication, `P31-00` DI virtuelles, `P17` VDI/VDO, `P11` multi-segments,
  `nn` décimal) ; sur l'A6-RS, `C00.00` est le mode de commande, `C03.00` la source de
  consigne de position, `C0A` la communication, et il n'existe pas de mot de DI virtuelles
  connu : StepperOnline fait piloter les DI en écrivant leur **logique** (`C04.01`, `C04.11`...).
  Un fichier de configuration LinuxCNC de la version EtherCAT présente l'A6 comme un OEM de
  l'Inovance SV660N, mais la numérotation des paramètres (`C00.04`, `C01.00`...) ne correspond
  pas à celle d'Inovance (`H08-xx`...) : rien ne permet de réutiliser `LichuanServo`.
* **T6 ≠ A6-RS** : le T6 est de lignée Leadshine (logiciel Motion Studio, paramètres
  `Prx.yy`, mode PR `0x6000`/`0x6200`, mot de commande `0x1801`). C'est le **même protocole PR**
  que les pas-à-pas StepperOnline RS (`src/StepperOnlineRS`, `docs/axes/stepperonline-rs.md`),
  mais le T6 est un servo : servo ON par la fonction d'entrée `Pr4.00`, registres de
  surveillance du bloc `0x0Bxx` (au lieu de `0x1003` / `0x2203` sur les pas-à-pas).

D'où une seule classe avec une **famille** qui choisit le profil de commande.

---

## 2. Principe

Le variateur est paramétré **une fois** à la mise en service (sections 6 et 7). Ensuite
l'automate n'écrit que des registres de fonctionnement, **en RAM** (aucune sauvegarde EEPROM ;
`saveParameters()` renvoie `false`).

### A6-RS

* **Commandes par logique des DI** (méthode du guide RS485 StepperOnline) : chaque fonction
  utile est affectée à une DI non câblée, et l'automate écrit la logique de cette DI
  (`1` = active, `0` = inactive) :
  `C04.11` (DI5, servo ON), `C04.01` (DI1, départ de planification), `C04.05` (DI2,
  acquittement), `C04.09` (DI3, départ de prise d'origine).
* **Consigne** : groupe 1 de la planification de positions : déplacement 32 bits `C11.06`,
  vitesse `C11.08` (tr/min), rampes `C11.0A` / `C11.0C` (ms). Départ = impulsion de 100 ms sur
  la logique de DI1, une fois cible et vitesse confirmées sur le bus.
* **Lectures** : état des DO `U40.05` (`0x4005`), position absolue `U40.16` (`0x4016`).

### T6

* **Chemin PR0** : mode `Pr9.00` (`0x6200`, écrit à l'initialisation = `0x0011`, absolu +
  interruption), position 32 bits `Pr9.01/9.02` (`0x6201/0x6202`, **mot haut d'abord**),
  vitesse `Pr9.03` (`0x6203`), rampes `Pr9.04/9.05` (`0x6204/0x6205`, ms pour 1000 tr/min).
* **Commande PR** `Pr8.02` (`0x6002`) : `0x10` lance PR0, `0x20` la prise d'origine, `0x40`
  arrêt rapide. Relu : `0x000P` terminé, `0x01P`/`0x020`/`0x040` pas encore pris en compte,
  `0x10P` en cours, `0x200` positionnement en cours.
* **Servo ON** : fonction de SI1 `Pr4.00` (`0x0401`) écrite en RAM : `0x83` (SRV-ON
  normalement fermé → actif sans câblage) / `0x03` (normalement ouvert → inactif).
* **Acquittement** : mot de commande `0x1801` = `0x1111`.
* **Lectures** : `Pr8.02` relu, position commande `Pr8.42/8.43` (`0x602A/0x602B`), alarme
  courante `0x0B03` (supposée).

### Écritures différées

Un `setValue()` ne part sur le bus qu'au prochain `BorneUniverselle::refresh()`. La classe ne
donne le départ (DI1 ou `0x10`) qu'après avoir constaté que cible, vitesse et rampes sont
passées (`getLastRefresh()` postérieur à l'écriture) ; toutes ces valeurs sont ré-écrites de
force à chaque déplacement.

### Mots 32 bits

Les nodes « Dobble » du firmware mettent le **mot bas à l'adresse de base**. Le T6 range le
**mot haut** à l'adresse de base : la classe échange les deux mots
(`StepperOnlineOptions::highWordFirst = true` pour le T6) ; la valeur brute du node n'est pas
la position, utilisez `getActualPosition()` (ou `AxisController::position()`). A6-RS : mot bas
d'abord **supposé** (`highWordFirst = false`).

---

## 3. Nodes et ids des fichiers matériels

Les ids sont les mêmes dans les deux fichiers ; un id absent n'existe pas pour la famille.
« Obl. » = obligatoire (`validateRequired(famille)`).

### Lecture 16 bits — `ModbusReadHoldingRegister`

| id | Champ `StepperOnlineServoNodes` | A6-RS | T6 | Obl. |
|---|---|---|---|---|
| 1 | `status` | `U40.05` (16389) état des DO | `0x0B05` (2821) état du variateur — *commenté, supposé* | A6-RS |
| 2 | `alarmCode` | — (registre non identifié) | `0x0B03` (2819) alarme courante — *supposé* | non |
| 3 | `actualSpeed` | `U40.01` (16385) — *commenté* | `0x0B06` (2822) — *commenté, supposé* | non |
| 4 | `prStatus` | — | `Pr8.02` `0x6002` (24578) relu | T6 |
| 5 | `servoOnReadback` | `C04.11` (1041) relu | `Pr4.00` (1025) relu | non (recommandé) |
| 6 | — (diagnostic IHM) | `U40.04` (16388) état des DI — *commenté* | — | non |

### Écriture 16 bits — `ModbusWriteHoldingRegister` (FC 0x06)

| id | Champ | A6-RS | T6 | Obl. |
|---|---|---|---|---|
| 1 | `servoOn` | `C04.11` (1041) logique DI5 | `Pr4.00` (1025) fonction SI1 | A6-RS (T6 : non, voir § 7) |
| 2 | `moveSpeed` | `C11.08` (4360) | `Pr9.03` (25091) | oui |
| 3 | `accelTime` | `C11.0A` (4362) ms | `Pr9.04` (25092) ms/1000 tr/min | non |
| 4 | `decelTime` | `C11.0C` (4364) ms | `Pr9.05` (25093) ms/1000 tr/min | non |
| 5 | `startMove` | `C04.01` (1025) logique DI1 | — | A6-RS |
| 6 | `homingStart` | `C04.09` (1033) logique DI3 | — | non (requis pour la prise d'origine A6-RS) |
| 7 | `alarmReset` | `C04.05` (1029) logique DI2 | `0x1801` (6145) mot de commande | non |
| 8 | `command` | — | `Pr8.02` `0x6002` (24578) | T6 |
| 9 | `pathMode` | — | `Pr9.00` `0x6200` (25088) | non |
| 10 | `torqueLimit` | — | `Pr0.13` `0x001B` (27), % | non |

### Lecture 32 bits — `ModbusReadDobbleHoldingRegister`

| id | Champ | A6-RS | T6 | Obl. |
|---|---|---|---|---|
| 1 | `position` | `U40.16` (16406) position absolue | `Pr8.42/8.43` (24618) position commande | oui |
| 2 | `feedbackPosition` | — | `0x602C/0x602D` (24620) position moteur — *commenté* | non |

### Écriture 32 bits — `ModbusWriteDobbleHoldingRegister` (FC 0x10)

| id | Champ | A6-RS | T6 | Obl. |
|---|---|---|---|---|
| 1 | `targetPosition` | `C11.06` (4358) déplacement du groupe 1 | `Pr9.01/9.02` (25089) position PR0 | oui |

`registersOffset` vaut **0** : les adresses sont les adresses brutes du protocole (le guide
Siemens StepperOnline précise de ne pas ajouter l'offset 40001).

Sorties virtuelles optionnelles (section `tx-bool` virtuelle, pour l'IHM) : `servoReady`,
`inPosition`, `zeroSpeed`, `homeDone`, `servoAlarm`, `driveInitialised`, `modbusError`.

### Exemple de sections `config.json` (T6, adresse 4)

Les sections doivent venir **après** la section de la carte automate (RS485 et Modbus).

```json
{ "name": "Axe Z lecture", "hardware": "StepperOnline_T6", "component": "no-show",
  "type": "ModbusReadHoldingRegister", "address": 4,
  "children": [
    { "id": 2, "name": "axeZ alarme", "refreshInterval": 500 },
    { "id": 4, "name": "axeZ pr", "refreshInterval": 200 },
    { "id": 5, "name": "axeZ servo relu", "refreshInterval": 1000 } ] },
{ "name": "Axe Z écriture", "hardware": "StepperOnline_T6", "component": "no-show",
  "type": "ModbusWriteHoldingRegister", "address": 4,
  "children": [ { "id": 1, "name": "axeZ servo" }, { "id": 2, "name": "axeZ vitesse" },
                { "id": 3, "name": "axeZ acc" }, { "id": 4, "name": "axeZ dec" },
                { "id": 7, "name": "axeZ cmd" }, { "id": 8, "name": "axeZ pr cmd" },
                { "id": 9, "name": "axeZ pr mode" }, { "id": 10, "name": "axeZ couple" } ] },
{ "name": "Axe Z position", "hardware": "StepperOnline_T6", "component": "no-show",
  "type": "ModbusReadDobbleHoldingRegister", "address": 4,
  "children": [ { "id": 1, "name": "axeZ position", "refreshInterval": 200 } ] },
{ "name": "Axe Z consigne", "hardware": "StepperOnline_T6", "component": "no-show",
  "type": "ModbusWriteDobbleHoldingRegister", "address": 4,
  "children": [ { "id": 1, "name": "axeZ cible" } ] }
```

Le registre `0x6002` est lu (R4) **et** écrit (W8) : deux nodes, même offset.

### Exemple côté C++

```cpp
#include "StepperOnlineServo/StepperOnlineServo.h"

StepperOnlineServoNodes n;               // pointeurs obtenus comme pour LichuanNodes
n.targetPosition = targetNode;           // Uint32OutputNode* (DW1)
n.moveSpeed      = speedNode;            // Uint16OutputNode* (W2)
n.position       = positionNode;         // Uint32InputNode*  (DR1)
n.command        = prCmdNode;            // Uint16OutputNode* (W8)  T6
n.prStatus       = prStatusNode;         // Uint16InputNode*  (R4)  T6
n.servoOn        = servoNode;            // Uint16OutputNode* (W1)
n.alarmReset     = ctrlWordNode;         // Uint16OutputNode* (W7)
n.alarmCode      = alarmNode;            // Uint16InputNode*  (R2)
if (!n.validateRequired(StepperOnlineFamily::T6)) BorneUniverselle::setPlcBroken("Axe Z : nodes manquants");

axeZ.reset(new StepperOnlineServo(StepperOnlineFamily::T6, n, "AXE_Z"));
axeZ->setAccelTime(300);                 // T6 : ms pour 1000 tr/min

// Options non standard :
StepperOnlineOptions o = StepperOnlineOptions::defaultsFor(StepperOnlineFamily::T6);
o.cmdHoming = 0x0021;                    // prise d'origine = position courante (sans mouvement)
axeY.reset(new StepperOnlineServo(StepperOnlineFamily::T6, nY, o, "AXE_Y"));
```

Aucun état statique : plusieurs axes (StepperOnline, Lichuan, SureServo, pas-à-pas) coexistent.

---

## 4. Options (`StepperOnlineOptions::defaultsFor`)

| Option | A6-RS | T6 | Rôle |
|---|---|---|---|
| `inputActiveValue` / `inputInactiveValue` | 1 / 0 | — | logique écrite dans `C04.01/05/09` |
| `servoOnValue` / `servoOffValue` | 1 / 0 | `0x83` / `0x03` | valeurs du node `servoOn` |
| `cmdStartPath` / `cmdHoming` / `cmdStop` | — | `0x10` / `0x20` / `0x40` | codes de `Pr8.02` (`0x21` : origine = position courante) |
| `alarmResetValue` | — | `0x1111` | écrit dans `0x1801` |
| `pathModeWord` | — | `0x0011` | `Pr9.00` : absolu (bits 6-7 = 0), positionnement (1), interruption INS (bit 4) |
| `stReady` / `stAlarm` / `stInPosition` / `stHomeDone` | 0 / 1 / 2 / 3 (DO1..DO4) | 0 / 2 / — / — | bits du node `status` (`NONE` = non utilisé) |
| `statusInvertMask` | 0 | 0 | XOR du mot d'état (sorties à logique inversée) |
| `highWordFirst` | false (supposé) | true | ordre des mots 32 bits |
| `alarmFromCode` | true (sans node : sans effet) | true | code ≠ 0 ⇒ alarme |
| `servoOffDuringReset` | true | true | servo OFF pendant l'acquittement d'une alarme active |
| `maxSpeedRpm` | 3000 | 3000 | borne des vitesses (déplacement et jog) |
| `maxAccelMs` | 65535 | 32767 | borne de `setAccelTime()` |
| `defaultMoveSpeedRpm` / `defaultJogSpeedRpm` | 100 / 60 | 100 / 60 | avant le premier `setMoveSpeed()` / `setJogSpeed()` |
| `torqueUnitsPerPercent` / `maxTorqueValue` | — | 1 / 300 | `Pr0.13` en % |
| `positionTolerance` | 100 | 100 | « position atteinte », unités de commande |
| `stillUnitsPerSecond` | 1000 | 1000 | sans `actualSpeed` : seuil de vitesse nulle (6 tr/min à 10000 unités/tour) |
| `jogWindow` | 100000 | 100000 | jog : fenêtre glissante, unités de commande |

---

## 5. Unités, vitesses, couple

* **Positions** : unités de commande du variateur (A6-RS `C00.02` impulsions par tour, T6
  `Pr0.08` — 10000 par défaut sur les deux).
* **Vitesses** : tr/min. Déplacements : 1..3000 (100 par défaut). Jog : 1..3000 (60 par
  défaut). `setMoveSpeed()` borne à `1..maxSpeedRpm`.
* **Rampes** (`setAccelTime(ms)`, 0 = réglage du variateur conservé) : A6-RS en ms (`C11.0A`
  et `C11.0C`), T6 en ms pour 0 → 1000 tr/min (`Pr9.04` et `Pr9.05`).
* **Couple** (`setMaxTorque(%)`) : T6 seulement (`Pr0.13`, 1re limite de couple, %, adresse
  supposée). A6-RS : paramètre de limite de couple non identifié → `setMaxTorque()` renvoie
  `false` (node `torqueLimit` absent).

---

## 6. Mise en service — A6-RS (liste de contrôle)

À faire une fois, au panneau ou avec le logiciel de réglage A6, puis **enregistrer**
(`C0A.05` = 1 ou fonction « sauvegarder » du logiciel).

1. **Bus** : `C0A.00` adresse Modbus (1..255, unique sur le bus) ; `C0A.01` vitesse
   (1 = 2400 … 7 = 115200, défaut 7) ; `C0A.02` format (0 = 8N1, défaut). Vitesse et format
   identiques à ceux du RS485 de l'automate (un Lichuan A6, fixé en 8E1, impose 8E1 sur tout le
   bus : choisir alors le code « parité paire » de `C0A.02`, voir le manuel). `C0A.01` ne
   prend effet qu'après coupure.
2. **Mode** : `C00.00` = 0 (position) ; `C00.02` = impulsions par tour (unités de commande).
3. **Consigne interne** : `C03.00` = 1 (planification de positions / multi-positions).
4. **Planification** : `C11.00` = 0 (exécution unique), `C11.01` = 0 (absolu), `C11.03` = 1
   et `C11.04` = 1 (groupe 1 seulement ; départ > fin ⇒ alarme ALF7.0), `C11.0E` = 0 (pas
   d'attente après le groupe 1, supposé), `C11.02` (mode de mise à jour) : laisser la valeur
   qui prend en compte une nouvelle consigne à chaque départ.
5. **DI** (fonction, logique enregistrée à 0, **entrées non câblées**) :
   `C04.00` = 19 (DI1 départ de planification), `C04.04` = 2 (DI2 acquittement défaut),
   `C04.08` = 25 (DI3 départ prise d'origine), `C04.10` = 1 (DI5 servo ON, réglage d'usine).
   Fins de course (fonctions 6 / 7) et capteur d'origine (5) sur d'autres DI, câblés.
6. **Prise d'origine** : `C10.01` méthode (34 = top Z, 35 = position courante = origine,
   méthodes avec capteur : voir le manuel) ; `C10.00` = déclenchement par DI (valeur 2 dans
   l'exemple StepperOnline) ; vitesses et décalage dans le groupe `C10`.
7. **DO** (fonctions, groupe `C04` à partir de `C04.30` selon le manuel A6-PN — vérifier dans
   le manuel A6-RS) : DO1 = 1 servo prêt, DO2 = 4 défaut, DO3 = 7 position atteinte,
   DO4 = 9 prise d'origine terminée. Vérifier dans `U40.05` que le bit n correspond à DOn+1.
8. Fenêtre « position atteinte » du variateur cohérente avec `positionTolerance`.

---

## 7. Mise en service — T6 RS485 (liste de contrôle)

À faire une fois avec Motion Studio (câble de réglage) puis enregistrer dans l'EEPROM.

1. **Bus** : `Pr5.31` adresse (1 par défaut) ; `Pr5.30` vitesse (6 = 115200 ; défaut 2) ;
   `Pr5.29` format (4 = 8N1 ; défaut 5). Identiques au RS485 de l'automate. Un utilisateur
   signale un réglage d'usine 38400 8N2 : vérifier dans Motion Studio (« PR5 réglages
   spéciaux »).
2. **Mode** : `Pr0.01` = 6 (mode PR, commande interne ; effectif après redémarrage).
3. **Résolution** : `Pr0.08` impulsions par tour (unités de commande).
4. **Servo ON** : `Pr4.00` (SI1) = `0x03` (SRV-ON normalement ouvert) enregistré, **SI1 non
   câblé** : l'automate écrit `0x83` / `0x03` en RAM. Si le variateur n'applique ce changement
   qu'après redémarrage, enregistrer `Pr4.00` = `0x83` (servo toujours actif) et retirer les
   ids W1 / R5 du projet : `setServoEnabled(false)` est alors refusé et l'arrêt d'urgence se
   limite à `0x40` (la coupure de puissance reste câblée).
5. **Prise d'origine** : `Pr8.10` méthode (capteur d'origine sur une entrée SI, fins de
   course...), `Pr8.11/8.12` position d'origine, `Pr8.15/8.16` vitesses haute / basse,
   `Pr8.17/8.18` rampes. Origine = position courante sans mouvement : option `cmdHoming = 0x21`.
6. **Arrêt rapide** : décélération de la commande `0x40` (`Pr8.23` sur la série RS Leadshine,
   à vérifier).
7. **Couple** : `Pr0.13` (1re limite de couple) écrit par `setMaxTorque()`.

---

## 8. Séquences (contrats identiques à LichuanServo)

* **Initialisation** : attente d'une première lecture (état / `Pr8.02` relu et position) ; si
  alarme : acquittement automatique ; T6 : écriture de `Pr9.00` ; servo ON (écriture forcée),
  300 ms, vérification (lectures postérieures, pas d'alarme, « prêt »). Timeout 15 s (gelé tant
  que l'esclave ne répond pas).
* **Déplacement** : cible, vitesse, rampes écrites et confirmées ; départ (A6-RS : impulsion
  DI1 ; T6 : `0x10`). Fin quand, sur des lectures postérieures au départ : position = cible ±
  `positionTolerance`, bit « position atteinte » (A6-RS), `Pr8.02` relu terminé (T6), vitesse
  nulle (+ 3 lectures stables si `waitForSync`). Échec : alarme, timeout (60 s).
* **Prise d'origine** : A6-RS : logique DI3 maintenue jusqu'au bit « origine faite » (DO4),
  axe arrêté, position stable ; T6 : `0x20`, fin quand `Pr8.02` relu est terminé, axe arrêté,
  position stable, après avoir vu le chemin en cours ou un mouvement (ou 2 s sans rien :
  origine prise sur place). Timeout 100 s. Origine mémorisée par la classe (perdue à un
  redémarrage détecté ou à une perte de communication non vérifiable).
* **Jog** : déplacement absolu vers la butée (si applicable) ou vers une fenêtre glissante de
  `jogWindow`, prolongé avant d'arriver au bout ou si la vitesse change (zone lente).
* **Arrêt** (`stopMovement`, `jogStop`) : T6 : `0x40` ; si `Pr8.02` reste à `0x40` une fois
  l'axe arrêté, `0x1801` = `0x1111`. A6-RS : re-consigne à la position courante extrapolée
  (bornée entre la position lue et la cible interrompue), nouvelle impulsion DI1. Puis attente
  de la vitesse nulle ; nouveaux mouvements refusés au moins 500 ms
  (`isImmediateStopActive()`). Axe toujours en mouvement après le délai : défaut variateur.
* **Arrêt d'urgence** : T6 `0x40` ; servo OFF (écriture forcée) ; toutes les commandes à 0 ;
  états en échec ; le réarmement relance l'initialisation.
* **Réarmement** (`startReset`) : servo OFF si alarme active ; A6-RS impulsion DI2 (200 ms),
  T6 `0x1801 = 0x1111` ; 500 ms ; ré-écriture du servo ON (et de `Pr9.00`) ; vérification.
* **Surveillance** : perte de communication (esclave déclaré mort ou lecture de surveillance
  non rafraîchie depuis 5 s) ; redémarrage du variateur détecté si le registre servo ON relu
  (R5) ≠ valeur écrite (le servo est alors coupé et un défaut signalé) ; A6-RS : perte du bit
  « origine faite » vu stable ⇒ défaut.

---

## 9. Différences entre familles

| | A6-RS | T6 |
|---|---|---|
| Commande | logique des DI (`C04.xx`) | registre PR `0x6002` + `0x1801` |
| Consigne | groupe 1 de `C11` | chemin PR0 `0x6200..0x6205` |
| Fin de mouvement | DO « position atteinte » + position | `Pr8.02` relu + position |
| Origine faite | DO « origine terminée » | mémorisée par la classe |
| Alarme | DO « défaut » (code non lu) | code `0x0B03` (supposé) |
| Arrêt | re-consigne (supposé accepté pendant un mouvement) | `0x40` |
| Couple | non | `Pr0.13` |
| Mots 32 bits | mot bas d'abord (supposé) | mot haut d'abord |
| Bus par défaut | 115200 8N1, adresse 1 | `Pr5.29/5.30/5.31` = 5/2/1 |

---

## 10. Vérifié / supposé

Les sites StepperOnline (omc-stepperonline.com, help.stepperonline.com), les manuels sur
Amazon / Scribd / manuals.plus et leadshine.com n'étaient pas accessibles depuis
l'environnement de développement : informations tirées des extraits des moteurs de recherche
et de dépôts publics.

**A6-RS — vérifié (guide RS485 et articles StepperOnline, extraits) :**
adresse = `(gg << 8) | nn` hexadécimal (`C00.00` → `0000`, `C03.20` → `0320`) ; `C0A.00`
adresse 1..255 (défaut 1), `C0A.01` vitesse 1..7 (7 = 115200, défaut), `C0A.02` format
(0 = 8N1) ; écritures RS485 perdues à la coupure sans `C0A.05 = 1` ; `C04.11` = 1 / 0 met le
moteur en / hors service, `C04.01` = 1 déclenche DI1 ; paires fonction/logique des DI
(`C04.00/01` … `C04.10/11`) ; fonctions DI 1 servo ON, 2 acquittement, 4 arrêt d'urgence,
5 capteur d'origine, 6/7 fins de course, 19 départ de planification, 20 pause, 25 départ
d'origine, 34 pas incrémental ; `C03.00` = 1 multi-positions ; `C11.00` … `C11.04` ;
groupe 1 : `C11.06` déplacement, `C11.08` vitesse, `C11.0A` / `C11.0C` rampes (ms) ;
`C10.01` méthode d'origine (34, 35), `C10.00` = 2 ; lectures `U40.01` vitesse, `U40.03`
couple (0,1 %), `U40.06` tension bus, `U40.16` position absolue, `U40.30` température.

**A6-RS — supposé (à confirmer à la mise en service) :**
`U40.05` = état des DO et `U40.04` = état des DI (manuel A6-PN) avec bit n = DOn+1 ;
fonctions DO 1 / 4 / 7 / 9 et leur emplacement (`C04.30`…, manuel A6-PN) ; `C11.06` en 32 bits
mot bas d'abord, `C11.08/0A/0C` en 16 bits ; `C11.0E` = attente du groupe 1 ; `U40.16` en
32 bits mot bas d'abord ; qu'un nouveau front sur DI1 pendant un mouvement recharge la
consigne (arrêt par re-consigne et jog à fenêtre glissante) ; que le relâchement de DI3 ou
la re-consigne arrête une prise d'origine ; registre du code d'alarme non identifié.

**T6 — vérifié :**
correspondance `Prx.yy` → adresse (`Pr3.04` = `0x0309`, `Pr3.12` = `0x0319`, manuel T6) ;
`Pr5.29/5.30/5.31` (défauts 5/2/1 ; 4 = 8N1, 6 = 115200, article S7-1200 StepperOnline) ;
`Pr8.43` position commande basse en `0x602B` (lue par l'exemple S7-1200) ; codes des entrées
SRV-ON `03h` / `83h`, A-CLR `04h`, CTRG `20h` ; 16 chemins PR pilotables par RS485 ; logiciel
Motion Studio (rebrand Leadshine, dépôt public `StepperOnline/Tunning-Software`). Protocole PR
Leadshine (`0x6002` : 0x10/0x20/0x40, codes relus 0x000P/0x10P/0x200 ; `0x6200..0x6205` ;
`0x1801` = 0x1111) vérifié sur les manuels Leadshine EM2RS / iEM-RS / iSV2-RS et repris par
`StepperOnlineRS` ; `Pr0.00` → `0x0001` sur l'EL6.

**T6 — supposé :**
`0x0B03` alarme courante, `0x0B05` état (bit 0 prêt, bit 2 défaut), `0x0B06` vitesse
(adresses du servo intégré Leadshine iSV2-RS ; le manuel T6 place la position moteur codeur
en `0x0B1C/0x0B1D`, même bloc) ; `0x602C` position moteur ; que l'écriture de `Pr4.00` prenne
effet immédiatement (sinon, voir § 7.4) ; bit 4 INS de `Pr9.00` (« toujours 1 » selon le
manuel iCL-RS) ; `Pr0.13` 1re limite de couple en % (organisation Panasonic / Leadshine) ;
`Pr0.08` impulsions par tour ; `Pr8.23` décélération de l'arrêt rapide ; codes d'alarme non
traduits (afficher le code et consulter le manuel).

---

## 11. Essais de mise en service

1. **Lecture seule** (outil Modbus ou IHM) : lire chaque registre du fichier matériel ; une
   exception Modbus (adresse illégale) sur un registre supposé ⇒ retirer son id du projet
   (sinon l'esclave est déclaré mort). Faire tourner l'axe avec le logiciel de réglage : la
   position doit suivre ; si elle saute de 65536 en 65536, inverser `highWordFirst`.
2. **Initialisation** : « Variateur initialisé » et « Servo prêt » à 1, moteur alimenté.
3. **Jog lent** dans les deux sens, relâchement : arrêt franc, pas d'alarme ; relancer
   aussitôt (verrou de 500 ms).
4. **Prise d'origine** : position à la valeur d'origine, « Origine faite » à 1.
5. **Déplacements** absolus courts puis longs ; **arrêt en cours de déplacement** (A6-RS :
   vérifie que la re-consigne est acceptée en mouvement ; sinon l'arrêt se termine en défaut
   « Arrêt non exécuté ») ; arrêt d'urgence puis réarmement.
6. **Redémarrage du variateur** servo ON : défaut « Variateur redémarré » signalé.
7. **Bus débranché** pendant un jog : l'axe s'arrête au plus au bout de la fenêtre
   (`jogWindow`) ; « Erreur Modbus » et alarme ; rebrancher, réarmer.

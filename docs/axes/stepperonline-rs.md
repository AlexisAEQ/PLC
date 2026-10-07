# Axes pas-à-pas StepperOnline RS (DM556RS / DM882RS / CL57RS / CL86RS) — `StepperOnlineRS`

Classe : `src/StepperOnlineRS/StepperOnlineRS.h` / `.cpp` — implémente l'interface `ServoDrive`
(mêmes contrats que `LichuanServo` et `SureServo`, pilotée par `AxisController`).
Fichiers matériels : `data/hardware/StepperOnline_DM_RS.json` (boucle ouverte) et
`data/hardware/StepperOnline_CL_RS.json` (boucle fermée).

| Modèle (`StepperOnlineRSModel`) | Boucle | Alimentation | Courant | Fichier matériel |
|---|---|---|---|---|
| `DM556RS` | ouverte | 24-48 VDC | 0,1-5,6 A | `StepperOnline_DM_RS` |
| `DM882RS` | ouverte | 30-110 VDC / 18-80 VAC | 2,1-8,2 A | `StepperOnline_DM_RS` |
| `CL57RS` | fermée (codeur) | 24-48 VDC | 0,5-7,0 A | `StepperOnline_CL_RS` |
| `CL86RS` | fermée (codeur) | 30-110 VDC / 18-80 VAC | 2,1-8,0 A | `StepperOnline_CL_RS` |

Les quatre variateurs reprennent le protocole Modbus RTU des variateurs **Leadshine RS**
(paramètres `Prx.yy`, table de trajets « PR » `Pr8.xx` / `Pr9.xx`, mot de commande `0x1801`,
alarme `0x2203`) : une seule classe, une option « modèle » pour les différences (section 3).
Les adresses ne sont **pas** dans le code C++ : elles sont dans les fichiers matériels.

---

## 1. Principe

Le variateur contient son propre contrôleur de position (mode PR, « indexer »). Il est
paramétré **une fois** à la mise en service (section 5). Ensuite l'automate n'écrit qu'en
RAM :

* **trajet PR0** : mode `Pr9.00` (`0x6200` = `0x0001`, positionnement absolu), position cible
  32 bits `Pr9.01/9.02` (`0x6201/0x6202`), vitesse `Pr9.03` (`0x6203`, tr/min), rampes
  `Pr9.04/9.05` (`0x6204/0x6205`, ms pour 1000 tr/min) ;
* **registre de déclenchement** `Pr8.02` (`0x6002`) : `0x10` lance PR0, `0x20` lance la prise
  d'origine, `0x21` prend la position actuelle comme origine, `0x40` arrêt rapide
  (décélération `Pr8.23`) ;
* **mot de commande** `0x1801` : `0x1111` acquitte l'alarme courante ;
* boucle fermée seulement : **alimentation forcée par logiciel** `Pr0.07` (`0x000F`) = 1.

Il lit le **mot d'état** `0x1003`, le **registre de déclenchement relu** (`0x6002`), le **code
d'alarme** `0x2203` et la **position commande** `Pr8.42/8.43` (`0x602A/0x602B`).

Aucune écriture EEPROM n'est faite à l'exécution (`saveParameters()` renvoie `false`).

### Écritures différées

Un `setValue()` ne part sur le bus qu'au prochain `BorneUniverselle::refresh()`, dans l'ordre
des nodes. La classe n'écrit le déclenchement `0x10` qu'après avoir constaté que mode, cible,
vitesse et rampes sont passés (`getLastRefresh()` postérieur à l'écriture). Toutes ces valeurs
sont ré-écrites de force à chaque déplacement : un variateur redémarré ne repart pas avec une
ancienne cible.

### Mots 32 bits (ordre des mots)

Le variateur range le mot de **poids fort à l'adresse basse** (`Pr9.01` = position haute,
`Pr9.02` = position basse ; idem `Pr8.42/8.43`). Les nodes `ModbusReadDobbleHoldingRegister` /
`ModbusWriteDobbleHoldingRegister` du firmware (`src/Node/Node.cpp`) lisent et écrivent le mot
de **poids faible à l'adresse basse** (`valeur = reg[offset+1] << 16 | reg[offset]`).
La classe échange donc les deux mots (`StepperOnlineRSOptions::highWordFirst = true`) : la
valeur brute du node n'est **pas** la position ; utilisez `getActualPosition()` (ou l'entrée
`position` de l'axe dans PLC Studio, qui passe par la classe). L'écriture de la cible part en
une seule trame FC 0x10 de 2 registres.

### Unités

* Positions : **pas** = impulsions de commande (`Pr0.00` « impulsions par tour », usine 10000
  sur les variateurs Leadshine RS — à vérifier sur le variateur). Exemple : 10000 pas/tour →
  `startGoToPosition(25000)` = 2,5 tours. La position est remise à 0 par la prise d'origine.
* Vitesses (déplacement **et** jog) : **tr/min**, bornées à `1..maxSpeedRpm` (3000 par défaut).
  Défauts : déplacement 120 tr/min, jog 60 tr/min. Un pas-à-pas perd du couple avec la
  vitesse : la vitesse utile dépend du moteur et de la tension (souvent < 1000-1500 tr/min).
* Rampe : `setAccelTime(ms)` = ms pour 0 → 1000 tr/min, écrite dans `Pr9.04` **et** `Pr9.05`
  à chaque déplacement / jog. Défaut **300 ms** (`defaultAccelMs`) ; 0 = rampe réglée dans le
  variateur conservée.
* Couple (`setMaxTorque(%)`, optionnel) : un pas-à-pas n'a pas de limite de couple ; la classe
  réduit le **courant crête** `Pr5.00` (`0x0191`, 0,1 A, en RAM) à `% × courant de référence`
  (plancher 20 %, jamais au-dessus du courant de référence ni du maximum du modèle). Il faut
  le node `peakCurrent` (W9) **et** un courant de référence (`setTorqueReferenceCurrent(courant
  crête du moteur en 0,1 A)` ou `torqueRefCurrent` dans les options) ; sinon `setMaxTorque()`
  renvoie `false` (couple non géré). Après une initialisation, `AxisController` ré-écrit le
  couple demandé.

---

## 2. Nodes et ids des fichiers matériels

Ids stables (référencés par le catalogue PLC Studio). Même id = même rôle dans les deux
fichiers ; un id absent n'existe pas pour la famille. Offsets = adresses du manuel converties
en décimal, `registersOffset` = 0.

### Lecture 16 bits — `ModbusReadHoldingRegister`

| id | Champ `StepperOnlineRSNodes` | Registre | DM | CL | Obligatoire |
|---|---|---|---|---|---|
| 1 | `status` | `0x1003` (4099) état du mouvement | ✓ | ✓ | oui |
| 2 | `alarmCode` | `0x2203` (8707) alarme courante | ✓ | ✓ | oui |
| 3 | `triggerStatus` | `Pr8.02` `0x6002` (24578) relu | ✓ | ✓ | oui |
| 4 | `prWarning` | `Pr8.29` `0x601D` (24605) avertissement PR | ✓ | ✓ | non (recommandé) |
| 5 | `softwareEnableReadback` | `Pr0.07` `0x000F` (15) relu | — | ✓ | non (recommandé, détection de redémarrage) |
| 6 | `actualSpeed` | `0x1047` (4167) vitesse, mot bas, tr/min signés | ✓ | ✓ | non (diagnostic) |

### Écriture 16 bits — `ModbusWriteHoldingRegister` (FC 0x06)

| id | Champ | Registre | DM | CL | Obligatoire |
|---|---|---|---|---|---|
| 1 | `prMode` | `Pr9.00` `0x6200` (25088) | ✓ | ✓ | oui |
| 2 | `moveSpeed` | `Pr9.03` `0x6203` (25091) tr/min | ✓ | ✓ | oui |
| 3 | `accelTime` | `Pr9.04` `0x6204` (25092) | ✓ | ✓ | non (recommandé) |
| 4 | `decelTime` | `Pr9.05` `0x6205` (25093) | ✓ | ✓ | non (recommandé) |
| 5 | `trigger` | `Pr8.02` `0x6002` (24578) | ✓ | ✓ | oui |
| 6 | `controlWord` | `0x1801` (6145) | ✓ | ✓ | oui |
| 7 | `softwareEnable` | `Pr0.07` `0x000F` (15) | — | ✓ | non (CL : recommandé) |
| 8 | `quickStopTime` | `Pr8.23` `0x6017` (24599) ms | ✓ | ✓ | non (écrit seulement si `quickStopMs` > 0) |
| 9 | `peakCurrent` | `Pr5.00` `0x0191` (401) 0,1 A | ✓ | ✓ | non (couple) |

### Lecture 32 bits — `ModbusReadDobbleHoldingRegister` (variateur : poids fort d'abord)

| id | Champ | Registre | DM | CL | Obligatoire |
|---|---|---|---|---|---|
| 1 | `position` | `Pr8.42/8.43` `0x602A` (24618) position commande, pas | ✓ | ✓ | oui |
| 2 | `feedbackPosition` | `Pr8.44/8.45` `0x602C` (24620) position réelle (codeur) | — | ✓ | non (diagnostic, unités codeur possibles) |

### Écriture 32 bits — `ModbusWriteDobbleHoldingRegister` (FC 0x10, poids fort d'abord)

| id | Champ | Registre | Obligatoire |
|---|---|---|---|
| 1 | `targetPosition` | `Pr9.01/9.02` `0x6201` (25089) position PR0, pas | oui |

Le registre `0x6002` est à la fois lu (id R3) et écrit (id W5) : deux nodes, même offset.

Sorties virtuelles optionnelles (section `tx-bool` virtuelle, pour l'IHM) : `servoReady`,
`inPosition`, `zeroSpeed` (= pas en mouvement), `homeDone`, `servoAlarm`, `driveInitialised`,
`modbusError` (`BooleanOutputNode*`).

### Exemple de sections `config.json` (CL57RS, adresse 3)

Les sections viennent **après** la section de la carte automate (qui initialise le RS485).

```json
{ "name": "Axe Z lecture", "hardware": "StepperOnline_CL_RS", "component": "no-show",
  "type": "ModbusReadHoldingRegister", "address": 3,
  "children": [
    { "id": 1, "name": "axeZ status", "refreshInterval": 200 },
    { "id": 2, "name": "axeZ alarm", "refreshInterval": 500 },
    { "id": 3, "name": "axeZ trigger read", "refreshInterval": 200 },
    { "id": 4, "name": "axeZ pr warning", "refreshInterval": 500 },
    { "id": 5, "name": "axeZ enable read", "refreshInterval": 1000 } ] },
{ "name": "Axe Z écriture", "hardware": "StepperOnline_CL_RS", "component": "no-show",
  "type": "ModbusWriteHoldingRegister", "address": 3,
  "children": [ { "id": 1, "name": "axeZ pr mode" }, { "id": 2, "name": "axeZ speed" },
                { "id": 3, "name": "axeZ accel" }, { "id": 4, "name": "axeZ decel" },
                { "id": 5, "name": "axeZ trigger" }, { "id": 6, "name": "axeZ control" },
                { "id": 7, "name": "axeZ enable" } ] },
{ "name": "Axe Z position", "hardware": "StepperOnline_CL_RS", "component": "no-show",
  "type": "ModbusReadDobbleHoldingRegister", "address": 3,
  "children": [ { "id": 1, "name": "axeZ position", "refreshInterval": 200 } ] },
{ "name": "Axe Z consigne", "hardware": "StepperOnline_CL_RS", "component": "no-show",
  "type": "ModbusWriteDobbleHoldingRegister", "address": 3,
  "children": [ { "id": 1, "name": "axeZ target" } ] }
```

### Exemple côté C++

```cpp
#include "StepperOnlineRS/StepperOnlineRS.h"

StepperOnlineRSNodes n;             // pointeurs obtenus comme pour LichuanNodes
n.prMode = modeNode;      n.moveSpeed = speedNode;     n.trigger = trigWNode;
n.controlWord = cwNode;   n.targetPosition = targetNode;
n.status = statusNode;    n.alarmCode = alarmNode;     n.triggerStatus = trigRNode;
n.position = positionNode;
n.accelTime = accNode;    n.decelTime = decNode;       // optionnels
n.softwareEnable = enNode; n.softwareEnableReadback = enReadNode;   // CL-RS
if (!n.validateRequired()) BorneUniverselle::setPlcBroken("Axe Z : nodes manquants");

axeZ.reset(new StepperOnlineRS(StepperOnlineRSModel::CL57RS, n, "AXE_Z"));
axeZ->setAccelTime(200);                 // ms pour 1000 tr/min (optionnel, défaut 300)
// Options non standard :
StepperOnlineRSOptions o = StepperOnlineRSOptions::defaultsFor(StepperOnlineRSModel::DM556RS);
o.homingSetsCurrentPosition = true;      // pas de capteur : origine = position actuelle
o.quickStopMs = 150;
axeX.reset(new StepperOnlineRS(StepperOnlineRSModel::DM556RS, nX, o, "AXE_X"));
```

Aucun état statique : plusieurs axes (StepperOnline, Lichuan, SureServo, pas-à-pas) coexistent.

---

## 3. Options (`StepperOnlineRSOptions`) et différences entre modèles

`StepperOnlineRSOptions::defaultsFor(modèle)` :

| Option | DM556RS | DM882RS | CL57RS | CL86RS | Rôle |
|---|---|---|---|---|---|
| `requireEnableBit` | false | false | true | true | « prêt » exige le bit 1 (alimenté) de `0x1003` |
| `maxPeakCurrent` (0,1 A) | 56 | 82 | 70 | 80 | borne de `setMaxTorque()` |

Communes : `highWordFirst = true`, `prMode = 0x0001`, bits d'état 0/1/2/4/5/6 (défaut,
alimenté, en mouvement, commande terminée, trajet terminé, origine faite), `maxSpeedRpm = 3000`,
`defaultMoveSpeedRpm = 120`, `defaultJogSpeedRpm = 60`, `defaultAccelMs = 300`,
`maxAccelMs = 10000`, `quickStopMs = 0` (Pr8.23 du variateur conservé), `torqueRefCurrent = 0`
(couple non géré), `minTorquePercent = 20`, `positionTolerance = 10` pas, `stepsPerRev = 10000`,
`jogWindowMinSteps = 20000`, `jogWindowMs = 2000`, `homingSetsCurrentPosition = false`,
`disableOnEmergency = false`, `resetAfterQuickStop = true`, `fastPollingDuringMotion = true`.

Différences de registres : les CL-RS ont en plus `Pr0.07` (alimentation forcée par logiciel,
écrit à 1 à l'initialisation) et la position réelle codeur `Pr8.44/8.45`. Les DM-RS (boucle
ouverte) sont alimentés tant que leur entrée d'activation ne les coupe pas.

---

## 4. Séquences

* **Initialisation** : attente d'une première lecture de l'état, de l'alarme, du déclenchement
  et de la position ; si alarme : acquittement automatique (`0x1111`) ; écriture de `Pr0.07 = 1`
  (CL), de `Pr8.23` (si `quickStopMs`) et du mode PR0 ; vérification : pas d'alarme et, si
  `requireEnableBit`, bit « alimenté ». Timeout 15 s (gelé tant que l'esclave ne répond pas).
* **Déplacement** (`startGoToPosition`) : écriture mode/cible/vitesse/rampes, confirmation,
  déclenchement `0x10`. Fin quand, sur des lectures postérieures au déclenchement : bit
  « en mouvement » à 0, registre de déclenchement ni « en attente » (`0x001x`) ni « en cours »
  (`0x01xx`) ni « attente positionnement » (`0x0200`), position = cible ± `positionTolerance`
  (+ 3 lectures stables si `waitForSync`). Échec si alarme, avertissement PR (fin de course...),
  axe arrêté hors cible pendant plus d'1 s, ou timeout (60 s par défaut).
* **Prise d'origine** : `0x20` (méthode `Pr8.10`, vitesses `Pr8.15/8.16` du variateur ;
  `homingSpeed` de `handleHoming()` est ignoré) ; terminée quand le bit 6 « origine faite » est
  à 1, l'axe arrêté, la position stable, après avoir vu le bit à 0 ou un mouvement (ou 2 s sans
  mouvement : origine déjà valide). Avec `homingSetsCurrentPosition` : `0x21`, terminée quand
  la position relue vaut 0. Échec : alarme, `Pr8.29` = `0x100` (fin de course) / `0x102`
  (dépassement de course), timeout 100 s.
* **Jog** : trajet PR0 absolu vers la butée (si applicable) ou vers une fenêtre glissante
  (distance parcourue en `jogWindowMs`, au moins `jogWindowMinSteps`), re-déclenché avant
  d'arriver au bout ou si la vitesse change (zone lente). Le jog RS485 natif (`0x4001/0x4002`
  dans `0x1801`) n'est pas utilisé : il faut le ré-écrire toutes les 50 ms. En cas de perte du
  bus, l'axe s'arrête au plus à la fin de la fenêtre.
* **Arrêt** (`stopMovement`, `jogStop`) : `0x40` (décélération `Pr8.23`), puis attente de l'arrêt
  de l'axe ; nouveaux mouvements refusés au moins 500 ms et jusqu'à l'arrêt
  (`isImmediateStopActive()`). Si le registre de déclenchement reste à `0x40` une fois l'axe
  arrêté (état d'arrêt verrouillé), `0x1111` est écrit (option `resetAfterQuickStop`, sans
  alarme présente). Axe toujours en mouvement après le délai : défaut variateur.
* **Arrêt d'urgence** (`emergencyStop`) : `0x40` ; le courant n'est coupé (`Pr0.07 = 0`) que si
  `disableOnEmergency` (un pas-à-pas non alimenté perd son couple de maintien). Tous les états
  passent en échec ; le réarmement relance l'initialisation.
* **Réarmement** (`startReset`) : `0x1111`, 500 ms, ré-écriture de `Pr0.07` / `Pr8.23`,
  vérification (pas d'alarme, alimenté). Le manuel indique que certaines alarmes (surintensité)
  ne s'acquittent qu'en coupant l'alimentation du variateur : le réarmement échoue alors.
* **Surveillance** : perte de communication (esclave déclaré mort ou état non relu depuis
  5 s) ; redémarrage du variateur détecté si `Pr0.07` relu ≠ 1 (CL, node R5) — l'origine est
  alors perdue. Sans node R5, l'origine est perdue à toute perte de communication.

---

## 5. Mise en service (liste de contrôle, une fois par variateur)

Avec le logiciel StepperOnline MotionStudio (RS232 ou RS485) ou par Modbus, puis
**sauvegarde EEPROM** (`0x2211` dans `0x1801`, ou bouton « Save » du logiciel) et
**coupure/remise sous tension** (obligatoire pour les fonctions d'entrées).

1. **Adresse Modbus** : interrupteurs DIP (SW1-SW5 = 1..31 ; tous ON = `Pr5.23`, `0x01BF`).
   Une adresse par variateur sur le bus.
2. **Vitesse du bus** : DIP SW6-SW7, ou tous OFF = `Pr5.22` (`0x01BD`) :
   0 = 2400, 1 = 4800, 2 = 9600, 3 = 19200, **4 = 38400 (usine)**, 5 = 57600, 6 = 115200.
3. **Format** `Pr5.24` (`0x01C1`) : 0 = 8E2, 1 = 8O2, 2 = 8E1, 3 = 8O1, **4 = 8N1 (usine)**,
   5 = 8N2. Tout le bus doit avoir le même format que l'automate (avec un Lichuan A6, fixe en
   8E1, régler `Pr5.24 = 2`).
4. **Résistance de fin de ligne** : sur le dernier variateur du bus seulement (DIP selon le
   modèle).
5. **Impulsions par tour** `Pr0.00` (`0x0001`) : unité des positions (régler
   `stepsPerRev` en conséquence si ≠ 10000, pour la fenêtre de jog). **Sens** `Pr0.03`
   (`0x0007`).
6. **Courant** : courant crête `Pr5.00` (`0x0191`, 0,1 A) adapté au **moteur** (pas au
   variateur). C'est aussi la référence à donner à `setTorqueReferenceCurrent()` si le couple
   est piloté.
7. **Prise d'origine** : `Pr8.10` (`0x600A`) bit 0 sens, bit 1 aller à `Pr8.13/8.14` après
   l'origine, bit 2 = 1 capteur d'origine / 0 fin de course ; vitesses `Pr8.15` / `Pr8.16`
   (`0x600F` / `0x6010`, tr/min), rampes `Pr8.17` / `Pr8.18`, position de l'origine
   `Pr8.11/8.12`. Entrées : ORG (`0x27`), POT (`0x25`), NOT (`0x26`) affectées aux entrées
   câblées (`Pr4.0x` ; `+0x80` = contact NF). Sans capteur : option
   `homingSetsCurrentPosition`.
8. **Trajets PR** : `Pr8.00` (`0x6000`) bit 2 (prise d'origine à la mise sous tension) = 0,
   bit 1 (butées logicielles) au choix ; `Pr8.26` (`0x601A`, déclenchement par combinaison
   d'entrées) = 0. Ne pas affecter CTRG / HOME / JOG à des entrées non câblées.
9. **Arrêt rapide** : `Pr8.23` (`0x6017`, ms) — décélération utilisée par `stopMovement()`,
   `jogStop()` et l'arrêt d'urgence ; une valeur trop courte fait décrocher un pas-à-pas
   chargé (ou la donner dans `quickStopMs`).
10. **CL-RS — activation** : soit l'entrée d'activation câblée (`Pr4.02`, usine : DI1 = enable
    NF), soit `Pr0.07` = 1 écrit par la classe (prioritaire sur l'entrée). Ne **pas**
    sauvegarder `Pr0.07 = 1` en EEPROM si la détection de redémarrage (node R5) est voulue.
11. Vérifier avec le logiciel : pas d'alarme, mot d'état `0x1003` bit 1 = 1 (CL), lecture de
    `0x602A` cohérente, déplacement PR0 manuel.

---

## 6. Alarmes (`0x2203`)

Champ de bits ; `getAlarmDescription()` liste les libellés connus séparés par « + » :

| Code | Libellé |
|---|---|
| `0x0001` | Surintensité (court-circuit moteur ?) |
| `0x0002` | Surtension (alimentation) |
| `0x0040` | Défaut du circuit de mesure de courant |
| `0x0080` | Échec du blocage de l'arbre (moteur débranché ?) |
| `0x0100` | Défaut d'auto-réglage |
| `0x0200` | Défaut EEPROM |
| autre | « Alarme 0x.... (voir le manuel du variateur) » — p. ex. erreur de poursuite des CL-RS |

Le bit 0 du mot d'état (défaut) sans code lu donne « Défaut variateur (code non disponible) ».

---

## 7. Vérifié / supposé

Les manuels StepperOnline (omc-stepperonline.com) et Leadshine n'étaient pas accessibles
depuis l'environnement de développement. Sources consultées :

* **Vérifié — même protocole Leadshine RS** : extraits des manuels StepperOnline DM882RS et
  CL86RS (résultats de recherche) : lecture du courant crête `0x0191`, origine par `0x600A`,
  `Pr0.07` en `0x00F` « enable the drive by RS485 » (CL86RS), table de correspondance
  `0x0F10-0x0F19`, alarme courante `0x2203`, fonctions Modbus 03 / 06 / 10.
* **Vérifié dans le manuel iCL-RS** (moteur intégré boucle fermée de la même gamme, manuel
  complet au format texte dans le dépôt public `puphup/baanrig-trivision-system`, qui
  pilote ces moteurs) : `0x6002` (valeurs écrites et relues), trajets `0x6200..0x6207`
  (mode, position haute puis basse, vitesse, rampes ms/1000 tr/min), `Pr8.00`, `Pr8.10..8.23`,
  `Pr8.29`, `Pr8.42..8.45`, `0x1003` (bits 0/1/2/4/5/6), `0x1801` (0x1111, 0x2211, 0x4001/0x4002),
  `0x2203` et les codes d'alarme, `Pr5.22..5.24`, `Pr0.00`, `Pr0.03`, `Pr0.07`, `Pr5.00`.
* **Vérifié sur EM2RS** (Leadshine, équivalent du DM556RS ; script public
  `jaredlandau/EM2RS-556`) : `Pr0.03 = 0x0007`, `Pr5.00 = 0x0191`, `Pr5.22 = 0x01BD`,
  `Pr6.00 = 0x01E1`, `Pr8.02 = 0x6002`, `0x1801` (jog 0x4001/0x4002), 38400 8N1.
* **Vérifié dans le firmware** : ordre des mots des nodes « Dobble » (poids faible à l'adresse
  basse, `src/Node/Node.cpp`), d'où l'échange de mots.
* **Supposé (à confirmer à la mise en service)** :
  - que `0x1003`, `0x602A/0x602B`, `Pr8.29` et `0x1047` existent sur DM556RS/DM882RS/CL57RS/
    CL86RS aux mêmes adresses et avec les mêmes bits que sur l'iCL-RS ;
  - que `0x6002` relu repasse à `0x0000` (`0x000P`) en fin de trajet (la classe ne s'en sert
    que comme indication « occupé ») et que le bit 6 « origine faite » de `0x1003` passe à 1
    en fin de prise d'origine ;
  - que l'arrêt rapide `0x40` est accepté pendant une prise d'origine, et qu'un nouveau `0x10`
    pendant un trajet l'interrompt (bit INS de `Pr9.00`, « toujours 1 » selon le manuel) — sur
    quoi repose le jog à fenêtre glissante ;
  - qu'une écriture de `Pr5.00` (couple) est prise en compte immédiatement et reste en RAM ;
  - que les DM-RS n'ont pas `Pr0.07` (sinon : ajouter les ids W7/R5 au fichier DM) ;
  - les plages : vitesse jusqu'à 3000 tr/min, rampe jusqu'à 10000 ms ;
  - le jeu complet de codes d'alarme des CL-RS (erreur de poursuite...).

---

## 8. Essais de mise en service

1. Lecture seule : vérifier dans l'IHM le mot d'état, l'alarme, la position (doit suivre un
   déplacement fait avec le logiciel ; si la valeur saute de 65536 en 65536, l'ordre des mots
   est inversé : `highWordFirst = false`).
2. Initialisation : `Variateur initialisé` à 1, `Servo prêt` à 1.
3. Jog lent dans les deux sens, relâchement : arrêt avec la rampe `Pr8.23`, pas d'alarme ;
   relancer aussitôt (verrou de 500 ms).
4. Prise d'origine : la position passe à 0, `Origine faite` à 1.
5. Déplacements absolus courts puis longs, arrêt en cours de déplacement, arrêt d'urgence et
   réarmement.
6. Débrancher le bus pendant un jog : l'axe s'arrête au bout de la fenêtre ; « Erreur Modbus »
   et alarme ; rebrancher, réarmer.

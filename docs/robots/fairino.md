# Robot FAIRINO (série FR) en Modbus TCP — `Robot_Fairino`

Fichier matériel : `data/hardware/Robot_Fairino.json`.
L'automate est **client Modbus TCP** (maître) ; le contrôleur FAIRINO est **serveur** (esclave).
Logiciel contrôleur **V3.7.3 ou plus récent** (ajout de la fonction « Modbus Slave Control Robot »).

> **Point essentiel.** Le manuel FAIRINO décrit le principe de l'esclave Modbus TCP et renvoie à
> une **Annexe 1** (table d'adresses complète) qui n'a pas pu être consultée (site du fabricant
> inaccessible depuis l'environnement de recherche). Seules les adresses ci-dessous marquées
> « vérifié » sont sûres. Les autres sont des **hypothèses** à confirmer (§ 6) **avant** toute
> mise en mouvement.

---

## 1. Réglages côté robot (WebApp)

1. **Réseau** : IP d'usine du contrôleur **192.168.58.2** (adresse utilisée par tous les
   exemples du SDK FAIRINO). Mettre l'automate dans le même sous-réseau ou changer l'IP du robot.
2. **Esclave Modbus TCP** : page *ModbusTCP* (programmation) → **paramètres de l'esclave** :
   IP, port **502**, n° d'esclave (unité) — régler **1** et reporter la valeur dans PLC Studio.
   Faire le « test de communication » de la page.
3. **Signaux utilisateur de l'esclave** : y déclarer les signaux de la poignée de main
   (noms libres). Vu du maître (l'automate) :

   | Signal robot | Objet Modbus | Fonction automate | Instruction Lua robot |
   |---|---|---|---|
   | DI (entrée TOR) | bobine | `ModbusWriteMultipleCoils` (FC15) | `ModbusSlaveReadDI`, `ModbusSlaveWaitDI` |
   | DO (sortie TOR) | entrée TOR | `ModbusReadMultipleInputsStatus` (FC02) | `ModbusSlaveWriteDO` |
   | AI (entrée analogique) | registre de maintien | `ModbusWriteHoldingRegister` (FC06) | `ModbusSlaveReadAI`, `ModbusSlaveWaitAI` |
   | AO (sortie analogique) | registre d'entrée | `ModbusReadInputRegister` (FC04) | `ModbusSlaveWriteAO` |

   **Relever l'adresse de chaque signal** affichée par le WebApp et corriger
   `Robot_Fairino.json` si elle diffère de l'hypothèse « base 100 ».
4. **E/S configurables du coffret** (codes de fonction vérifiés dans le SDK FAIRINO,
   `SetDIConfig` / `SetDOConfig`) — recommandé pour l'arrêt, la pause et les états critiques :

   | Voie | Code | Fonction | Câblage automate |
   |---|---|---|---|
   | CI0 | 7 | Stop (arrêt du programme) | sortie API, impulsion |
   | CI1 | 4 | Pause | sortie API, impulsion |
   | CI2 | 5 | Resume (reprise) | sortie API, impulsion |
   | CI3 | 30 | Clear all errors (acquittement) | sortie API, impulsion |
   | CI4 (option) | 6 | Start (si l'on ne veut pas la bobine 502) | sortie API, impulsion |
   | CO0 | 18 | Program start/stop (programme en marche) | entrée API |
   | CO1 | 13 | Suspend (programme en pause) | entrée API |
   | CO2 | 1 | Report errors (robot en défaut) | entrée API |
   | CO3 | 47 | Robot Enable (robot activé) | entrée API |
   | CO4 | 19 | Automatic / manual mode (niveau = auto à vérifier) | entrée API |
   | CO5 | 20 | Emergency stop output signal 1 (information) | entrée API |
   | CO6 (option) | 24 | Protective stop status output | entrée API |

5. **Programme maître** : écrire le programme Lua du § 3 (ex. `plc_maitre.lua`), le configurer
   comme programme lancé par la bobine 502 (programme chargé / par défaut) et passer le robot en
   **mode automatique** : la bobine 502 n'agit qu'en automatique.

---

## 2. Table d'adresses utilisée

`registersOffset = 0` (hypothèse : adresses du manuel comptées à partir de 0 — si la bobine 502
déclenche autre chose ou rien, essayer 501/503 **robot hors énergie** pour trancher).

### Bobines unitaires (`ModbusWriteCoil`, FC05)

| id | Adresse | Nom | Statut |
|---|---|---|---|
| 1 | 502 | `DEMARRER` : front 0 → 1 = lance le programme configuré (mode auto) | **vérifié** |
| 2 | 300 | `DO0` du coffret | **vérifié** |
| 3..9 | 301..307 | `DO1`..`DO7` du coffret | supposé (contigu) |

Bobines 300..599 = zone de **commande** du robot (vérifié). Pause, reprise, arrêt existent
probablement dans cette zone (un fragment du manuel 3.8.0 associe « 501 » à la reprise) mais
**ne sont pas activés** dans le fichier : passer par les E/S configurables câblées du § 1.

### Poignée de main (signaux utilisateur, base 100 **supposée**)

| Fonction automate | id / bit | Adresse | Nom | Rôle |
|---|---|---|---|---|
| `ModbusWriteMultipleCoils` | 1 / bit 1 | 100 | `DEPART` | demande de cycle, remise à 0 dès `EN_COURS` |
| | 1 / bit 2 | 101 | `ABANDON` | abandon coopératif de la tâche |
| | 1 / bit 3 | 102 | `ACQUIT` | acquittement du défaut programme |
| | 1 / bits 4..8 | 103..107 | — | libres |
| `ModbusReadMultipleInputsStatus` | 1 / bit 1 | 100 | `PRET` | programme maître en attente |
| | 1 / bit 2 | 101 | `EN_COURS` | tâche acceptée, en cours |
| | 1 / bit 3 | 102 | `FINI` | tâche terminée |
| | 1 / bit 4 | 103 | `DEFAUT` | défaut programme |
| | 1 / bits 5..8 | 104..107 | — | libres |
| `ModbusWriteHoldingRegister` | 1 | 100 | `PROG` | numéro de tâche (1..n) |
| | 2..4 | 101..103 | `P1`..`P3` | paramètres libres |
| | 5 | 104 | `VIE_API` | battement de vie automate |
| `ModbusReadInputRegister` | 1 | 100 | `ECHO_PROG` | tâche acceptée |
| | 2 | 101 | `CODE_DEFAUT` | 0 = aucun |
| | 3 | 102 | `VIE_ROBOT` | battement de vie robot |
| | 4..5 | 103..104 | `V1`, `V2` | valeurs retour libres |
| | 6..11 | 105..110 | `TCP_X`..`TCP_RZ` | X, Y, Z en 0,1 mm ; RX, RY, RZ en 0,1° |
| | 12..17 | 111..116 | `J1`..`J6` | articulations en 0,1° |

### État natif du contrôleur

Registres d'entrée **310..473** = état temps réel du robot (**vérifié** pour la plage ; le
détail par adresse est dans l'Annexe 1, non consultée). Le SDK FAIRINO donne les codages
probables : état programme 1 = arrêté, 2 = en marche, 3 = en pause ; mode 0 = automatique,
1 = manuel ; positions en **flottants** (non lisibles par l'automate). Une fois une adresse
identifiée (§ 6), l'ajouter dans `ModbusReadInputRegister` avec un nouvel id.

---

## 3. Programme robot (pseudo-code Lua FAIRINO)

Les instructions `ModbusSlave…` sont insérées depuis l'éditeur du WebApp (page ModbusTCP), qui
génère la syntaxe exacte ; ci-dessous le principe (noms = noms déclarés au § 1.3).

```lua
-- plc_maitre.lua (pseudo-code)
ModbusSlaveWriteDO("FINI", 1, {0}) ; ModbusSlaveWriteDO("EN_COURS", 1, {0})
ModbusSlaveWriteDO("PRET", 1, {1})
vie = 0
while 1 do
  vie = (vie + 1) % 30000 ; ModbusSlaveWriteAO("VIE_ROBOT", 1, {vie})
  depart = ModbusSlaveReadDI("DEPART", 1)
  if depart == 1 then
    prog = ModbusSlaveReadAI("PROG", 1)
    ModbusSlaveWriteDO("FINI", 1, {0}) ; ModbusSlaveWriteAO("ECHO_PROG", 1, {prog})
    ModbusSlaveWriteDO("PRET", 1, {0}) ; ModbusSlaveWriteDO("EN_COURS", 1, {1})
    -- appeler le sous-programme de la tâche « prog » ; tester ABANDON entre les mouvements ;
    -- en cas d'erreur : ModbusSlaveWriteDO("DEFAUT", 1, {1}) et CODE_DEFAUT
    ModbusSlaveWriteDO("EN_COURS", 1, {0}) ; ModbusSlaveWriteDO("FINI", 1, {1})
    ModbusSlaveWriteDO("PRET", 1, {1})
  end
  -- recopier TCP / articulations × 10 (entiers) dans TCP_X..J6 si besoin
  WaitMs(50)
end
```

---

## 4. Séquence à programmer dans le grafcet

1. **Initialisation** : `DEMARRER` = 0 et la banque `DEPART`/`ABANDON`/`ACQUIT` = 0 (la classe
   générée par PLC Studio écrit l'état de repos des sorties Modbus câblées au démarrage). Conditions de marche : pas
   d'erreur Modbus, mode automatique (CO4), robot activé (CO3), pas de défaut (CO2 = 0).
2. **Lancement du programme maître** (si CO0 « en marche » = 0) : `DEMARRER` = 1, attendre CO0 = 1
   puis `PRET` = 1 (temporisation de surveillance), remettre `DEMARRER` = 0.
3. **Ordre** : écrire `PROG` = N (et `P1`..`P3`) **puis** `DEPART` = 1.
4. **Accusé** : attendre `EN_COURS` = 1 et `ECHO_PROG` = N (≈ 2 s max), puis `DEPART` = 0.
5. **Fin** : attendre `FINI` = 1 (temps de cycle + marge) ou `DEFAUT` = 1.
6. **Défaut / perte de liaison** (`DEFAUT`, CO2, erreur Modbus, `VIE_ROBOT` figé > 1 s) :
   impulsion CI0 « Stop » (ou `ABANDON` pour un arrêt propre de la tâche) ; après intervention,
   impulsion CI3 « Clear all errors », `ACQUIT` = 1 jusqu'à `DEFAUT` = 0, retour à l'étape 2.
7. **Pause / reprise** : impulsions CI1 / CI2, état lu sur CO1.

---

## 5. Sécurité

* Le Modbus TCP (et le Wi-Fi) **n'est pas une fonction de sécurité** : aucun arrêt d'urgence,
  protecteur ou mode réduit ne doit en dépendre.
* L'**arrêt d'urgence** est **câblé** sur les entrées de sécurité du robot (double canal) et
  dans la chaîne d'AU de la machine ; les signaux d'AU lus par l'automate sont informatifs.
* Les bobines 300..599 commandent directement le robot : **n'écrire aucune adresse non vérifiée**
  de cette zone. Les DO du coffret écrits par Modbus peuvent entrer en conflit avec le
  programme robot qui pilote les mêmes sorties.
* Réseau robot isolé : le serveur Modbus n'a pas d'authentification.

---

## 6. Vérifications à la mise en service (robot hors énergie / sans programme dangereux)

1. Analyseur Modbus sur `192.168.58.2:502`, unité 1. Lire les registres d'entrée 310..473 (FC04)
   en changeant l'état sur le pendant (mode manuel / auto, activation, pause, défaut) : noter les
   adresses qui bougent, et dans quel format (entier, flottant 2 mots).
2. Basculer chaque signal utilisateur côté robot (WebApp) et repérer son adresse côté maître.
3. Écrire `DO0` (bobine 300) et vérifier la sortie physique → confirme la base 0.
4. Avec un programme de test inoffensif chargé, en mode automatique : bobine 502 de 0 à 1 →
   le programme doit démarrer.

## 7. Vérifié / supposé / limites

| Élément | Statut | Source |
|---|---|---|
| Esclave Modbus TCP depuis V3.7.3, réglage IP / port / n° d'esclave, test de communication | vérifié | manuel FAIRINO § 9.18.3 et notes de version |
| Bobines 300..599 = commandes, 300 = DO0, 502 = départ programme (front, mode auto) | vérifié | manuel FAIRINO § 9.18.3.4 |
| Registres d'entrée 310..473 = état temps réel | vérifié (plage) | idem |
| Correspondance DI = bobine, DO = entrée TOR, AI = maintien, AO = registre d'entrée | vérifié | manuel FAIRINO § 9.18.3.3 |
| Codes de fonction CI / CO configurables | vérifié | SDK C++ FAIRINO (`SetDIConfig`, `SetDOConfig`) |
| IP d'usine 192.168.58.2 | vérifié (usage SDK) | exemples du SDK Python FAIRINO |
| Adresses comptées à partir de 0 | supposé | — |
| DO1..DO7 = 301..307 | supposé | — |
| Signaux utilisateur à partir de 100 | **supposé** | — |
| Pause / reprise / arrêt par bobine (500 / 501 / 503 ?) | **non vérifié, non activé** | — |
| Syntaxe exacte des instructions `ModbusSlave…` | à reprendre de l'éditeur WebApp | — |

Limites côté automate :

* **Pas de flottant** : positions natives illisibles ; le programme robot les recopie en
  entiers 16 bits mis à l'échelle.
* **Entiers signés** : les voies `POS` du catalogue sont lues en signé (−32768 à 32767).
* Nodes 32 bits : **mot de poids faible en premier** (FAIRINO utilise probablement l'ordre
  inverse pour ses valeurs natives) — non utilisés ici.
* **Une transaction par registre** (sauf les banques de 8 bits) : rafraîchir les positions
  lentement (≥ 500 ms).
* **Écriture sur changement** : au démarrage, la classe générée écrit une fois l'état de repos
  des sorties câblées (`DEMARRER` = 0) ; sans cela, une bobine restée à 1 côté robot
  empêcherait le front suivant.

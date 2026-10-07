# Robot AUBO (contrôleur ARCS) en Modbus TCP — `Robot_Aubo`

Fichier matériel : `data/hardware/Robot_Aubo.json`.
L'automate est **client Modbus TCP** (maître) ; le contrôleur du robot est **serveur** (esclave).

| Contrôleur | Logiciel | Ce document |
|---|---|---|
| AUBO i-series / iS-series récents (coffret CB-M, etc.) | **ARCS** (AuboScope) | s'applique |
| AUBO i-series anciens (coffret CB4) | AUBOPE 4.x | serveur Modbus TCP non confirmé : utiliser le câblage TOR « linkage » (§ 6) |

> **Point essentiel.** La table officielle de l'esclave Modbus ARCS (`modbus从站地址表_v1.14.xlsx`,
> qui contient les commandes natives « départ / arrêt du projet », l'état du projet, la levée
> d'arrêt de protection, etc.) **n'est pas publique** : AUBO la fournit sur demande à son support
> technique. Ce fichier matériel n'utilise donc que la zone **documentée publiquement** : les
> **registres généraux 300 à 525**, dans lesquels ce projet définit sa propre poignée de main avec
> un programme robot. Le départ / l'arrêt / la pause du programme et les états critiques passent
> par des **E/S TOR câblées** dont les fonctions sont, elles, vérifiées.

---

## 1. Réglages côté robot

1. **Réseau** : IP fixe du contrôleur dans le sous-réseau de l'automate (relever l'IP dans les
   paramètres réseau d'ARCS ; l'IP d'usine n'a pas pu être confirmée).
2. **Serveur Modbus** : *Configuration > Bus de terrain > Modbus*, activer **Modbus TCP**
   (esclave). Port **502**, n° d'unité **1** (valeurs de l'exemple AUBO). Tester ensuite avec un
   analyseur Modbus (QModMaster, Modbus Poll…) : lecture FC03 de l'adresse 300.
3. **E/S configurables** (*Configuration > Général > Réglages E/S*) — noms des fonctions
   relevés dans le SDK ARCS 0.24.1 (`StandardInputAction` / `StandardOutputRunState`) :

   | Sens | Fonction ARCS | Câblée vers / depuis l'automate |
   |---|---|---|
   | Entrée robot | `StartProgram` | sortie API « Départ programme robot » (impulsion) |
   | Entrée robot | `StopProgram` | sortie API « Arrêt programme robot » (impulsion) |
   | Entrée robot | `PauseProgram` | sortie API « Pause robot » (impulsion) |
   | Entrée robot | `ResumeProgram` | sortie API « Reprise robot » (impulsion) |
   | Entrée robot (option) | `PopupDismiss` | sortie API « Fermer message robot » |
   | Sortie robot | `RunningHigh` | entrée API « Programme robot en marche » |
   | Sortie robot | `PausedHigh` | entrée API « Robot en pause » |
   | Sortie robot | `NotSystemError` | entrée API « Robot sans défaut » |
   | Sortie robot | `RobotOperable` | entrée API « Robot opérationnel » |
   | Sortie robot | `ExternalEmergencyStop` / `InternalEmergencyStop` | entrées API « AU robot » (information seulement) |
   | Sortie robot (option) | `AtHome` | entrée API « Robot en position de repos » |

   Les E/S AUBO sont de type **NPN** : vérifier la compatibilité avec les entrées/sorties de
   l'automate (relais ou interface si besoin).
4. **Programme maître** : créer un projet (ex. `plc_maitre`) qui exécute la boucle du § 3, le
   définir comme projet par défaut chargé au démarrage, et passer le robot en mode automatique /
   distant selon la politique du site.

---

## 2. Table d'adresses utilisée

`registersOffset = 0` : l'exemple AUBO lit le registre général 300 avec l'adresse 300 telle
quelle (adresses comptées à partir de 0). Tous les échanges sont des **registres de maintien**
16 bits.

### Automate → robot (`ModbusWriteHoldingRegister`, FC06)

| id | Adresse | Nom | Rôle |
|---|---|---|---|
| 1 | 300 | `PROG` | numéro de tâche demandé (1..n) |
| 2 | 301 | `DEPART` | 1 = demande de cycle ; remis à 0 dès que `EN_COURS` = 1 |
| 3 | 302 | `ABANDON` | 1 = abandon coopératif de la tâche (pas un arrêt de sécurité) |
| 4 | 303 | `ACQUIT` | 1 = acquittement du défaut programme |
| 5..8 | 304..307 | `P1`..`P4` | paramètres libres |
| 9 | 308 | `VIE_API` | battement de vie de l'automate (compteur) |

### Robot → automate (`ModbusReadHoldingRegister`, FC03)

| id | Adresse | Nom | Rôle |
|---|---|---|---|
| 1 | 310 | `PRET` | 1 = programme maître en attente d'ordre |
| 2 | 311 | `EN_COURS` | 1 = tâche acceptée et en cours |
| 3 | 312 | `FINI` | 1 = tâche terminée (remis à 0 au départ suivant) |
| 4 | 313 | `DEFAUT` | code défaut programme, 0 = aucun |
| 5 | 314 | `ECHO_PROG` | numéro de la tâche acceptée |
| 6 | 315 | `VIE_ROBOT` | battement de vie du robot (compteur) |
| 7..10 | 316..319 | `V1`..`V4` | valeurs retour libres |
| 11..16 | 320..325 | `TCP_X`..`TCP_RZ` | position outil : X, Y, Z en 0,1 mm ; RX, RY, RZ en 0,1° |
| 17..22 | 326..331 | `J1`..`J6` | angles articulaires en 0,1° |

Positions : le contrôleur les donne en flottants (m, rad) ; le firmware de l'automate ne lit
pas de flottant. Le programme robot les recopie donc **en entiers signés 16 bits mis à
l'échelle** (portée ±3276,7 mm et ±3276,7°, suffisante pour un cobot). Côté automate, PLC Studio
lit ces voies `POS` en signé (une valeur > 32767 du node devient négative). Rafraîchir ces registres
lentement (≥ 500 ms) : chaque registre est une transaction Modbus.

---

## 3. Programme robot (pseudo-code)

Les fonctions `getInt16Register(i)` / `setInt16Register(i, v)` existent dans ARCS (SDK 0.24.1).
**Supposé** : l'index `i` correspond à l'adresse Modbus `300 + i` — à confirmer en écrivant une
valeur depuis l'analyseur Modbus et en la relisant dans le robot avant toute mise en mouvement.

```lua
-- Programme maître ARCS (pseudo-code) : index = adresse - 300
setInt16Register(12, 0)  -- FINI
setInt16Register(11, 0)  -- EN_COURS
setInt16Register(10, 1)  -- PRET
vie = 0
while true do
  vie = (vie + 1) % 30000 ; setInt16Register(15, vie)          -- VIE_ROBOT
  if getInt16Register(3) == 1 then setInt16Register(13, 0) end  -- ACQUIT -> DEFAUT = 0
  if getInt16Register(1) == 1 and getInt16Register(13) == 0 then -- DEPART et pas de défaut
    prog = getInt16Register(0)
    setInt16Register(12, 0) ; setInt16Register(14, prog)        -- FINI = 0, ECHO_PROG
    setInt16Register(10, 0) ; setInt16Register(11, 1)           -- PRET = 0, EN_COURS = 1
    -- appeler ici la procédure de la tâche « prog » (P1..P4 en paramètres) ;
    -- tester ABANDON (index 2) entre deux mouvements ; en cas d'erreur : setInt16Register(13, code)
    setInt16Register(11, 0) ; setInt16Register(12, 1)           -- EN_COURS = 0, FINI = 1
    setInt16Register(10, 1)                                      -- PRET = 1
  end
  -- recopier ici TCP / articulations (× 10) dans les index 20..31 si besoin
  -- attente ~50 ms
end
```

---

## 4. Séquence à programmer dans le grafcet

1. **Initialisation** : `DEPART` = 0, `ABANDON` = 0, `ACQUIT` = 0. La classe générée par PLC
   Studio écrit l'état de repos de chaque sortie Modbus câblée au démarrage de l'automate. Conditions de marche : pas d'erreur Modbus,
   `VIE_ROBOT` évolue, entrées TOR `NotSystemError` et `RobotOperable` à 1.
2. **Lancement du programme robot** (si `RunningHigh` = 0) : impulsion TOR `StartProgram`
   (≈ 0,5 s), attendre `RunningHigh` = 1 puis `PRET` = 1 (temporisation de surveillance).
3. **Ordre de tâche** : écrire `PROG` = N (et `P1`..`P4`), **puis** `DEPART` = 1.
4. **Accusé** : attendre `EN_COURS` = 1 **et** `ECHO_PROG` = N (délai max ≈ 2 s, sinon défaut),
   puis `DEPART` = 0.
5. **Fin** : attendre `FINI` = 1 (délai max = temps de cycle + marge) ou `DEFAUT` ≠ 0.
6. **Défaut** (`DEFAUT` ≠ 0, perte Modbus, `VIE_ROBOT` figé > 1 s, `NotSystemError` = 0) :
   impulsion `StopProgram` (TOR) ou `ABANDON` = 1 selon la gravité ; après intervention,
   `ACQUIT` = 1 jusqu'à `DEFAUT` = 0, puis retour à l'étape 2.
7. **Pause / reprise opérateur** : impulsions TOR `PauseProgram` / `ResumeProgram`, état lu sur
   `PausedHigh`.

---

## 5. Sécurité

* Le Modbus TCP (et le Wi-Fi) **n'est pas une fonction de sécurité** : ni l'arrêt d'urgence, ni
  les protecteurs, ni le mode réduit ne doivent en dépendre.
* L'**arrêt d'urgence** de la cellule est **câblé** sur les entrées de sécurité du robot
  (double canal) et dans la chaîne d'AU de la machine. Les signaux d'AU lus par l'automate sont
  informatifs.
* Le départ d'un cycle depuis l'automate doit respecter l'analyse de risques (zone protégée,
  mode automatique, réarmement opérateur).
* Garder le robot sur un réseau isolé : le serveur Modbus n'a pas d'authentification.

---

## 6. Ancien contrôleur CB4 (AUBOPE 4.x)

Le manuel AUBO-i5 & CB4 (V4.5.11) décrit un mode **« linkage »** par E/S TOR de la carte
d'interface : `LI00` départ programme, `LI01` arrêt, `LI02` pause, `LI03` retour position
initiale ; `LO00` en marche, `LO01` arrêté, `LO02` en pause, `LO03` en position initiale.
Câbler ces signaux sur des E/S de l'automate (logique NPN). L'échange de données (n° de
programme) se fait alors par E/S utilisateur `U_DI_xx` / `U_DO_xx` codées en binaire.
Un serveur Modbus TCP n'a pas pu être confirmé sur ces contrôleurs.

---

## 7. Vérifié / supposé / limites

| Élément | Statut | Source |
|---|---|---|
| Serveur Modbus TCP à activer dans *Configuration > Bus de terrain > Modbus* | vérifié | guide AUBO « 29 ARCS Modbus » |
| Registres généraux 300..525, lecture FC03 à l'adresse 300 | vérifié | même guide (exemple `modbusAddSignal(..., 1, 300, 0x03, ...)`) |
| Table native (commande / état du projet, levée d'arrêt de protection) | existe, adresses **non publiques** | même guide (historique de versions, table v1.14 sur demande) |
| Fonctions E/S `StartProgram`, `StopProgram`, `PauseProgram`, `ResumeProgram`, `RunningHigh`, `PausedHigh`, `NotSystemError`, `RobotOperable`… | vérifié | SDK ARCS `pyaubo_sdk` 0.24.1 |
| `getInt16Register` / `setInt16Register` | vérifié (existence) | SDK ARCS 0.24.1 |
| Index script ↔ adresse Modbus `300 + i` | **supposé** | — |
| Répartition des registres 300..331 | **convention de ce projet** | — |
| Port 502, unité 1 | usuel, unité 1 dans l'exemple AUBO | — |
| Écriture FC06 acceptée | **supposé** (standard) | — |
| E/S linkage CB4 `LI00..LI03` / `LO00..LO03` | vérifié | manuel AUBO-i5 & CB4 V4.5.11 |

Limites côté automate :

* **Pas de flottant** : positions en entiers mis à l'échelle par le programme robot.
* **Entiers signés** : les voies `POS` du catalogue sont lues en signé (−32768 à 32767) ;
  les autres registres sont non signés (0 à 65535).
* Nodes 32 bits (`ModbusReadDobbleHoldingRegister`) : **mot de poids faible en premier**
  (adresse N = poids faible) — à respecter si le programme robot écrit des valeurs 32 bits.
* **Une transaction par registre** : 31 registres lus = 31 requêtes ; espacer les
  rafraîchissements (états 100–200 ms, positions ≥ 500 ms).
* **Écriture sur changement** : un node de sortie n'écrit que si sa valeur change ; au
  démarrage, la classe générée par PLC Studio écrit une fois l'état de repos (0) de chaque
  sortie Modbus câblée, donc `DEPART` = 0.

Dès réception de la table officielle AUBO, ajouter dans `Robot_Aubo.json` les commandes et états
natifs (nouveaux ids, sans renuméroter les existants).

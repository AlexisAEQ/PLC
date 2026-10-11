# Robots AUBO — ARCS (Modbus TCP) et AUBOPE 4.x (E/S « linkage »)

Ce qui est possible depuis l'automate dépend du **logiciel** du contrôleur, pas du coffret :

| Logiciel du contrôleur | Robots concernés | Liaison avec l'automate | Profil PLC Studio |
|---|---|---|---|
| **ARCS** (interface AUBO Scope) | AUBO iS-series, robots i-series livrés ou migrés sous ARCS (coffret CB-M possible) | Modbus TCP (le robot est serveur) + E/S câblées | `Robot_Aubo` (§ A) |
| **AUBOPE 4.x** | souvent les coffrets CB4 et CB-M d'origine (ex. AUBO i10 CB-M du banc, manuel V4.5.13) | **E/S câblées « linkage »** ; aucun serveur Modbus TCP documenté | aucun : E/S de l'automate (§ B) |

**Première chose à faire :** sur le pendant, ouvrir *À propos* et relever la version.
« AUBOPE 4.x » : aller au § B. « ARCS » : § A.

> Retour d'expérience : le banc `BancAubo` (AUBO i10, coffret CB-M sous AUBOPE 4.5.13) a été
> configuré avec le profil ARCS et l'automate n'a jamais communiqué avec le robot. Analyse et
> tests : `docs/robots/diagnostic-aubo-i10.md`.

---

## A. Contrôleur ARCS — profil `Robot_Aubo` (Modbus TCP)

Fichier matériel : `data/hardware/Robot_Aubo.json`. L'automate est **client Modbus TCP** ; le
contrôleur ARCS est **serveur**. Le profil utilise les **registres généraux 300 à 525** d'ARCS,
dans lesquels ce projet définit sa propre poignée de main : il faut donc **un programme ARCS**
qui lit et écrit ces registres (§ A.3). Sans ce programme, les registres restent à 0.

La table native de l'esclave ARCS (`modbus从站地址表`, commandes « départ / arrêt du projet »,
état du projet…) n'est pas publique : AUBO la fournit sur demande. Le départ, l'arrêt, la pause
et les états critiques passent donc par des **E/S câblées**.

### A.1 Réglages côté robot (ARCS)

1. **Réseau** : IP fixe du contrôleur dans le sous-réseau de l'automate.
2. **Serveur Modbus TCP** : l'activer dans les réglages Modbus d'ARCS (« 从站开关 », port 502 par
   défaut selon la note d'application AUBO 61). Le chemin exact du menu est à confirmer sur le
   robot. Tester ensuite depuis un PC : `tools/modbus-tests/test_modbus_tcp.py <ip> --adresses 300`.
3. **E/S configurables** (*Configuration > Général > Réglages E/S*) — fonctions du SDK ARCS
   (`StandardInputAction` / `StandardOutputRunState`) :

   | Sens | Fonction ARCS | Câblée vers / depuis l'automate |
   |---|---|---|
   | Entrée robot | `StartProgram` | sortie API « Départ programme robot » (impulsion) |
   | Entrée robot | `StopProgram` | sortie API « Arrêt programme robot » (impulsion) |
   | Entrée robot | `PauseProgram` / `ResumeProgram` | sorties API « Pause » / « Reprise » (impulsions) |
   | Sortie robot | `RunningHigh` | entrée API « Programme robot en marche » |
   | Sortie robot | `PausedHigh` | entrée API « Robot en pause » |
   | Sortie robot | `NotSystemError` | entrée API « Robot sans défaut » |
   | Sortie robot | `RobotOperable` | entrée API « Robot opérationnel » |

   Les E/S AUBO sont de type **NPN** : vérifier la compatibilité avec l'automate.
4. **Programme maître** : un projet ARCS qui exécute la boucle du § A.3, chargé au démarrage.

### A.2 Table d'adresses du profil (convention de ce projet)

Registres de maintien 16 bits, `registersOffset = 0` (probable : adresse Modbus = 300 + index du
registre ARCS ; à vérifier avant toute mise en mouvement, § A.3).

Automate → robot (`ModbusWriteHoldingRegister`, FC06) :

| id | Adresse | Nom | Rôle |
|---|---|---|---|
| 1 | 300 | `PROG` | numéro de tâche demandé (1..n) |
| 2 | 301 | `DEPART` | 1 = demande de cycle ; remis à 0 dès que `EN_COURS` = 1 |
| 3 | 302 | `ABANDON` | 1 = abandon coopératif de la tâche (pas un arrêt de sécurité) |
| 4 | 303 | `ACQUIT` | 1 = acquittement du défaut programme |
| 5..8 | 304..307 | `P1`..`P4` | paramètres libres |
| 9 | 308 | `VIE_API` | battement de vie de l'automate (compteur) |

Robot → automate (`ModbusReadHoldingRegister`, FC03) :

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

Le contrôleur donne les positions en flottants et le firmware de l'automate ne lit pas de
flottant : le programme robot les recopie en entiers signés 16 bits mis à l'échelle (±3276,7 mm
ou degrés). PLC Studio lit ces voies `POS` en signé. Rafraîchir les positions lentement (≥ 500 ms) :
chaque registre est une transaction Modbus.

### A.3 Programme robot ARCS (pseudo-code)

**Avant toute mise en mouvement**, vérifier la correspondance entre index du programme et
adresse Modbus : depuis un PC, écrire des valeurs distinctes en 300 à 308 (pymodbus
`write_register`) et les relire dans le robot ; faire écrire par le robot une valeur connue à
l'index 10 et la relire à l'adresse 310. Un décalage d'un registre ferait partir une tâche sur
l'écriture de `PROG` au lieu de `DEPART`. Une simple lecture depuis le PC ne le voit pas : sans
programme robot, tous les registres valent 0.

```lua
-- index = adresse - 300 (getInt16Register / setInt16Register du SDK ARCS)
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
    -- procédure de la tâche « prog » (P1..P4 en paramètres) ; tester ABANDON (index 2)
    -- entre deux mouvements ; en cas d'erreur : setInt16Register(13, code)
    setInt16Register(11, 0) ; setInt16Register(12, 1)           -- EN_COURS = 0, FINI = 1
    setInt16Register(10, 1)                                      -- PRET = 1
  end
  -- recopier ici TCP / articulations (× 10) dans les index 20..31 si besoin ; attente ~50 ms
end
```

### A.4 Séquence à programmer dans le grafcet

1. **Initialisation** : `DEPART`, `ABANDON`, `ACQUIT` à 0 (la classe générée écrit l'état de
   repos des sorties Modbus câblées au démarrage). Conditions de marche : pas d'erreur Modbus,
   `VIE_ROBOT` évolue, `NotSystemError` et `RobotOperable` à 1.
2. **Lancement du programme robot** (si `RunningHigh` = 0) : impulsion `StartProgram` (≈ 0,5 s),
   attendre `RunningHigh` = 1 puis `PRET` = 1 (temporisation de surveillance).
3. **Ordre de tâche** : écrire `PROG` = N (et `P1`..`P4`), **puis** `DEPART` = 1.
4. **Accusé** : attendre `EN_COURS` = 1 et `ECHO_PROG` = N (≈ 2 s max), puis `DEPART` = 0.
5. **Fin** : attendre `FINI` = 1 ou `DEFAUT` ≠ 0.
6. **Défaut** : impulsion `StopProgram` ou `ABANDON` = 1 ; après intervention `ACQUIT` = 1 jusqu'à
   `DEFAUT` = 0.

---

## B. Contrôleur sous AUBOPE 4.x (souvent les coffrets CB4 et CB-M d'origine)

**Pas de profil Modbus pour ces contrôleurs.** Sous AUBOPE 4.x :

- le seul Modbus documenté fait du robot un **maître** : le plug-in Modbus (*Extensions*) et la
  configuration `ModbusCfg` du SDK AUBOPE V4 déclarent des équipements que le robot interroge ;
- aucune documentation ne décrit de **serveur** Modbus TCP ni de table d'adresses. Une vidéo AUBO
  intitulée « AUBOPE Tutorial - Modbus: TCP Server » laisse penser qu'une option existe : à
  vérifier sur le pendant, puis à tester (`tools/modbus-tests/`) avant tout projet ;
- les registres généraux 300-525 et `setInt16Register` ne sont documentés que pour ARCS
  (absents du SDK AUBOPE V4 ; le langage de script AUBOPE n'a pas été vérifié).

### B.1 E/S câblées « linkage » (mode de référence)

D'après le manuel AUBO-i5 & CB4 V4.5.11 (à confirmer sur le bornier du CB-M) :

| Signal robot | Fonction | Câblé vers / depuis l'automate |
|---|---|---|
| `LI00` (entrée) | départ du programme par défaut | sortie API « Départ programme robot » (impulsion) |
| `LI01` (entrée) | arrêt | sortie API « Arrêt programme robot » (impulsion) |
| `LI02` (entrée) | pause | sortie API « Pause robot » (impulsion) |
| `LI03` (entrée) | retour en position initiale | sortie API (impulsion) |
| `LO00` (sortie) | programme en marche | entrée API « Programme robot en marche » |
| `LO01` (sortie) | programme arrêté | entrée API |
| `LO02` (sortie) | en pause | entrée API « Robot en pause » |
| `LO03` (sortie) | en position initiale | entrée API |

Le coffret fournit aussi des sorties d'état : en mouvement, pas à l'arrêt, mode réduit,
**erreur système** (probablement active en cas d'erreur : polarité inverse d'un « robot sans
défaut », à mesurer), arrêt d'urgence, AUBOPE en exécution. `RobotOperable` (ARCS) n'a
probablement pas d'équivalent.

Mise en œuvre (d'après les extraits du manuel V4.x, à confirmer sur le robot) : choisir le mode
avec le bouton MANUAL/LINKAGE du coffret, robot hors tension, puis redémarrer le pendant et le
bras ; identifier sur le pendant le projet lancé par `LI00` ; vérifier la logique NPN (une sortie
robot active tire vers 0 V ; une entrée robot s'active en la ramenant au 0 V). L'échange de
valeurs (n° de programme) se fait par E/S utilisateur codées en binaire.

### B.2 Pour échanger des données

| Topologie | Principe | État |
|---|---|---|
| PC passerelle | le PC qui pilote déjà le robot par le SDK (port 8899) republie les états vers l'automate, par exemple en serveur Modbus TCP lu par l'automate (rien à changer dans le firmware : `"tcp": {"ip": <PC>}`) | recommandé |
| Automate serveur | le plug-in Modbus d'AUBOPE interroge l'automate | le firmware n'est que client : à développer |
| Robot serveur | seulement si le test du port 502 et un balayage révèlent une table réelle | non documenté |

Le protocole du port 8899 est propriétaire : ne pas chercher à l'implémenter sur l'ESP32.

---

## C. Sécurité

* Le Modbus TCP (et le WiFi) **n'est pas une fonction de sécurité** : ni l'arrêt d'urgence, ni
  les protecteurs, ni le mode réduit ne doivent en dépendre.
* L'arrêt d'urgence de la cellule est **câblé** sur les entrées de sécurité du robot (double
  canal) et dans la chaîne d'AU de la machine. Les signaux d'AU lus par l'automate sont informatifs.
* Le départ d'un cycle depuis l'automate respecte l'analyse de risques (zone protégée, mode
  automatique, réarmement opérateur).
* Garder le robot sur un réseau isolé : le Modbus n'a pas d'authentification.

---

## D. Vérifié / probable / inconnu

| Élément | Statut | Source |
|---|---|---|
| CB-M du banc sous AUBOPE 4.5.13 ; manuels « AUBO-i5/i10 & CB-M V4.5.x » | vérifié | document utilisateur, intitulés des manuels |
| Un CB-M peut aussi tourner sous ARCS (iS-series, migration) | probable | manuel ARCS, échanges avec AUBO (extraits) |
| Registres généraux 300..525 | vérifié, **ARCS seulement** | guide AUBO « 29 ARCS Modbus », note d'application 61 |
| Index ARCS ↔ adresse `300 + i` | probable (ARCS) ; tester avant mouvement (§ A.3) | note d'application 61 (extraits) |
| Serveur Modbus TCP ARCS, port 502 par défaut | vérifié (ARCS) ; chemin du menu à confirmer | note d'application 61 |
| `registersOffset = 0`, unité 1 | probable (ARCS) | déduits d'un exemple `modbusAddSignal` où le robot ARCS est **client** |
| Écriture FC06 acceptée par ARCS | probable (standard) | — |
| Fonctions E/S `StartProgram`… `RobotOperable` | vérifié, **ARCS seulement** | SDK ARCS `pyaubo_sdk` 0.24.1 |
| Répartition des registres 300..331 | **convention de ce projet** | — |
| AUBOPE 4.x : Modbus maître (plug-in Extensions) | vérifié | SDK AUBOPE V4 (`ModbusCfg`), manuel, document utilisateur |
| AUBOPE 4.x : serveur Modbus TCP | **inconnu** : non documenté ; une vidéo AUBO « Modbus: TCP Server » l'évoque | — |
| E/S linkage `LI00..LI03` / `LO00..LO03` | vérifié sur CB4, probable sur CB-M | manuel AUBO-i5 & CB4 V4.5.11 |

Limites côté automate : pas de flottant, une transaction par registre, écriture seulement sur
changement (état de repos écrit au démarrage), nœuds 32 bits avec le mot de poids faible en
premier.

# Diagnostic : pas de communication Modbus TCP entre l'automate et l'AUBO i10 (octobre 2026)

## Contexte

- **Automate** : Waveshare ESP32-S3-POE-ETH-8DI-8DO, projet `BancAubo` (branche
  `Test_Modif_Claude_01`), Ethernet en IP fixe 192.168.10.20/24, WiFi en point d'accès 192.168.4.1.
- **Robot** : AUBO i10, coffret **CB-M**, logiciel **AUBOPE 4.5.13**, IP 192.168.10.50. Un programme
  PC (`aubo_jog.py`) le pilote déjà par le SDK AUBO sur le port 8899.
- **Projet** : l'automate lit en Modbus TCP (client, FC03, unité 1, port 502) les registres 310-315
  et 320-322 du profil `Robot_Aubo` ; départ et arrêt par E/S câblées.
- **Symptôme** : le projet compile, l'écran s'affiche sur la tablette (par le WiFi), mais aucune
  donnée du robot ; les témoins du RJ45 de l'automate clignotent très peu.

## Verdict

**C'est un problème d'adressage, pas de méthode Modbus.** Le profil `Robot_Aubo` a été écrit pour
un contrôleur **ARCS** : un serveur Modbus TCP qui expose les registres généraux 300-525, remplis
par un programme ARCS (`setInt16Register`). Le robot du banc tourne sous **AUBOPE 4.5.13** :

- le seul Modbus documenté y fait du robot un **maître** (plug-in *Extensions*) ;
- aucun serveur Modbus TCP ni aucune table d'adresses n'y sont documentés ; une vidéo AUBO
  intitulée « AUBOPE Tutorial - Modbus: TCP Server » laisse penser qu'une option serveur existe,
  sans table d'adresses connue (non vérifié) ;
- les registres 300-525 et `setInt16Register` ne sont documentés que pour ARCS (absents du SDK
  AUBOPE V4 ; le langage de script AUBOPE n'a pas été vérifié) ;
- aucun programme robot n'a été écrit pour remplir ces registres.

Notre propre documentation rangeait le coffret CB-M parmi les contrôleurs ARCS, sans faire
vérifier le logiciel réellement installé : c'est l'erreur d'origine (corrigée dans
`docs/robots/aubo.md`, qui classe désormais les contrôleurs par logiciel).

Ce verdict n'est pas encore une preuve : aucun test de port ni aucune trace série n'ont été faits.
Les tests ci-dessous le confirment ou non en quelques minutes.

## Ce qui a été vérifié côté automate

- **Firmware Modbus TCP** : le vrai `MyModbus.cpp` et la vraie bibliothèque modbus-esp8266 4.1.0,
  compilés sur PC face à un serveur Modbus local : 156 lectures FC03 réussies sur 156, unité 1,
  bonnes adresses, une seule connexion. La trame envoyée est conforme (FC03, unité 1, port 502,
  adresses brutes 310-315 et 320-322, un registre par requête).
- **Ethernet** : relus dans le code et le framework installé :
  - le brochage W5500 ;
  - l'appel `ETH.begin` ;
  - l'ordre `begin` puis `config` ;
  - le chargement de la configuration avant le démarrage ;
  - le routage vers 192.168.10.50 par l'Ethernet (le point d'accès est sur 192.168.4.0/24).

  Aucun défaut bloquant. Ce n'est pas mesuré sur la carte elle-même.
- **Témoins RJ45** : ils ne permettent pas de trancher. Sans connexion, le firmware ne retente
  qu'une connexion toutes les 3 s, puis, après 10 échecs, une toutes les 5 s environ. Une session
  normale ferait clignoter le témoin presque en continu.

## Effets de bord qui brouillaient les essais

- **Tant qu'une entrée Modbus n'a jamais été lue, la logique ne tourne pas**, même le départ et
  l'arrêt câblés : le firmware attend que chaque entrée ait été lue au moins une fois. Le message
  série « Not calling logic executor because not all inputs are read once » en est la conséquence,
  pas une deuxième panne.
- La logique ne tourne que si un écran (la tablette) est connecté (constat CR-2 de l'audit).
- Les E/S câblées de `BancAubo` portent des fonctions ARCS : `StartProgram`, `RunningHigh`,
  `NotSystemError`, `RobotOperable`. Sous AUBOPE, l'équivalent probable est le jeu d'E/S linkage
  `LI00`/`LI01` et `LO00`/`LO02` (noms du manuel CB4, à confirmer au bornier du CB-M) ;
  `RobotOperable` n'a probablement pas d'équivalent, ce qui peut bloquer la transition de départ
  même avec un Modbus parfait.

## Tests décisifs, dans l'ordre

1. **Pendant du robot** (2 minutes) :
   - *À propos* : AUBOPE 4.5.13 ou ARCS ?
   - réglages réseau : IP réelle du robot ;
   - chercher une option « serveur » ou « esclave » Modbus TCP, et la photographier.
2. **Ports du robot**, depuis le PC d'`aubo_jog.py` branché sur le même switch :
   ```
   Test-NetConnection 192.168.10.50 -Port 8899
   Test-NetConnection 192.168.10.50 -Port 502
   ```
   8899 ouvert et 502 fermé : pas de serveur Modbus TCP, l'automate ne peut pas communiquer avec
   ce robot en Modbus, quel que soit son firmware.
3. **Si le port 502 est ouvert** : `python tools/modbus-tests/test_modbus_tcp.py 192.168.10.50`,
   puis `--scan` (voir `tools/modbus-tests/README.md` pour lire le résultat).
4. **Ce que dit l'automate**, sans rien modifier :
   - Moniteur série sur l'USB natif. Lignes attendues : « Ethernet demarre (W5500, 192.168.10.20) »,
     « Modbus TCP : equipement 192.168.10.50:502 unite 1 -> adresse interne 1000 », puis
     « Modbus TCP : connecte a … ». En cas d'échec, l'alerte « MODBUS addr:1000 MORT apres 10
     erreurs consecutives (…) » arrive environ 30 s après le démarrage si la connexion échoue, ou
     quelques secondes après « connecte a » si le serveur répond mal : laisser le moniteur ouvert.
     Aucune ligne « Ethernet… » : la section Ethernet est absente ou désactivée dans le config.json
     de la carte (retéléverser le système de fichiers : `pio run -e esp32s3_n16r8 -t uploadfs`).
   - Sans câble USB : http://192.168.4.1/diagnostic.txt (ligne « MORT » ; les lignes « connecte a »
     n'y figurent pas) et http://192.168.4.1/api/status (`all_inputs_read_once: false` = au moins
     une entrée jamais lue).

   La cause entre parenthèses de l'alerte « MORT » se lit ainsi :

   | Texte | Cause |
   |---|---|
   | Connection lost | connexion TCP impossible (port fermé, hôte injoignable, mauvais réseau) |
   | Output Address not exists | serveur présent, adresses inexistantes |
   | Operation timeout | connecté mais sans réponse (n° d'unité, serveur muet) |
   | Function Code not Supported | code fonction non géré |
5. **Ethernet de l'automate vu du PC** : `ping 192.168.10.20` et ouvrir http://192.168.10.20 par le
   câble. Cela valide le W5500, l'IP fixe et la pile TCP de l'automate.
6. **Substitution du robot** :
   - débrancher le robot ;
   - donner l'IP 192.168.10.50 au PC ;
   - lancer `python tools/modbus-tests/faux_esclave_modbus.py`.

   Si l'écran affiche 310…315 et 320…322, l'automate est hors de cause.

## Corrections apportées (branche `claude/cartes-et-axes`)

- `docs/robots/aubo.md` : contrôleurs classés par logiciel (ARCS / AUBOPE 4.x) ; profil
  `Robot_Aubo` réservé à ARCS ; E/S linkage comme référence pour AUBOPE 4.x ; statuts corrigés.
- `data/hardware/Robot_Aubo.json`, catalogue PLC Studio : profil marqué « ARCS seulement,
  programme robot requis ».
- Firmware (`MyModbus`), validé sur un banc PC avec la vraie bibliothèque :
  - après un délai dépassé, la requête est annulée : une réponse tardive n'est plus écrite dans
    une variable déjà libérée ;
  - reconnexion immédiate après la perte d'une connexion qui avait répondu ;
  - connexions mortes purgées avant chaque nouvelle connexion, et échec d'envoi compté comme une
    erreur de l'équipement (il déclenchait sinon des reconnexions sans fin, sans alerte).
- `tools/modbus-tests/` : script de test et faux esclave Modbus TCP.
- `docs/modbus-tcp.md` : comment diagnostiquer une liaison qui ne s'établit pas.

## Suite proposée pour BancAubo

À décider après les tests 1 et 2 :

- **Le pendant n'offre aucune option serveur Modbus TCP, ou le port 502 reste fermé** :
  - départ, arrêt et états par les E/S linkage : `LI00` / `LI01`, `LO00` / `LO02`, et la sortie
    « erreur système », dont la polarité est à mesurer ;
  - retirer les voies Modbus du robot du projet, qui ne servaient qu'à l'affichage ;
  - retirer `RobotOperable` de la transition de départ ;
  - pour les données (états détaillés, positions), faire republier par le PC (SDK 8899) dans un
    serveur Modbus TCP lu par l'automate.
- **Serveur présent** : relever la vraie table avec `--scan` et utiliser l'équipement
  « Modbus générique » de PLC Studio avec ces adresses.

L'arrêt d'urgence et les E/S de sécurité restent câblés dans tous les cas.

# Audit d'incohérences : dépôt PLC (BorneUniverselle + RessortRoyal2)

## Résumé exécutif

- Après fusion des doublons signalés par plusieurs zones, il reste **225 constats** : **3 critiques, 39 majeurs, 139 mineurs et 44 cosmétiques**.
- **Critique : les butées logicielles sont contournables.** En mode aller-retour, la cible PR1 n'est jamais comparée à `maxRangePuu` (CR-1). En jog, la butée n'est vérifiée qu'au moment de l'appui (CR-3).
- **Critique : la supervision dépend du navigateur.** Toute la supervision du servo (`SureServo::process`, E-stop logiciel, arrêt du jog) ne tourne que si un client web est connecté, et rien n'est arrêté quand il se déconnecte (CR-2).
- **Les entrées réseau sont fragiles.** Une commande `deleteFile` peut déborder la pile (MA-20). La validation de config.json a des effets de bord et provoque un use-after-free (MA-9, MA-10). Un hash orphelin dans interface.json bloque le boot sans aucun recours (MA-12).
- **Le build n'est pas reproductible.** Plusieurs includes ont la mauvaise casse, des fichiers CSV de partitions sont introuvables, et l'environnement par défaut n'est pas celui qui est déployé, dont le LittleFS est rempli à 96 % (MA-29 à MA-31). Aucune suite de tests ne compile ni ne vise RessortRoyal2 (MA-32 à MA-39).
- **Le front et le back divergent.** Les messages FATAL ne s'affichent pas, la garde métier de suspension est contournée, et la protection des fichiers comme celle des mots de passe est incohérente (MA-15, MA-16, MA-18, MA-19).

> **Conventions.** Les fichiers de `src/` et `test/` sont cités par leur nom de base, unique dans le dépôt. Les autres fichiers sont cités par leur chemin relatif. Les identifiants indiquent la sévérité : CR (critique), MA (majeur), MI (mineur), CO (cosmétique).
>
> **Sévérités.** Pour les doublons, j'ai retenu la sévérité confirmée la plus haute, avec trois ajustements. CR-1 à CR-3 sont relevés en critique parce qu'ils exposent à un risque physique. MA-30 et MA-32, signalés « critiques » par la zone tests, sont ramenés en majeur car ils n'ont aucun effet sur le firmware livré.

## 1. Constats critiques (3)

### Sécurité machine : butées et supervision des mouvements

#### CR-1 · Mode aller-retour : la cible PR1 n'est jamais comparée à la butée physique
- **Sévérité** : critique · **Emplacements** : `SureServo.cpp:666-672, 1672-1680, 3320-3358` · `RessortRoyal2.cpp:397-399, 842, 930-943, 1027, 1728, 1738, 1904` · `data/interface.json:10-21`
- **Problème** : Seule `startGoToPosition` contrôle la butée (`positionPuu > maxRangePuu + PHYSICAL_LIMIT_MARGIN_PUU`). En mode chaîné, RessortRoyal2 ne lui transmet que la position de retour. La cible aller est écrite par `configurePR(1, userTarget)` sans aucun contrôle de plage. Une cible refusée en mode simple est donc exécutée telle quelle en aller-retour. De plus, le champ « Position cible » n'a ni `low` ni `high` dans l'IHM.
- **Correction** : Appliquer dans `configurePR` la même règle de butée (et de position négative) que dans `startGoToPosition`, et borner `userTarget` et `returnPos` dans RessortRoyal2 et dans l'IHM.

#### CR-2 · Supervision, E-stop logiciel et arrêt du jog dépendent de la présence d'un client web
- **Sévérité** : critique · **Emplacements** : `main.cpp:1162, 1199` · `RessortRoyal2.cpp:626-628, 1479-1510, 1517-1530` · `borneUniverselle.cpp:630-637, 4029-4037, 4064-4080, 4121-4124, 4317-4330` · `SureServo.cpp:913-914`
- **Problème** : `logicExecutor` porte `sureServo->process()`, `checkEmergencyConditions` et `jogTreatment`, mais il ne s'exécute que si `isClientFullyConnected()` est vrai, et `setClientDisconnected` n'arrête aucun mouvement. Si le navigateur se ferme pendant un jog ou un cycle, le drive continue sans timeout et sans contrôle de l'E-stop ni des alarmes. En parallèle, `notifyWebClient` efface les fronts d'entrée avant que la logique ne les ait lus. Un éventuel câblage STO matériel n'est pas vérifiable dans le code.
- **Correction** : Exécuter la supervision indépendamment du client, appeler `jogStop`/`stopMovement` (ou passer en STOP) à la déconnexion, et ne plus effacer les fronts métier depuis la couche web.

#### CR-3 · JOG : SureServo attend des appels cycliques, mais RessortRoyal2 n'appelle jogForward/jogReverse qu'au front d'appui
- **Sévérité** : critique · **Emplacements** : `SureServo.cpp:784-795, 797-837, 869-911` · `RessortRoyal2.cpp:1478-1510` · `SureServo/readme.md:549-555`
- **Problème** : Le premier appel n'envoie que la direction. La vitesse, `jogForwardActive`, la slow zone et le test de butée `pos >= maxRangePuu` ne sont traités qu'aux appels suivants, qui n'arrivent jamais puisque `jogTreatment` ne relance le jog que sur `getIsChanged()`. Un jog commencé sous la butée (1 000 000 PUU) la dépasse donc sans arrêt logiciel. Le readme décrit le même usage.
- **Correction** : Surveiller la butée et la slow zone dans `SureServo::process()` tant qu'un jog est actif, ou imposer et documenter l'appel cyclique.

## 2. Constats majeurs (39)

### 2.1 Logique machine et pilotage du servo

#### MA-1 · Récupération post-urgence : le retour démarre sur le même appui alors que le message demande d'appuyer à nouveau
- **Sévérité** : majeure · **Emplacements** : `RessortRoyal2.cpp:1011, 1036-1049, 1124-1156, 1335, 1925-1927`
- **Problème** : Le bloc `goingPhase && getPreviousState() == EMERGENCY` affiche « appuyez à nouveau sur Start », mais il n'a pas de `return`. Le même appui lance donc aussitôt CYCLING/GOING. En mode simple, relâcher Start pour « appuyer à nouveau » redéclenche alors une urgence. La condition s'applique aussi après une urgence déclenchée au repos : le premier Start envoie l'axe vers la position de retour et compte un cycle fantôme.
- **Correction** : Utiliser un drapeau explicite (`abortedDuringGo`) posé dans les chemins d'urgence, et faire `return` après le message ou corriger le message.

#### MA-2 · DRIVE_IDLE signifie à la fois « succès » et « annulé » : un homing interrompu est ensuite déclaré réussi
- **Sévérité** : majeure · **Emplacements** : `ServoDrive.h:23` · `SureServo.cpp:2929-2960, 3035-3057, 3141, 3145-3149, 3220-3227` · `RessortRoyal2.cpp:1377-1381, 1397-1401`
- **Problème** : `stopMovement` remet `homingState` à DRIVE_IDLE sans remettre `driveHomingStarted` à false, et `clearMovementLock` ne le fait que si l'état n'est pas IDLE. Au homing suivant, `handleHoming` saute le démarrage, lit DRIVE_IDLE et renvoie COMPLETED : RessortRoyal2 affiche « Homing terminé avec succès » et force `homeDone`. La même ambiguïté touche `handleMovingToPosition(0)` après une annulation.
- **Correction** : Réinitialiser `driveHomingStarted` dans `stopMovement`, ou ajouter un état CANCELLED traité comme ABORTED.

#### MA-3 · Deux sources de vérité (parameters.* et nœuds) : des changements sont perdus pendant les mouvements
- **Sévérité** : majeure · **Emplacements** : `RessortRoyal2.cpp:933-939, 969, 979-997, 1081, 1119, 1148, 1178, 1469, 1707-1731` · `Node.h:675-684` · `borneUniverselle.cpp:4121-4123` · `main.cpp:1129-1202`
- **Problème** : La phase 1 d'un mouvement lit `parameters.goAndBack`, la phase 2 lit `goAndBack->getValue()`. La recopie nœuds → `parameters` n'a lieu qu'en STOP, IDLE, EMERGENCY et JOGGING, alors que les drapeaux `isChanged` sont effacés à chaque boucle. Un go-and-back activé pendant l'aller est donc compté comme un cycle complet sans retour, et les réglages modifiés en HOMING, GOING ou CYCLING ne sont ni persistés ni envoyés au drive. Le bloc `userTarget` est en outre dupliqué.
- **Correction** : Figer le mode au démarrage du mouvement, traiter les changements à chaque boucle (immédiatement ou différés jusqu'au retour en IDLE), et supprimer le doublon.

#### MA-4 · SureServo::process() ne peut jamais détecter une perte de communication Modbus
- **Sévérité** : majeure · **Emplacements** : `SureServo.cpp:305-348, 1299` · `Node.h:249` · `Node.cpp:431-447, 458-473` · `MyModbus.cpp:748-752`
- **Problème** : Le test `hasValidData` repose sur `status > 0 || position != 0xFFFFFFFF`, alors que les nœuds démarrent à 0 et gardent leur dernière valeur en cas d'échec. La branche « Perte communication Modbus » (`notifyBusinessLogicFault`, passage en FAILED) est donc morte : un mouvement ne s'arrête qu'au timeout global (60 s, ou 100 s pour le homing).
- **Correction** : Utiliser `isSlaveConsideredDead(getSlaveAddress())`, comme le fait déjà `processInitialize`, ou l'âge du dernier rafraîchissement.

#### MA-5 · « Full torque » n'est jamais persisté depuis l'IHM et est réinitialisé à chaque connexion d'un client
- **Sévérité** : majeure · **Emplacements** : `RessortRoyal2.cpp:243, 486, 756-766, 1594-1599, 1611-1782, 1808` · `data/RessortRoyal2.json:10` · `data/interface.json:285-291`
- **Problème** : `parameters.fullTorque` n'est écrit qu'au chargement. Il est pourtant réécrit à chaque sauvegarde et réappliqué au nœud à chaque `initialStateLoadedHandler`. Le choix de l'opérateur est donc perdu au redémarrage, et annulé, éventuellement en plein cycle, dès qu'un autre navigateur se connecte.
- **Correction** : Sur `fullTorque->getIsChanged()`, mettre à jour `parameters.fullTorque` et appeler `markDirty(CONFIG_FILE_NAME)`.

#### MA-6 · requestRestart : le délai demandé est ignoré, et le redémarrage n'a jamais lieu sans client ou si le PLC est cassé
- **Sévérité** : majeure · **Emplacements** : `BusinessLogic.h:212, 489-490` · `BusinessLogic.cpp:615-630, 640-644` · `borneUniverselle.cpp:117-118` · `main.cpp:1125, 1156, 1162`
- **Problème** : `restartDelayMs` est stocké mais jamais relu : le redémarrage a lieu après `RESTART_GRACE_PERIOD_MS` = 2 s, alors que l'utilisateur lit « 10 seconds ». Surtout, la séquence n'avance que dans `logicExecutor`, qui ne tourne ni si le PLC est cassé (cas typique : on vient de corriger un config.json fautif) ni sans client web.
- **Correction** : Comparer à `restartDelayMs` et traiter `restartPending` dans `loop()`, hors des conditions sur le client et l'état du PLC.

#### MA-7 · PF8574BooleanInputNode::isInterrupt() efface le drapeau alors que les appelants l'utilisent comme simple test
- **Sévérité** : majeure · **Emplacements** : `Node.h:357-360` · `Node.cpp:758-762` · `borneUniverselle.cpp:300, 1535, 2300, 4053` · `RessortRoyal2.cpp:1561-1566` · `main.cpp:1132, 1154`
- **Problème** : Quatre fonctions de BorneUniverselle font `if (isInterrupt()) return;` et consomment ainsi le drapeau (`refresh()` le consomme même si `allConditionsOK` est faux). Le vrai traitant, `RessortRoyal2::handleInterrupts`, passe en dernier et ne le voit presque jamais : le rafraîchissement accéléré des entrées PCF8574 (E-stop A, départ cycle) n'a pas lieu. `handleNodesChange` abandonne en outre les états restants d'un message, et `clearInterruptFlag()` n'est jamais appelée.
- **Correction** : Rendre `isInterrupt()` non destructif et n'effacer le drapeau que dans le traitant.

### 2.2 Configuration : parsing, validation et rechargement

#### MA-8 · configNeedsRestart cherche les nœuds dans « config » au lieu de « children » : un changement matériel ne déclenche jamais de redémarrage
- **Sévérité** : majeure · **Emplacements** : `borneUniverselle.cpp:120-129, 2933-2940, 4872-4873, 4903-4953, 5062` · `data/config.json:27, 79` · `CLAUDE_TASK_LittleFS_Etape2_CommitRenameUnique.md:106`
- **Problème** : `parseHardwares` lit `children` à la racine du fichier, mais `configNeedsRestart` ne parcourt que `config` (Wifi et parameters). Ajouter, supprimer ou retyper un nœud passe donc pour une modification « mineure ». `reloadMinorConfigParameters` ne relit que `logLevel`, une clé absente, et le client reçoit « reloaded without restart » alors que `nodesMap` ne correspond plus au fichier.
- **Correction** : Comparer `children` et les clés racine critiques, et redémarrer par défaut tant qu'aucun rechargement à chaud réel n'existe.

#### MA-9 · parseConfig en mode validation (check=true) modifie l'état, peut casser le PLC, sauvegarder et redémarrer
- **Sévérité** : majeure · **Emplacements** : `borneUniverselle.h:375` · `borneUniverselle.cpp:1280-1284, 2522, 2535-2593, 2613-2618, 2695-2697, 2934, 2953, 2962, 3268-3272, 5017-5018` · `data/config.json:5`
- **Problème** : À chaque sauvegarde de config.json, la prévalidation applique quand même les options et les filtres, remet `projectVersion` à 0, tente de supprimer diagnostic.txt, copie `ota_url`, appelle `setPlcBroken` en cas d'erreur et, si un hash diffère, exécute `saveParameters(doc); ESP.restart();`. À l'inverse, la sauvegarde « before applying changes » n'est faite que hors validation, c'est-à-dire uniquement au boot.
- **Correction** : En mode check, travailler sur des variables locales et remonter les erreurs dans `errorMsg`, sans aucun effet de bord.

#### MA-10 · addNode en mode validation : use-after-free
- **Sévérité** : majeure · **Emplacements** : `borneUniverselle.cpp:3577, 3804, 3889, 4018, 4723-4729, 5018`
- **Problème** : `delete node;` est suivi de `node->getName()` dans un `Serial.printf`, pour chaque nœud de chaque validation de config.json.
- **Correction** : Journaliser avant le `delete`, ou copier le nom au préalable.

#### MA-11 · hashLocal non initialisé pour un nœud matériel sans clé « hash »
- **Sévérité** : majeure · **Emplacements** : `borneUniverselle.cpp:1679-1680, 2989-2991, 3117, 3121-3123, 3197, 3209, 3224, 3244-3258` · `Node.cpp:76-99`
- **Problème** : `uint32_t hash = 0, hashLocal;` puis `&hashLocal` est transmis aux fonctions `create*Node`. Une valeur indéterminée non nulle provoque « PLC CONFIGURATION ERROR » au lieu de la régénération prévue aux lignes 3244-3258. La branche des nœuds virtuels fait de son côté `return false`, avec un message trompeur. Le défaut est latent tant que tous les nœuds ont un hash, mais le front n'en calcule aucun.
- **Correction** : Initialiser `hashLocal` à 0 et aligner la branche virtuelle sur la régénération.

#### MA-12 · Un hash d'interface.json absent de config.json bloque définitivement le boot, alors que l'éditeur l'accepte
- **Sévérité** : majeure · **Emplacements** : `borneUniverselle.cpp:236-247, 1716-1717, 1730, 4345-4356, 4989-5006` · `RessortRoyal2.cpp:573-577` · `PLC_InterfaceMenu.h:22` · `PLC_InterfaceMenu.cpp:83` · `main.cpp:958, 1083, 1091` · `index.js:4599`
- **Problème** : `initializeConfig` teste `node != nullptr`, mais `findNodeImpl` a déjà appelé `setPlcBroken`. Avant la fin du setup, et donc avant le démarrage du serveur web, cet appel boucle sur `while (true) delay(1000);`. `validateConfigFile` accepte interface.json sans contrôle, et le front se contente d'une notification : une simple faute de frappe impose de reflasher LittleFS par liaison série.
- **Correction** : Valider les hash d'interface.json à la sauvegarde, et utiliser au boot une recherche sans effet de bord avec un simple avertissement.

#### MA-13 · RessortRoyal2.json : la validation accepte tout, et l'extraction écrase `parameters` avant de rejeter
- **Sévérité** : majeure · **Emplacements** : `RessortRoyal2.cpp:150-158, 195-262, 573-577, 795-810` · `borneUniverselle.cpp:5002-5003` · `PLC_Persistence.cpp:380-428`
- **Problème** : `validateConfigFile` renvoie toujours true. Après sauvegarde, `extractMachineParameters` écrit des valeurs par défaut dans `parameters` pour chaque clé manquante, puis renvoie false sans mettre à jour les nœuds. La sauvegarde différée suivante persiste alors ces valeurs par défaut. Un JSON syntaxiquement invalide reste, lui, refusé avant écriture.
- **Correction** : Implémenter la validation avec les règles de l'extraction, et extraire dans une structure temporaire copiée seulement en cas de succès.

#### MA-14 · « cycle speed » vaut 1500 alors que le code attend un index entre 0 et 15
- **Sévérité** : majeure · **Emplacements** : `data/RessortRoyal2.json:7` · `RessortRoyal2.cpp:235-239, 842-846, 1027, 1699-1702, 1894-1907` · `SureServo.h:405, 417, 419` · `SureServo.cpp:974-977, 3330-3333` · `data/interface.json:265-276` · `files.zip:RessortRoyal2.h:147`
- **Problème** : La valeur, une vitesse brute héritée de l'ancienne version, est ramenée sans message à 15, dernier index de la table P5.060–P5.075, puis réécrite à 15. Côté IHM, `cycleSpeedChange` affecte la valeur et appelle `markDirty` avant toute validation, avec un cast `(uint8_t)` silencieux.
- **Correction** : Corriger le JSON avec un index valide, refuser ou journaliser toute valeur hors plage, et valider avant d'affecter et de persister.

### 2.3 Protocole front/back et exposition réseau

#### MA-15 · Les notifications FATAL partent sans champ `type` et ne s'affichent pas comme des alertes
- **Sévérité** : majeure · **Emplacements** : `PLC_CommonTypes.h:8` · `borneUniverselle.cpp:226, 1102-1109, 1200-1203` · `index.js:4599, 4621`
- **Problème** : `setPlcBrokenImpl` émet un message FATAL, mais le `switch` de `sendMessage` ne connaît pas ce niveau. Le front prévoit pourtant `Bo.FATAL` (toast rouge persistant). Avec un type indéfini, le filtre n'affiche aucun toast : « Plc is broken » n'apparaît que comme une simple info dans le centre de notifications.
- **Correction** : Ajouter `case FATAL: notif[TYPE] = "fatal";` et un `default` explicite.

#### MA-16 · Suspension : la garde métier « refus hors IDLE » de BusinessLogic n'est jamais consultée
- **Sévérité** : majeure · **Emplacements** : `BusinessLogic.h:284-291, 435-444` · `BusinessLogic.cpp:557-593` · `borneUniverselle.h:437-454` · `borneUniverselle.cpp:1865-1867, 1906-1908, 1916-1963, 1979-2027, 5076-5079` · `index.js:4621` (openEditor, withPlcSuspended)
- **Problème** : `suspend_plc` aboutit à `BorneUniverselle::handleSuspendRequest`, qui suspend sans condition. `BusinessLogic::handleSuspendRequest`, `onBusinessLogicSuspendEvent` et les hooks associés sont du code mort. Le front ne vérifie `is_plc_idle` qu'à l'ouverture de l'éditeur : un chargement, une sauvegarde ou une suppression peut ensuite suspendre le PLC pendant un mouvement.
- **Correction** : Déléguer la décision à `businessLogic->handleSuspendRequest` et appliquer l'état via `updateSuspendState`, ou supprimer le chemin mort.

#### MA-17 · Politique « un seul client WebSocket » contradictoire : l'ancien client n'est jamais fermé et tooMuchClients n'est pas géré
- **Sévérité** : majeure · **Emplacements** : `main.cpp:616-620, 635-643` · `borneUniverselle.cpp:521-528, 615-684, 766-768, 1139-1157, 4794-4819` · `borneUniverselle.h:100` · `index.js:4599`
- **Problème** : main.cpp éjecte l'ancien client en mettant `client = nullptr`, sans `close()`. La branche « rejeter le nouveau » de `setClientConnected` est donc morte, et `{"tooMuchClients":true}` n'a aucune cible côté front. L'onglet éjecté garde une socket ignorée, son heartbeat expire, puis il se reconnecte et éjecte l'autre : ping-pong. Le verrou logue « forcing release » mais renvoie false.
- **Correction** : Choisir une seule politique (fermeture explicite de l'ancien client, ou rejet géré côté front) et aligner le log du verrou.

#### MA-18 · Fichiers protégés : trois listes différentes, dont une avec `/index.json` au lieu de `/index.js`
- **Sévérité** : majeure · **Emplacements** : `PLC_Persistence.h:14-21` · `PLC_Persistence.cpp:588-600` · `RessortRoyal2.h:28` · `RessortRoyal2.cpp:583-591` · `borneUniverselle.cpp:2204, 2216` · `index.js:4621` · `data/index.html:21` · `data/config.json:9-16`
- **Problème** : Chaque couche protège une liste différente. Le front protège notamment index.js et styles.min.css, et lit une clé `protected_files` absente de config.json. Le back protège `/index.json`, un fichier qui n'existe pas, mais ni index.js ni styles.min.css. RessortRoyal2 ne protège que son propre fichier. Une commande `deleteFile`, par exemple via le champ « Custom WebSocket Message », peut donc supprimer l'IHM, alors que l'éditeur propose « Delete » sur des fichiers que le back refusera.
- **Correction** : Corriger en `/index.js`, ajouter styles.min.css et component-manifest.json, et exposer au front une liste unique tenue par le serveur.

#### MA-19 · Mots de passe d'écran vérifiés uniquement dans le navigateur, alors que config.json est servi en clair
- **Sévérité** : majeure · **Emplacements** : `data/config.json:20-26, 31-45` · `index.js:4599` · `main.cpp:783-790, 809` · `borneUniverselle.cpp:1811-1890`
- **Problème** : Le mot de passe « Configuration » est comparé côté client. Le même config.json, qui contient aussi les mots de passe WiFi `pwd` et `ap_password`, est servi tel quel par `serveStatic("/")` et par la route `/config`. De son côté, `processMessage` exécute `saveFile`, `deleteFile`, `config` et `states` sans aucune autorisation.
- **Correction** : Soit documenter que cette protection est purement cosmétique, soit sortir les secrets des fichiers servis et vérifier les commandes sensibles côté serveur.

#### MA-20 · Débordement de pile via deleteFile (PATH_MAX_SIZE jamais appliqué)
- **Sévérité** : majeure · **Emplacements** : `PLC_Persistence.h:37` · `PLC_Persistence.cpp:65-84` · `borneUniverselle.cpp:2138-2147, 2177, 2192-2200`
- **Problème** : `handleDeleteFileRequest` copie la valeur brute reçue par `sprintf`/`strcpy` dans `char pathBuff[80]`, sans contrôle de longueur ni de nullité (`strstr` reçoit nullptr si la valeur n'est pas une chaîne). `isPathValid` ne vérifie pas la longueur, et `handleSaveFile` fait un `sprintf` du chemin dans 256 octets. N'importe quel client du réseau peut faire planter la tâche `loop`.
- **Correction** : Faire appliquer PATH_MAX_SIZE par `isPathValid`, vérifier le type, et utiliser `snprintf` en rejetant toute troncature.

### 2.4 Concurrence, état client et persistance

#### MA-21 · Les deux chemins de déconnexion ne remettent pas à zéro le même état
- **Sévérité** : majeure · **Emplacements** : `borneUniverselle.cpp:566-604, 607-685, 703-731, 872` · `main.cpp:654-660, 1162`
- **Problème** : `setClientDisconnected` remet à zéro `clientFullyInitialized`, la suspension JS et les tentatives de connexion. `setClientConnected(false)`, appelé quand la file reste pleine 5 s, ne le fait pas mais remet `heartbeatTimeout`. Comme il met aussi `client = nullptr`, l'événement WS_EVT_DISCONNECT suivant est ignoré : `isClientFullyConnected()` reste vrai, et une suspension JS peut rester active.
- **Correction** : Une seule fonction de remise à zéro, appelée par tous les chemins de déconnexion.

#### MA-22 · flushAll() s'exécute hors état idle : écritures LittleFS pendant un mouvement
- **Sévérité** : majeure · **Emplacements** : `BusinessLogic.cpp:604-613, 676-679, 683-686` · `PLC_Persistence.cpp:1306-1329` · `borneUniverselle.cpp:1876-1883, 1965-1968` · `main.cpp:783-790` · `RessortRoyal2.cpp:46-48, 986, 1004, 1013`
- **Problème** : `isIdle()` exclut volontairement les mouvements et `process()` n'est appelé qu'en idle. Pourtant, `onTabChange` (qui logue « no action »), `handleIsPLCIdleRequest` et le filtre HTTP sur toute URL `.json` appellent `flushAll()` sans condition. Comme le fichier est marqué « dirty » pendant 2 minutes après chaque cycle, un simple changement d'onglet en cours de mouvement déclenche une écriture flash qui bloque la boucle de contrôle.
- **Correction** : Conditionner `flushAll()` à `isIdle()` (ou le différer) et corriger le log.

#### MA-23 · deferredEntries est modifié depuis plusieurs tâches sans mutex, et dirty est remis à false même en cas d'échec
- **Sévérité** : majeure · **Emplacements** : `PLC_Persistence.h:109` · `PLC_Persistence.cpp:1244, 1270-1329` · `main.cpp:783-788, 1162` · `BusinessLogic.cpp:676-677` · `RessortRoyal2.cpp:46-48, 1784-1813` · `borneUniverselle.cpp:5111`
- **Problème** : `registerDeferredSave`, `markDirty`, `process` et `flushAll` n'utilisent pas `persistenceMutex`, alors que `flushAll` tourne dans `async_tcp` en parallèle de `process()` dans `loop()`. On risque une double sauvegarde, la perte d'un `markDirty` survenu pendant le callback, et la sérialisation d'un état en cours de modification. `dirty = false` est posé même si la sauvegarde échoue, et le commentaire du .h situe mal l'appel de `process()`.
- **Correction** : Protéger `deferredEntries`, remettre `dirty` à false avant le callback (et le rétablir en cas d'échec), et faire traiter le flush HTTP par `loop()`.

#### MA-24 · logFileSystemStatus utilise f.name() après f.close() (use-after-free au boot)
- **Sévérité** : majeure · **Emplacements** : `PLC_Persistence.cpp:1356, 1407-1409, 1412-1424`
- **Problème** : `name()` pointe dans le `_path` libéré par `close()`. Ce pointeur sert pourtant à détecter les orphelins et à construire le chemin passé à `LittleFS.remove`. `recoverInterruptedCommits`, dans le même fichier, copie correctement le nom avant de fermer. Cela fonctionne aujourd'hui par chance.
- **Correction** : Copier le nom dans une `String` avant `close()`.

### 2.5 WiFi et horloge

#### MA-25 · La reconnexion WiFi, annoncée « non-bloquante », bloque loop() jusqu'à ~20 s, et il n'y a aucun retour en mode station après la bascule en AP
- **Sévérité** : majeure · **Emplacements** : `main.cpp:82-106, 1112-1116, 1162` · `borneUniverselle.cpp:2871-2873` · `wifimanagment.cpp:360-371, 404-426, 467-486, 578-584`
- **Problème** : `connectWifi()` enchaîne des `delay()` puis une attente pouvant aller jusqu'à 20 s, plus environ 4,6 s en mode AP ; pendant ce temps, `refresh()` et Modbus sont gelés. La valeur de retour est ignorée et, une fois en AP, aucun événement ne replanifie de tentative : un routeur qui démarre après l'ESP32 laisse la borne définitivement en AP.
- **Correction** : Rendre la connexion asynchrone (machine d'état suivie dans `loop()`) et retenter périodiquement depuis le mode AP.

#### MA-26 · RTC : la validité de l'heure n'est évaluée qu'au boot, un réglage depuis l'IHM est ignoré jusqu'au redémarrage
- **Sévérité** : majeure · **Emplacements** : `DS3231_RTC.cpp:68, 94, 103, 143-147` · `DS3231_RTC.h:111-113` · `borneUniverselle.cpp:1875, 5246-5253` · `BusinessLogic.cpp:771-774` · `RessortRoyal2.cpp:1775-1776`
- **Problème** : `_isValid` n'est écrit que dans `begin()`, et `setTime()` ne le remet jamais à true. Avec une pile vide ou une année antérieure à 2026 au boot, l'horloge IHM reste vide et les logs restent horodatés en `millis()`, alors que le bouton de réglage, prévu justement pour ce cas, annonce un succès.
- **Correction** : Passer `_isValid = true` après une écriture réussie.

### 2.6 Framework BusinessLogic

#### MA-27 · initNodePtr : le repli static_cast valide des types qui n'héritent pas de T
- **Sévérité** : majeure · **Emplacements** : `BusinessLogic.inl:10-28, 208-240` · `Node.h:393, 588`
- **Problème** : Le RTTI étant actif, `dynamic_cast` réussit déjà pour toute hiérarchie correcte. Le repli `isTypeCompatible` + `static_cast` ne s'exécute donc que pour deux classes Modbus multiples qui ne dérivent pas de `BooleanInputNode`/`BooleanOutputNode`. L'erreur de configuration que la fonction doit diagnostiquer produit alors un pointeur mal typé, avec risque de corruption mémoire.
- **Correction** : Supprimer le repli et réserver `isTypeCompatible` au diagnostic.

#### MA-28 · readme de BusinessLogic : l'exemple ne compile pas et contourne le framework
- **Sévérité** : majeure · **Emplacements** : `BusinessLogic/readme.md:51-53, 122-130, 307, 1103-1111, 1124, 1372, 1764, 2097, 2168` · `BusinessLogic.h:51-61, 150, 182, 248-250, 299, 424, 468` · `BusinessLogic.cpp:19-30, 604-613, 638-681` · `PLC_CommonTypes.h:4` · `PLC_Persistence.h:8` · `RessortRoyal2.h:311`
- **Problème** : Le readme ne liste qu'une des cinq méthodes pures et fait surcharger `logicExecutor`, ce qui court-circuite le restart et la persistance, au lieu de `doBusinessLogic`. Son enum (IDLE=2, RUNNING=3, PAUSED=4, ERROR=5) entre en collision avec `BaseState` et avec la macro `ERROR` : il ne compile pas et rendrait `isIdle()` vrai en RUNNING. Restart, suspension, contextes et historique ne sont pas documentés.
- **Correction** : Réécrire la partie architecture d'après BusinessLogic.h, en prenant RessortRoyal2 comme modèle.

### 2.7 Build et chaîne d'outils

#### MA-29 · Includes dont la casse diffère des fichiers réels : compilation impossible sous Linux
- **Sévérité** : majeure · **Emplacements** : `BusinessLogic.cpp:2` · `Node.cpp:2` · `DoorController.cpp:2` · `Steppermotor.cpp:14` · `SureServo.h:10` · `BusinessLogic/readme.md:1311`
- **Problème** : `"BorneUniverselle/BorneUniverselle.h"` (au lieu de `borneUniverselle.h`) et `"node/node.h"` (au lieu de `Node/Node.h`) ne compilent que sur un système de fichiers insensible à la casse. Sous Linux, en CI ou en conteneur, c'est le firmware lui-même qui échoue.
- **Correction** : Aligner tous les includes sur la casse réelle.

#### MA-30 · Tables de partitions référencées introuvables ; filesystem_size mal écrit
- **Sévérité** : majeure · **Emplacements** : `platformio.ini:35-36, 76, 78, 148` · `partitions_16Mb.csv:1` · `partitions_4MB_no_ota.csv:5-6`
- **Problème** : esp32dev, l'env par défaut, référence `partitions_16MB.csv`, alors que le fichier s'appelle `partitions_16Mb.csv` : échec sous Linux. test_esp32 référence `partitions_16MB_no_ota.csv`, qui n'existe pas : échec partout. test_esp32 ne déclare pas non plus littlefs. Dans quatre_mb_huge, `filesystem_size = 0x140000;` inclut le `;` dans la valeur et fait doublon avec le CSV.
- **Correction** : Corriger les noms, créer ou retirer le CSV `_no_ota`, ajouter littlefs à test_esp32 et supprimer `filesystem_size`.

#### MA-31 · L'environnement par défaut (esp32dev) ne correspond pas à la configuration déployée (quatre_mb_huge)
- **Sévérité** : majeure · **Emplacements** : `platformio.ini:12, 14-36, 53-77` · `CLAUDE_TASK_LittleFS_Etape1_RecoveryBoot.md:140` · `CLAUDE_TASK_LittleFS_Etape2_CommitRenameUnique.md:99` · `logs/device-monitor-260928-153158.log:16, 34, 42-44` · `logs/device-monitor-260929-103042.log:1` · `PLC_Persistence.cpp:1445` · `test_results.log:9-13`
- **Problème** : La carte et les procédures de test utilisent quatre_mb_huge (découpage 4 Mo, PSRAM=0). L'env par défaut déclare au contraire la PSRAM, une table de 16 Mo avec un autre offset LittleFS, d'autres bibliothèques, et une plateforme `espressif32` non figée. La puce fait 16 Mo, mais LittleFS est rempli à 96 % : 53 248 octets libres, juste au-dessus du seuil critique de 50 Ko du code.
- **Correction** : Mettre `default_envs = quatre_mb_huge`, retirer les drapeaux PSRAM, et prévoir un env pioarduino en découpage 16 Mo.

### 2.8 Tests

#### MA-32 · Les suites de tests ciblent Formaca2, absent du dépôt, et RessortRoyal2 n'a aucun test
- **Sévérité** : majeure · **Emplacements** : `test_transition_complette.cpp:31, 37` · `test_transitionInitNodePtr.cpp:36, 129, 165` · `transitionDecorator.h:9` · `testFormaca.cpp:21, 47, 54, 73, 97-148` · `README.md:6` · `main.cpp:13, 1011` · `RessortRoyal2.cpp:9-14` · `platformio.ini:119, 135-143, 150`
- **Problème** : Deux suites incluent `src/Formaca/Formaca2.cpp`, absent, et ne compilent pas. testFormaca redéfinit localement `Node`, `BorneUniverselle` et une classe Formaca simplifiée : il ne teste aucun code du projet. La branche `UNIT_TEST_MODE` de RessortRoyal2 n'est jamais compilée.
- **Correction** : Réécrire les tests contre RessortRoyal2 et BusinessLogic, et remplacer testFormaca.

#### MA-33 · test_esp32 : build_src_filter sans effet, et liste des .cpp inclus à la main incomplète
- **Sévérité** : majeure · **Emplacements** : `platformio.ini:135-143, 151` · `test_transition_complette.cpp:8, 10-32` · `test_transitionInitNodePtr.cpp:16-37` · `borneUniverselle.h:27-28` · `borneUniverselle.cpp:37, 3605, 5246` · `SureServo.cpp:2257` · `SafeJsonDocument.h:18-23` · `Node.cpp:3, 54` · `PLC_Tools.cpp:49`
- **Problème** : Avec `test_build_src = no`, le filtre n'est pas utilisé ; il liste d'ailleurs deux fois borneUniverselle et omet DS3231_RTC, les drivers RTC/PCA9554, ServoDrive et SafeJsonDocument. Les tests incluent les .cpp à la main, mais sans MemoryMonitor.cpp, DS3231_RTC.cpp, ServoDrive.cpp ni SafeJsonDocument.cpp : on obtient des références indéfinies. Passer à `yes` provoquerait à l'inverse des définitions en double.
- **Correction** : Ne garder qu'un mécanisme : soit un filtre complet sans inclusion de .cpp, soit l'inverse.

#### MA-34 · test_native inconstructible
- **Sévérité** : majeure · **Emplacements** : `platformio.ini:155-168` · `main.cpp:931, 1104` · `testFormaca.cpp:5, 358-362` · `test/unity_config.h:14` · `MyModbus.cpp:162-163`
- **Problème** : `platform = native` avec `test_build_src = yes` compile tout `src/`, y compris main.cpp, AsyncWebServer, FreeRTOS et LittleFS. lib_deps embarque FastAccelStepper mais pas ArduinoJson. `build_unflags` retire un `-DMODBUSRTU_TIMEOUT=100` jamais défini, et testFormaca inclut `<Arduino.h>` sans condition.
- **Correction** : Supprimer l'env, ou le limiter à des tests sans Arduino.

#### MA-35 · Assertions 10 à 100 fois plus laxistes que les seuils annoncés
- **Sévérité** : majeure · **Emplacements** : `test_transition_complette.cpp:276, 317-358, 379, 637-638` · `test_results.log:4100, 5340, 11750, 11776`
- **Problème** : « < 50 ms » est testé à 500 000 µs, « 1 Ko » à 102 400 octets, et la cible de 0,5 à 2 ms par transition à moins de 10 s pour 100 transitions. Le journal mesure 52 ms pour EMERGENCY, une moyenne de 11,36 ms (« LENT ») et une dérive de 15 Ko, et pourtant 10/10 tests passent.
- **Correction** : Aligner assertions et commentaires, et faire échouer le test quand le tableau affiche « LENT ».

#### MA-36 · Hash arbitraires et identifiants dupliqués dans les tests : le PLC est cassé dès la création des nœuds
- **Sévérité** : majeure · **Emplacements** : `test_transition_complette.cpp:84-90, 103-110, 116-117` · `test_transitionInitNodePtr.cpp:202-209, 287-290, 310-311, 390, 461` · `Node.cpp:76-100` · `borneUniverselle.cpp:236-246, 4737-4746` · `test_results.log:921`
- **Problème** : Le hash ne dépend que de crc32(id + section), pas du nom, contrairement à ce que dit le commentaire. Les hash 0x1000… et les paires d'identifiants dupliquées déclenchent `setPlcBroken`. Comme `markSetupComplete()` n'est jamais appelé dans les tests, `setPlcBrokenImpl` boucle indéfiniment dès le premier `setUp`.
- **Correction** : Passer 0 (ou le hash réel), utiliser des identifiants uniques par section, et vérifier `isPlcBroken()` dans `setUp`.

#### MA-37 · VirtualBooleanInputNode appelé avec 5 arguments au lieu de 6
- **Sévérité** : majeure · **Emplacements** : `test_transitionInitNodePtr.cpp:207-208` · `Node.h:610` · `Node.cpp:1263`
- **Problème** : Il manque `_inputInverted`, et aucune surcharge ne correspond : erreur de compilation.
- **Correction** : Ajouter `false` avant `webRefreshInterval`.

#### MA-38 · test_transitionInitNodePtr teste une réimplémentation d'initNodePtr
- **Sévérité** : majeure · **Emplacements** : `test_transitionInitNodePtr.cpp:43-44, 53-113, 452-464, 476-481` · `BusinessLogic.inl:207-238`
- **Problème** : La copie de test n'a ni le `setPlcBroken` sur hash 0, ni le repli `isTypeCompatible`. Le cas « Hash = 0 » vérifie un effet de bord impossible et passe toujours, et le `static_cast` dangereux décrit en MA-27 n'est jamais couvert.
- **Correction** : Appeler la vraie méthode via une sous-classe de test de BusinessLogic.

#### MA-39 · test_transitionInitNodePtr n'utilise pas Unity alors que l'env l'impose
- **Sévérité** : majeure · **Emplacements** : `test_transitionInitNodePtr.cpp:486, 489, 535-566` · `platformio.ini:103, 153`
- **Problème** : Il n'y a ni `unity.h`, ni `RUN_TEST`, ni `TEST_ASSERT` : les résultats ne passent que par `Serial.printf`, et le runner finit en timeout ou en ERRORED.
- **Correction** : Convertir chaque cas en `RUN_TEST` avec des `TEST_ASSERT`.

## 3. Constats mineurs (139)

### 3.1 Logique machine (RessortRoyal2)

#### MI-1 · Sortie servoOn pilotée par deux logiques : le bouton « Servo on » n'agit que pendant une boucle
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.cpp:641-646, 853-857, 1178, 1580-1591, 1622-1627` · `SureServo.cpp:1437-1445, 2887-2900` · `data/interface.json:320-323`
- **Problème** : `updateOutputs` impose `servoOn` à chaque boucle selon l'état, alors que `interfaceTreatment` recopie `v_servoOn`. Le bouton ne produit donc qu'une impulsion d'une boucle, y compris en EMERGENCY. La coupure décidée par SureServo reste préservée par le passage en EMERGENCY.
- **Correction** : Donner un seul propriétaire à cette sortie, ou retirer la commande de l'IHM.

#### MI-2 · Polarité d'immediateStop inversée dans initialStateLoadedHandler
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.cpp:475-479, 516` · `SureServo.cpp:159, 252, 1041, 1384, 3128` · `borneUniverselle.cpp:1186-1189`
- **Problème** : RessortRoyal2 signale « Arrêt immédiat actif » quand la sortie vaut false, alors que SureServo la met à true pour arrêter. Chaque connexion écrit donc une erreur parasite dans le diagnostic, et un arrêt réellement actif n'est jamais signalé.
- **Correction** : Tester `immediateStop->getValue()`, ou interroger SureServo.

#### MI-3 · Les alertes préparées pour le nouveau client sont effacées dans le même handler
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.cpp:469-479, 516` · `borneUniverselle.cpp:798-812, 1260` · `main.cpp:1154, 1202`
- **Problème** : « Machine en arrêt d'urgence » est mis en file, puis `clearAllClientNotifications()` vide cette file avant le `sendMessage()` de la boucle, alors que le log affirme « Client notified ».
- **Correction** : Appeler `clearAllClientNotifications()` au début du handler.

#### MI-4 · Couple maximum : deux chemins d'écriture aux règles différentes
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.cpp:247-250, 761-766, 1594-1599, 1640-1647` · `SureServo.cpp:1008-1019` · `SureServo.h:406` · `data/interface.json:298-306`
- **Problème** : Seul le chemin IHM passe par `setMaxTorque`, qui travaille en `uint8_t` et refuse les valeurs > 100. `updateOutputs` et `applyParametersToNodes` écrivent la valeur brute, et « initial torque [%] » n'est pas borné. Une valeur refusée par SureServo est malgré tout persistée et envoyée au drive.
- **Correction** : Valider une seule fois (0 à 100) et faire passer toutes les écritures par `setMaxTorque()`.

#### MI-5 · Rechargement de RessortRoyal2.json refusé en mouvement, puis écrasé par la sauvegarde différée
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.cpp:46-48, 780-789, 986, 1004, 1013` · `BusinessLogic.cpp:604-613`
- **Problème** : Hors idle, le rechargement est refusé sans être mémorisé, alors que le fichier est déjà sur le disque. Le `markDirty` de fin de mouvement le réécrit ensuite avec l'ancien `parameters`. Ce cas est atteignable via l'éditeur (cf. MA-16). Les commentaires citent des états et une reconfiguration PR1/PR2 que le code ne fait pas.
- **Correction** : Mémoriser un rechargement en attente et l'appliquer à l'entrée en IDLE ou STOP.

#### MI-6 · La notification « Free Run » dit toujours « activé »
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.cpp:1652-1657` · `data/interface.json:33-38`
- **Problème** : Le message teste `getIsChanged()`, toujours vrai dans ce bloc, au lieu de `getValue()`, que le log voisin utilise correctement.
- **Correction** : Utiliser `freeRun->getValue()`.

#### MI-7 · Position affichée en uint32 alors que SureServo travaille en int32
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.cpp:1601-1607` · `RessortRoyal2.h:474` · `SureServo.h:88-91, 428` · `SureServo.cpp:1420-1423, 3336` · `borneUniverselle.cpp:4200-4202` · `data/interface.json:74-77`
- **Problème** : Une dérive négative tolérée de -2 PUU s'affiche 4294967294 dans « Position actuelle ».
- **Correction** : Alimenter `v_position` avec `sureServo->getPosition()` dans un nœud signé.

#### MI-8 · handleJoggingState et handleIdleState continuent après une transition
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.cpp:1148-1170, 1436-1452, 1457-1469` · `BusinessLogic.cpp:14-51`
- **Problème** : Faute de `return` après la sortie du JOG, `jogTreatment` s'exécute hors de JOGGING, et un front fwd/rwd peut relancer un jog. `handleIdleState` enchaîne de la même façon sur le test JOG après être passé en CYCLING.
- **Correction** : Ajouter `return true;` après chaque transition.

#### MI-9 · Compteurs de cycles incrémentés sur des événements différents
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.cpp:982-985, 999-1013, 1046, 1069` · `RessortRoyal2.h:148-149`
- **Problème** : En mode simple, `numberOfCyclesMade` est incrémenté à la fin de l'aller et `nbTotalCycles` à la fin du retour. Les retours forcés après urgence ne comptent que dans le total.
- **Correction** : Définir un seul événement « cycle terminé » pour les deux compteurs.

#### MI-10 · Arrêt immédiat virtuel traité deux fois, garde inversée, nœud absent de l'IHM
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.cpp:1079-1112, 1520-1523, 1545-1550, 1615-1620` · `BusinessLogic.cpp:455-460` · `data/config.json:444-447`
- **Problème** : `v_immediateStop` est traité dans `interfaceTreatment` et dans `checkEmergencyConditions`. Quand le premier passe en EMERGENCY, la garde `return false` du second laisse le handler enchaîner d'autres transitions dans la même boucle. Le nœud n'apparaît dans aucune page d'interface.json.
- **Correction** : Un seul point de traitement, `return true` si l'état est déjà EMERGENCY, et exposer ou retirer le nœud.

#### MI-11 · Barre « Cycles du job effectués » : maximum figé à 100 après chaque reconnexion
- **Sévérité** : mineure · **Emplacements** : `data/interface.json:63-72` · `RessortRoyal2.cpp:429, 441-517, 1753-1756` · `RenderTools.h:79-82` · `data/RessortRoyal2.json:8` · `index.js:4599`
- **Problème** : `setMaxValue` n'est envoyé que lorsque l'opérateur modifie le nombre de cycles. Après un rechargement de la page, le front reprend `max: 100`.
- **Correction** : Renvoyer le maximum dans `initialStateLoadedHandler`.

#### MI-12 · Vestiges de l'ancienne version de RessortRoyal2
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.h:26-27, 163, 475, 478-479, 503-504, 508, 520, 605-606, 617-618` · `RessortRoyal2.cpp:1474-1477, 1572-1576` · `SureServo.h:15`
- **Problème** : `handleFreeRunningState` est déclarée mais pas définie, alors que le « Mode free run » est encore annoncé. Les membres `torque`, `targetGo`, `targetBack`, `cycleCounter` et `inCycle` sont inutilisés, `saveDriveParameters` n'est jamais appelée, `DELAY_INIT` est redéfini et le buzzer est calculé puis commenté.
- **Correction** : Supprimer ces éléments, ou réimplémenter le mode free run.

#### MI-13 · machineName[80] : dépassement d'un octet
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.h:137` · `RessortRoyal2.cpp:195-196, 215` · `PLC_Tools.cpp:1204`
- **Problème** : 80 caractères sont acceptés, et `strcpy` écrit alors 81 octets dans un tableau de 80. Le zéro final écrase `userTarget`, réaffecté juste après : le comportement indéfini est masqué.
- **Correction** : Passer `sizeof(machineName) - 1` comme longueur maximale et utiliser `strlcpy`.

#### MI-14 · Nœuds orphelins et doublons dans config.json et RessortRoyal2.h
- **Sévérité** : mineure · **Emplacements** : `data/config.json:125-129, 172-177, 364-368, 430-435, 464-473` · `RessortRoyal2.h:39-45, 52, 86-88, 98, 102-103, 423` · `RessortRoyal2.cpp:285, 295` · `data/interface.json:285-291` · `SureServo.cpp:1712`
- **Problème** : Un second « Full torque » (Virtual bool inputs/7) et « clear all notifications » ne sont référencés nulle part. La sortie physique `triggerPR` est initialisée mais jamais pilotée, les PR étant déclenchés par registre Modbus. `JOGSPEED` et `AUX_FUNCTIONS` dupliquent `JOG_SPEED` et `AUX_FUNCTION`, et `GO_AND_BACK` est rangé sous une mauvaise rubrique. Les 58 hash du header correspondent bien à des nœuds.
- **Correction** : Supprimer les nœuds et define morts et éviter les noms en double.

### 3.2 Servo (SureServo, ServoDrive, MyModbus)

#### MI-15 · getHasAlarms : la tolérance « alarme sans code » ne fonctionne jamais
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:899` · `SureServo.cpp:1073-1080, 2172-2181, 2233-2251` · `data/config.json:327-332, 341-346`
- **Problème** : `alarmPendingTime` n'est jamais affecté, donc cette branche renvoie toujours false, et le log affiche « Alarme servo effacée » au moment même où l'alarme apparaît. Le code d'alarme est relu toutes les 1000 ms, et non 200 ms.
- **Correction** : Armer `alarmPendingTime` sur le front montant et aligner le délai sur le rafraîchissement réel.

#### MI-16 · Rotation des PR : alternance 1↔2 alors que l'en-tête annonce 1→2→3
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:288-307, 752, 787` · `SureServo.cpp:1692-1693, 2115-2120`
- **Problème** : `advancePR` revient à 1 après 2, et le smart delay ne connaît que `targetA` et `targetB`. `targetC` et `pr3Speed` restent pourtant obligatoires.
- **Correction** : Aligner les commentaires et les nœuds obligatoires, ou rétablir réellement trois PR.

#### MI-17 · getTargetPositionReached() renvoie un pointeur converti en bool
- **Sévérité** : mineure · **Emplacements** : `SureServo.cpp:1110-1112` · `SureServo.h:443` · `SureServo/readme.md:612, 705`
- **Problème** : Le nœud étant obligatoire, la fonction renvoie toujours true. Le problème est latent : seul le readme l'utilise.
- **Correction** : `return status.targetPositionReached;`.

#### MI-18 · EEPROM_SAVE_DELAY_MS défini deux fois (200 et 1000 ms)
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:78, 682` · `SureServo.cpp:394, 2419`
- **Problème** : Les méthodes membres utilisent la constante de classe (1000 ms). La constante globale à 200 ms est morte.
- **Correction** : N'en garder qu'une.

#### MI-19 · Garde « couple nul » du constructeur testée sur le pointeur
- **Sévérité** : mineure · **Emplacements** : `SureServo.cpp:130-133, 1464`
- **Problème** : `nodes.maxTorque == 0` compare le pointeur, déjà validé. Un nœud valant 0 envoie donc 0 % au drive à l'init, et si le test était vrai, `setValue` déréférencerait un pointeur nul.
- **Correction** : Tester `nodes.maxTorque->getValue() == 0`.

#### MI-20 · processReset : la stabilité de 200 ms n'est pas continue
- **Sévérité** : mineure · **Emplacements** : `SureServo.cpp:1499-1502, 1559-1582, 1584-1606`
- **Problème** : `static successStartTime` n'est remis à 0 qu'en cas de succès, ni sur échec, ni sur FAILED, ni au timeout. Il est en outre partagé entre instances.
- **Correction** : En faire un membre d'instance, remis à 0 sur échec et à chaque `startReset()`.

#### MI-21 · NEGATIVE_TOLERATED : une position négative déclenche l'erreur fatale « hors limites »
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:87-91` · `SureServo.cpp:1420-1435`
- **Problème** : Au-delà de `-TOLERANCE_PUU`, le cast `(uint32_t)` dépasse `maxRangePuu`, ce qui contredit la politique. Le problème est latent, car RessortRoyal2 utilise `NEGATIVE_INVALIDATES_HOME`.
- **Correction** : Ne tester la butée haute que pour une position positive.

#### MI-22 · La marge de butée s'applique aux mouvements, mais pas à l'init ni au jog
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:34-36` · `SureServo.cpp:668, 784-786, 1431`
- **Problème** : Une position comprise entre `maxRangePuu` et `maxRangePuu` + 100 est acceptée en mouvement, puis déclarée fatale à l'initialisation suivante.
- **Correction** : Utiliser une seule limite effective pour les trois contrôles.

#### MI-23 · Garde homeAtCurrentPosition sans effet sur la détection de redémarrage
- **Sévérité** : mineure · **Emplacements** : `SureServo.cpp:292-298, 414, 2485-2518`
- **Problème** : `detectDriveRestart` ne lit pas la garde : un home virtuel peut provoquer un faux « REDÉMARRAGE DRIVE » suivi d'une urgence. Latent, car `setHomeAtCurrentPosition` n'est jamais appelée.
- **Correction** : Tester la garde dans `detectDriveRestart`.

#### MI-24 · Commentaires contraires au code sur immediateStop et movementCancelled
- **Sévérité** : mineure · **Emplacements** : `SureServo.cpp:257-260, 503-504, 621-625, 1384, 3162-3165` · `SureServo.h:890`
- **Problème** : Les commentaires annoncent `immediateStop` forcé à true par un `handleInitialize` inexistant, alors que la phase 0 le met à false. Ils annoncent aussi que `movementCancelled` est levé par `startGoToPosition`, alors que c'est `process()` qui le lève après 500 ms.
- **Correction** : Corriger ces commentaires, qui portent sur des sorties de sécurité.

#### MI-25 · getPosition() renvoie la cible théorique, contrairement à la doc et aux logs
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:553-565` · `SureServo.cpp:58-59, 1059-1067, 1198, 2136, 3141, 3227`
- **Problème** : Sans position fiable, la fonction renvoie `targetPosition`, remise à 0 par les annulations, alors que la doc annonce `nodes.position`. `setError` préfixe pourtant ses messages avec cette valeur.
- **Correction** : Renvoyer `nodes.position->getValueAsInt32()`, ou corriger la doc.

#### MI-26 · readme de SureServo : API et constantes inexistantes ou périmées
- **Sévérité** : mineure · **Emplacements** : `SureServo/readme.md:154, 281, 343, 399, 504, 592, 606, 611, 666, 700, 721, 805, 936, 1004-1010, 1074, 1147, 1176` · `ServoDrive.h:22-27` · `SureServo.h:26-28, 33, 37, 367-371` · `SureServo.cpp:1964`
- **Problème** : Le readme cite `DRIVE_SUCCESS`, un constructeur à 2 arguments, `setCycleSpeed`, `getIsMoving` et Formaca2.cpp. Ses valeurs FWD/RWD sont inversées, et il annonce `TOLERANCE_PUU` = 50 et un timeout de 30 s, contre 500 et 60 s dans le code.
- **Correction** : Réécrire le readme d'après SureServo.h et RessortRoyal2.cpp.

#### MI-27 · targetPositionReached obligatoire dans le code, optionnel dans le diagnostic et le readme
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:239, 288-307` · `SureServo.cpp:70-86, 172, 182, 2151` · `SureServo/readme.md:58-75, 195-198`
- **Problème** : `validateRequired` exige 18 nœuds, mais le diagnostic ne nomme pas celui-ci, affiche « OK (17) » (un `int` passé à `%zu`) et le classe parmi les optionnels.
- **Correction** : Aligner le diagnostic, calculer le nombre de nœuds, et corriger le readme.

#### MI-28 · Table d'alarmes : deux conventions d'encodage incompatibles
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:97-144, 161-167, 725-733` · `SureServo/readme.md:529-542, 1094-1104` · `RessortRoyal2.cpp:1533-1540`
- **Problème** : 0x0213 code AL531 en décimal, alors que les autres entrées recopient les chiffres en hexadécimal. RessortRoyal2 affiche 0x0013 comme « AL019 », et le readme raisonne sur des codes 0x2xxx.
- **Correction** : Vérifier le format réel de P0-01, puis normaliser la table, l'affichage et le readme.

#### MI-29 · Adresses de registres : commentaires contraires à SureServo.json
- **Sévérité** : mineure · **Emplacements** : `SureServo.cpp:2315, 2320, 2326` · `SureServo.h:727` · `data/hardware/SureServo.json:3, 7-23, 69-77, 84-98, 128-132` · `data/config.json:432-435`
- **Problème** : Le code commente 0x9C, 0x9E et 0xA0, alors que le JSON (avec offset -1) donne 0x5C, 0x02 et 0x64. « Register 5 » désigne en réalité le registre 4, et l'offset 1551 est étiqueté « Data PR2 » en lecture mais correspond à PR3.
- **Correction** : Corriger les commentaires (P0-46, P0-01, P0-50, Register 4, Data PR3).

#### MI-30 · Registre PR : TYPE annoncé en bits 31-28, écrit en bits 3-0
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:45-47` · `SureServo.cpp:990-993, 3339-3343, 3390-3393`
- **Problème** : Corriger le code d'après ce commentaire casserait l'enchaînement PR1→PR2.
- **Correction** : Documenter la disposition réelle : TYPE 3-0, ACC 11-8, DEC 15-12, SPD 19-16.

#### MI-31 · Voyant modbusError : jamais écrit par SureServo, alimenté par un critère global
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:265, 321` · `SureServo.cpp:185, 2150-2213` · `RessortRoyal2.cpp:387, 1844-1855`
- **Problème** : RessortRoyal2 l'écrit d'après le `getLastEvent()` de n'importe quel esclave. Trois critères différents de « perte Modbus » coexistent.
- **Correction** : Le faire écrire par SureServo d'après `isSlaveConsideredDead(getSlaveAddress())`.

#### MI-32 · Des nœuds « optionnels » conditionnent le callback d'alarme et la butée du jog
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:256-264` · `SureServo.cpp:790, 812, 827, 884, 900, 2172-2181`
- **Problème** : Sans `servoAlarm` ou `homeDone`, plus aucune alarme n'est remontée par callback et la butée avant du jog disparaît, sans avertissement.
- **Correction** : S'appuyer sur `status.*` et `getHomeDone()`, en réservant les nœuds à l'affichage.

#### MI-33 · MyModbus : récupération toutes les 5 s, message « toutes les 30 s »
- **Sévérité** : mineure · **Emplacements** : `MyModbus.h:14, 40` · `MyModbus.cpp:78, 569, 611-614`
- **Problème** : `RECOVERY_ATTEMPT_INTERVAL` vaut 5000 ms, mais le commentaire et le message à l'opérateur annoncent 30 s.
- **Correction** : Construire le message à partir de la constante.

#### MI-34 · ServoDrive::startGoToPosition(float) : des pouces passés comme des PUU
- **Sévérité** : mineure · **Emplacements** : `ServoDrive.h:60-66` · `SureServo.h:387`
- **Problème** : La valeur est arrondie sans conversion, et le commentaire cite une classe VFD absente. SureServo masque cette surcharge : le résultat dépend du type statique (latent).
- **Correction** : Supprimer la surcharge ou faire la conversion, avec `using ServoDrive::startGoToPosition;`.

#### MI-35 · Fonctions publiques de SureServo non câblées
- **Sévérité** : mineure · **Emplacements** : `SureServo.h:275-278, 538, 891-892` · `SureServo.cpp:467-480, 3182-3185, 3404-3408, 3433-3438` · `data/hardware/SureServo.json:1-134` · `RessortRoyal2.cpp:360-393`
- **Problème** : `isPostResetStabilizing` renvoie toujours false, `clearUserPulses` n'a pas de consommateur, et `setHomeAtCurrentPosition` exige des nœuds `homingMode` qui ne sont déclarés nulle part.
- **Correction** : Implémenter ces fonctions ou les retirer.

#### MI-36 · Chaque accès fichier suspend PLC et Modbus, et remet à zéro les statistiques Modbus
- **Sévérité** : mineure · **Emplacements** : `PLC_Persistence.cpp:130, 219, 243, 865-882` · `borneUniverselle.cpp:2042-2046` · `MyModbus.h:49` · `MyModbus.cpp:311-338, 476-519, 572-591` · `RessortRoyal2.cpp:1843-1854` · `main.cpp:1274-1275` · `index.js:4621`
- **Problème** : Avec un client connecté, chaque opération de persistance, diagnostic compris, suspend puis reprend. `resume()` remet `lastEvent` à succès (deux fois) et appelle `resetStats()`, ce qui fausse « Durée panne » et le voyant d'erreur. Côté front, l'overlay « PLC is busy » clignote et masque celui de l'éditeur.
- **Correction** : Ne plus remettre les stats à zéro dans `resume()`, réserver la suspension aux écritures lourdes et utiliser un overlay distinct.

### 3.3 Framework BusinessLogic

#### MI-37 · logicExecutor ignore le retour de doBusinessLogic
- **Sévérité** : mineure · **Emplacements** : `BusinessLogic.h:351` · `BusinessLogic.cpp:224-228, 674, 680` · `RessortRoyal2.cpp:635, 697-699, 832, 1394, 1460` · `main.cpp:1163-1164` · `BusinessLogic/readme.md:1510`
- **Problème** : `logicExecutor` renvoie toujours true. Le contrat « false = PLC cassé » et `handleExecutorFailure`, qui ne casse d'ailleurs rien, sont donc morts. Les handlers concernés cassent toutefois déjà le PLC eux-mêmes.
- **Correction** : Propager l'échec, ou supprimer ce contrat.

#### MI-38 · Contexte de transition : NORMAL écrasé, et code 11 en collision avec SERVO_ALARM
- **Sévérité** : mineure · **Emplacements** : `BusinessLogic.cpp:441-446, 457-467, 473-478` · `RessortRoyal2.h:235-246, 271, 571` · `RessortRoyal2.cpp:1322, 1335, 1555-1559, 2073-2078`
- **Problème** : `context <= 1` auto-détecte aussi NORMAL (0). Le code 11, « sortie urgence », vaut SERVO_ALARM dans RessortRoyal2 : chaque sortie d'urgence est donc loguée « Alarme servo ».
- **Correction** : N'auto-détecter que pour AUTO_DETECT, et réserver dans la base des codes génériques distincts.

#### MI-39 · Deux API de nommage de contexte ; tables X-macro non branchées
- **Sévérité** : mineure · **Emplacements** : `BusinessLogic.h:82-106, 123, 184-186` · `BusinessLogic.cpp:249-257, 313-314, 490, 510` · `RessortRoyal2.h:223-228, 620-623` · `RessortRoyal2.cpp:82-102, 2043-2050, 2063-2070`
- **Problème** : Les transitions utilisent `contextToString`, l'historique utilise `getContextName`, non surchargé (« CTX_11 »). `getStateDescription` et `classifyContext` ne sont pas surchargés, et les quatre fonctions `*FromTable` ne sont jamais appelées.
- **Correction** : Fusionner les deux API et faire déléguer les méthodes virtuelles aux tables.

#### MI-40 · Méthodes déclarées sans définition, membres fantômes et getters non const
- **Sévérité** : mineure · **Emplacements** : `BusinessLogic.h:13-18, 214-215, 428-429` · `BusinessLogic.cpp:5-8, 220-222` · `borneUniverselle.h:513` · `BusinessLogic/readme.md:67, 103-106, 1872, 1896-1897`
- **Problème** : `getStateStartTime` et `getTypedPointerForHash` n'ont pas de définition, ce qui provoque une référence indéfinie à l'édition de liens. `NodeInfo` et `nodeInfos` sont morts, et le readme appelle des getters non const depuis une méthode const.
- **Correction** : Définir `getStateStartTime() const` et supprimer le reste.

#### MI-41 · initNodePtr est défini dans BusinessLogic.inl, que BusinessLogic.h n'inclut pas
- **Sévérité** : mineure · **Emplacements** : `BusinessLogic.h:108-109` · `BusinessLogic.inl:207-208` · `RessortRoyal2.h:12-14` · `Node.h:226-264` · `BusinessLogic/readme.md:153-168, 557-580, 732, 744, 1198-1199, 1719, 2023-2024`
- **Problème** : Une application qui n'inclut que le .h obtient une référence indéfinie. Le readme remplace le membre par une lambda mal formée, appelle des méthodes inexistantes, et laisse un bloc de code non fermé.
- **Correction** : Inclure le .inl à la fin du .h et corriger le readme.

#### MI-42 · Tables de types divergentes (isTypeCompatible, diagnostic, getTypeName)
- **Sévérité** : mineure · **Emplacements** : `BusinessLogic.inl:71-110, 121-205, 327-359` · `BusinessLogic.cpp:77-118, 160-164` · `RessortRoyal2.h:474-486` · `data/config.json:388-403`
- **Problème** : Le diagnostic entrée/sortie omet les classes Modbus 32 bits, pourtant utilisées par `position` et `target_A/B/C`. `getTypeName` ignore `CLASS_VIRTUAL_TEXT_OUTPUT_NODE`, et `handleTypeMismatchError` n'est jamais appelée.
- **Correction** : Une seule table classType → {nom, direction, type de base}.

#### MI-43 · updateClockNode efface l'horloge puis la réécrit aussitôt
- **Sévérité** : mineure · **Emplacements** : `BusinessLogic.h:457-468` · `BusinessLogic.cpp:757-760, 774-780` · `RessortRoyal2.cpp:1779-1781, 2080-2085`
- **Problème** : `clearClock()` n'est pas suivi d'un `return`. Comme RessortRoyal2 n'appelle la fonction que si `shouldShowClock()`, l'horloge reste figée en GOING, CYCLING et JOGGING.
- **Correction** : Ajouter un `return` après `clearClock()` et appeler la fonction sans condition.

### 3.4 Moteur BorneUniverselle et parsing de la configuration

#### MI-44 · processMessage : le contrat « false = garder le message » n'est pas respecté
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:1631, 1642, 1754, 1770-1800, 1821-1833, 2254-2266` · `main.cpp:1091, 1154`
- **Problème** : Le message est retiré de la file avant `processMessage`, dont le retour est ignoré : les requêtes « différées » sont perdues malgré le log, et la branche `!setupComplete` est inatteignable.
- **Correction** : Ne retirer le message qu'après un retour true, avec une limite de tentatives, ou supprimer le contrat.

#### MI-45 · Test du type de projet inversé
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:2594-2606` · `borneUniverselle.h:98` · `data/config.json:3`
- **Problème** : `!strcmp(...)` rejette précisément « BorneUniverselle » et accepte toute autre chaîne. Le défaut est masqué par `"type": 2`.
- **Correction** : `strcmp(...) != 0`.

#### MI-46 · Initialisation RS485/Modbus dépendante de l'ordre des sections
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:241-247, 3080-3087, 3628-3631, 3696-3707` · `data/hardware/Kincony_KC868_A8S.json` · `data/hardware/SureServo.json:1-4` · `data/hardware/Waveshare_8DIO.json:1-5` · `data/config.json:79-98`
- **Problème** : `modbusInit` est appelé pour chaque fichier matériel. Un fichier sans clé RS485 (SureServo, Waveshare), traité avant la carte Kincony, provoque `setPlcBroken`, et le boot reste bloqué.
- **Correction** : N'appeler `modbusInit` que pour les types Modbus, et lire RS485 depuis la carte maîtresse.

#### MI-47 · L'échec de createModbusNode est ignoré
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:3197-3201, 3209-3213, 3222-3228`
- **Problème** : Contrairement aux branches Rx et Tx, l'échec ne produit qu'un `prepareMessage` suivi de `found = true`. L'erreur n'apparaît que plus tard, dans `initNodePtr`.
- **Correction** : `return false;`.

#### MI-48 · Clé « inverse » traitée différemment selon le type de nœud
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:3021-3026, 3191-3192, 3205-3206, 3311-3313, 3536, 3905-3907` · `Node.h:610` · `Node.cpp:334, 1263-1285` · `data/config.json:103`
- **Problème** : ModbusReadCoil lit la clé dans le fichier matériel, donc pour toute la carte, alors que les autres types la lisent dans config.json. VirtualBooleanInputNode la reçoit mais ne l'applique jamais.
- **Correction** : Lire la clé sur le nœud dans tous les cas, et l'appliquer dans `VirtualBooleanInputNode::setValue`.

#### MI-49 · Types « ModbusRead/WriteDobbleInputRegisters » déclarés mais impossibles à créer
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.h:160-161` · `borneUniverselle.cpp:3215-3217, 3308-3572, 4197, 4201, 4392, 4476-4477` · `Node.h:45-46, 491-498` · `Node.cpp:881` · `BusinessLogic.cpp:103`
- **Problème** : Les macros, la classe de lecture et la sérialisation existent, mais aucune branche de création ne les gère. Quant à l'écriture d'un input register, elle n'existe pas en Modbus.
- **Correction** : Ajouter la branche de lecture et supprimer la variante en écriture.

#### MI-50 · Nœuds Modbus multi-bits : buffers de 32 et 64 pour nb_values jusqu'à 255 ; WriteMultipleCoils non géré
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:2493-2496, 3362-3366, 3526, 3535-3540, 4216-4243` · `Node.h:476, 591` · `Node.cpp:1137-1148, 1170-1199`
- **Problème** : `nb_values` n'est pas borné et déborde `text[32]` puis `values[64]`. `handleNodesChange` n'a pas de case pour WriteMultipleCoils et tombe dans `setPlcBroken`, et la classe de lecture n'a pas de destructeur. Latent, car nb_values ≤ 16 aujourd'hui.
- **Correction** : Borner `nb_values` à 64 au parsing, dimensionner les buffers en conséquence et compléter le switch.

#### MI-51 · isKinconyA8S vaut true par défaut : la détection est morte et GPIO2 est forcé à chaque GotIP
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.h:390` · `borneUniverselle.cpp:3060-3063` · `main.cpp:38, 118-126` · `Node.cpp:953-959` · `RessortRoyal2.cpp:1570-1576` · `data/hardware/Kincony_KC868_A8S.json:26-32`
- **Problème** : `turnOffBuzzer` met GPIO2 à LOW quelle que soit la carte, en court-circuitant le nœud Buzzer. Sans effet sur la carte A8S actuelle.
- **Correction** : Initialiser le drapeau à false et piloter le buzzer via son nœud.

#### MI-52 · Options de config.json : défauts et noms de clés incohérents
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.h:185-191, 428-431` · `borneUniverselle.cpp:2538-2539, 2545-2546, 2552, 2572, 2576-2577` · `data/config.json:8`
- **Problème** : Les valeurs par défaut diffèrent entre le .h (false) et `parseConfig` (true). Les logs citent des clés camelCase inexistantes, et « log historry and transition » est mal orthographié des deux côtés.
- **Correction** : Centraliser les défauts, citer les vraies clés, et corriger la faute en acceptant encore l'ancienne clé.

#### MI-53 · Commande WebSocket {"config": …} : sauvegarde et redémarrage hors du flux normal
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:1811-1819` · `borneUniverselle.h:375` · `index.js:4599`
- **Problème** : La commande valide (avec les effets de bord décrits en MA-9), sauvegarde puis redémarre, sans prévalidation ni backup. Le front ne l'envoie jamais.
- **Correction** : Supprimer la branche, ou la rediriger vers `handleSaveFile`.

#### MI-54 · setPlcBrokenImpl : un échec de malloc désarme définitivement le mécanisme
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:208-234`
- **Problème** : Le `return` intervient avant `plcBroken = true` et la remise à zéro de `inSetPlcBroken` : toutes les erreurs fatales suivantes sont ignorées.
- **Correction** : Utiliser un buffer sur la pile et remettre l'état à zéro sur tous les chemins.

#### MI-55 · Contrôles défensifs appliqués de façon incohérente
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:1880, 1971, 2204, 3766-3767, 3779, 3825-3826, 3840, 3863, 3900-3904`
- **Problème** : `handleIsPLCIdleRequest` déréférence `businessLogic` sans le tester. Rx et Tx font un placement new sur le résultat de `ps_malloc` sans tester nullptr, contrairement à Virtual et Modbus.
- **Correction** : Harmoniser les tests, avec un repli `new (std::nothrow)`.

#### MI-56 · parseHardwares : erreurs étiquetées avec le nom du projet au lieu de la section
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:2948, 2952, 2961, 3098` · `borneUniverselle.h:338`
- **Problème** : Le membre `name` (« ressort_royal ») est utilisé à la place de `sectionName`.
- **Correction** : Utiliser `sectionName`.

#### MI-57 · La machine d'état de nettoyage client n'est jamais exécutée
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.h:269, 523-545` · `borneUniverselle.cpp:564, 597-601, 676-684, 703-735, 1497, 4635-4660`
- **Problème** : `processClientCleanup` n'est appelé nulle part, alors que les déconnexions posent CLEANUP_PENDING et loguent « Nettoyage immédiat terminé ». Le brancher tel quel effacerait une reconnexion survenue dans les 2 s.
- **Correction** : Supprimer la machine d'état, ou l'appeler dans `loop()` en réinitialisant `isCleanupPending` à la connexion.

#### MI-58 · isAllConditionsOk() annoncée « entrées lues une fois » alors qu'elle teste aussi Modbus
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:401-439, 1318` · `main.cpp:861, 1156, 1181-1184`
- **Problème** : `/api/status` et le log de `loop` attribuent à des entrées non lues ce qui vient d'un Modbus mort. Le log interne porte en plus le nom d'une autre méthode.
- **Correction** : Exposer séparément `all_inputs_read_once` et `modbus_dead`.

#### MI-59 · Nœuds alloués par ps_malloc et placement new, mais libérés par delete
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:3315-3318, 3778-3780, 4723-4729, 4776-4778`
- **Problème** : C'est un comportement indéfini, qui ne fonctionne que parce que l'`operator delete` d'ESP-IDF appelle `free()`.
- **Correction** : Appeler explicitement le destructeur puis `free`, ou définir un `operator new`/`delete` de classe dans `Node`.

#### MI-60 · Clé « modbusRTU » jamais lue ; macro MODBUS_RTU inutilisée et de casse différente
- **Sévérité** : mineure · **Emplacements** : `data/hardware/Kincony_KC868_A8S.json:22` · `borneUniverselle.h:151` · `borneUniverselle.cpp:3696-3742`
- **Problème** : Un `ModbusRTU` est toujours créé, alors que la clé laisse croire que le mode est configurable.
- **Correction** : Supprimer la clé et la macro, ou les lire avec une casse unique.

#### MI-61 · OTA câblé mais impossible à déclencher ; strcpy non borné sur ota_url
- **Sévérité** : mineure · **Emplacements** : `main.cpp:10, 22-23, 229-251, 1062-1065` · `borneUniverselle.cpp:495-498, 2692-2698` · `borneUniverselle.h:51, 340, 344` · `data/config.json:73-77` · `partitions_4MB_no_ota.csv:4` · `partitions_16Mb.csv:3`
- **Problème** : `update()` n'est jamais appelé. `ota_url` (« truc ») est copié sans borne dans 80 octets et jamais relu, `otaPending` n'est pas défini, et aucune table de partitions ne prévoit de slot OTA.
- **Correction** : Supprimer ce code mort, ou implémenter l'OTA complètement (partitions OTA et `strlcpy`).

#### MI-62 · Clé « RTC » : seule sa présence compte, et begin() ignore sda/scl et renvoie toujours true
- **Sévérité** : mineure · **Emplacements** : `data/hardware/Kincony_KC868_A8S.json:9` · `borneUniverselle.h:202` · `borneUniverselle.cpp:3590, 3602, 3604-3610` · `DS3231_RTC.h:40-47` · `DS3231_RTC.cpp:32-96`
- **Problème** : `"RTC": false` active quand même la sonde. La branche d'erreur, qui ne cite d'ailleurs que le DS3231, est morte, et `Wire.begin` est appelé deux fois.
- **Correction** : Lire la valeur booléenne, renvoyer false si aucune puce n'est détectée, et retirer `sda`/`scl` de la signature.

### 3.5 Concurrence et suspension

#### MI-63 · Suspension interne : suspendedByPLC peut rester vrai avec suspendCount à 0
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.h:462-463, 566-569` · `borneUniverselle.cpp:661-675, 870-873, 1925-1934, 1946-1960, 2247-2249, 5075-5101, 5104-5153` · `main.cpp:783-788, 1131` · `PLC_Persistence.cpp:243`
- **Problème** : Un compteur unique et non protégé est partagé entre la suspension JS et la suspension interne, qui peut venir de la tâche `async_tcp`. Une imbrication de ces deux suspensions, ou une sortie anticipée sans client, laisse `refresh()` et Modbus suspendus.
- **Correction** : Un compteur protégé par source, et la levée de `suspendedByPLC` même sans client connecté.

#### MI-64 · Ordre d'acquisition inversé entre notifMutex et webSocketMutex
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:516, 557, 739, 842-845, 1063-1068, 1126` · `main.cpp:643, 1202` · `MutexGuard.h:21`
- **Problème** : La boucle principale et la tâche AsyncTCP prennent les deux verrous dans l'ordre inverse. Le timeout de 300 ms évite l'interblocage, mais provoque des blocages et des purges sautées.
- **Correction** : Fixer un ordre global et ne pas imbriquer les verrous.

#### MI-65 · Appel explicite de ~MutexGuard() suivi de la destruction automatique
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:1455, 1468, 1602` · `MutexGuard.h:30-35`
- **Problème** : C'est une double destruction, donc un comportement indéfini. Le second relâchement est aujourd'hui refusé par FreeRTOS.
- **Correction** : Ajouter une méthode `release()`, ou restreindre la portée du garde.

#### MI-66 · preventReentrance de saveJsonToFile : ne couvre pas les callbacks et fait échouer les sauvegardes concurrentes
- **Sévérité** : mineure · **Emplacements** : `PLC_Persistence.cpp:233-236, 247, 254, 340, 345-348`
- **Problème** : Le drapeau est remis à false avant le callback et testé avant le mutex. Une autre tâche reçoit alors `false` au lieu d'attendre, et sa modification est perdue (voir MA-23).
- **Correction** : Supprimer le drapeau et s'appuyer sur le mutex.

#### MI-67 · Gestion incohérente des événements WiFi : GotIP différé, déconnexion traitée sur place
- **Sévérité** : mineure · **Emplacements** : `main.cpp:41-50, 190-223, 1055-1057, 1107-1116`
- **Problème** : `WiFiGotIP` ne pose qu'un drapeau `volatile`, alors que `WiFiStationDisconnected` exécute `MDNS.end()` et la planification directement dans le contexte de l'événement, avec des drapeaux non `volatile`. Les commentaires se contredisent sur ce contexte.
- **Correction** : Ne poser qu'un drapeau horodaté et faire tout le traitement dans `loop()`.

#### MI-68 · logFileSystemStatus : fonction publique de « log » destructive par défaut et sans mutex
- **Sévérité** : mineure · **Emplacements** : `PLC_Persistence.h:81` · `PLC_Persistence.cpp:45, 1338, 1386-1455` · `AtomicFileWriter.cpp:37`
- **Problème** : La fonction renomme les `.backup` et supprime les `.tmp`. Appelée hors du boot, elle casserait un commit en cours.
- **Correction** : Rendre la récupération privée au boot et l'affichage en lecture seule par défaut.

### 3.6 Front/back et IHM

#### MI-69 · plc_suspended : la reprise répond true alors que le front attend false
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:1939-1961, 2011-2023, 2058-2064` · `borneUniverselle.h:467` · `index.js:4599, 4621`
- **Problème** : Le back envoie « demande acceptée » et non l'état réel. Le front réessaie donc 1 s plus tard, ce qui ajoute environ 1 s à chaque opération de l'éditeur.
- **Correction** : Envoyer l'état réel, et mettre l'acceptation dans une clé séparée.

#### MI-70 · Le premier message `states` est pris pour l'état initial complet
- **Sévérité** : mineure · **Emplacements** : `index.js:4599` · `borneUniverselle.h:127` · `borneUniverselle.cpp:1217-1227, 1851-1861, 4028-4033` · `main.cpp:1162, 1197-1199` · `RessortRoyal2.cpp:1778-1780`
- **Problème** : `notifyWebClient` pousse des états partiels dès la connexion, et le front envoie `initialStatesLoaded` dès qu'il les reçoit. L'interface et la logique sont donc activées avant l'état complet, dans une fenêtre courte.
- **Correction** : Marquer explicitement la réponse à `allStatesRequest`, et ne rien pousser avant `clientFullyInitialized`.

#### MI-71 · Chemin « horloge Unix » mort des deux côtés
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.h:203-206` · `borneUniverselle.cpp:5215-5230, 5261-5268` · `index.js:4621`
- **Problème** : `GET_RTC_UNIX_TIME` n'est jamais émis. Le handler annonce « Updating RTC » mais se contente d'imprimer, et il cite une macro qui n'existe pas.
- **Correction** : Supprimer ce chemin, ou l'implémenter avec `RTC_Service::setTime`.

#### MI-72 · Routes HTTP désalignées entre back et front
- **Sévérité** : mineure · **Emplacements** : `main.cpp:783-790, 798-809, 812, 893, 900` · `index.js:4599` · `data/index.html:10, 42`
- **Problème** : `Lo.rpc` appelle un objet `HTTP` et une route `/api/rpc` qui n'existent pas. `serveStatic("/")`, enregistré en premier, capte `/assets/` (le cache d'un an ne s'applique donc jamais). `/static/css/` est un dossier inexistant, et `/config` contourne le flush.
- **Correction** : Supprimer le code et les routes morts, et enregistrer les routes spécifiques avant `/`.

#### MI-73 · URL WebSocket : en HTTPS, l'adresse se réduit à « wss:// »
- **Sévérité** : mineure · **Emplacements** : `index.js:4621` · `main.cpp:32`
- **Problème** : À cause de la précédence du ternaire, l'hôte et `/ws` ne sont ajoutés qu'au cas `ws://`. Le « repli » réessaie ensuite la même URL.
- **Correction** : `(https ? "wss://" : "ws://") + location.host + "/ws"`.

#### MI-74 · Propriétés d'interface.json ignorées par les composants (unit, color)
- **Sévérité** : mineure · **Emplacements** : `data/interface.json:13, 19, 36, 42, 60, 76, 288, 295` · `data/component-manifest.json` · `index.js:4599`
- **Problème** : rx-numeric et tx-numeric ignorent `unit`, tx-bool ignore `color`, et rx-label utilise `state` au lieu de `color`. Le manifeste ne déclare pas ces propriétés.
- **Correction** : Les implémenter ou les retirer d'interface.json.

#### MI-75 · Mot de passe de page dans interface.json jamais lu
- **Sévérité** : mineure · **Emplacements** : `data/interface.json:246` · `data/config.json:20-26` · `index.js:4599`
- **Problème** : Le front ne lit que `security.passwords` de config.json.
- **Correction** : Retirer la clé d'interface.json.

#### MI-76 · Champs `component` de config.json ignorés et contradictoires
- **Sévérité** : mineure · **Emplacements** : `data/config.json:19, 83, 104, 201, 207, 213, 225, 261, 353` · `data/interface.json:150-173` · `index.js:4599, 4621`
- **Problème** : `hw-module` n'existe pas, `no-show` n'est jamais interprété, et interface.json écrase ces valeurs : par exemple, `zeroSpeed` est marqué `no-show` mais s'affiche.
- **Correction** : Retirer ces champs de config.json.

#### MI-77 · renderers.json obsolète ; logs et repli de l'éditeur erronés
- **Sévérité** : mineure · **Emplacements** : `data/renderers.json:1, 65-115` · `data/component-manifest.json:62-82, 140-160` · `data/config.json:12-16` · `data/interface.json:106-108` · `index.js:4621`
- **Problème** : L'éditeur charge le manifeste mais ses logs citent renderers.json, et son repli exécute `Object.keys(null)`, ce qui lève une TypeError. renderers.json n'est lu par personne et diverge du manifeste (`min`/`max`, un `tx-button` inexistant, des composants absents).
- **Correction** : Supprimer renderers.json, corriger les logs et écrire un vrai repli.

#### MI-78 · Éditeur WiFi du front incompatible avec le parsing C++
- **Sévérité** : mineure · **Emplacements** : `index.js:4621` · `borneUniverselle.h:36-44` · `borneUniverselle.cpp:2704-2727, 2753-2757` · `data/config.json:38-45`
- **Problème** : L'éditeur ne connaît pas le mode `access_point`. Il affiche `dhcp` à false quand la clé est absente, alors que le C++ le considère vrai par défaut. Enfin, le `label for` ne correspond pas à l'`id` du champ.
- **Correction** : Ajouter le mode AP, utiliser `dhcp !== false` et aligner `for`/`id`.

#### MI-79 · Les commentaires JSON disparaissent à chaque sauvegarde depuis l'éditeur
- **Sévérité** : mineure · **Emplacements** : `data/config.json:46-69, 103` · `data/interface.json:69` · `index.js:4621` · `platformio.ini` (ARDUINOJSON_ENABLE_COMMENTS)
- **Problème** : Le fichier est parsé en JSON5 puis réécrit avec `nativeJSON.stringify`. Les réseaux et options mis en commentaire sont détruits sans avertissement.
- **Correction** : Avertir l'utilisateur, ou sauvegarder le texte validé tel quel.

#### MI-80 · Faux avertissement « recetteFileName dans PMB.json » à chaque sauvegarde de config.json ou interface.json
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:137-144` · `RessortRoyal2.cpp:552-565` · `index.js:4621` · `PLC_Tools.cpp:1949`
- **Problème** : Le handler logue « not config.json » sans condition, puis délègue tous les fichiers à RessortRoyal2, qui renvoie un message copié d'un autre projet.
- **Correction** : Ignorer config.json et interface.json à cet endroit, et corriger les textes.

#### MI-81 · ClockDisplay::attach envoie son descriptor pendant le setup, sans client connecté
- **Sévérité** : mineure · **Emplacements** : `ClockDisplay.cpp:6-15` · `ClockDisplay.h:6-17` · `RessortRoyal2.cpp:355, 442-494` · `main.cpp:1025` · `RenderTools.cpp:247-262` · `borneUniverselle.cpp:849-851`
- **Problème** : Sans RTC, le champ horloge n'est jamais grisé, contrairement à ce qu'affirme le log.
- **Correction** : Déplacer l'appel dans `initialStateLoadedHandler`.

#### MI-82 · RenderTools : sendBatch non défini, setDisabled masqué, addNodeProperty incohérent
- **Sévérité** : mineure · **Emplacements** : `RenderTools.h:54, 60-67, 216-218` · `RenderTools.cpp:145-150, 156-239, 241-245, 359-364` · `data/interface.json:255`
- **Problème** : `sendBatch` est déclaré sans définition. `EnhancedButton::setDisabled` masque celui de `Render` et force la couleur « neutral » même quand on réactive le bouton. `addNodeProperty(bool)` modifie `disabled` quel que soit le hash. L'essentiel est latent.
- **Correction** : Implémenter ou supprimer `sendBatch`, rendre `setDisabled` virtuel, et ne forcer la couleur qu'à la désactivation.

#### MI-83 · DropDown::setItems codé en dur sur doc["recettes"][i]["name"]
- **Sévérité** : mineure · **Emplacements** : `RenderTools.h:99-106` · `RenderTools.cpp:101-106` · `RenderTools/example.txt:1-14`
- **Problème** : La doc annonce un document générique, mais tout autre format produit une liste vide (latent).
- **Correction** : Documenter le format attendu, ou rendre les clés paramétrables.

### 3.7 Node, PLC_Tools et diagnostic

#### MI-84 · Bit::isRisingEdge et getIsChanged restent vrais en permanence
- **Sévérité** : mineure · **Emplacements** : `Node.h:412-447` · `Node.cpp:1160-1166, 1193-1198`
- **Problème** : `bitHideValue` n'est resynchronisé qu'au changement suivant : le front se comporte comme un niveau (latent).
- **Correction** : Appeler `setValue` à chaque lecture, ou ajouter `clearIsChanged()`.

#### MI-85 · Désactivation temporaire des nœuds : branche Modbus morte, et 10 s au lieu de 5
- **Sévérité** : mineure · **Emplacements** : `Node.h:142-146` · `Node.cpp:215-231, 242-249`
- **Problème** : La désactivation est réservée aux nœuds non Modbus, ce qui rend la réactivation Modbus inatteignable. La durée est doublée avant même la première désactivation.
- **Correction** : Supprimer la branche morte et ne doubler la durée qu'après une désactivation.

#### MI-86 · Throttling de logDiagnostic : aucun préfixe ne correspond aux messages réels
- **Sévérité** : mineure · **Emplacements** : `PLC_Tools.cpp:1072-1079` · `Node.cpp:225-226, 246-248` · `borneUniverselle.cpp:1186-1203` · `MyModbus.cpp:398-411`
- **Problème** : Les accents, les préfixes de sévérité et les textes diffèrent des messages émis, et ces messages n'atteignent de toute façon pas `logDiagnostic`. La table de throttling est donc morte.
- **Correction** : Aligner les préfixes sur les messages réels, ou supprimer la table.

#### MI-87 · Déclarations de PLC_Tools sans définition
- **Sévérité** : mineure · **Emplacements** : `PLC_Tools.h:66-67, 118-120, 146-147, 175, 181-191, 277-280, 365-366, 380-386` · `PLC_Tools.cpp:11-20, 1500-1503, 1696-1699, 1942` · `PLC_Persistence.h:78-79`
- **Problème** : `getFilteredFiles(AsJson)`, implémentée dans PLC_Persistence avec un autre type, n'a pas de définition ici, pas plus que `logDiagnosticThrottled` et les fonctions « recette », les fonctions libres du GC et les statiques de `resetPeriodicMessages` : le premier appel échouerait à l'édition de liens. `autoFlushEnabled` n'est jamais lu.
- **Correction** : Supprimer ces déclarations ou les implémenter.

#### MI-88 · Journal /reboot_log.json : deux implémentations et des clés incohérentes
- **Sévérité** : mineure · **Emplacements** : `PLC_Persistence.h:80` · `PLC_Persistence.cpp:1179-1222` · `PLC_Tools.h:20-21, 361-362` · `PLC_Tools.cpp:43-46, 77-89, 117-145, 163, 185-187` · `borneUniverselle.h:201` · `borneUniverselle.cpp:1689, 1696`
- **Problème** : `addRebootLogEntry` n'est jamais appelée. `getRebootLog` lit `time` alors que `logReboot` écrit `timestamp` et `reset_reason`, si bien que l'heure et la cause ne s'affichent pas. `MAX_LOG_ENTRIES` est ignoré (20 en dur) et le chemin est défini en double.
- **Correction** : Une seule implémentation, avec des clés alignées.

#### MI-89 · Flush du diagnostic pendant un mouvement quand le buffer est plein
- **Sévérité** : mineure · **Emplacements** : `PLC_Tools.cpp:1162-1190, 1865-1871, 1899-1915` · `PLC_Tools.h:135-136`
- **Problème** : `flushDiagnosticBuffer()` est appelé sans tester `isIdle()`, contrairement à la règle « jamais de flush pendant un mouvement », et il déclenche la suspension de MI-36.
- **Correction** : Utiliser un buffer circulaire, ou rejeter les messages pendant les mouvements.

#### MI-90 · ModbusWriteMultipleCoilslNode : setValues refuse un tableau complet et publie des valeurs non confirmées
- **Sévérité** : mineure · **Emplacements** : `Node.cpp:1072, 1076-1088, 1116-1128` · `Node.h:176-180` · `borneUniverselle.cpp:4216-4229`
- **Problème** : Le test `<` rejette exactement `nbValues` valeurs. Par ailleurs, `values` est mis à jour avant la transaction : la notification expose donc des valeurs que le module n'a pas confirmées.
- **Correction** : Utiliser `<=`, et ne copier les valeurs qu'après `EX_SUCCESS`.

#### MI-91 · Node : déclaration sans définition, contrôles inopérants, dépassement d'un octet
- **Sévérité** : mineure · **Emplacements** : `Node.h:18, 102, 112, 131, 164-168, 223, 549-550, 585` · `Node.cpp:112-121, 146-153, 273`
- **Problème** : `setMode(char*)` n'a pas de définition, et `isnan` est appliqué à un `uint32_t`. `setName` appelle `strlen` avant de tester nullptr et peut écrire 81 octets dans un tableau de 80. On trouve aussi des setters en `uint16` pour des membres en `uint32`, `lastUp` et `lastDown` non initialisés, et des membres morts.
- **Correction** : Nettoyer le code mort, tester nullptr d'abord, corriger le bornage et initialiser les membres.

#### MI-92 · MemoryMonitor::trackStats ne fait rien dès que le WiFi STA est connecté
- **Sévérité** : mineure · **Emplacements** : `MemoryMonitor.cpp:101-111` · `main.cpp:1262`
- **Problème** : `lastSafeCall = now` est affecté avant le test des 5 s, qui est donc toujours vrai : la surveillance mémoire est muette en fonctionnement normal.
- **Correction** : Faire le test avant la mise à jour de `lastSafeCall`.

#### MI-93 · printFlashStats annonce une partition applicative pleine à 100 %
- **Sévérité** : mineure · **Emplacements** : `MemoryMonitor.cpp:21-23, 37-41` · `partitions_4MB_no_ota.csv:4` · `logs/device-monitor-260928-153158.log:36-37`
- **Problème** : `getFreeSketchSpace()` renvoie 0 sans partition OTA, alors que la partition app0 fait 2,7 Mo.
- **Correction** : Utiliser la taille de la partition courante, obtenue par `esp_ota_get_running_partition()`.

### 3.8 Persistance et JSON

#### MI-94 · createBackup et recoverFromBackup supposent que File::name() renvoie le chemin complet
- **Sévérité** : mineure · **Emplacements** : `PLC_Persistence.h:28` · `PLC_Persistence.cpp:727-735, 762, 785, 983-985, 1368, 1423` · `borneUniverselle.cpp:2195, 2617` · `platformio.ini:16, 54`
- **Problème** : Depuis le cœur 2.x, `name()` ne renvoie que le nom du fichier. Aucune version n'est donc trouvée : config.v1.json est écrasé à chaque application de la configuration, sans rotation.
- **Correction** : Normaliser le nom comme le fait `recoverInterruptedCommits`, ou utiliser `path()`.

#### MI-95 · Les suppressions internes de fichiers protégés sont toujours refusées
- **Sévérité** : mineure · **Emplacements** : `PLC_Persistence.h:19-20, 69` · `PLC_Persistence.cpp:588-600` · `borneUniverselle.cpp:2583-2592` · `PLC_Tools.cpp:102, 209-211` · `data/config.json:5`
- **Problème** : L'appel se fait sans `force` : l'option « delete diagnostic file at startup » ne peut jamais fonctionner, et `clearRebootLog`, qui n'est d'ailleurs jamais appelée, renvoie toujours false.
- **Correction** : Passer `force = true` pour les suppressions internes légitimes.

#### MI-96 · fileSystemReady vaut true même après un échec de montage
- **Sévérité** : mineure · **Emplacements** : `PLC_Persistence.cpp:34-44, 913, 924, 1073, 1080` · `PLC_Persistence.h:98`
- **Problème** : Le drapeau passe à true juste après `setPlcBroken("LittleFS mount/format failed")`, et `isFileSystemReady()` n'est jamais appelée.
- **Correction** : Refléter l'état réel du montage, ou supprimer le drapeau.

#### MI-97 · Le « timeout dynamique » de la persistance n'est jamais branché
- **Sévérité** : mineure · **Emplacements** : `PLC_Persistence.h:26-34, 147` · `PLC_Persistence.cpp:10, 86-92` · `MutexGuard.h:21`
- **Problème** : `getTimeoutForFileSize` et ses constantes sont morts : `MutexGuard` attend toujours 300 ms. `lastFileSize` et `lastOperationTime` ne sont jamais lus.
- **Correction** : Transmettre le timeout à `MutexGuard`, ou supprimer ce code.

#### MI-98 · SafeJsonDocument(size_t) ignore la capacité, et l'allocation « PSRAM » se fait en MEMORY_AUTO
- **Sévérité** : mineure · **Emplacements** : `SafeJsonDocument.h:6, 15, 36` · `SafeJsonDocument.cpp:10` · `PLC_Persistence.h:29` · `PLC_Persistence.cpp:258-259, 275, 376, 481` · `borneUniverselle.cpp:1756` · `RessortRoyal2.cpp:150, 1790` · `PLC_Tools.h:160` · `PLC_Tools.cpp:500-503`
- **Problème** : Avec ArduinoJson 7, les capacités passées (65536, 8192, 4096) ne bornent rien. La PSRAM n'est utilisée qu'à partir de 512 octets.
- **Correction** : Supprimer ce constructeur ou plafonner réellement la taille, et aligner le nom et la documentation.

#### MI-99 · SafeJsonDocument : le déplacement échange aussi le pointeur d'allocateur
- **Sévérité** : mineure · **Emplacements** : `SafeJsonDocument.h:31, 45-53` · `PLC_Persistence.cpp:1158-1172` · `borneUniverselle.cpp:2235`
- **Problème** : Le document déplacé pointe sur l'allocateur de la source, qui devient pendant quand celle-ci est détruite. Seul le NRVO protège aujourd'hui le `return doc;`.
- **Correction** : Interdire le déplacement, ou utiliser un allocateur statique sans état.

### 3.9 WiFi, console série et API HTTP

#### MI-100 · Quatre timeouts WiFi pour une même notion
- **Sévérité** : mineure · **Emplacements** : `wifimanagment.h:20-21, 382` · `wifimanagment.cpp:401, 405, 548-557, 983`
- **Problème** : On trouve 60 s (inutilisé), 20 s (masqué par une variable locale), 15 s pour la bascule et 30 s (méthode non définie).
- **Correction** : Une constante pour l'attente et une pour la bascule, la seconde au moins égale à la première.

#### MI-101 · Méthodes WifiManagment déclarées sans définition ; parsing annoncé comme délégué mais fait ailleurs
- **Sévérité** : mineure · **Emplacements** : `wifimanagment.h:291, 322, 381-383` · `borneUniverselle.cpp:461, 2674-2678, 2705-2727` · `main.cpp:324, 480`
- **Problème** : `getWifiStatus`, `parseWifiFromJson`, `configureStaticIP`, `waitForConnection` et `setupMDNS` ne sont que déclarées. Le commentaire annonce que le parsing est délégué à WifiManagement, alors qu'il est fait dans BorneUniverselle.
- **Correction** : Supprimer ou implémenter ces méthodes, et corriger le commentaire.

#### MI-102 · initializeEventHandlers jamais appelé ; deux sources de vérité pour mDNS
- **Sévérité** : mineure · **Emplacements** : `wifimanagment.h:305, 388-391` · `wifimanagment.cpp:460-465, 509-512, 572-580, 1025-1133` · `main.cpp:191-193, 1047-1057`
- **Problème** : `onDisconnectedCallback` est mort, et `mdnsStarted` (présenté comme « source unique ») reste à false pendant que main.cpp tient `mdnsInitialized`.
- **Correction** : Centraliser les événements et mDNS en un seul endroit.

#### MI-103 · Deux implémentations de connexion, et le hostname est ignoré
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp:2871-2873` · `wifimanagment.cpp:351-440, 918-1012`
- **Problème** : `connectWifi`, la seule utilisée, n'applique ni le hostname, ni le retour en DHCP, ni les validations. `connectWifiWithResult`, qui les fait, n'est jamais appelée.
- **Correction** : Fusionner les deux fonctions.

#### MI-104 · Deux drapeaux de connexion WiFi : fausse « INCOHÉRENCE » à chaque démarrage
- **Sévérité** : mineure · **Emplacements** : `wifimanagment.cpp:428-432, 709-759` · `main.cpp:328, 1075, 1088`
- **Problème** : `connectWifi` ne pose que `isConnected`, alors que `showIpAddress` lit `wificonnected`. Appelée avant le traitement de GotIP, elle signale une incohérence puis corrige l'état depuis une fonction d'affichage.
- **Correction** : Un seul drapeau, et un affichage sans effet de bord.

#### MI-105 · Diagnostic série WiFi faux en mode AP
- **Sévérité** : mineure · **Emplacements** : `main.cpp:166, 379, 403-404, 451` · `data/config.json:42` · `wifimanagment.h:202` · `wifimanagment.cpp:637-659`
- **Problème** : Le mode est affiché à la place du SSID, et « Formaca-Backup » est codé en dur au lieu de « RS- AP3 ». Le « Temps actif » démarre au premier appel de la commande, pas à la connexion.
- **Correction** : Utiliser `WiFi.softAPSSID()` et mémoriser l'instant de connexion.

#### MI-106 · apMaxClients jamais lu ni transmis à softAP()
- **Sévérité** : mineure · **Emplacements** : `wifimanagment.h:198, 203` · `wifimanagment.cpp:481` · `borneUniverselle.cpp:2875-2915` · `borneUniverselle.h:35-39`
- **Problème** : La structure annonce 1 client, mais l'AP en accepte 4, la valeur par défaut du cœur.
- **Correction** : Ajouter une clé `ap_max_clients` et la transmettre, ou supprimer le champ.

#### MI-107 · Clés WIFI_MODE et AP_* définies dans deux en-têtes
- **Sévérité** : mineure · **Emplacements** : `wifimanagment.h:24-32` · `borneUniverselle.h:8, 34-43`
- **Problème** : Sept macros sont redéfinies sous un commentaire de « préparation » devenu obsolète. Il y a un risque de désynchronisation.
- **Correction** : Supprimer le bloc de borneUniverselle.h.

#### MI-108 · Tailles SSID/PWD incohérentes ; sérialisation obsolète et dangereuse
- **Sévérité** : mineure · **Emplacements** : `wifimanagment.h:77-81, 181-183, 371` · `wifimanagment.cpp:96-185, 245, 877, 1015`
- **Problème** : On trouve une constante de 30 pour des champs de 80, une écriture hors limites dans `ssid[30]` et des `strcpy` non bornés qui perdent en plus la configuration AP. Ce code n'a aucun appelant.
- **Correction** : Supprimer ce code, ou le réécrire en s'appuyant sur `sizeof`.

#### MI-109 · L'aide série ne correspond pas aux commandes réelles
- **Sévérité** : mineure · **Emplacements** : `main.cpp:338, 341-351, 415, 481, 504-589`
- **Problème** : Les commandes 'a' et 'B' sont annoncées mais non gérées, 'I' et 'T' ne sont pas documentées, et « 4 réseaux » est affiché en dur.
- **Correction** : Générer l'aide à partir de la table des commandes.

#### MI-110 · Commande 'A' : effacement en RAM seulement, et entrée fantôme sur liste vide
- **Sévérité** : mineure · **Emplacements** : `main.cpp:506-511` · `borneUniverselle.cpp:2923-2925` · `wifimanagment.cpp:87-91, 309-331`
- **Problème** : L'effacement n'est pas persisté. Sur une liste vide, `operator[]` insère un élément sans SSID, ce qui déclenche une connexion bloquante de 20 s.
- **Correction** : Ne rien faire si la liste est vide, et persister l'effacement ou préciser qu'il ne l'est pas.

#### MI-111 · Statistiques de boucle faussées et diagnostic mort
- **Sévérité** : mineure · **Emplacements** : `main.cpp:40, 811, 836-842, 1105, 1129-1178, 1271-1274, 1284-1322`
- **Problème** : Les durées sont cumulées même quand les appels sont sautés, et le commentaire dit 500 ms alors que le code utilise 1000. `diagnosticLoop` est morte, et `/api/status` ne contient pas tout ce qu'il annonce.
- **Correction** : Ne mesurer que les appels réels, et supprimer le code mort.

#### MI-112 · /api/restart annonce 2 s mais redémarre après 100 ms, sans flush
- **Sévérité** : mineure · **Emplacements** : `main.cpp:892-897`
- **Problème** : `ESP.restart()` est appelé dans le handler asynchrone avant l'envoi de la réponse, et les sauvegardes différées sont perdues.
- **Correction** : Poser un drapeau traité dans `loop()`, qui fait le flush puis redémarre.

#### MI-113 · MyToolBox : gardes REALTIME différentes entre le .h et le .cpp
- **Sévérité** : mineure · **Emplacements** : `MyToolBox.h:29-76` · `MyToolBox.cpp:37-154, 164-362`
- **Problème** : Définir REALTIME provoquerait des références indéfinies (latent).
- **Correction** : Aligner les gardes, ou supprimer l'option.

### 3.10 Horloge temps réel

#### MI-114 · Drivers RTC désactivés définitivement après 3 erreurs I2C consécutives
- **Sévérité** : mineure · **Emplacements** : `Ds3231Driver.cpp:20-22, 45-59` · `Pcf85063Driver.cpp:20-22, 45-59` · `DS3231_RTC.h:89` · `DS3231_RTC.cpp:59`
- **Problème** : Aucune nouvelle tentative n'est faite, donc le message « rétabli » est inatteignable. Le service continue d'annoncer une horloge matérielle.
- **Correction** : Sonder à nouveau périodiquement, et remonter l'état réel du driver.

#### MI-115 · Jour de la semaine décalé dans les deux drivers ; masques de décodage différents
- **Sévérité** : mineure · **Emplacements** : `Ds3231Driver.cpp:81-82, 105, 117-118` · `Pcf85063Driver.cpp:83-84, 107, 119-120`
- **Problème** : La formule de Zeller est mal appliquée : un lundi donne 3 au lieu de 2 sur le DS3231 et 2 au lieu de 1 sur le PCF, et le calcul est faux pour 2000-2009. Le DS3231 ne masque ni l'heure ni les minutes.
- **Correction** : Corriger le mapping en entier signé et ajouter les masques `0x3F` et `0x7F`.

#### MI-116 · RTC réglé en heure locale mais converti en Unix comme de l'UTC
- **Sévérité** : mineure · **Emplacements** : `index.js:4621` · `DS3231_RTC.cpp:168-201, 284, 307-337` · `DS3231_RTC.h:60-64` · `borneUniverselle.cpp:5232-5257` · `PLC_Tools.cpp:56-60, 78-79, 118-119`
- **Problème** : Le décalage ISO est ignoré, si bien que le champ `unix` du journal de redémarrage est décalé de 1 à 2 h. L'exemple du commentaire est faux.
- **Correction** : Stocker l'UTC dans le RTC, ou documenter que l'heure est locale.

### 3.11 Pilotes et modules non instanciés

#### MI-117 · PCA9554 : begin() force toutes les sorties à 0 sans tenir compte de l'inversion
- **Sévérité** : mineure · **Emplacements** : `Pca9554Driver.cpp:15-16` · `Node.cpp:987-1002` · `Node.h:128`
- **Problème** : Une sortie inversée reste active jusqu'au premier changement de valeur (latent, car aucun fichier matériel n'utilise le PCA9554).
- **Correction** : Écrire l'état de repos dans le constructeur du nœud.

#### MI-118 · PCA9554BooleanOutputNode : test de nullité incohérent et résultat d'écriture ignoré
- **Sévérité** : mineure · **Emplacements** : `Node.cpp:54-60, 989-992, 1002` · `Pca9554Driver.h:30`
- **Problème** : `setNewValue` ne teste pas le driver et ignore le booléen renvoyé par `write()`, si bien que les erreurs I2C ne remontent jamais.
- **Correction** : Tester le pointeur et renvoyer le résultat de l'écriture.

#### MI-119 · DoorController, StepperMotor et SkipPattern sont compilés mais jamais utilisés
- **Sévérité** : mineure · **Emplacements** : `DoorController.h:36, 39` · `skipPattern.cpp:4` · `StepperMotor.h:59` · `platformio.ini:14-51, 53-92`
- **Problème** : Sans `build_src_filter`, ces modules sont tout de même liés. skipPattern construit au boot un vecteur de 44 chaînes, et la documentation renvoie à du code absent.
- **Correction** : Les exclure du build, ou les déplacer dans `lib/`.

#### MI-120 · StepperMotor : MAX_SERVO_SPEED (15) sert de plafond en steps/s
- **Sévérité** : mineure · **Emplacements** : `StepperMotor.h:28-30, 98` · `Steppermotor.cpp:189, 1040-1050, 1338-1341, 1498`
- **Problème** : La même constante est un index dans un cas et une vitesse dans l'autre : le moteur est bridé à 15 Hz et le `maxSpeed` envoyé par l'IHM est ignoré.
- **Correction** : Créer une constante distincte en steps/s.

#### MI-121 · StepperMotor : position en uint32 face à une API int32, jogReverse bloqué après le homing
- **Sévérité** : mineure · **Emplacements** : `StepperMotor.h:90, 210` · `ServoDrive.h:80, 92` · `Node.h:287` · `Steppermotor.cpp:719, 949-952, 975-978, 1093, 1427-1428`
- **Problème** : La comparaison avec `INT32_MIN` est toujours vraie, donc le jog arrière est refusé. Les positions négatives sont ramenées à 0, et le message de `jogForward` est faux.
- **Correction** : Comparer en `int32_t` avec un nœud signé.

#### MI-122 · StepperMotor : alarme matérielle dite « verrouillée » mais recalculée à chaque cycle
- **Sévérité** : mineure · **Emplacements** : `Steppermotor.cpp:355-363, 1458-1460, 1576-1582` · `StepperMotor.h:34, 336-344`
- **Problème** : L'alarme s'acquitte toute seule dès que le signal retombe, et un timeout ne lève l'alarme que pendant un cycle.
- **Correction** : Un drapeau verrouillé, remis à zéro uniquement par `alarmsReset`.

#### MI-123 · StepperMotor : setError lève stepperAlarm trop tôt et onAlarmCallback ne se déclenche jamais
- **Sévérité** : mineure · **Emplacements** : `StepperMotor.h:336-344` · `Steppermotor.cpp:579-583, 1390-1415, 1458-1471`
- **Problème** : Comme le nœud est déjà à true, le front montant n'est jamais vu : timeouts, fins de course et échecs de homing ne préviennent jamais la classe métier.
- **Correction** : Détecter le front sur l'état mémorisé dans la classe.

#### MI-124 · StepperMotor : timeouts tronqués ou ignorés
- **Sévérité** : mineure · **Emplacements** : `ServoDrive.h:60, 68, 102-104` · `StepperMotor.h:248` · `Steppermotor.cpp:873, 903, 1164, 1308` · `SureServo.cpp:727`
- **Problème** : Le cast `(uint16_t)` ramène 120 000 ms à 54 464 ms. Le homing ignore le timeout reçu, et `setMoveTimeout` est écrasé.
- **Correction** : Retirer le cast, et appliquer les timeouts comme le fait SureServo.

#### MI-125 · StepperMotor : pointeurs nuls déréférencés sans test
- **Sévérité** : mineure · **Emplacements** : `StepperMotor.h:126-127` · `Steppermotor.cpp:89, 101, 126, 158, 890, 949, 975, 1390, 1403, 1412, 1417, 1427, 1562`
- **Problème** : `limitError` et `homeDone`, pourtant optionnels, ainsi que `fasMotor`, qui peut rester nul, sont déréférencés sans test.
- **Correction** : Ajouter des gardes systématiques, ou rendre ces nœuds obligatoires.

#### MI-126 · StepperMotor : variables statiques de fonction partagées entre les instances
- **Sévérité** : mineure · **Emplacements** : `Steppermotor.cpp:256, 330, 768, 1210, 1275` · `StepperMotor.h:181-184, 358-361`
- **Problème** : Avec deux moteurs, les fronts `servoOn` et alarme de l'un masquent ceux de l'autre.
- **Correction** : En faire des membres de la classe.

#### MI-127 · DoorController : la doc dit « doorLocked true = fermeture », le code fait l'inverse
- **Sévérité** : mineure · **Emplacements** : `DoorController.h:12` · `DoorController.cpp:77, 166, 182, 204`
- **Problème** : Une sortie câblée d'après la doc serait inversée (latent).
- **Correction** : Corriger la doc ou renommer la sortie.

#### MI-128 · DoorController : l'état EMERGENCY est inatteignable
- **Sévérité** : mineure · **Emplacements** : `DoorController.h:25, 41-44` · `DoorController.cpp:22-23, 33-56, 232-242, 288-292`
- **Problème** : Seul le constructeur y entre, et le premier front y est ignoré.
- **Correction** : Passer en EMERGENCY au début de `update()`.

#### MI-129 · DoorController : capteur documenté « verrouillée » mais utilisé comme « fermée »
- **Sévérité** : mineure · **Emplacements** : `DoorController.h:10, 87, 92-94` · `DoorController.cpp:113-115, 196-199, 235-236`
- **Problème** : L'état UNLOCKED_CLOSED est fugace, ce qui rend le modèle d'états imprévisible.
- **Correction** : Choisir une seule sémantique et renommer les accesseurs.

### 3.12 Dépôt, build et documentation

#### MI-130 · Restes du projet Formaca dans les messages et les valeurs par défaut
- **Sévérité** : mineure · **Emplacements** : `RessortRoyal2.h:28` · `RessortRoyal2.cpp:2098` · `borneUniverselle.cpp:2876-2883, 4419-4429` · `wifimanagment.h:202`
- **Problème** : Les messages parlent de « Formaca.json » à la lecture de RessortRoyal2.json. `validateNodeType` inverse le type attendu et le type réel, et cite Formaca.h. L'AP par défaut s'appelle « ESP32-Formaca ».
- **Correction** : Utiliser `CONFIG_FILE_NAME`, réordonner les arguments et choisir des valeurs par défaut propres au projet.

#### MI-131 · Vestiges Formaca à la racine : README, formaca.json, recette.txt
- **Sévérité** : mineure · **Emplacements** : `README.md:6` · `formaca.json:1548-1618` · `RecetteGenerator.js:77-86` · `recette.txt:145-147` · `testFormaca.cpp:80-92` · `RenderTools.cpp:101-105`
- **Problème** : Le README pointe vers src/Formaca2, qui n'existe pas. formaca.json est une sortie console brute (JSON invalide), et recette.txt est tronqué et contient un fragment de conversation IA.
- **Correction** : Mettre le README à jour et archiver ces fichiers.

#### MI-132 · Archives obsolètes : borneUniverselle.cpp.ws_backup et files.zip
- **Sévérité** : mineure · **Emplacements** : `borneUniverselle.cpp.ws_backup:370-376, 1315-1330, 3168-3169, 3482, 3685` · `borneUniverselle.h:323, 370-373` · `files.zip` (RessortRoyal2.h:37-38, 46, 83-103, 193-198, 280, 306, 316) · `RessortRoyal2.h:192-197` · `BusinessLogic.h:295`
- **Problème** : Le backup force `isAllConditionsOk()` à true et ne compile plus. files.zip contient 13 hash absents de config.json et des overrides disparus : restaurer l'un ou l'autre induirait en erreur.
- **Correction** : Les retirer, l'historique git suffit.

#### MI-133 · .gitignore inopérant ; artefacts et journaux de test périmés versionnés
- **Sévérité** : mineure · **Emplacements** : `.gitignore:6-7` · `platformio.ini:37, 79` · `logs/` · `test_output.txt:1, 207, 211, 230, 654, 675` · `test_results.log:672, 896, 921, 11776` · `platformio.ini.bak-2026-08-21` · `BorneUniverselle/desktop.ini` · `Node/desktop.ini` · `temp/`
- **Problème** : `;logs/*` est un motif littéral et non un commentaire. Les logs, test_output.txt (2,9 Mo), test_results.log, le .bak et les desktop.ini sont donc versionnés. Les journaux de test proviennent du projet Formaca et ne prouvent rien sur le code actuel.
- **Correction** : Utiliser `logs/*` avec `!logs/.gitkeep`, ignorer les artefacts et les retirer de l'index.

#### MI-134 · Versions de plateforme et de bibliothèques divergentes entre les envs
- **Sévérité** : mineure · **Emplacements** : `platformio.ini:16, 42-47, 54, 83-88, 95, 104-111, 166-168` · `platformio.ini.bak-2026-08-21:16-17` · `test_results.log:9-13`
- **Problème** : Seul quatre_mb_huge a migré vers pioarduino, avec une URL « stable » non figée. Les autres envs restent sur `espressif32` non versionné (résolu différemment selon la machine), avec AsyncTCP 3.3.8 et des carets.
- **Correction** : Une section `[env]` commune, figée sur les versions de production.

### 3.13 Tests

#### MI-135 · Drapeaux de test sans effet
- **Sévérité** : mineure · **Emplacements** : `platformio.ini:124, 127` · `test_transition_complette.cpp:495` · `BusinessLogic.h:306` · `BusinessLogic.cpp:242`
- **Problème** : `DEBUG_STATE_HISTORY` et `CONFIG_LITTLEFS_FOR_IDF_3_2` ne sont lus nulle part, et le test recommande un `HISTORY_ENABLED` qui n'existe pas. L'historique est toujours compilé.
- **Correction** : Supprimer ces define, ou implémenter un vrai `#ifdef`.

#### MI-136 · transitionDecorator.h jamais inclus, et il désactive le mauvais drapeau
- **Sévérité** : mineure · **Emplacements** : `transitionDecorator.h:9, 206-214` · `BusinessLogic.cpp:248, 265, 423, 433` · `borneUniverselle.h:433`
- **Problème** : Il ne coupe que les heartbeats, alors que les logs de transition dépendent de `isMachineStateLogging()`. Sa restauration force true au lieu de rétablir la valeur précédente.
- **Correction** : Supprimer le fichier, ou basculer `machineStateLogging` en sauvegardant son état.

#### MI-137 · c_str() d'un std::string temporaire dans le test initNodePtr
- **Sévérité** : mineure · **Emplacements** : `test_transitionInitNodePtr.cpp:74, 79-80, 103-104` · `BusinessLogic.h:220` · `BusinessLogic.cpp:77`
- **Problème** : Le pointeur est utilisé après la destruction du temporaire (comportement indéfini), et la variable de la ligne 79 en masque une autre.
- **Correction** : Stocker le résultat dans un `std::string` local.

#### MI-138 · Les environnements ne s'accordent pas sur la PSRAM
- **Sévérité** : mineure · **Emplacements** : `platformio.ini:60-63, 117-134` · `PLC_Tools.cpp:441` · `test_results.log:60, 157, 938` · `logs/device-monitor-260928-153158.log:42-44`
- **Problème** : esp32dev déclare la PSRAM, test_esp32 non, et la carte déployée rapporte PSRAM=0. Les derniers tests tournaient sur un ESP32-D0WDR2, doté de PSRAM, sans l'activer : les mesures de tas ne représentent aucune cible définie.
- **Correction** : Décider si la cible a de la PSRAM, et aligner tous les envs sur ce choix.

#### MI-139 · Mocks obsolètes et inutilisables (temp/ et test/)
- **Sévérité** : mineure · **Emplacements** : `temp/mock_BorneUniverselle.h:15, 34, 42, 52` · `temp/mock_BorneUniverselle.cpp:1, 34` · `temp/mock_RenderTools.h:13, 15, 31` · `temp/mock_RenderTools.cpp:2, 27, 40` · `temp/mock_PLC_Persistance.h:1, 9, 23-24, 29-30` · `temp/mock_PLC_Persistance.cpp:2` · `temp/mock_ServoDrive.h:24-54` · `temp/mock_SureServo.h:4, 13, 31` · `temp/mock_SureServo.cpp:1` · `temp/mock_MyModbus.h` · `test/test_transition/mock_MyModbus.h:2, 64` · `test/mocks/mockBorneUniverselle.cpp:4-75`
- **Problème** : Les includes pointent vers des fichiers inexistants, un mock est vide et un autre est entièrement commenté. Les signatures ont divergé (static, types de retour, `typedef SafeJsonDocument`, ancienne API ServoDrive). Aucun de ces mocks n'est inclus ni compilé.
- **Correction** : Les supprimer, ou les régénérer depuis les headers actuels.

## 4. Constats cosmétiques (44)

### 4.1 Code mort et déclarations inutilisées

#### CO-1 · findNodeSecure, validateNodeType et validatePointerType : API inutilisable et inutilisée
- **Sévérité** : cosmétique · **Emplacements** : `borneUniverselle.h:295-296` · `borneUniverselle.cpp:4410-4440, 4510-4607` · `BusinessLogic.inl:208-240`
- **Problème** : Le template n'est défini que dans le .cpp. Aucune de ces fonctions n'est appelée, et `initNodePtr` fait déjà ce contrôle.
- **Correction** : Les supprimer.

#### CO-2 · tabChangeCallback jamais enregistré ni invoqué
- **Sévérité** : cosmétique · **Emplacements** : `borneUniverselle.h:335, 409` · `borneUniverselle.cpp:32, 1876-1885, 2134-2136`
- **Problème** : `onTabChange` passe uniquement par BusinessLogic.
- **Correction** : Supprimer le callback.

#### CO-3 · Membres et constantes de BorneUniverselle jamais utilisés
- **Sévérité** : cosmétique · **Emplacements** : `borneUniverselle.h:209-220, 355-357, 393-394, 464, 513, 534, 542, 544-549` · `borneUniverselle.cpp:227, 538, 1318, 2067, 4618`
- **Problème** : `HARDWARE_MODEL`, `inputsCache`, `MIN_TIME_BETWEEN_CLIENTS` (avec un `< 1000` en dur), `JSON_CONFIG_SIZE`… ne servent pas. `cleanupDelay` fait doublon avec `CLIENT_CLEANUP_DELAY`, et `clientConnectedAt` est écrit, jamais initialisé et jamais lu.
- **Correction** : Supprimer ces éléments, ou utiliser les constantes.

#### CO-4 · transitionWithTimeout : le retour et la doc ne correspondent pas au comportement
- **Sévérité** : cosmétique · **Emplacements** : `BusinessLogic.h:362-365` · `BusinessLogic.cpp:22-30, 175-205` · `BusinessLogic/readme.md:95`
- **Problème** : Les deux branches font la même transition, le log est écrit deux fois, et la comparaison utilise `>` d'un côté et `>=` de l'autre. Aucun appelant.
- **Correction** : Supprimer la fonction, ou la redéfinir.

#### CO-5 · MyModbus : mécanismes documentés mais non branchés
- **Sévérité** : cosmétique · **Emplacements** : `MyModbus.h:16-18, 64-65, 115, 209, 261` · `MyModbus.cpp:12, 754-767` · `borneUniverselle.cpp:372-389`
- **Problème** : `allActiveSlavesDead` et `executeWithRetry` ne sont jamais appelés. C'est BorneUniverselle qui déclare le bus mort.
- **Correction** : Supprimer ces mécanismes, ou les brancher.

#### CO-6 · Constantes mortes ou contredites dans SureServo
- **Sévérité** : cosmétique · **Emplacements** : `SureServo.h:14, 39, 43, 50-53, 683` · `SureServo.cpp:2489`
- **Problème** : `DRIVE_RESTART_THRESHOLD` vaut 3 mais la détection se fait sur un seul échantillon. `DRIVE_INIT_TIMEOUT` fait doublon, et les constantes `PRx_START` ne servent pas.
- **Correction** : Les supprimer, ou implémenter le seuil.

#### CO-7 · Mode EEPROM : MANUAL_SAVE_ONLY est un alias, et une seconde voie de sauvegarde ne restaure rien
- **Sévérité** : cosmétique · **Emplacements** : `SureServo.h:80-85` · `SureServo.cpp:394-398, 2393-2399, 2415-2436` · `RessortRoyal2.cpp:1474-1476`
- **Problème** : Les deux modes écrivent 5. `saveDriveParameters`, jamais appelée, laisserait P2-30 à 8.
- **Correction** : Supprimer l'alias, et passer par `sureServo->saveParameters()`.

#### CO-8 · Deux routines de nettoyage de diagnostic.txt aux seuils contradictoires
- **Sévérité** : cosmétique · **Emplacements** : `PLC_Tools.h:28-30` · `PLC_Tools.cpp:412-431, 1312-1313, 1360-1408`
- **Problème** : Une routine (16 Ko / 3,5 Ko) est morte, l'autre utilise 20 Ko / 15 Ko, et `DIAGNOSTIC_SAFETY_MARGIN` ne sert pas.
- **Correction** : Supprimer la routine et la constante mortes.

#### CO-9 · Node/toDoList décrit un setDescriptor qui n'existe plus
- **Sévérité** : cosmétique · **Emplacements** : `Node/toDoList:1-2` · `Node.h:114-120` · `borneUniverselle.cpp:4261-4275`
- **Problème** : `registerDescriptorCallback` n'est jamais appelé, si bien que la branche `addCustomDescriptor` est morte.
- **Correction** : Mettre la note à jour et supprimer le hook.

#### CO-10 · Serveur web démarré deux fois
- **Sévérité** : cosmétique · **Emplacements** : `main.cpp:48-50, 143-148, 914-915`
- **Problème** : `setupServer()` ne pose pas `serverStarted`, donc le second `begin()` (sans effet) et un log en double ont lieu.
- **Correction** : Poser le drapeau dans `setupServer()`.

#### CO-11 · uploadfs_prep.py jamais référencé
- **Sévérité** : cosmétique · **Emplacements** : `uploadfs_prep.py:1-7` · `platformio.ini`
- **Problème** : Aucun `extra_scripts` ne le charge.
- **Correction** : Le brancher ou le supprimer.

#### CO-12 · Helper de test : la variable globale `bu` vaut toujours nullptr
- **Sévérité** : cosmétique · **Emplacements** : `test_transition_complette.cpp:38, 56-64, 76, 161-162` · `borneUniverselle.cpp:156-158, 166` · `borneUniverselle.h:254-258`
- **Problème** : Elle est affectée juste après `resetForTests()`, et le test `if (!bu)` qui suit est inatteignable.
- **Correction** : Supprimer la variable globale.

#### CO-13 · RessortRoyal2 : nullité de sureServo testée après usage, et sentinelle « Pas d'erreur » jamais produite
- **Sévérité** : cosmétique · **Emplacements** : `RessortRoyal2.cpp:399-408, 412` · `SureServo.cpp:134` · `SureServo.h:757`
- **Correction** : Supprimer ces deux tests.

### 4.2 Commentaires, logs et documentation

#### CO-14 · Logs et commentaires Modbus erronés (copier-coller)
- **Sévérité** : cosmétique · **Emplacements** : `borneUniverselle.cpp:2387, 2395, 3360, 3530` · `Node.h:393-394, 519-520` · `Node.cpp:790-805, 835, 1180, 1238, 1251`
- **Problème** : La création de WriteCoil logue « ReadCoilNode », et des registres uint16/uint32 sont affichés « true »/« false ». Les commentaires indiquent FC01 au lieu de FC02 et FC06 au lieu de FC16. Seul ReadCoil teste `transId`.
- **Correction** : Corriger les libellés, et factoriser la séquence de transaction dans `ModbusNode`.

#### CO-15 · Constantes heartbeat : commentaire « 15 s » pour 10 s, et « /6 » codé en dur
- **Sévérité** : cosmétique · **Emplacements** : `borneUniverselle.h:53-54` · `borneUniverselle.cpp:544, 698, 956, 4685`
- **Problème** : La détection prend environ 13 s côté PLC contre 3 s côté navigateur, sans que ce soit documenté.
- **Correction** : Corriger le commentaire et utiliser `MAX_HEARTBEAT_FAILURES`.

#### CO-16 · Commentaires contradictoires de BusinessLogic et cycle d'inclusion prétendument évité
- **Sévérité** : cosmétique · **Emplacements** : `BusinessLogic.h:9-11, 239-243, 494-495` · `BusinessLogic.cpp:226-227, 604-612, 677` · `PLC_Persistence.h:9, 42-43, 109` · `PLC_Persistence.cpp:1, 4-5` · `PLC_Tools.h:14-16` · `borneUniverselle.h:25, 30, 45-46` · `AtomicFileWriter.h:5`
- **Problème** : Le commentaire annonce « 20 s » pour une valeur de 5000 ms, et décrit `isIdle` comme « en IDLE » alors qu'elle couvre quatre états. Le cycle BusinessLogic → PLC_Persistence → PLC_Tools → borneUniverselle → BusinessLogic existe bel et bien, malgré les commentaires affirmant le contraire.
- **Correction** : Corriger les commentaires, et casser le cycle en déplaçant les includes vers les .cpp.

#### CO-17 · setJogSpeed : commentaire inverse du code
- **Sévérité** : cosmétique · **Emplacements** : `RessortRoyal2.cpp:1862-1866`
- **Problème** : Le commentaire dit « permettre 0 pour arrêter », alors que le code remplace 0 par 100.
- **Correction** : Corriger le commentaire.

#### CO-18 · Re-synchronisation : 5 lectures annoncées, 3 dans le code
- **Sévérité** : cosmétique · **Emplacements** : `SureServo.cpp:1203, 2656, 2674-2675` · `SureServo.h:838-840`
- **Correction** : Utiliser une constante nommée partout.

#### CO-19 · handleInitializing cherche « timeout » en minuscules
- **Sévérité** : cosmétique · **Emplacements** : `SureServo.cpp:1336, 2901-2904`
- **Problème** : L'erreur posée est « Timeout » : la branche est morte, et seul le libellé change.
- **Correction** : Tester `getInitializeState() == DRIVE_TIMEOUT`.

#### CO-20 · handleHoming : le paramètre homingSpeed est ignoré et renommé
- **Sévérité** : cosmétique · **Emplacements** : `ServoDrive.h:102` · `SureServo.h:517` · `SureServo.cpp:2929-2931`
- **Problème** : La vitesse est un paramètre du drive (P5-05 et P5-06). Le paramètre inutilisé déclenche un avertissement avec `-Wextra`.
- **Correction** : Le documenter et le marquer comme inutilisé.

#### CO-21 · startHoming : la valeur 120 commentée « Reset » n'est jamais écrite
- **Sévérité** : cosmétique · **Emplacements** : `SureServo.cpp:737-738, 3462` · `Node.cpp:520-526`
- **Problème** : Seul le 0 qui suit part effectivement : c'est une astuce pour forcer la réécriture.
- **Correction** : `setValue(0, true)`.

#### CO-22 · Documentation SureServo obsolète (nextPR, Formaca2/Woodzco, ToDo list)
- **Sévérité** : cosmétique · **Emplacements** : `SureServo.h:416-419, 468-482, 876-883` · `RessortRoyal2.cpp:401-403` · `SureServo/ToDo list:1` · `SureServo.cpp:2678-2692`
- **Problème** : `nextPR` est en réalité `chainToNext`. Les consommateurs cités n'existent pas, et `driveFaultPending` n'est jamais acquitté. La ToDo décrit une tâche déjà réalisée.
- **Correction** : Mettre la documentation à jour.

#### CO-23 · Messages périodiques : doc « 15 s » pour 10 s, et seuil de purge contraire à la limite
- **Sévérité** : cosmétique · **Emplacements** : `PLC_Tools.h:238, 348-349, 382` · `PLC_Tools.cpp:1347-1353, 1649-1650` · `RessortRoyal2.cpp:30-32` · `MemoryMonitor.cpp:198-226` · `MemoryMonitor.h:53-67`
- **Problème** : Le seuil de purge (15) est inférieur à la limite (20), et le log « vidée » est faux. Ce chemin n'est de toute façon jamais exécuté.
- **Correction** : Harmoniser la doc, les seuils et le log.

#### CO-24 · Logs mémoire de PLC_Tools contraires au calcul
- **Sévérité** : cosmétique · **Emplacements** : `PLC_Tools.cpp:511-518, 548-552, 1996-2001`
- **Problème** : Le log affiche une marge fixe de 20 000 alors qu'elle est variable, et « malloc(32) » pour un `malloc(320)`.
- **Correction** : Afficher les valeurs réelles.

#### CO-25 · Spécifications CLAUDE_TASK LittleFS obsolètes par rapport au code
- **Sévérité** : cosmétique · **Emplacements** : `CLAUDE_TASK_LittleFS_Etape1_RecoveryBoot.md:4, 6-11, 30, 37, 138, 146, 165` · `CLAUDE_TASK_LittleFS_Etape2_CommitRenameUnique.md:7, 26, 35, 110` · `PLC_Persistence.cpp:1331-1340, 1348, 1400` · `AtomicFileWriter.cpp:83-110`
- **Problème** : Les deux étapes ont été appliquées, mais le commentaire de récupération décrit encore l'ancien commit, et le Test B ne vise plus que le repli. L'analyse citée en référence n'existe pas dans le dépôt.
- **Correction** : Marquer ces spécifications comme appliquées et corriger le commentaire.

#### CO-26 · MemoryMonitor : métriques et commentaires trompeurs
- **Sévérité** : cosmétique · **Emplacements** : `MemoryMonitor.h:27, 33` · `MemoryMonitor.cpp:7, 73-74, 81, 119, 155, 172-178` · `PLC_Tools.cpp:828, 1991` · `main.cpp:822`
- **Problème** : `getMaxAllocHeap()` est déclaré « à éviter » mais reste appelé, et `lowestMaxBloc` est figé. La « moyenne » divise un cumul par un seul intervalle, et la « fragmentation » mesure en fait la baisse historique du tas.
- **Correction** : Corriger les calculs et les commentaires.

#### CO-27 · Message « Backup conservé pour investigation » contredit par le boot
- **Sévérité** : cosmétique · **Emplacements** : `AtomicFileWriter.cpp:181-184` · `PLC_Persistence.cpp:1372-1376, 1419-1429`
- **Problème** : Au boot suivant, le `.backup` est restauré ou supprimé.
- **Correction** : Reformuler le message.

#### CO-28 · Bornes de validation RTC contradictoires
- **Sévérité** : cosmétique · **Emplacements** : `DS3231_RTC.cpp:76, 126, 302-304, 325`
- **Problème** : Le commentaire dit 2020 alors que le code utilise 2026, et le parseur accepte l'an 2100, que `setTime` refuse.
- **Correction** : Aligner le commentaire et les bornes.

#### CO-29 · « WiFi ERROR: Succès - Connexion réussie »
- **Sévérité** : cosmétique · **Emplacements** : `wifimanagment.h:347-351` · `wifimanagment.cpp:433, 1005` · `logs/device-monitor-260928-153158.log:559`
- **Correction** : Ne journaliser en ERROR que si le code diffère de `WIFI_SUCCESS`.

### 4.3 IHM et front

#### CO-30 · Le filtre de log heartbeat du front ne correspond jamais
- **Sévérité** : cosmétique · **Emplacements** : `index.js:4599` · `borneUniverselle.cpp:68-70`
- **Problème** : La chaîne comparée contient des espaces alors que le JSON est compact, et `"true"` est envoyé comme chaîne.
- **Correction** : Tester la clé parsée, et envoyer un booléen.

#### CO-31 · Section « Inputs » de config.json typée tx-uint32 alors que le web y écrit
- **Sévérité** : cosmétique · **Emplacements** : `data/config.json:272-319` · `borneUniverselle.cpp:2288-2291, 3939-3951` · `Node.h:671-691`
- **Correction** : Séparer les consignes saisies et les valeurs publiées, et renommer la section.

#### CO-32 · Front : catégories de debug et type de notification non définis
- **Sévérité** : cosmétique · **Emplacements** : `index.js:4599, 4621`
- **Problème** : Sept catégories (`PASSWORD_MANAGER`, `WATCHDOG`, `HANDSHAKE`…) sont absentes de `ho`, et `type: "warn"` retombe sur INFO.
- **Correction** : Utiliser les clés existantes et « warning ».

#### CO-33 · Deux éléments `<hw-notification>`, dont un hors du `<body>`
- **Sévérité** : cosmétique · **Emplacements** : `data/index.html:25` · `index.js:4599, 4621`
- **Correction** : Garder l'élément d'index.html et lui rattacher le socket.

#### CO-34 · Tooltip de l'horloge « updated only when IDLE »
- **Sévérité** : cosmétique · **Emplacements** : `data/interface.json:84` · `RessortRoyal2.cpp:1779-1781, 2080-2085` · `ClockDisplay.h:9-10`
- **Problème** : L'horloge est mise à jour en IDLE, en STOP et en EMERGENCY.
- **Correction** : Corriger le tooltip.

### 4.4 Nommage, versions et configuration

#### CO-35 · `#ifdef DEBUG` toujours vrai
- **Sévérité** : cosmétique · **Emplacements** : `borneUniverselle.cpp:1108, 2108-2110` · `PLC_CommonTypes.h:9`
- **Problème** : `DEBUG` est aussi la constante de sévérité 5.
- **Correction** : Utiliser un drapeau dédié.

#### CO-36 · Numéros de version contradictoires
- **Sévérité** : cosmétique · **Emplacements** : `main.cpp:21, 309, 882` · `RessortRoyal2.cpp:16` · `data/index.html:32` · `borneUniverselle.h:32`
- **Problème** : On trouve « v1.0 » contre « VERSION 2.0 », et l'IHM affiche « 1.3.17 » en dur.
- **Correction** : Une seule source de version, affichée par l'IHM.

#### CO-37 · PLC_InterfaceMenu : paramètre mal nommé, arguments de printf inversés, champ inutilisé
- **Sévérité** : cosmétique · **Emplacements** : `PLC_InterfaceMenu.h:22, 29-30` · `PLC_InterfaceMenu.cpp:11, 54, 83` · `borneUniverselle.cpp:1704`
- **Correction** : Renommer le paramètre en `filePath`, réordonner les arguments et supprimer le code mort.

#### CO-38 · MyToolBox::stringToURL : « &f6 » au lieu de « %f6 », et hypothèse ISO-8859-1
- **Sévérité** : cosmétique · **Emplacements** : `MyToolBox.cpp:12, 27, 30, 35`
- **Correction** : Encoder en pourcentage tout octet non ASCII.

#### CO-39 · Nommage incohérent des modules et des fichiers
- **Sévérité** : cosmétique · **Emplacements** : `wifimanagment.h:1-2, 75, 78-80, 258, 270` · `wifimanagment.cpp:1-2` · `PLC_Persistence.h:1` · `RessortRoyal2.h:337` · `temp/mock_PLC_Persistance.h` · `Steppermotor.cpp:2, 13` · `test/test_transitionInitNodedePtr/` · `test_transition_complette.cpp:1, 220-221, 292-295` · `test_transitionInitNodePtr.cpp:1` · `transitionDecorator.h:1` · `test/mocks/mockBorneUniverselle.cpp:1` · `temp/mock_BorneUniverselle.h:1` · `temp/mock_SureServo.cpp:1`
- **Problème** : Les variantes coexistent : WifiManagement, wifimanagment et WifiManagment, Persistance et Persistence, Steppermotor, « Nodede », « complette ». Des en-têtes de fichiers de test portent un autre nom, et certains commentaires de transitions sont inversés.
- **Correction** : Normaliser en une seule passe de renommage.

#### CO-40 · RTC_Service nommé et journalisé comme s'il ne gérait que le DS3231
- **Sévérité** : cosmétique · **Emplacements** : `DS3231_RTC.h:1, 17-18, 47, 129` · `DS3231_RTC.cpp:1-2, 121, 149` · `borneUniverselle.cpp:3608` · `Ds3231Driver.h:10`
- **Correction** : Renommer en RTC_Service.*, utiliser `chipName()` et factoriser une base I2C commune.

#### CO-41 · StepperMotor : nom de fichier, commentaires et déclarations incohérents
- **Sévérité** : cosmétique · **Emplacements** : `Steppermotor.cpp:2, 25-65, 181` · `StepperMotor.h:129-131, 159-172, 192-193, 295`
- **Problème** : Le `@file` est faux, les numéros de ligne cités sont périmés et la bannière est en double. Il manque des `override`, et `maxRangeSteps` n'est jamais appliqué.
- **Correction** : Nettoyer.

#### CO-42 · Débit RS485 « 115000 » non standard
- **Sévérité** : cosmétique · **Emplacements** : `data/hardware/Kincony_KC868_A8S.json:16` · `borneUniverselle.cpp:3653, 3680` · `logs/device-monitor-260928-153158.log:125` · `logs/device-monitor-260929-093253.log:142`
- **Correction** : Remettre 115200.

#### CO-43 · .vscode/extensions.json recommande et rejette la même extension
- **Sévérité** : cosmétique · **Emplacements** : `.vscode/extensions.json:5, 6, 10`
- **Correction** : Ne garder que pioarduino-ide.

#### CO-44 · Broche d'interruption PCF8574 (GPIO 14) codée en dur, avec un commentaire « Désactivé » faux
- **Sévérité** : cosmétique · **Emplacements** : `Node.cpp:743` · `data/hardware/Kincony_KC868_A8S.json:3-10` · `borneUniverselle.cpp:3779-3782`
- **Correction** : Ajouter une clé `int_pin` au descripteur matériel et corriger le commentaire.

## Recommandations prioritaires

1. **Sécuriser les mouvements (CR-1 à CR-3, MA-1, MA-2).** Contrôler la butée dans `configurePR`, surveiller le jog dans `SureServo::process()`, faire tourner la supervision sans dépendre du client web, et arrêter tout mouvement à la déconnexion.
2. **Durcir les entrées WebSocket et HTTP (MA-9, MA-10, MA-12, MA-18 à MA-20, MA-24).** Borner les chemins, rendre la validation sans effet de bord, valider interface.json, corriger les deux use-after-free, unifier la liste des fichiers protégés et sortir les secrets des fichiers servis.
3. **Unifier la suspension et la persistance différée (MA-16, MA-21 à MA-23, MI-36, MI-63).** Un seul mécanisme soumis à `isIdle()`, des flushs faits depuis la boucle uniquement, et des mutex sur l'état partagé.
4. **Rendre le build reproductible (MA-29 à MA-31, MI-134).** Corriger la casse des includes et les noms des CSV, fixer `default_envs = quatre_mb_huge`, figer les versions, et passer en découpage 16 Mo pour soulager LittleFS.
5. **Corriger la configuration machine (MA-3, MA-5, MA-8, MA-13, MA-14).** Une source de vérité unique, la persistance de « full torque », la détection des changements matériels, une validation réelle de RessortRoyal2.json et une cycle speed exprimée en index.
6. **Reconstruire les tests (MA-32 à MA-39).** Cibler RessortRoyal2 et BusinessLogic avec Unity et un seul mécanisme de compilation, puis supprimer les mocks et les journaux périmés (MI-133, MI-139).
7. **Aligner le protocole front/back (MA-15, MA-17, MI-69 à MI-83).** Corriger le type FATAL, la sémantique de `plc_suspended` et la politique à client unique.
8. **Nettoyer la documentation et le code mort (MA-28, MI-26, MI-130 à MI-132, section 4).** Mettre à jour les readmes de BusinessLogic et SureServo, et retirer les vestiges Formaca.
# BancAubo

Banc de test AUBO : départ / arrêt du programme robot par un sélecteur maintenu (E/S câblées), états du robot et du programme affichés (E/S + Modbus TCP).

Mode d'exécution : avec écran (HMI) · nom réseau : bancaubo · 23 nœuds

## Équipements

| Équipement | Type | Adresse Modbus | Options |
|---|---|---|---|
| Automate | Waveshare ESP32-S3-POE-ETH-8DI-8DO | — | rtc=true, rs485Speed=, rs485Config= |
| Robot | Robot AUBO (contrôleur ARCS, Modbus TCP) | 1 | transport=tcp, ip=192.168.10.50, port=502 |

## Entrées / sorties

| Symbole | Libellé | Sens | Voie | Remarque |
|---|---|---|---|---|
| Sel_start | Sélecteur START | entrée | Automate · Entrées TOR · DI1 | Contact NO, sélecteur maintenu |
| Sel_stop | Sélecteur STOP | entrée | Automate · Entrées TOR · DI2 | Contact NO, sélecteur maintenu |
| Rob_marche | Programme robot en marche | entrée | Automate · Entrées TOR · DI3 | Sortie robot RunningHigh |
| Rob_pause | Robot en pause | entrée | Automate · Entrées TOR · DI4 | Sortie robot PausedHigh |
| Rob_sans_defaut | Robot sans défaut | entrée | Automate · Entrées TOR · DI5 | Sortie robot NotSystemError |
| Rob_operable | Robot opérationnel | entrée | Automate · Entrées TOR · DI6 | Sortie robot RobotOperable |
| Cmd_start | Départ programme robot | sortie | Automate · Sorties transistor · DO1 | repli à 0, Entrée robot StartProgram |
| Cmd_stop | Arrêt programme robot | sortie | Automate · Sorties transistor · DO2 | repli à 0, Entrée robot StopProgram |
| Mb_pret | Prog. maître prêt | entrée | Robot · Retours du robot (310-319) · PRET |  |
| Mb_en_cours | Tâche en cours | entrée | Robot · Retours du robot (310-319) · EN_COURS |  |
| Mb_fini | Tâche finie | entrée | Robot · Retours du robot (310-319) · FINI |  |
| Mb_defaut | Code défaut programme | entrée | Robot · Retours du robot (310-319) · DEFAUT |  |
| Mb_echo_prog | N° tâche en cours | entrée | Robot · Retours du robot (310-319) · ECHO_PROG |  |
| Mb_vie | Vie robot | entrée | Robot · Retours du robot (310-319) · VIE_ROBOT |  |
| Pos_X | TCP X (0,1 mm) | entrée | Robot · Positions (320-331, 0,1 mm / 0,1°, signées) · TCP_X |  |
| Pos_Y | TCP Y (0,1 mm) | entrée | Robot · Positions (320-331, 0,1 mm / 0,1°, signées) · TCP_Y |  |
| Pos_Z | TCP Z (0,1 mm) | entrée | Robot · Positions (320-331, 0,1 mm / 0,1°, signées) · TCP_Z |  |

## Variables internes

| Symbole | Libellé | Nature | Type | Valeur initiale / bornes | Remarque |
|---|---|---|---|---|---|
| Tempo_reponse | Délai réponse robot (ms) | Paramètre | int | 5000 · min 500 · max 30000 |  |
| Etat_programme | État programme | Indicateur | text |  |  |
| Defaut_cmd | Robot ne répond pas | Indicateur | bool |  |  |

## Grafcet — étapes

| Étape | Libellé | Actions |
|---|---|---|
| ((0)) | Programme arrêté | Etat_programme := "Arrêté" |
| 1 | Départ programme | N Cmd_start ; Etat_programme := "Démarrage" |
| 2 | Programme en cours | Etat_programme := "En cours" |
| 3 | Arrêt programme | N Cmd_stop ; Etat_programme := "Arrêt demandé" |
| 4 | Attente sélecteur | Etat_programme := "Terminé" ; Message : Programme robot terminé |
| 5 | Robot muet | N Defaut_cmd ; Etat_programme := "Défaut" ; Message : Robot ne répond pas |

## Grafcet — transitions

| Transition | De | Vers | Réceptivité |
|---|---|---|---|
| T0 | 0 | 1 | Sel_start ET NON Sel_stop ET NON Rob_marche ET Rob_operable ET Rob_sans_defaut |
| T1 | 0 | 2 | Rob_marche |
| T2 | 1 | 2 | Rob_marche |
| T3 | 1 | 5 | NON Rob_marche ET X1.t >= Tempo_reponse |
| T4 | 2 | 3 | Sel_stop |
| T5 | 2 | 4 | NON Rob_marche ET NON Sel_stop |
| T6 | 3 | 4 | NON Rob_marche |
| T7 | 3 | 5 | Rob_marche ET X3.t >= Tempo_reponse |
| T8 | 4 | 0 | NON Sel_start |
| T9 | 5 | 0 | NON Sel_start |

## Modes et sécurités

| Bloc | Réglage |
|---|---|
| Condition d'arrêt d'urgence | — aucune — |
| Acquittement | — aucun (sortie automatique après 1 s) — |
| Mise en marche | automatique |
| Condition d'arrêt | — aucune — |

## Écrans (disposition personnalisée)

- **Banc**
  - Commande : État machine, État programme, Sélecteur START, Sélecteur STOP, Départ programme robot, Arrêt programme robot, Robot ne répond pas
  - Robot (E/S) : Programme robot en marche, Robot en pause, Robot sans défaut, Robot opérationnel
  - Robot (Modbus) : Prog. maître prêt, Tâche en cours, Tâche finie, Code défaut programme, N° tâche en cours, Vie robot
  - Position outil : TCP X (0,1 mm), TCP Y (0,1 mm), TCP Z (0,1 mm)
- **Réglages**
  - Surveillance : Délai réponse robot (ms)

## Hypothèses (à confirmer)
- Automate Waveshare ESP32-S3-POE-ETH-8DI-8DO, Ethernet fixe 192.168.10.20 ; robot AUBO (ARCS) en 192.168.10.50, port 502, unité 1.
- Sélecteur : DI1 = START, DI2 = STOP (contacts NO). Le départ se fait au passage sur START : à la fin du programme, pas de relance tant que le sélecteur n'a pas quitté START.
- Départ / arrêt par impulsions câblées sur les entrées robot `StartProgram` (DO1) et `StopProgram` (DO2), maintenues jusqu'à la réponse de `RunningHigh` (DI3).
- Départ seulement si `RobotOperable` (DI6) et `NotSystemError` (DI5) sont à 1. `PausedHigh` (DI4) est affiché seulement.
- Pas d'arrêt d'urgence logiciel : l'AU est câblé sur le banc (avertissements de vérification attendus).
- Le robot ne répond pas au départ ou à l'arrêt dans le délai réglable (5 s par défaut) : défaut affiché, qui s'efface quand le sélecteur quitte START.
- Un programme lancé depuis le pendant est suivi, et le sélecteur sur STOP l'arrête.
- Registres Modbus (PRET, EN_COURS, FINI, DEFAUT, ECHO_PROG, VIE_ROBOT, TCP X/Y/Z) : affichage seulement. Ils ne sont alimentés que si le programme robot suit la convention de docs/robots/aubo.md.

## Scénarios validés (5/5)
1. START : départ, programme en cours, fin sans relance automatique.
2. STOP : arrêt du programme en cours.
3. Pas de départ si le robot n'est pas opérationnel ou en défaut.
4. Le robot ne démarre pas : défaut puis reprise.
5. Programme lancé depuis le robot : suivi, puis arrêt par STOP.

## À vérifier sur la machine
- Les E/S AUBO sont de type NPN : vérifier la compatibilité avec les entrées et les sorties transistor de la Waveshare.
- Configurer les fonctions E/S dans ARCS (Configuration > Général > Réglages E/S), puis activer le serveur Modbus TCP.
- Tester d'abord la lecture FC03 du registre 300 avec un analyseur Modbus.
- Régler le délai de réponse selon le temps de démarrage réel du programme.

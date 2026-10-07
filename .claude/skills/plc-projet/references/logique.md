# Logique : sémantique du code généré et bonnes pratiques

Ce que fait réellement la classe C++ générée (et le simulateur, qui suit le même ordre).
À lire avant d'écrire le grafcet et les modes d'un projet.

## Modes de marche (machine d'états de la classe générée)

```
INITIALISATION ─▶ ARRÊT ─▶ (PRISE D'ORIGINE) ─▶ REPOS ⇄ CYCLE
      ▲             ▲  └──▶ MANUEL (jog) ──┘ (retour ARRÊT)
      └─ RÉARMEMENT ◀─ acquittement ◀─ URGENCE ◀── (depuis tout mode)
```

- **INITIALISATION** : sans servo, passe aussitôt en ARRÊT ; avec servo, initialise le
  variateur (échec → URGENCE).
- **ARRÊT** : grafcet vide, **toutes les sorties à 0**. Reste en ARRÊT tant que
  `run.stop` est vrai. Passe en REPOS quand `run.start` est vrai (vide = immédiatement),
  après la prise d'origine du servo si nécessaire (`servo.homing` `auto` ou `condition`).
  Avec un servo et `jogEnabled`, `jogMode` vrai → MANUEL.
- **REPOS / CYCLE** : le grafcet tourne. REPOS = grafcet en situation initiale (étapes
  initiales seules actives et servo immobile), CYCLE sinon. Les paramètres ne sont
  sauvegardés qu'au repos (`isIdle()`).
- **`run.stop`** vrai en REPOS, CYCLE, PRISE D'ORIGINE ou MANUEL → ARRÊT immédiat (grafcet
  remis à zéro, servo arrêté). Le cycle en cours est perdu : l'arrêt n'est pas un « arrêt
  en fin de cycle » (pour cela, le gérer dans le grafcet, voir les patrons).
- **URGENCE** (prioritaire partout) : `emergency.condition` vraie, ou alarme du servo. Grafcet
  remis à zéro, servo stoppé, chaque sortie à son état de repli (`fallback` : off / on /
  hold), les autres variables pilotées par N/S/R à 0. Sortie : condition fausse **puis**
  `emergency.reset` vrai (vide = automatiquement 1 s après la disparition — déconseillé) →
  RÉARMEMENT (servo) → ARRÊT. La mise en marche (`run.start`) est donc à nouveau
  nécessaire après une urgence.
- **MANUEL** (servo) : `jogPlus` / `jogMinus` font avancer le servo tant qu'ils sont vrais ;
  `jogMode` faux → ARRÊT. Accessible depuis ARRÊT, ou depuis REPOS si le grafcet est en
  situation initiale : `jogMode` activé pendant un cycle fait passer en MANUEL dès le
  retour en situation initiale (fin du cycle).

## Cycle d'automate (10 ms)

1. Capture des opérandes des fronts (`FM`, `FD`).
2. Transitions franchissables = toutes les étapes amont actives ET réceptivité vraie ;
   franchissement simultané ; actions à l'activation (S, R, SET, INC, DEC, SERVO_*, MSG) des
   nouvelles étapes ; on recommence tant qu'une transition est franchissable (recherche de
   stabilité, 16 itérations max., au-delà : message « grafcet instable »).
3. Actions continues (N, NSET) des étapes actives, puis écriture des sorties.

Conséquences :
- Une étape traversée dans le même cycle (instable) n'exécute **pas** ses actions N/NSET,
  mais exécute ses actions à l'activation.
- Un front ne vaut qu'**une fois par cycle** : `FM(Depart)` ne peut pas franchir deux
  transitions successives dans le même cycle.
- Une variable pilotée par N/S/R vaut le **OU** de toutes ses actions N actives et de sa
  mémoire S/R ; elle est recalculée à chaque cycle (0 hors REPOS/CYCLE). Ne pas l'affecter
  aussi par SET/INC (avertissement : l'affectation serait écrasée).
- Les variables affectées par SET/INC/DEC/NSET gardent leur valeur entre les cycles et
  entre les modes (sauf les sorties câblées, remises à 0 hors REPOS/CYCLE).
- Les commandes (`command`) sont des boutons maintenus de l'écran : vraies tant que
  l'opérateur appuie. Utiliser `FM(Commande)` pour réagir à un appui.

## Langage des réceptivités et expressions

| Écriture | Signification |
|---|---|
| `Depart ET NON Porte` | logique (`ET`/`OU`/`NON`, ou `AND`/`OR`/`NOT`, `&&` `\|\|` `!`) |
| `FM(Bouton)` `↑Bouton` · `FD(Bouton)` `↓Bouton` | front montant / descendant |
| `X3` | étape 3 active |
| `X3.t >= 5s` · `t/X3/5s` | étape 3 active depuis 5 s (unités ms, s, min, h ; un nombre seul ou une variable comparé à `X3.t` est en ms) |
| `Compteur >= Nb_pieces` | comparaisons `=` `<>` `<` `<=` `>` `>=` |
| `(Cible + 100) * 2` | arithmétique `+ - * / %` (division entière entre entiers, entiers 32 bits) |
| `MIN(a, b)` `MAX(a, b)` `ABS(a)` | fonctions |
| `Recette = "A"` | texte : `=` et `<>` seulement |
| `VRAI` `FAUX` | constantes |
| `Servo.pret` `Servo.enPosition` `Servo.enMouvement` `Servo.origineFaite` `Servo.alarme` `Servo.position` | état du servo |

Symboles sensibles à la casse. Une réceptivité doit être booléenne ; une valeur
(`SET`, `SERVO_MOVE`) peut être numérique ou texte selon la cible.

## Servo

- `SERVO_MOVE pos @ vitesse` à l'activation : vitesse = index 0-15 ; une cible négative ou
  au-delà de la course maximale (`maxRange` × `pulsesPerUnit`) **met la machine en URGENCE**
  (« Cible servo hors course ») : borner les paramètres de position avec `min`/`max`.
  `Servo.enPosition` devient vrai à la fin du déplacement et retombe au déplacement
  suivant : attendre `Servo.enPosition` dans la transition qui suit.
- Prise d'origine : `homing: "auto"` la fait au passage ARRÊT → REPOS tant que l'origine
  n'est pas faite ; `"condition"` attend en plus `homingCondition` ; `"none"` laisse le
  grafcet faire `SERVO_HOME` (puis attendre `Servo.origineFaite`).
- Un déplacement refusé par le variateur (pas prêt, en alarme, mouvement déjà en cours) ou
  une alarme du servo met la machine en URGENCE : ne pas enchaîner deux `SERVO_MOVE` sans
  attendre `Servo.enPosition`.
- `SERVO_STOP` : `Servo.enMouvement` retombe aussitôt (une transition
  `NON Servo.enMouvement` derrière un arrêt est franchie dans le même cycle) ; un
  `SERVO_MOVE` qui suit attend la fin de l'arrêt immédiat du variateur (≈ 0,5 s) avant de
  partir — attendre `Servo.enPosition` comme d'habitude.
- `Servo.position` est relue sur le variateur (toutes les 250 ms environ) : valeur un peu
  en retard pendant un mouvement, exacte à l'arrêt. Pas de déplacement relatif : calculer
  la cible (voir « Indexation » dans les patrons).
- Les signaux `servoOn`, `immediateStop`, `alarmsReset` sont pilotés par le bloc servo :
  câblez-les (`servoSignals`), ne les écrivez pas dans le grafcet.

## Patrons recommandés

**Arrêt d'urgence** : chaîne de sécurité câblée sur une entrée (`AU_OK`, vraie quand tout
va bien) ; `emergency.condition: "NON AU_OK"`, `emergency.reset` = commande d'écran
`Acquit` ou bouton physique. Le relais de sécurité reste matériel : l'automate ne fait que
suivre. Prévoir `fallback` sur chaque sortie (`off` par défaut ; `hold` pour un frein ou
une électrovanne monostable qui doit garder sa position).

**Marche / arrêt** :
- boutons poussoirs : `run.start: "BP_Marche"`, `run.stop: "BP_Arret"` (bouton arrêt NF
  câblé avec `inverse: true`) ;
- sélecteur Auto/Arrêt : `start: "Auto"`, `stop: "NON Auto"` ;
- démarrage automatique : `start` vide (REPOS dès que possible) ;
- arrêt en fin de cycle : commande `Fin_cycle` testée dans la transition qui revient à
  l'étape initiale ou qui lance le cycle (`FM(Depart) ET NON Fin_cycle`).

**Temporisations** : `X4.t >= 2s` ou un paramètre en ms (`X4.t >= Tempo_ms`) — préférer un
paramètre réglable quand la valeur dépend du réglage machine.

**Indexation (pas à pas du servo)** : étape « Calcul » avec `SET Cible := Poste + Pas` (ou,
pour se recaler sur la grille après un jog, `SET Cible := ((Servo.position + Pas / 2) / Pas + 1) * Pas`),
divergence OU `Cible <= Course_max` → étape « Avance » (`SERVO_MOVE Cible @ Vitesse`,
`SET Poste := Cible`) / `Cible > Course_max` → retour à 0 puis reprise ; transition suivante
`Servo.enPosition`. Calculer depuis une variable mémorisée (`Poste`) évite d'accumuler les
écarts de mesure.

**Comptage** : `INC Pieces` à l'activation d'une étape traversée une fois par pièce ;
remise à zéro par une commande : étape ou transition dédiée avec `SET Pieces := 0`.

**Mémoriser un événement** : `SET Mem := Condition` à l'activation (photo de l'instant) ou
S/R entre deux étapes.

**Défauts process (bourrage, capteur qui ne vient pas)** : les gérer **dans le grafcet** —
divergence OU avec une transition de surveillance (`X4.t >= Tempo_defaut ET NON Capteur`)
vers une étape « Défaut » (`N Voyant_defaut`, `MSG error: …`, sorties dangereuses absentes),
puis retour sur `FM(Acquit) ET <cause disparue>`. Réserver le bloc URGENCE à la sécurité :
une variable mise à 1 par le grafcet ne peut pas servir de condition d'urgence (le grafcet
est remis à zéro en urgence et ne pourrait plus la remettre à 0).

**Réceptivités exclusives** dans une divergence OU (`Cel ET Defaut` / `Cel ET NON Defaut`) :
si deux transitions d'une même étape sont vraies en même temps, toutes deux sont franchies.

**Signalisation d'état** : l'état machine (`@etat`) s'affiche à l'écran ; les sorties
câblées sont toutes à 0 en ARRÊT : un voyant « en marche » se pilote par `N Voyant` dans
les étapes concernées.

**Messages à l'opérateur** : `MSG error:` / `MSG warning:` (toujours affichés), `MSG info:`
(affiché si `showInfoMessages`, défaut oui) ; `MSG success:` est masqué par défaut
(`showSuccessMessages: false`). Textes courts : ils s'affichent dans le grafcet.

**Grafcets indépendants** (ex. remise à zéro d'un compteur pendant le cycle) : une seconde
étape initiale avec sa propre boucle ; elle tourne en parallèle du grafcet principal en
REPOS/CYCLE (mais rappel : REPOS exige que *toutes* les étapes initiales seules soient
actives).

## Limites actuelles du framework à signaler à l'utilisateur

- `execution: "headless"` est écrit dans `config.json` mais le firmware ne le lit pas
  encore : la logique ne tourne que lorsqu'un navigateur est connecté (audit CR-2).
- Un seul SureServo par projet ; ses signaux TOR sur des relais de l'automate.
- GPIO2 de la Kincony = buzzer, remis à 0 à chaque connexion WiFi.
- Le simulateur modélise le servo de façon simplifiée (temps indicatifs) : valider les
  temps réels sur la machine.
- Compilation ESP32 complète : sur le poste avec PlatformIO après « Installer ».

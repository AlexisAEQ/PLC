# CLAUDE_TASK — LittleFS Étape 2 : commit atomique en un seul rename

## Prérequis
L'étape 1 (`CLAUDE_TASK_LittleFS_Etape1_RecoveryBoot.md`) doit être validée au banc et committée. Elle reste le filet de sécurité si le repli décrit ci-dessous s'active.

## Contexte
Référence : `claude/RessortRoyal2_LittleFS_corruption_analysis.md`.

Aujourd'hui, `commit()` renomme d'abord `X` en `X.backup`. Pendant ce temps, `X` n'existe pas, et c'est cette fenêtre qui est à l'origine des pertes. Or `lfs_rename` (LittleFS) **remplace la destination de façon atomique** : un seul `rename(X.tmp → X)` suffit. À tout instant, `X` existe, soit dans son ancienne version, soit dans la nouvelle.

## Objectif
Dans `AtomicFileWriter::commit()` :
- tenter d'abord le rename direct `X.tmp → X` ;
- si la plateforme le refuse, **se replier sur l'ancien comportement** (backup puis rename), avec un log `repli`. Le code actuel n'est donc jamais moins sûr qu'avant.

Le nettoyage d'un éventuel `X.backup` résiduel après succès est conservé.

## Périmètre
- `src/AtomicFileWriter/AtomicFileWriter.cpp` : méthode `commit()` uniquement.

`.h`, `writeData`, `createBackup`, `restoreBackup` et `cleanup` restent inchangés.

## Préconditions
```bash
git status --short                                   # arbre propre attendu
git log --oneline -3                                 # l'étape 1 doit apparaître
grep -c "recoverInterruptedCommits" src/PLC_Persistence/PLC_Persistence.cpp   # attendu : 3 (étape 1 présente)
grep -c "    // Créer un backup de l'original si existant" src/AtomicFileWriter/AtomicFileWriter.cpp   # attendu : 1
grep -c "    // Opération atomique : renommer le fichier temporaire" src/AtomicFileWriter/AtomicFileWriter.cpp   # attendu : 1
grep -c "LittleFS.rename" src/AtomicFileWriter/AtomicFileWriter.cpp           # attendu : 3
grep -c "SIMULATE_COMMIT_CRASH" src/AtomicFileWriter/AtomicFileWriter.cpp     # attendu : 0 (instrumentation de l'étape 1 retirée)
```
Si un compte diffère : **STOP** et rapporter l'état réel.

## Modification (relire l'ancre live ; fichier en CRLF)

old_str :
```cpp
    // Créer un backup de l'original si existant
    if (LittleFS.exists(originalPath.c_str())) {
        if (!createBackup()) {
            lastError = "Échec création backup";
            return false;
        }
    }
    
    // Opération atomique : renommer le fichier temporaire
    if (!LittleFS.rename(tempPath.c_str(), originalPath.c_str())) {
        lastError = "Échec commit atomique";
        Serial.printf("ERREUR: %s - %s -> %s\n", 
                     lastError.c_str(), tempPath.c_str(), originalPath.c_str());
        
        // Tentative de restauration du backup
        restoreBackup();
        return false;
    }
```
(Attention aux lignes contenant des espaces de fin, par exemple `    ` entre les blocs : copier l'ancre exacte depuis le fichier.)

new_str :
```cpp
    // Corruption LittleFS - Étape 2 : remplacement atomique en UN SEUL rename.
    // lfs_rename remplace la destination atomiquement : X existe à tout instant
    // (ancienne ou nouvelle version). Plus de fenêtre où X est absent.
    if (!LittleFS.rename(tempPath.c_str(), originalPath.c_str())) {
        // Repli : ancien comportement (backup puis rename) si la plateforme
        // refuse le rename direct. Journalisé pour le détecter au banc.
        Serial.printf("AtomicFileWriter: ⚠️ rename direct refusé pour %s, repli backup+rename\n",
                      originalPath.c_str());

        // Créer un backup de l'original si existant
        if (LittleFS.exists(originalPath.c_str())) {
            if (!createBackup()) {
                lastError = "Échec création backup";
                return false;
            }
        }

        // Opération atomique : renommer le fichier temporaire
        if (!LittleFS.rename(tempPath.c_str(), originalPath.c_str())) {
            lastError = "Échec commit atomique";
            Serial.printf("ERREUR: %s - %s -> %s\n",
                         lastError.c_str(), tempPath.c_str(), originalPath.c_str());

            // Tentative de restauration du backup
            restoreBackup();
            return false;
        }
    }
```

La suite (`committed = true;`, puis suppression d'un `backupPath` résiduel) reste **inchangée**.

## Vérifications après modification
```bash
grep -c "rename direct refusé" src/AtomicFileWriter/AtomicFileWriter.cpp   # attendu : 1
grep -c "LittleFS.rename" src/AtomicFileWriter/AtomicFileWriter.cpp        # attendu : 4
git diff --stat                                                            # attendu : 1 fichier, AtomicFileWriter.cpp
pio run -e quatre_mb_huge                                                  # compile sans nouvelle erreur ni warning
```

## Validation au banc (par Thierry)
1. **Le rename direct fonctionne sur la plateforme** : modifier un paramètre et attendre la sauvegarde (2 min, ou recharger la page). Le log série ne doit **jamais** afficher `rename direct refusé`.
   - Si ce message apparaît, rapporter : la plateforme ne remplace pas la destination. Le code reste fonctionnel grâce au repli, mais l'étape n'apporte rien et il faudra revoir la stratégie.
2. **Persistance** : redémarrer. La valeur modifiée est conservée, et le listing `=== LittleFS Status ===` ne montre **aucun** `← ORPHAN`.
3. **Non-régression des autres écritures** : sauvegarder `config.json` depuis l'éditeur (redémarrage attendu si changement critique). Vérifier aussi `reboot_log.json` (un boot) et `diagnostic.txt` (générer quelques erreurs).
4. **Endurance** (recommandé) : une série de coupures d'alimentation pendant que des sauvegardes ont lieu. Le fichier doit toujours être présent et lisible au boot.

## STOP
Ne pas committer ni pousser avant validation au banc. Il faut un commit dédié, sur sa propre branche.

Note de propagation, à faire plus tard et **pas dans cette tâche** : `AtomicFileWriter` et `PLC_Persistence` sont des fichiers d'infrastructure. Une fois validés sur RessortRoyal2, il faudra les propager aux autres branches (Woodzco, RotaryIndexingTable, Formaca2), en vérifiant par un diff l'état réel de chaque branche au préalable.

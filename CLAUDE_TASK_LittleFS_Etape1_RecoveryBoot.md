# CLAUDE_TASK — LittleFS Étape 1 : restaurer au boot les fichiers dont le commit a été interrompu

## Contexte
Référence : `claude/RessortRoyal2_LittleFS_corruption_analysis.md` (mécanisme A).

L'actuel `AtomicFileWriter::commit()` procède ainsi :
1. `rename(X → X.backup)`
2. `rename(X.tmp → X)`
3. `remove(X.backup)`

Un reset entre 1 et 2 laisse `X` **absent**, avec `X.backup` (dernière version validée) et `X.tmp` sur la flash. Au boot, `PLC_Persistence::logFileSystemStatus(true)` supprime tous les `*.tmp` et `*.backup`, donc le fichier est perdu et le PLC passe en état broken.

## Objectif
Avant le nettoyage des orphelins, **si `X` est absent et que `X.backup` existe, renommer `X.backup` en `X`**.

- Si `X` existe, rien ne change (comportement actuel strictement conservé).
- Le cas `X.tmp` seul (sans `.backup`) garde lui aussi son comportement actuel : suppression. Ce `.tmp` peut être partiel.
- On restaure le `.backup` plutôt que le `.tmp`. C'est la dernière version **committée**, donc valide par construction. Au pire, on perd la dernière sauvegarde (compteurs des dernières minutes), jamais le fichier entier.

## Périmètre (fichiers)
- `src/PLC_Persistence/PLC_Persistence.h` : 1 déclaration privée
- `src/PLC_Persistence/PLC_Persistence.cpp` : 1 appel et 1 nouvelle méthode

Aucun autre fichier. Pas de refactoring opportuniste.

## Préconditions (à exécuter, stopper si un compte diffère)
```bash
git status --short                      # arbre propre attendu
grep -c "        bool isPathValid(const char\* path);" src/PLC_Persistence/PLC_Persistence.h        # attendu : 1
grep -c "recoverInterruptedCommits" src/PLC_Persistence/PLC_Persistence.h src/PLC_Persistence/PLC_Persistence.cpp   # attendu : 0 et 0
grep -c "void PLC_Persistence::logFileSystemStatus(bool cleanOrphans) {" src/PLC_Persistence/PLC_Persistence.cpp  # attendu : 1
grep -c "    size_t orphanBytes = 0;" src/PLC_Persistence/PLC_Persistence.cpp                        # attendu : 1
grep -c "    logFileSystemStatus(true);" src/PLC_Persistence/PLC_Persistence.cpp                     # attendu : 1 (appel dans le constructeur)
```
Si un compte ne correspond pas : **STOP** et rapporter l'état réel. Si `recoverInterruptedCommits` existe déjà, rapporter « déjà conforme » au lieu de l'ignorer.

## Modifications (relire les ancres live dans le fichier avant d'éditer ; fichiers en CRLF)

### 1. `PLC_Persistence.h` : déclaration privée
old_str :
```cpp
        bool isPathValid(const char* path);
```
new_str :
```cpp
        bool isPathValid(const char* path);

        // Corruption LittleFS - Étape 1 : restaure X depuis X.backup si X est absent
        void recoverInterruptedCommits();
```

### 2. `PLC_Persistence.cpp` : appel avant le nettoyage des orphelins
old_str :
```cpp
    size_t orphanBytes = 0;
    File root = LittleFS.open("/");
```
new_str :
```cpp
    // Corruption LittleFS - Étape 1 : restaurer les commits interrompus
    // AVANT que le nettoyage ne supprime les .backup / .tmp
    if (cleanOrphans) {
        recoverInterruptedCommits();
    }

    size_t orphanBytes = 0;
    File root = LittleFS.open("/");
```

### 3. `PLC_Persistence.cpp` : nouvelle méthode, insérée juste avant `logFileSystemStatus`
old_str :
```cpp
void PLC_Persistence::logFileSystemStatus(bool cleanOrphans) {
```
new_str :
```cpp
// ============================================================================
// RÉCUPÉRATION DES COMMITS INTERROMPUS (Corruption LittleFS - Étape 1)
// ----------------------------------------------------------------------------
// AtomicFileWriter::commit() renomme X -> X.backup puis X.tmp -> X.
// Un reset entre les deux laisse X ABSENT et X.backup présent (dernière
// version committée, donc valide). On restaure X.backup -> X.
// Règle : si X existe, on ne touche à rien (cas normal).
// Appelé depuis logFileSystemStatus(true), donc au boot, avant tout accès métier.
// ============================================================================
void PLC_Persistence::recoverInterruptedCommits() {
    static const char* BACKUP_SUFFIX = ".backup";
    const size_t suffixLen = strlen(BACKUP_SUFFIX);

    // Passe 1 : collecter les noms (aucun rename pendant le parcours du répertoire)
    std::vector<String> backups;
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("recoverInterruptedCommits: impossible d'ouvrir la racine");
        if (root) root.close();
        return;
    }

    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            String name = String(f.name());
            if (name.length() > suffixLen && name.endsWith(BACKUP_SUFFIX)) {
                backups.push_back(name);
            }
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();

    // Passe 2 : restaurer X.backup -> X uniquement si X est absent
    for (const String& name : backups) {
        const char* prefix = name.startsWith("/") ? "" : "/";
        String backupPath   = String(prefix) + name;
        String originalPath = String(prefix) + name.substring(0, name.length() - suffixLen);

        if (LittleFS.exists(originalPath.c_str())) {
            continue;  // Cas normal : X présent, le .backup sera nettoyé comme orphelin
        }

        if (LittleFS.rename(backupPath.c_str(), originalPath.c_str())) {
            Serial.printf("  ⚠️ RECOVERY: %s absent -> restauré depuis %s\n",
                          originalPath.c_str(), backupPath.c_str());
        } else {
            Serial.printf("  ❌ RECOVERY: échec restauration de %s depuis %s\n",
                          originalPath.c_str(), backupPath.c_str());
        }
    }
}

void PLC_Persistence::logFileSystemStatus(bool cleanOrphans) {
```

## Vérifications après modification
```bash
grep -c "recoverInterruptedCommits" src/PLC_Persistence/PLC_Persistence.h     # attendu : 1
grep -c "recoverInterruptedCommits" src/PLC_Persistence/PLC_Persistence.cpp   # attendu : 3 (commentaire, appel, définition)
git diff --stat                                                               # attendu : 2 fichiers, PLC_Persistence.h/.cpp uniquement
pio run -e quatre_mb_huge                                                     # doit compiler sans nouvelle erreur ni warning
```

## Validation au banc (par Thierry)
**Test A (non-régression)** : boot normal. Le log `=== LittleFS Status ===` doit être identique à avant et ne contenir aucune ligne `RECOVERY`. La machine démarre et les paramètres sont conservés.

**Test B (preuve que le bug est corrigé)** : instrumentation **temporaire, à retirer**. Dans `src/AtomicFileWriter/AtomicFileWriter.cpp`, dans `commit()`, juste après le bloc `if (LittleFS.exists(originalPath.c_str())) { if (!createBackup()) {...} }` :
```cpp
#ifdef SIMULATE_COMMIT_CRASH   // À RETIRER
    if (originalPath == "/RessortRoyal2.json") {
        Serial.println("SIMULATION: reset entre backup et rename");
        Serial.flush();
        ESP.restart();
    }
#endif
```
Compiler avec `-DSIMULATE_COMMIT_CRASH` (build_flags, temporaire), puis :
1. Modifier un paramètre (vitesse jog, par exemple) et attendre la sauvegarde différée (2 min), ou recharger la page web (`flushAll`).
2. Le PLC redémarre. Au boot, on doit voir `RECOVERY: /RessortRoyal2.json absent -> restauré depuis /RessortRoyal2.json.backup`.
3. La machine démarre normalement, avec les valeurs de la sauvegarde **précédente**.
4. Retirer l'instrumentation et le flag, recompiler, et vérifier que `grep -c SIMULATE_COMMIT_CRASH -r src platformio.ini` renvoie 0.

(Optionnel : faire le test B **sans** l'étape 1 pour reproduire le bug. On doit alors voir le fichier supprimé et le PLC broken. Il faudra ensuite refaire un `uploadfs`.)

## STOP
Ne pas committer ni pousser avant validation au banc par Thierry. Il faut un commit dédié, sur sa propre branche, contenant uniquement cette étape.

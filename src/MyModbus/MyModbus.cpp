#include "MyModbus/MyModbus.h"
#include "BorneUniverselle/borneUniverselle.h"
#include "MyModbus.h"

// =============================================================================
// Constructeur
// =============================================================================

MyModbus::MyModbus() {
    lastTransaction = millis();
    lastEvent       = Modbus::EX_SUCCESS;
    lastProblem     = 0;
    modbus          = nullptr;

    modbusInitialized = false;
    modbusDeclaredDead        = false;
    lastRecoveryAttempt       = 0;
    recoveryAttemptCount      = 0;

    resetStats();
}

// =============================================================================
// Messages internes
// =============================================================================

void MyModbus::showMessages(const char* message) {
    if (isShowMessages) {
        Serial.printf("%lu:: %s\r\n", millis(), message);
    }
}

void MyModbus::setShowMessages(bool status) {
    if (status != isShowMessages) {
        // Serial.println(status ? "Show modbus messages: true" : "Show modbus messages: false");
    }
    isShowMessages = status;
}

// =============================================================================
// Configuration
// =============================================================================

void MyModbus::setModbus(ModbusRTU *_modbus) {
    modbus = _modbus;
}

void MyModbus::setTimeout(uint32_t ms) {
    if (ms < MIN_TIMEOUT) ms = MIN_TIMEOUT;
    if (ms > MAX_TIMEOUT) ms = MAX_TIMEOUT;
    transactionTimeout = ms;
    Serial.printf("MyModbus timeout: %lu ms\n", (long unsigned int)transactionTimeout);
}

uint32_t MyModbus::getTimeout() {
    return transactionTimeout;
}

void MyModbus::resetTimeout() {
    transactionTimeout = DEFAULT_TIMEOUT;
}

// =============================================================================
// Gestion du bus
// =============================================================================

bool MyModbus::requestTransaction(uint16_t slaveAddress) {
    if (suspended) return false;

    // Enregistrer l adresse pour les stats de la transaction a venir
    currentTransactionAddress = slaveAddress;

    // ===== BLOCAGE SI ADRESSE MORTE (throttle par adresse) =====
    if (modbusInitialized && slaveAddress != 0) {
        SlaveStats* slot = findOrCreateSlaveSlot(slaveAddress);
        if (slot != nullptr && slot->consecutiveErrors >= ERROR_THRESHOLD_PER_SLAVE) {
            uint32_t now = millis();
            if (now - slot->lastRecoveryAttemptTime >= RECOVERY_ATTEMPT_INTERVAL) {
                slot->lastRecoveryAttemptTime = now;
                slot->recoveryAttemptsLeft    = 2;
                Serial.printf("🔄 Recovery addr:%u — fenêtre ouverte (consec=%lu, depuis %lus)\r\n",
                    slaveAddress, (unsigned long)slot->consecutiveErrors,
                    (unsigned long)(now - slot->lastErrorTime) / 1000);
            }
            if (slot->recoveryAttemptsLeft > 0) {
                slot->recoveryAttemptsLeft--;
                Serial.printf("Recovery addr:%u tentative autorisee restantes=%u consec=%lu\r\n",
                    slaveAddress, slot->recoveryAttemptsLeft, (unsigned long)slot->consecutiveErrors);
            } else {
                // Fenetre epuisee : bloquer
                char who[48];
                describeAddress(slaveAddress, who, sizeof(who));
                char throttleMsg[128];
                snprintf(throttleMsg, sizeof(throttleMsg), MSG_FMT_PERIPHERIQUE_INJOIGNABLE, who);
                PLC_Tools::sendPeriodicMessage(true, WARNING, throttleMsg);
                currentTransactionAddress = slaveAddress;
                lastEvent = Modbus::EX_TIMEOUT;
                updateSlaveStats(slaveAddress, Modbus::EX_TIMEOUT);  // maintenir le compteur
                currentTransactionAddress = 0;
                return false;  // <-- transaction interdite
            }
        }
    }

    // ===== Modbus TCP : pas de bus partagé, seulement la connexion à établir =====
    if (isTcpAddress(slaveAddress)) {
        currentTcpTransaction = 0;
        return tcpPrepare(slaveAddress);
    }
    if (modbus == nullptr) {
        lastEvent = Modbus::EX_GENERAL_FAILURE;
        return false;
    }

    // ===== Variante 2 : refus rapide si le bus est connu bloqué =====
    // On ne consomme pas tout le budget d'attente si on sait déjà, depuis un
    // timeout récent (waitEndTransaction), que le bus est occupé par une
    // transaction qui n'a pas pu être drainée. On laisse une chance à la lib
    // de se libérer (un seul task(), non bloquant) avant de trancher.
    if (busStuckSince != 0) {
        modbus->task();
        constexpr uint32_t BUS_STUCK_WINDOW_MS = 600; // > max mesuré ~500ms
        if (modbus->slave() != 0 && (millis() - busStuckSince < BUS_STUCK_WINDOW_MS)) {
            currentTransactionAddress = slaveAddress;
            constexpr uint32_t REFUS_LOG_INTERVAL_MS = 100;
            uint32_t now = millis();
            if (lastRefusLogTime == 0 || now - lastRefusLogTime >= REFUS_LOG_INTERVAL_MS) {
                lastRefusLogTime = now;
                char refusRapideMsg[128];
                snprintf(refusRapideMsg, sizeof(refusRapideMsg),
                    ">>> requestTransaction REFUS_RAPIDE: demande:%u bloquant:%u depuis:%lums lastEvent:%d",
                    slaveAddress, busStuckAddress,
                    (unsigned long)(now - busStuckSince), lastEvent);
                showMessages(refusRapideMsg);
            }
            return false;
        }
        // Bus libéré entretemps, ou fenêtre expirée : état périmé, on l'efface
        Serial.printf("BUS_STUCK_CLEARED;loc=REQ;elapsed=%lu;reason=%s;addr=%u\r\n",
            (unsigned long)(millis() - busStuckSince),
            (modbus->slave() == 0) ? "slave_freed" : "window_expired",
            busStuckAddress);
        busStuckSince = 0;
    }

    // ===== Code normal si Modbus vivant ou adresse autorisee =====
    showMessages("requestTransaction");

    transactionRequestStart = millis();  // ← timestamp entrée
    uint32_t deadline = transactionRequestStart + transactionTimeout + DELAY_TRANSACTION;

    // Attendre si cet esclave a été écrit récemment (firmware latency)
#if DELAY_AFTER_WRITE > 0
    if (slaveAddress != 0) {
        SlaveStats* slot = findOrCreateSlaveSlot(slaveAddress);
        if (slot != nullptr && slot->lastWriteTime > 0) {
            uint32_t elapsed = millis() - slot->lastWriteTime;
            if (elapsed < DELAY_AFTER_WRITE) {
                vTaskDelay(pdMS_TO_TICKS(DELAY_AFTER_WRITE - elapsed));
            }
        }
    }
#endif

    while (millis() < deadline) {
        bool delayOk   = (millis() - lastTransaction >= DELAY_TRANSACTION);
        bool slaveFree = (modbus->slave() == 0);
        if (delayOk && slaveFree) break;
        modbus->task();
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (modbus->slave() != 0) {
        // Un prédécesseur a laissé _slaveId collé (réponse perdue pendant une
        // suspension flash, non purgeable avant le timeout lib ~MODBUSRTU_TIMEOUT).
        // On REFUSE la transaction ce tour au lieu de la lancer sur un bus sale :
        // évite la cascade de fausses erreurs sur les esclaves innocents.
        // Le node sera re-scruté au cycle suivant, une fois _slaveId libéré.
        char refuseeMsg[128];
        snprintf(refuseeMsg, sizeof(refuseeMsg),
            ">>> requestTransaction REFUSEE: demande:%u bloquant:%u attente_reelle:%lums lastEvent:%d",
            slaveAddress, modbus->slave(),
            (long unsigned int)(millis() - transactionRequestStart), lastEvent);
        showMessages(refuseeMsg);
        return false;  // <-- bus non propre : refus, pas de cascade
    }
    showMessages("requestTransaction done");
    return true;  // <-- bus propre : transaction autorisee
}

bool MyModbus::waitEndTransaction() {
    if (suspended) {
        return false;
    }
    if (isTcpAddress(currentTransactionAddress)) {
        return waitEndTcpTransaction();
    }
    if (modbus == nullptr) {
        return false;
    }

    uint32_t start    = millis();
    uint32_t deadline = millis() + transactionTimeout;

    char message[256];

    snprintf(message, sizeof(message),
        "MyModbus waitEndTransaction, allowed time: %lu [ms]",
        (long unsigned int)transactionTimeout);
    showMessages((const char *)message);

    uint8_t slaveActive;

    // Attente de fin de transaction
    while (millis() < deadline) {
        modbus->task();
        slaveActive = modbus->slave();

        if (slaveActive == 0) {
            stats.lastTransactionTime = millis() - start;
            lastRefusLogTime = 0;

            // ===== Variante 2 : bus libéré, l'état "bloqué" n'a plus lieu d'être =====
            if (busStuckSince != 0) {
                Serial.printf("BUS_STUCK_CLEARED;loc=WAIT;elapsed=%lu;reason=slave_freed;addr=%u\r\n",
                    (unsigned long)(millis() - busStuckSince), busStuckAddress);
            }
            busStuckSince = 0;

            if (lastEvent == Modbus::EX_SUCCESS) {
                stats.lastSuccessTime = millis();
                if (stats.lastTransactionTime > stats.maxTransactionTime) {
                    stats.maxTransactionTime = stats.lastTransactionTime;
                }
                snprintf(message, sizeof(message),
                    "MyModbus waitEndTransaction done SUCCESS, in: %lu [ms]",
                    (long unsigned int)stats.lastTransactionTime);
                showMessages((const char *)message);

                return true;
            } else {
                snprintf(message, sizeof(message),
                    "MyModbus waitEndTransaction done ERROR (code:%d), in: %lu [ms]",
                    lastEvent, (long unsigned int)stats.lastTransactionTime);
                showMessages((const char *)message);
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Sortie par timeout
    snprintf(message, sizeof(message),
        "MyModbus waitEndTransaction out of transaction allowed time (%lu ms)",
        (long unsigned int)transactionTimeout);
    showMessages(message);

    if (modbus->slave() != 0) {
        for (int i = 0; i < 5 && modbus->slave() != 0; i++) {
            modbus->task();
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        if (modbus->slave() != 0) {
            // Toujours bloqué après le drain : vrai timeout, comportement inchangé

            // ===== Variante 2 : bus toujours occupé après drain → marquer bloqué =====
            busStuckSince   = millis();
            busStuckAddress = modbus->slave();

            lastEvent = Modbus::EX_TIMEOUT;
            showMessages("Will call handleError");
            handleError(Modbus::EX_TIMEOUT);
            lastTransaction = millis();
            return false;
        }

        // Le drain a laissé le temps à la vraie réponse d'arriver ; task() l'a
        // déjà traitée via le callback (cbRead/cbWrite -> processEvent), qui a
        // mis à jour lastEvent et les stats. On rapporte le vrai résultat au
        // lieu de forcer un faux EX_TIMEOUT (bug corrigé).
        stats.lastTransactionTime = millis() - start;
        bool success = (lastEvent == Modbus::EX_SUCCESS);
        if (success) {
            stats.lastSuccessTime = millis();
            if (stats.lastTransactionTime > stats.maxTransactionTime) {
                stats.maxTransactionTime = stats.lastTransactionTime;
            }
        }
        showMessages(success ? "waitEndTransaction done SUCCESS (apres drain)"
                              : "waitEndTransaction done ERROR (apres drain)");
        return success;
    }
    return false;
} // waitEndTransaction

bool MyModbus::isModbusFree() {
    return (millis() - lastTransaction > DELAY_TRANSACTION);
}

void MyModbus::waitUntilFree(uint32_t timeoutMs) {
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        if (tcp != nullptr && currentTcpTransaction != 0 && tcp->isTransaction(currentTcpTransaction)) {
            tcp->task();
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (modbus == nullptr || modbus->slave() == 0) return;  // bus libre
        modbus->task();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    // Timeout dépassé — on suspend quand même pour ne pas bloquer la persistence
    Serial.printf("%lu:: ⚠️ MyModbus::waitUntilFree timeout %lums — suspension forcée\r\n",
        millis(), (unsigned long)timeoutMs);
}

Modbus::ResultCode MyModbus::getLastEvent() {
    return lastEvent;
}

// =============================================================================
// Statistiques globales
// =============================================================================

const MyModbus::ModbusStats& MyModbus::getStats() {
    return stats;
}

void MyModbus::resetStats() {
    stats = ModbusStats();
}

void MyModbus::getErrorReport(char* buffer, size_t size) {
    snprintf(buffer, size,
        "Modbus Stats:\n"
        "Success rate: %.1f%%\n"
        "Total trans.: %lu\n"
        "Success trans: %lu\n"
        "Total errors: %lu\n"
        "CRC errors: %lu\n"
        "Timeout errors: %lu\n"
        "Connection errors: %lu\n"
        "Hard errors: %lu\n"
        "Max trans. time: %lu ms\n"
        "Last trans. time: %lu ms\n"
        "Last error: %lu ms ago",
        (stats.totalTransactions > 0) ?
            (float)stats.successfulTransactions * 100.0f / stats.totalTransactions : 0,
        (long unsigned int)stats.totalTransactions,
        (long unsigned int)stats.successfulTransactions,
        (long unsigned int)stats.totalErrors,
        (long unsigned int)stats.crcErrors,
        (long unsigned int)stats.timeoutErrors,
        (long unsigned int)stats.connectionErrors,
        (long unsigned int)stats.hardErrors,
        (long unsigned int)stats.maxTransactionTime,
        (long unsigned int)stats.lastTransactionTime,
        (long unsigned int)(stats.lastErrorTime > 0 ? millis() - stats.lastErrorTime : 0)
    );
}

// =============================================================================
// Callbacks Modbus
// =============================================================================

bool MyModbus::cbRead(Modbus::ResultCode event, uint16_t transactionId, void* data) {
    MyModbus& instance = getInstance();
    if (instance.suspended) return false;
    // Réponse TCP tardive d'une requête déjà abandonnée (le RTU passe toujours 0) : ignorée.
    if (transactionId != 0 && transactionId != instance.currentTcpTransaction) return false;

    instance.lastTransaction = millis();

    instance.showMessages("MyModbus::cbRead, will call processEvent");
    instance.processEvent(event);
    return (event == Modbus::EX_SUCCESS);
}

bool MyModbus::cbWrite(Modbus::ResultCode event, uint16_t transactionId, void* data) {
    MyModbus& instance = getInstance();
    if (instance.suspended) return false;
    if (transactionId != 0 && transactionId != instance.currentTcpTransaction) return false;

    instance.lastTransaction = millis();
    SlaveStats* slot = instance.findOrCreateSlaveSlot(instance.currentTransactionAddress);
    if (slot != nullptr) {
        slot->lastWriteTime = millis();
    }

    instance.showMessages("MyModbus::cbWrite, will call processEvent");
    instance.processEvent(event);
    return (event == Modbus::EX_SUCCESS);
}

// =============================================================================
// Traitement des événements
// =============================================================================

void MyModbus::processEvent(Modbus::ResultCode event) {
    lastEvent = event;
    stats.totalTransactions++;
    // La détection « bus RS485 mort » ne compte que le RTU : un équipement TCP injoignable
    // (robot éteint, réseau coupé) est suivi par ses propres statistiques d'adresse.
    const bool rtu = !isTcpAddress(currentTransactionAddress);
    if (rtu) transactionsThisRefresh++;

    if (event == Modbus::EX_SUCCESS) {
        stats.successfulTransactions++;
        stats.lastSuccessTime = millis();

        // Mise à jour stats par adresse (succès)
        updateSlaveStats(currentTransactionAddress, event);
        return;
    }

    // Erreur
    if (rtu) transactionErrorsThisRefresh++;

    // Mise à jour stats par adresse (erreur)
    updateSlaveStats(currentTransactionAddress, event);

    showMessages("MyModbus::processEvent: will call handleError");
    handleError(event);
}

void MyModbus::handleError(Modbus::ResultCode event) {
    stats.totalErrors++;
    stats.lastErrorCode = event;

    switch (event) {
        case Modbus::EX_TIMEOUT:         stats.timeoutErrors++;    break;
        case Modbus::EX_DATA_MISMACH:    stats.crcErrors++;        break;
        case Modbus::EX_CONNECTION_LOST: stats.connectionErrors++; break;
        case Modbus::EX_SLAVE_FAILURE:
        case Modbus::EX_GENERAL_FAILURE: stats.hardErrors++;       break;
        default: break;
    }

    if (modbusDeclaredDead) return;
}

// =============================================================================
// Messages d'erreur Modbus
// =============================================================================

void MyModbus::getModbusErrorMessage(Modbus::ResultCode event, char* message, size_t size) {
    #define preamb "Modbus error: "

    switch (event) {
        case Modbus::EX_ILLEGAL_FUNCTION:
            snprintf(message, size, "%s Function Code not Supported\r\n", preamb);   break;
        case Modbus::EX_ILLEGAL_ADDRESS:
            snprintf(message, size, "%s Output Address not exists\r\n", preamb);     break;
        case Modbus::EX_ILLEGAL_VALUE:
            snprintf(message, size, "%s Output Value not in Range\r\n", preamb);     break;
        case Modbus::EX_SLAVE_FAILURE:
            snprintf(message, size, "%s Slave Device Fails to process request\r\n", preamb); break;
        case Modbus::EX_TIMEOUT:
            snprintf(message, size, "%s Operation timeout\r\n", preamb);             break;
        case Modbus::EX_CONNECTION_LOST:
            snprintf(message, size, "%s Connection lost\r\n", preamb);               break;
        case Modbus::EX_DATA_MISMACH:
            snprintf(message, size, "%s Data mismatch/CRC error\r\n", preamb);       break;
        default:
            snprintf(message, size, "%s Unknown error\r\n", preamb);                 break;
    }
}

bool MyModbus::shouldBeVerbose() const {
    BorneUniverselle* bu = BorneUniverselle::getInstance();
    return (bu != nullptr && bu->isClientConnected());
}

// =============================================================================
// Suspension / Reprise
// =============================================================================

void MyModbus::suspend() {
    if (shouldBeVerbose() && suspended) {
        Serial.println("MyModbus::suspend - already suspended");
        return;
    }
    
    // ✅ NOUVEAU : Attendre la fin de toute transaction en cours
    uint32_t start = millis();
    uint32_t timeout = 500; // 500ms max pour attendre
    
    while (modbus && modbus->slave() != 0 && (millis() - start < timeout)) {
        modbus->task();  // Laisser la transaction se terminer
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    if (modbus && modbus->slave() != 0) {
        Serial.printf("⚠️ MyModbus::suspend - Transaction encore active après %lums (slave=%d)\n",
                     (long unsigned int)timeout, modbus->slave());
    }
    
    suspended = true;
    if (shouldBeVerbose()) {
        Serial.printf("%lu:: MyModbus SUSPENDED\r\n", millis());
    }
}

void MyModbus::resume() {
    if (!suspended && shouldBeVerbose()) {
        Serial.println("⚠️ MyModbus::resume - not suspended");
    }

    // Drainer toute transaction résiduelle AVANT de lever la suspension,
    // pour qu'aucune nouvelle transaction ne parte pendant le nettoyage.
    // Motif identique à suspend(). Budget borné (couvre le pire cas timeout lib).
    uint32_t start = millis();
    uint32_t timeout = 500; // 500ms max, comme suspend()

    while (modbus && modbus->slave() != 0 && (millis() - start < timeout)) {
        modbus->task();  // Laisser la transaction résiduelle se terminer
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (modbus && modbus->slave() != 0) {
        Serial.printf("⚠️ MyModbus::resume - Transaction résiduelle après %lums (slave=%d)\r\n",
                     (long unsigned int)timeout, modbus->slave());
    }

    // Réinitialiser le timestamp de dernière transaction : repartir le
    // compteur DELAY_TRANSACTION à neuf, pas de conflit avec l'avant-pause.
    lastTransaction = millis();
    // Réinitialiser lastEvent : état neutre, pas un timeout résiduel.
    lastEvent = Modbus::EX_SUCCESS;

    suspended = false;

    // Réinitialiser le timestamp de dernière transaction pour éviter les conflits
    lastTransaction = millis();
    // Réinitialiser lastEvent pour partir sur des bases saines
    lastEvent = Modbus::EX_SUCCESS;

    // Sauvegarder les stats par adresse avant reset
    SlaveStats savedSlaves[MAX_SLAVES];
    memcpy(savedSlaves, stats.slaveStats, sizeof(savedSlaves));

    resetStats();

    // Restaurer les stats par adresse (consecutiveErrors, throttle, etc.)
    memcpy(stats.slaveStats, savedSlaves, sizeof(savedSlaves));
}

// =============================================================================
// Initialisation
// =============================================================================

void MyModbus::setInitialized(bool initialized) {
    modbusInitialized = initialized;
    if (initialized) {
        Serial.println("MyModbus marque comme initialise");
    } else {
        Serial.println("MyModbus marque comme NON initialise");
    }
}

bool MyModbus::isModbusDead() const {
    return modbusDeclaredDead;
}

// =============================================================================
// Santé et récupération automatique
// =============================================================================

void MyModbus::checkModbusHealthAndRecover() {
    if (!modbusInitialized) return;

    uint32_t now = millis();

    // ---- TRANSITION : MORT → VIVANT ----
    // Un seul esclave qui repond suffit pour lever la mort globale
    if (modbusDeclaredDead && anySlaveRecovered()) {
        modbusDeclaredDead = false;

        Serial.println("╔═══════════════════════════════════════════════╗");
        Serial.println("║       MODBUS RECUPERE                         ║");
        Serial.println("╚═══════════════════════════════════════════════╝");
        Serial.printf("  Tentatives necessaires: %lu\n", recoveryAttemptCount);

        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg),
            "MODBUS RECUPERE apres %lu tentatives", recoveryAttemptCount);
        PLC_Tools::logDiagnostic(logMsg);

        PLC_Tools::sendPeriodicMessage(false, ERROR, MSG_BUS_MODBUS_MORT);

        BorneUniverselle::prepareMessage(SUCCESS, "Communication Modbus retablie");
        recoveryAttemptCount = 0;
    }

    // ---- ÉTAT STABLE "MORT" : tentatives de récupération ----
    else if (modbusDeclaredDead) {
        if (now - lastRecoveryAttempt >= RECOVERY_ATTEMPT_INTERVAL) {
            recoveryAttemptCount++;
            lastRecoveryAttempt = now;

            Serial.println("╔═══════════════════════════════════════════════╗");
            Serial.printf("║  TENTATIVE RECUP #%2lu (auto)                  ║\n", recoveryAttemptCount);
            Serial.println("╚═══════════════════════════════════════════════╝");
            Serial.printf("  Duree panne: %lu s\n", getTimeSinceLastSuccess() / 1000);
            Serial.printf("  Erreurs totales: %lu\n", stats.totalErrors);

            PLC_Tools::sendPeriodicMessage(true, ERROR, MSG_BUS_MODBUS_MORT);
        }
    }
}

uint32_t MyModbus::getTimeSinceLastSuccess() const {
    if (stats.lastSuccessTime == 0) return 0;
    uint32_t now = millis();
    if (now < stats.lastSuccessTime) {
        return (UINT32_MAX - stats.lastSuccessTime) + now;
    }
    return now - stats.lastSuccessTime;
}

void MyModbus::forceModbusDead() {
    if (modbusDeclaredDead) return;

    uint32_t now = millis();
    modbusDeclaredDead          = true;
    lastRecoveryAttempt         = now;
    recoveryAttemptCount        = 0;

    // Les adresses mortes (>= 10 erreurs) gerent leur propre throttle de recuperation
    // Pas d initialisation ici : chaque adresse a deja son lastRecoveryAttemptTime

    Serial.println("╔═══════════════════════════════════════════════╗");
    Serial.println("║   MODBUS FORCE MORT (100% echecs)             ║");
    Serial.println("╚═══════════════════════════════════════════════╝");
    Serial.printf("  Detection: Taux d'echec 100%% dans refresh()\n");

    PLC_Tools::logDiagnostic("MODBUS MORT: Detection par taux d'echec 100%");

    BorneUniverselle::prepareMessage(ERROR,
        "BUS MODBUS MORT - Communication perdue\n"
        "Verifier cablage RS485 et alimentations\n"
        "Tentatives de recuperation automatiques toutes les 30s");
}

// =============================================================================
// Statistiques par adresse esclave (Étape 1)
// =============================================================================

MyModbus::SlaveStats* MyModbus::findOrCreateSlaveSlot(uint16_t address) {
    if (address == 0) return nullptr;

    for (uint8_t i = 0; i < MAX_SLAVES; i++) {
        if (stats.slaveStats[i].address == address) {
            return &stats.slaveStats[i];
        }
    }

    for (uint8_t i = 0; i < MAX_SLAVES; i++) {
        if (stats.slaveStats[i].address == 0) {
            stats.slaveStats[i].address = address;
            stats.slaveStats[i].lastRecoveryAttemptTime = millis();
            return &stats.slaveStats[i];
        }
    }

    Serial.printf("MyModbus::findOrCreateSlaveSlot: MAX_SLAVES atteint\r\n");
    return nullptr;
}

void MyModbus::updateSlaveStats(uint16_t address, Modbus::ResultCode event) {
    SlaveStats* slot = findOrCreateSlaveSlot(address);
    if (slot == nullptr) return;

    uint32_t now = millis();

    if (event == Modbus::EX_SUCCESS) {
        // Si l'adresse etait morte : eteindre son message periodique
        if (slot->consecutiveErrors >= ERROR_THRESHOLD_PER_SLAVE) {
            char who[48];
            describeAddress(slot->address, who, sizeof(who));
            char offMsg[128];
            snprintf(offMsg, sizeof(offMsg), MSG_FMT_PERIPHERIQUE_INJOIGNABLE, who);
            PLC_Tools::sendPeriodicMessage(false, WARNING, offMsg);

            char recovMsg[80];
            snprintf(recovMsg, sizeof(recovMsg),
                "MODBUS addr:%u RETABLI apres %lu erreurs",
                address, (unsigned long)slot->consecutiveErrors);
            Serial.printf("✅ %s\r\n", recovMsg);
            PLC_Tools::logDiagnostic(recovMsg);
        }
        slot->consecutiveErrors       = 0;
        slot->lastSuccessTime         = now;
        slot->lastRecoveryAttemptTime = now;
        slot->recoveryAttemptsLeft    = 3;

    } else {
        slot->consecutiveErrors++;
        slot->lastErrorTime = now;
        slot->lastErrorCode = event;

        // Alerte utilisateur au franchissement du seuil (une seule fois)
        if (slot->consecutiveErrors == ERROR_THRESHOLD_PER_SLAVE) {
            char errMsg[50];
            getModbusErrorMessage(slot->lastErrorCode, errMsg, sizeof(errMsg));

            char alertMsg[128];
            snprintf(alertMsg, sizeof(alertMsg),
                "MODBUS addr:%u MORT apres %lu erreurs consecutives (%s)",
                (unsigned)slot->address,
                (unsigned long)slot->consecutiveErrors,
                errMsg);

            if (messageCallback) messageCallback(WARNING, alertMsg);
            PLC_Tools::logDiagnostic(alertMsg);
            slot->lastRecoveryAttemptTime = now;
            slot->recoveryAttemptsLeft    = 0;
            Serial.printf("Throttle ARME addr:%u apres %lu erreurs\r\n",
                (unsigned)slot->address, (unsigned long)slot->consecutiveErrors);
        }

        // Message periodique continu tant que le slave ne repond pas
        if (slot->consecutiveErrors >= ERROR_THRESHOLD_PER_SLAVE) {
            // Au franchissement exact du seuil : purge les messages periodiques resolus/obsoletes
            // pour liberer des slots, SANS effacer les erreurs actives (preserveActiveErrors=true)
            if (slot->consecutiveErrors == ERROR_THRESHOLD_PER_SLAVE) {
                PLC_Tools::clearPeriodicMessages();
            }
            char who[48];
            describeAddress(slot->address, who, sizeof(who));
            char periodicMsg[128];
            snprintf(periodicMsg, sizeof(periodicMsg), MSG_FMT_PERIPHERIQUE_INJOIGNABLE, who);
            PLC_Tools::sendPeriodicMessage(true, WARNING, periodicMsg);
        }
    }
}

const MyModbus::SlaveStats* MyModbus::getSlaveStats(uint16_t address) const {
    for (uint8_t i = 0; i < MAX_SLAVES; i++) {
        if (stats.slaveStats[i].address == address) {
            return &stats.slaveStats[i];
        }
    }
    return nullptr;
}

void MyModbus::getSlaveReport(uint16_t address, char* buffer, size_t size) const {
    const SlaveStats* s = getSlaveStats(address);
    if (s == nullptr) {
        snprintf(buffer, size, "Addr %u: aucune statistique disponible", address);
        return;
    }

    uint32_t now          = millis();
    uint32_t sinceLastErr = (s->lastErrorTime   > 0) ? now - s->lastErrorTime   : 0;
    uint32_t sinceLastOk  = (s->lastSuccessTime > 0) ? now - s->lastSuccessTime : 0;

    snprintf(buffer, size,
        "Addr %u: consec=%lu lastErr=%lums ago lastOk=%lums ago",
        (unsigned)s->address,
        (unsigned long)s->consecutiveErrors,
        (unsigned long)sinceLastErr,
        (unsigned long)sinceLastOk
    );
}

// Retourne vrai si au moins un esclave a reussi (consecutiveErrors == 0)
bool MyModbus::anySlaveRecovered() const {
    for (uint8_t i = 0; i < MAX_SLAVES; i++) {
        const SlaveStats& s = stats.slaveStats[i];
        if (s.address == 0) continue;
        if (s.consecutiveErrors == 0 && s.lastSuccessTime > 0) {
            return true;
        }
    }
    return false;
}

// Retourne vrai si l adresse a consecutiveErrors >= seuil
bool MyModbus::isSlaveConsideredDead(uint16_t address) const {
    const SlaveStats* s = getSlaveStats(address);
    if (s == nullptr) return false;
    return s->consecutiveErrors >= ERROR_THRESHOLD_PER_SLAVE;
}

// Retourne vrai si toutes les adresses connues ont consecutiveErrors >= seuil
// → condition pour declarer le bus mort globalement
bool MyModbus::allActiveSlavesDead() const {
    bool anyKnown = false;
    for (uint8_t i = 0; i < MAX_SLAVES; i++) {
        const SlaveStats& s = stats.slaveStats[i];
        if (s.address == 0) continue;
        anyKnown = true;
        if (s.consecutiveErrors < ERROR_THRESHOLD_PER_SLAVE) {
            return false;  // au moins une adresse encore vivante
        }
    }
    return anyKnown;  // true seulement si au moins une adresse connue et toutes mortes
}

bool MyModbus::executeWithRetry(std::function<bool()> refreshFn, uint16_t mbAddr, const char* nodeName) {
    bool success = refreshFn();
    if (success) return true;

    if (mbAddr == 0 || isSlaveConsideredDead(mbAddr)) return false;

    uint32_t retryStart = millis();
    int attempt = 0;

    while (!success
        && attempt < MODBUS_MAX_RETRIES
        && (millis() - retryStart) < MODBUS_RETRY_TIMEOUT_MS) {

        vTaskDelay(pdMS_TO_TICKS(MODBUS_RETRY_DELAY_MS));
        success = refreshFn();
        attempt++;
    }

    if (success) {
        char retryMsg[128];
        snprintf(retryMsg, sizeof(retryMsg),
            "MODBUS retry OK addr:%u %s (attempt %d)", mbAddr, nodeName, attempt);
        Serial.printf("✅ %s\r\n", retryMsg);
        PLC_Tools::logDiagnostic(retryMsg);
    }

    return success;
}

// =============================================================================
// Modbus TCP (client)
// =============================================================================

uint16_t MyModbus::registerTcpDevice(IPAddress ip, uint16_t port, uint8_t unitId) {
    if (port == 0) port = MODBUSTCP_PORT;
    for (uint8_t i = 0; i < MAX_TCP_DEVICES; i++) {
        TcpDevice& d = tcpDevices[i];
        if (d.used && d.ip == ip && d.port == port && d.unit == unitId) {
            return TCP_ADDRESS_BASE + i;
        }
    }
    for (uint8_t i = 0; i < MAX_TCP_DEVICES; i++) {
        TcpDevice& d = tcpDevices[i];
        if (d.used) continue;
        if (tcp == nullptr) {
            tcp = new ModbusTCP();
            tcp->client();
            Serial.println("Modbus TCP : client initialise");
        }
        d.used = true;
        d.ip = ip;
        d.port = port;
        d.unit = unitId;
        d.lastConnectAttempt = 0;
        Serial.printf("Modbus TCP : equipement %s:%u unite %u -> adresse interne %u\r\n",
                      ip.toString().c_str(), port, unitId, (unsigned)(TCP_ADDRESS_BASE + i));
        return TCP_ADDRESS_BASE + i;
    }
    Serial.printf("Modbus TCP : table pleine (%u equipements)\r\n", MAX_TCP_DEVICES);
    return 0;
}

MyModbus::TcpDevice* MyModbus::tcpDevice(uint16_t address) {
    if (!isTcpAddress(address)) return nullptr;
    uint16_t i = address - TCP_ADDRESS_BASE;
    if (i >= MAX_TCP_DEVICES || !tcpDevices[i].used) return nullptr;
    return &tcpDevices[i];
}

void MyModbus::describeAddress(uint16_t address, char* buffer, size_t size) const {
    if (isTcpAddress(address)) {
        uint16_t i = address - TCP_ADDRESS_BASE;
        if (i < MAX_TCP_DEVICES && tcpDevices[i].used) {
            const TcpDevice& d = tcpDevices[i];
            snprintf(buffer, size, "TCP %u.%u.%u.%u:%u unite %u", d.ip[0], d.ip[1], d.ip[2], d.ip[3], d.port, d.unit);
            return;
        }
    }
    snprintf(buffer, size, "addr %u", (unsigned)address);
}

// Connexion à l'équipement : une tentative au plus toutes les TCP_RECONNECT_MS
// (la connexion bloque jusqu'à MODBUSIP_CONNECT_TIMEOUT si l'équipement ne répond pas).
bool MyModbus::tcpPrepare(uint16_t address) {
    TcpDevice* d = tcpDevice(address);
    if (tcp == nullptr || d == nullptr) {
        lastEvent = Modbus::EX_GENERAL_FAILURE;
        return false;
    }
    if (tcp->isConnected(d->ip)) {
        return true;
    }
    uint32_t now = millis();
    bool attempt = (d->lastConnectAttempt == 0) || (now - d->lastConnectAttempt >= TCP_RECONNECT_MS);
    if (attempt) {
        d->lastConnectAttempt = now;
        if (tcp->connect(d->ip, d->port)) {
            Serial.printf("Modbus TCP : connecte a %s:%u\r\n", d->ip.toString().c_str(), d->port);
            return true;
        }
    }
    // Non connecté : compté comme une erreur de l'équipement (message périodique au-delà du seuil).
    lastEvent = Modbus::EX_CONNECTION_LOST;
    if (attempt) {
        stats.totalTransactions++;
        updateSlaveStats(address, Modbus::EX_CONNECTION_LOST);
        handleError(Modbus::EX_CONNECTION_LOST);
    }
    return false;
}

void MyModbus::startTcpTransaction(uint16_t transactionId) {
    currentTcpTransaction = transactionId;
    if (transactionId == 0) {
        // Requête refusée par la bibliothèque (connexion perdue entre-temps, file pleine)
        lastEvent = Modbus::EX_CONNECTION_LOST;
    }
}

bool MyModbus::waitEndTcpTransaction() {
    if (tcp == nullptr || currentTcpTransaction == 0) {
        return false;
    }
    uint32_t start = millis();
    while (tcp->isTransaction(currentTcpTransaction) && millis() - start < tcpTimeout) {
        tcp->task();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (tcp->isTransaction(currentTcpTransaction)) {
        // Pas de réponse dans le délai : la bibliothèque appellera le callback (timeout)
        // plus tard ; on rend la main avec une erreur sans attendre.
        lastEvent = Modbus::EX_TIMEOUT;
        stats.totalTransactions++;
        updateSlaveStats(currentTransactionAddress, Modbus::EX_TIMEOUT);
        handleError(Modbus::EX_TIMEOUT);
        lastTransaction = millis();
        currentTcpTransaction = 0;
        return false;
    }
    currentTcpTransaction = 0;
    stats.lastTransactionTime = millis() - start;
    if (lastEvent == Modbus::EX_SUCCESS) {
        stats.lastSuccessTime = millis();
        if (stats.lastTransactionTime > stats.maxTransactionTime) {
            stats.maxTransactionTime = stats.lastTransactionTime;
        }
        return true;
    }
    return false;
}

void MyModbus::task() {
    if (modbus != nullptr) modbus->task();
    if (tcp != nullptr) tcp->task();
}

// Aiguillage des requêtes : RTU (adresse = n° d'esclave) ou TCP (adresse interne).
#define MYMODBUS_DISPATCH(rtuCall, tcpCall)                                   \
    if (isTcpAddress(address)) {                                              \
        TcpDevice* d = tcpDevice(address);                                    \
        if (tcp == nullptr || d == nullptr) { lastEvent = Modbus::EX_GENERAL_FAILURE; return 0; } \
        uint16_t id = tcpCall;                                                \
        startTcpTransaction(id);                                              \
        return id;                                                            \
    }                                                                         \
    if (modbus == nullptr) { lastEvent = Modbus::EX_GENERAL_FAILURE; return 0; } \
    return rtuCall;

uint16_t MyModbus::readCoil(uint16_t address, uint16_t offset, bool* value, uint16_t count, cbTransaction cb) {
    MYMODBUS_DISPATCH(modbus->readCoil((uint8_t)address, offset, value, count, cb),
                      tcp->readCoil(d->ip, offset, value, count, cb, d->unit))
}

uint16_t MyModbus::readIsts(uint16_t address, uint16_t offset, bool* value, uint16_t count, cbTransaction cb) {
    MYMODBUS_DISPATCH(modbus->readIsts((uint8_t)address, offset, value, count, cb),
                      tcp->readIsts(d->ip, offset, value, count, cb, d->unit))
}

uint16_t MyModbus::readHreg(uint16_t address, uint16_t offset, uint16_t* value, uint16_t count, cbTransaction cb) {
    MYMODBUS_DISPATCH(modbus->readHreg((uint8_t)address, offset, value, count, cb),
                      tcp->readHreg(d->ip, offset, value, count, cb, d->unit))
}

uint16_t MyModbus::readIreg(uint16_t address, uint16_t offset, uint16_t* value, uint16_t count, cbTransaction cb) {
    MYMODBUS_DISPATCH(modbus->readIreg((uint8_t)address, offset, value, count, cb),
                      tcp->readIreg(d->ip, offset, value, count, cb, d->unit))
}

uint16_t MyModbus::writeCoil(uint16_t address, uint16_t offset, bool value, cbTransaction cb) {
    MYMODBUS_DISPATCH(modbus->writeCoil((uint8_t)address, offset, value, cb),
                      tcp->writeCoil(d->ip, offset, value, cb, d->unit))
}

uint16_t MyModbus::writeCoil(uint16_t address, uint16_t offset, bool* values, uint16_t count, cbTransaction cb) {
    MYMODBUS_DISPATCH(modbus->writeCoil((uint8_t)address, offset, values, count, cb),
                      tcp->writeCoil(d->ip, offset, values, count, cb, d->unit))
}

uint16_t MyModbus::writeHreg(uint16_t address, uint16_t offset, uint16_t value, cbTransaction cb) {
    MYMODBUS_DISPATCH(modbus->writeHreg((uint8_t)address, offset, value, cb),
                      tcp->writeHreg(d->ip, offset, value, cb, d->unit))
}

uint16_t MyModbus::writeHreg(uint16_t address, uint16_t offset, uint16_t* values, uint16_t count, cbTransaction cb) {
    MYMODBUS_DISPATCH(modbus->writeHreg((uint8_t)address, offset, values, count, cb),
                      tcp->writeHreg(d->ip, offset, values, count, cb, d->unit))
}

#undef MYMODBUS_DISPATCH

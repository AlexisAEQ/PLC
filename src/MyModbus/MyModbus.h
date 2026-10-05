#pragma once
#include <ModbusRTU.h>
//#include "esp_task_wdt.h"
#include "PLC_CommonTypes/PLC_CommonTypes.h"
#include "PLC_Tools/PLC_Tools.h"

// ATTENTION !!! CETTE CLASSE EST UN SINGLETON !!!!

#define MIN_TIMEOUT          10    // 10ms minimum
#define MAX_TIMEOUT          1000  // 1s maximum
#define DEFAULT_TIMEOUT      100   // 100 ms par défaut
#define DELAY_TRANSACTION    5    // délai minimum entre transactions
#define DELAY_AFTER_WRITE    0    // Confirmé par analyse (juillet 2026) : aucun effet mesurable sur le taux d'erreur, voir historique git.
#define RECOVERY_ATTEMPT_INTERVAL 5000  // 5 secondes entre tentatives de récupération

#define MODBUS_MAX_RETRIES     5
#define MODBUS_RETRY_DELAY_MS  5
#define MODBUS_RETRY_TIMEOUT_MS 600

#define MSG_BUS_MODBUS_MORT "BUS MODBUS MORT - Communication perdue\n"\
                            "Verifier cablage RS485 et alimentations\n" \
                            "Tentatives  automatiques et périodiques de récupération"

#define MSG_FMT_PERIPHERIQUE_INJOIGNABLE \
    "Peripherique Modbus addr %u injoignable - verifier alimentation et cablage"

class MyModbus {
public:

    // -------------------------------------------------------------------------
    // Statistiques par adresse esclave
    // Allouées dans ModbusStats::slaveStats[MAX_SLAVES]
    // Un slot est libre si address == 0
    // -------------------------------------------------------------------------
    struct SlaveStats {
        uint8_t  address                 = 0;   // 0 = slot libre
        uint32_t consecutiveErrors       = 0;
        uint32_t lastErrorTime           = 0;
        uint32_t lastSuccessTime         = 0;
        uint32_t lastRecoveryAttemptTime = 0;   // throttle 30s par adresse
        uint8_t  recoveryAttemptsLeft    = 0;   // tentatives restantes dans la fenetre
        uint32_t lastWriteTime           = 0;
        Modbus::ResultCode lastErrorCode = Modbus::EX_SUCCESS;
    };

    static constexpr uint8_t MAX_SLAVES = 8;

    // -------------------------------------------------------------------------
    // Statistiques globales du bus Modbus
    // -------------------------------------------------------------------------
    struct ModbusStats {
        uint32_t crcErrors                  = 0;
        uint32_t timeoutErrors              = 0;
        uint32_t connectionErrors           = 0;
        uint32_t hardErrors                 = 0;
        uint32_t totalErrors                = 0;
        uint32_t successfulTransactions     = 0;
        uint32_t totalTransactions          = 0;
        uint32_t lastErrorTime              = 0;
        uint32_t lastSuccessTime            = 0;
        uint32_t maxTransactionTime         = 0;
        uint32_t lastTransactionTime        = 0;
        Modbus::ResultCode lastErrorCode    = Modbus::EX_SUCCESS;
        uint32_t lastErrorNotificationTime  = 0;   // timestamp dernière notification
        uint32_t errorsSinceLastNotification = 0;  // erreurs non encore notifiées
        // Statistiques par adresse esclave (Étape 1)
        SlaveStats slaveStats[8];
    };

    static constexpr uint8_t  ERROR_THRESHOLD_PER_SLAVE    = 10;   // seuil alerte par adresse
    

    // Supprimer la possibilité de copier l'instance
    MyModbus(const MyModbus&) = delete;
    void operator=(const MyModbus&) = delete;

    static MyModbus& getInstance() {
        static MyModbus instance;
        return instance;
    }

    // -------------------------------------------------------------------------
    // Configuration Modbus
    // -------------------------------------------------------------------------
    void setModbus(ModbusRTU *_modbus);
    ModbusRTU *getModbus() { return modbus; }

    // -------------------------------------------------------------------------
    // Gestion du timeout
    // -------------------------------------------------------------------------
    void setTimeout(uint32_t ms);
    uint32_t getTimeout();
    void resetTimeout();

    // -------------------------------------------------------------------------
    // Gestion du bus Modbus
    // -------------------------------------------------------------------------
    bool requestTransaction(uint8_t slaveAddress = 0);
    bool waitEndTransaction();
    bool isModbusFree();
    void waitUntilFree(uint32_t timeoutMs = 100);
    Modbus::ResultCode getLastEvent();

    // -------------------------------------------------------------------------
    // Gestion des statistiques
    // -------------------------------------------------------------------------
    const ModbusStats& getStats();
    void resetStats();
    void getErrorReport(char* buffer, size_t size);

    // Statistiques par adresse esclave
    const SlaveStats* getSlaveStats(uint8_t address) const;
    void getSlaveReport(uint8_t address, char* buffer, size_t size) const;
    bool isSlaveConsideredDead(uint8_t address) const;  // >= 10 erreurs consecutives
    bool allActiveSlavesDead() const;                   // toutes les adresses mortes

    static bool cbRead(Modbus::ResultCode event, uint16_t transactionId, void* data);
    static bool cbWrite(Modbus::ResultCode event, uint16_t transactionId, void* data);

    // -------------------------------------------------------------------------
    // Messages d'erreurs
    // -------------------------------------------------------------------------
    typedef void (*MessageCallback)(uint8_t severity, const char* message);
    void setShowMessages(bool status);
    void getModbusErrorMessage(Modbus::ResultCode event, char* message, size_t size);

    void setMessageCallback(MessageCallback callback) {
        messageCallback = callback;
    }

    // -------------------------------------------------------------------------
    // Suspension / Reprise
    // -------------------------------------------------------------------------

    /**
     * @brief Suspend les opérations Modbus.
     * Pendant la suspension :
     * - Aucune transaction ne sera initiée
     * - waitEndTransaction() retourne immédiatement false
     * - Les tentatives de récupération sont désactivées
     */
    void suspend();

    /**
     * @brief Reprend les opérations Modbus.
     * Après la reprise :
     * - Les transactions reprennent normalement
     * - Les statistiques sont réinitialisées pour un démarrage propre
     */
    void resume();

    /** @return true si Modbus est suspendu */
    bool isSuspended() const { return suspended; }

    // -------------------------------------------------------------------------
    // Initialisation et santé du bus
    // -------------------------------------------------------------------------

    /**
     * @brief Informe MyModbus qu'il a été initialisé.
     * Appelé par BorneUniverselle::modbusInit() après succès.
     */
    void setInitialized(bool initialized);

    /** @return true si Modbus est configuré et initialisé */
    bool isInitialized() const { return modbusInitialized; }

    /**
     * @brief Vérifie si le bus Modbus est mort (SEULEMENT si initialisé).
     * @return true si Modbus initialisé ET mort, false sinon
     */
    bool isModbusDead() const;

    // -------------------------------------------------------------------------
    // Compteurs par cycle de refresh
    // -------------------------------------------------------------------------
    void resetRefreshCounters() {
        transactionsThisRefresh = 0;
        transactionErrorsThisRefresh = 0;
    }

    uint32_t getTransactionsThisRefresh() const {
        return transactionsThisRefresh;
    }

    uint32_t getTransactionErrorsThisRefresh() const {
        return transactionErrorsThisRefresh;
    }

    // -------------------------------------------------------------------------
    // Récupération automatique
    // -------------------------------------------------------------------------

    /**
     * @brief Vérification santé + tentatives de récupération.
     * NE FAIT RIEN si Modbus pas initialisé.
     */
    void checkModbusHealthAndRecover();

    uint32_t getTimeSinceLastSuccess() const;

    void forceModbusDead();

    void resetLastEvent() { lastEvent = Modbus::EX_SUCCESS; }

    uint32_t getLastTransactionTime() const { return lastTransaction; }
    uint8_t  getLastTransactionAddress() const { return currentTransactionAddress; }

    bool executeWithRetry(std::function<bool()> refreshFn, uint8_t mbAddr, const char* nodeName); 

protected:

private:
    MyModbus(); // Constructeur privé - singleton

    ModbusRTU *modbus;
    uint32_t lastTransaction;
    Modbus::ResultCode lastEvent;
    uint32_t lastProblem;

    // ===== Variante 2 : refus rapide si bus connu bloqué (fil cascade REFUSEE) =====
    uint32_t busStuckSince   = 0;  // 0 = bus non marqué bloqué
    uint8_t  busStuckAddress = 0;  // adresse qui occupait le bus au moment du blocage
    uint32_t lastRefusLogTime = 0; // throttle du log REFUS_RAPIDE

    bool suspended = false;

    ModbusStats stats;
    uint32_t transactionTimeout;

    void processEvent(Modbus::ResultCode event);
    void handleError(Modbus::ResultCode event);

    bool isShowMessages;
    void showMessages(const char *text);

    MessageCallback messageCallback;

    bool shouldBeVerbose() const;
   
    // Détection bus mort
    bool modbusInitialized            = false;
    bool modbusDeclaredDead           = false;
    uint32_t lastRecoveryAttempt      = 0;
    uint32_t recoveryAttemptCount     = 0;

    uint32_t transactionsThisRefresh      = 0;
    uint32_t transactionErrorsThisRefresh = 0;
    uint8_t currentTransactionAddress = 0;  // adresse de la transaction en cours
    uint32_t transactionRequestStart  = 0;  // timestamp début requestTransaction
    uint32_t transactionWaitStart     = 0;  // timestamp début waitEndTransaction
    uint32_t currentTxStart           = 0;  // timestamp début transaction (request ou wait)


    // -------------------------------------------------------------------------
    // Statistiques par adresse esclave (Étape 1)
    // -------------------------------------------------------------------------
    void updateSlaveStats(uint8_t address, Modbus::ResultCode event);
    SlaveStats* findOrCreateSlaveSlot(uint8_t address);
    bool anySlaveRecovered() const;  // vrai si au moins un esclave a reussi depuis la mort
    static constexpr uint32_t ERROR_NOTIFICATION_INTERVAL  = 5000; // 5 secondes

};

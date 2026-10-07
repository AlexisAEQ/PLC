#pragma once
#include <ModbusRTU.h>
#include <ModbusTCP.h>
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
    "Peripherique Modbus %s injoignable - verifier alimentation et cablage"

class MyModbus {
public:

    // -------------------------------------------------------------------------
    // Statistiques par adresse esclave
    // Allouées dans ModbusStats::slaveStats[MAX_SLAVES]
    // Un slot est libre si address == 0
    // -------------------------------------------------------------------------
    struct SlaveStats {
        uint16_t address                 = 0;   // 0 = slot libre (RTU 1-247, TCP >= TCP_ADDRESS_BASE)
        uint32_t consecutiveErrors       = 0;
        uint32_t lastErrorTime           = 0;
        uint32_t lastSuccessTime         = 0;
        uint32_t lastRecoveryAttemptTime = 0;   // throttle 30s par adresse
        uint8_t  recoveryAttemptsLeft    = 0;   // tentatives restantes dans la fenetre
        uint32_t lastWriteTime           = 0;
        Modbus::ResultCode lastErrorCode = Modbus::EX_SUCCESS;
    };

    static constexpr uint8_t MAX_SLAVES = 16;

    // -------------------------------------------------------------------------
    // Modbus TCP (client) : chaque équipement TCP (IP, port, n° d'unité) reçoit une
    // adresse interne >= TCP_ADDRESS_BASE, utilisée par les nœuds comme une adresse
    // d'esclave RTU. Les requêtes passent alors par le client TCP (WiFi ou Ethernet).
    // -------------------------------------------------------------------------
    static constexpr uint16_t TCP_ADDRESS_BASE   = 1000;
    static constexpr uint8_t  MAX_TCP_DEVICES    = 16;
    static constexpr uint32_t TCP_RECONNECT_MS   = 3000;   // délai entre deux tentatives de connexion
    static constexpr uint32_t DEFAULT_TCP_TIMEOUT = 500;   // ms, réponse d'une requête TCP

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
        SlaveStats slaveStats[MAX_SLAVES];
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
    bool requestTransaction(uint16_t slaveAddress = 0);
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
    const SlaveStats* getSlaveStats(uint16_t address) const;
    void getSlaveReport(uint16_t address, char* buffer, size_t size) const;
    bool isSlaveConsideredDead(uint16_t address) const;  // >= 10 erreurs consecutives

    // ---- Modbus TCP ----
    // Déclare un équipement TCP ; retourne son adresse interne (0 si la table est pleine).
    uint16_t registerTcpDevice(IPAddress ip, uint16_t port, uint8_t unitId);
    static bool isTcpAddress(uint16_t address) { return address >= TCP_ADDRESS_BASE; }
    bool hasTcpDevices() const { return tcp != nullptr; }
    void setTcpTimeout(uint32_t ms) { tcpTimeout = ms; }
    // Libellé lisible d'une adresse : « addr 3 » ou « TCP 192.168.1.50:502 unite 1 ».
    void describeAddress(uint16_t address, char* buffer, size_t size) const;

    // ---- Requêtes (RTU ou TCP selon l'adresse) ----
    // Identifiant de transaction, 0 si la requête n'a pas pu être lancée (lastEvent renseigné).
    uint16_t readCoil(uint16_t address, uint16_t offset, bool* value, uint16_t count, cbTransaction cb);
    uint16_t readIsts(uint16_t address, uint16_t offset, bool* value, uint16_t count, cbTransaction cb);
    uint16_t readHreg(uint16_t address, uint16_t offset, uint16_t* value, uint16_t count, cbTransaction cb);
    uint16_t readIreg(uint16_t address, uint16_t offset, uint16_t* value, uint16_t count, cbTransaction cb);
    uint16_t writeCoil(uint16_t address, uint16_t offset, bool value, cbTransaction cb);
    uint16_t writeCoil(uint16_t address, uint16_t offset, bool* values, uint16_t count, cbTransaction cb);
    uint16_t writeHreg(uint16_t address, uint16_t offset, uint16_t value, cbTransaction cb);
    uint16_t writeHreg(uint16_t address, uint16_t offset, uint16_t* values, uint16_t count, cbTransaction cb);
    void task();   // fait avancer les transactions RTU et TCP
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
    uint16_t getLastTransactionAddress() const { return currentTransactionAddress; }

    bool executeWithRetry(std::function<bool()> refreshFn, uint16_t mbAddr, const char* nodeName); 

protected:

private:
    MyModbus(); // Constructeur privé - singleton

    ModbusRTU *modbus;

    // ===== Modbus TCP =====
    struct TcpDevice {
        IPAddress ip;
        uint16_t  port = 502;
        uint8_t   unit = 1;
        bool      used = false;
        uint32_t  lastConnectAttempt = 0;
    };
    TcpDevice tcpDevices[MAX_TCP_DEVICES];
    ModbusTCP* tcp = nullptr;
    uint16_t currentTcpTransaction = 0;      // transaction TCP en cours (0 = aucune)
    uint32_t tcpTimeout = DEFAULT_TCP_TIMEOUT;
    TcpDevice* tcpDevice(uint16_t address);
    bool tcpPrepare(uint16_t address);       // connexion (avec délai entre tentatives)
    bool waitEndTcpTransaction();
    void startTcpTransaction(uint16_t transactionId);

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
    uint16_t currentTransactionAddress = 0; // adresse de la transaction en cours
    uint32_t transactionRequestStart  = 0;  // timestamp début requestTransaction
    uint32_t transactionWaitStart     = 0;  // timestamp début waitEndTransaction
    uint32_t currentTxStart           = 0;  // timestamp début transaction (request ou wait)


    // -------------------------------------------------------------------------
    // Statistiques par adresse esclave (Étape 1)
    // -------------------------------------------------------------------------
    void updateSlaveStats(uint16_t address, Modbus::ResultCode event);
    SlaveStats* findOrCreateSlaveSlot(uint16_t address);
    bool anySlaveRecovered() const;  // vrai si au moins un esclave a reussi depuis la mort
    static constexpr uint32_t ERROR_NOTIFICATION_INTERVAL  = 5000; // 5 secondes

};

#ifndef BORNE_UNIVERSELLE_LIB_H
#define BORNE_UNIVERSELLE_LIB_H

#define ARDUINOJSON_ENABLE_COMMENTS 1

#include <ESPAsyncWebServer.h>
#include "ArduinoJson.h"
#include "WifiManagement/wifimanagment.h"
#include <map>
#include <list>
#include <memory>
#include <mutex>        
#include <atomic>  
#include <Wire.h>
#include <iostream>
#include "Node/Node.h"
#include "MyToolBox/MyToolBox.h" 
#include "PLC_CommonTypes/PLC_CommonTypes.h"
#include "PLC_InterfaceMenu/PLC_InterfaceMenu.h"
#include "PLC_Tools/PLC_Tools.h"
#include "MemoryMonitor/MemoryMonitor.h"
#include "MutexGuard/MutexGuard.h"
#include "PLC_Persistence/PLC_Persistence.h"
#include "MyModbus/MyModbus.h"
#include "BusinessLogic/BusinessLogic.h"
#include "SafeJsonDocument/SafeJsonDocument.h"
#include "DS3231_RTC/DS3231_RTC.h"
#include "DS3231_RTC/ClockDisplay.h"

class BusinessLogic;  // Forward declaration

const char BORNE_UNIVERSELLE_VERSION[] PROGMEM = "Borne Universelle 2.2.2";

// Préparation pour pouvoir supporrter le mode access point
#define WIFI_MODE           "mode"              // Mode WiFi dans JSON
#define AP_NAME             "ap_name"           // Nom Access Point dans JSON
#define AP_PASSWORD         "ap_password"       // Mot de passe AP dans JSON
#define AP_IP               "ap_ip"             // IP Access Point dans JSON
#define AP_CHANNEL          "ap_channel"        // Canal WiFi dans JSON

// Valeurs de mode dans JSON
#define WIFI_MODE_STATION       "station"      
#define WIFI_MODE_ACCESS_POINT  "access_point" 

// Déclaration anticipée de NodeInfo pour éviter la dépendance à BusinessLogic.h
struct NodeInfo;

// Forward declaration de PLC_Tools pour utilisation dans WebSocketMessageDeleter
class PLC_Tools;

#define OTA_URL_SIZE	            80
//#define HEARTBEAT_DELAY           1500L
#define HEARTBEAT_DELAY             500L    // ✅ GARDER 500ms - réactif  
#define HEARTHBEAT_TIMEOUT          10000L     // ✅ 15s au lieu de 3s - tolérant
#define MAX_HEARTBEAT_FAILURES      6       // ✅ 6 tentatives au lieu de 3
#define HEARTBEAT_RECOVERY_DELAY    2000L   // ✅ 2s entre récupérations



#define ABNORMAL_REFRESH_TIME       500L
#define ALLOWED_TIME_TO_PROCESS_MESSAGE 300
#define JSON_CONFIG_SIZE            4000L
#define SOCKET_MESSAGE_MAX_SIZE     20000
#define MAX_MESSAGES_PENDING        WS_MAX_QUEUED_MESSAGES * 4
#define INTERFACE_PATH_FILE         "/interface.json"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define INPUT_READ_WARNING_INTERVAL_MS 5000

#define NAME_LENGHT                 80
#define MAX_MESSAGE_LENGTH          360

// errors messages send to js

const char JSON_NOT_VALID[] PROGMEM = "JSON is not valid";
const char NO_NAME_KEY[] PROGMEM = "The key Name (project name) is not found";
//const char NO_HARDWARE_KEY_CONFIG[] PROGMEM = "The key hardware is not found in config file";
//const char NO_WIFI_KEY[] PROGMEM = "No config.Wifi key";
const char NO_CONFIG_KEY[] PROGMEM = "No config key";
const char WIFI_NAME_KEY_MISSING[] PROGMEM = "Wifi name ky missing";
const char NO_CHILDREN_KEY_CONFIG[] PROGMEM = "The key children is not found in config file";
const char MODBUS_NOT_INITIALISED[] PROGMEM = "PLC want to create modbus node but Modbus not initialised";



#define NB_DIGITAL_INPUTS           5
#define NB_ANALOG_INPUT             4
#define NB_DIGITAL_OUTPUTS          5
#define NB_ANALOG_OUTPUTS           4
#define MODBUS_ADDRESS_OFFSET       0
#define HARDWARE_FOLDER             "/hardware"
#define WEB_SOCKET_MESSAGE_MAX_SIZE 10000
#define WEB_SOCKET_MESSAGE_MAX_ENTRYS 100

// Json keys
#define PROJECT_TYPE_V0 "BorneUniverselle"
#define HEARTBEAT       "heartbeat"
#define TOO_MUCH_CLIENTS    "tooMuchClients"
#define TRUE_J            "true"
#define FALSE_J           "false"
#define CONFIG          "config"
#define DESCRIPTION     "desc"
#define NAME            "name"
#define HARDWARE        "hardware"
#define WIFI            "Wifi"
#define SSID_           "ssid"
#define PWD_            "pwd"
#define IP              "ip"
#define DHCP            "dhcp"
#define DNS_            "dns"
#define GATEWAY_        "gateway"
#define MASK_           "mask"
#define PARAMETERS      "parameters"
#define OTA_URL         "ota_url"
#define STATUS          "status"
#define TAG             "tag"
#define CHILDREN        "children"
#define TYPE            "type"
#define ID              "id"
#define PIN             "pin"
#define ADDR            "addr"
#define INPUT_INVERTED  "inverse"
#define HASH            "hash"
#define VALUE           "value"
#define NOTIFY_STATES_CHANGED    "states"
#define VIRTUAL         "virtual"
#define RX_BOOL         "rx-bool"
#define TX_BOOL         "tx-bool"
#define PF8574_RX_BOOL  "PF8574_rx-bool"
#define PF8574_TX_BOOL  "PF8574_tx-bool"
#define PCA9554_TX_BOOL "PCA9554_tx-bool"
#define RX_UINT16       "rx-uint16"
#define TX_UINT16       "tx-uint16"
#define RX_UINT32       "rx-uint32"
#define TX_UINT32       "tx-uint32"
#define TX_FLOAT        "tx-float"
#define RX_FLOAT        "rx-float"
#define TX_TEXT         "tx-text"
#define I2C             "I2C"
#define BU_SDA          "SDA"
#define BU_SCL             "SCL"
#define RX_ADDR         "rx_addr"
#define TX_ADDR         "tx_addr"
#define RS485           "RS485"
#define RX              "rx"
#define TX              "tx" 
#define SPEED           "speed"
#define MODBUS          "Modbus"
#define MODBUS_RTU      "ModbusRTU"
#define TIMEOUT         "timeout"
#define MODBUS_READ_COIL "ModbusReadCoil"
#define MODBUS_WRITE_COIL "ModbusWriteCoil"
#define MODBUS_READ_HOLDING_REGISTER    "ModbusReadHoldingRegister"
#define MODBUS_WRITE_HOLDING_REGISTER   "ModbusWriteHoldingRegister"
#define MODBUS_READ_INPUT_REGISTER      "ModbusReadInputRegister"
#define MODBUS_READ_MULTIPLE_INPUTS_STATUS "ModbusReadMultipleInputsStatus"
#define MODBUS_READ_DOUBLE_HOLDING_REGISTER "ModbusReadDobbleHoldingRegister"
#define MODBUS_READ_DOUBLE_INPUTREGISTER "ModbusReadDobbleInputRegisters"
#define MODBUS_WRITE_DOUBLE_INPUTREGISTER "ModbusWriteDobbleInputRegisters"
#define MODBUS_WRITE_DOUBLE_HOLDING_REGISTER "ModbusWriteDobbleHoldingRegister"
#define MODBUS_WRITE_MULTIPLE_COILS  "ModbusWriteMultipleCoils" // function 0F (15)
#define RX_TEXT         "rx-text"
#define NB_VALUES       "nb_values"
#define ADDRESS         "address"
#define OFFSET          "offset"
#define REGISTERS_OFFSET    "registersOffset" 
#define REFRESH_INTERVAL    "refreshInterval"
#define WEB_UPDATE_INTERVAL "webUpdateInterval" 
#define ALL_STATES_REQUEST "allStatesRequest"  
#define FATAL_MESSAGE   "PLC IS BROKEN !!!!"
#define GET_VALUE       "get"
#define DESCRIPTOR      "descriptor"
#define GET_DESCRIPTOR  "get_descriptor"
#define SAVE_FILE       "saveFile"
#define DIRECTORY       "directory"
#define FILTER          "filter"
#define PATH            "path"
#define DATA            "data"
#define INITIAL_STATE_LOADED    "initialStatesLoaded"
#define ITEMS          "items"
#define CONFIG_PATH_FILE "/config.json"
#define ENABLE_INTERFACE   "enableInterface"
#define LOG_ON_DIAGNOSTIC_FILE  "log on diagnostic file"
#define DELETE_DIAGNOSTIC_FILE_AT_STARTUP   "delete diagnostic file at startup"
#define NOTIFICATION        "notification"
#define CLEAR_ALL           "clearAll"
#define MACHINE_STATE_LOGGING   "log historry and transition"
#define SHOW_SUCCESS_MESSAGES   "show success messages"
#define SHOW_INFO_MESSAGES      "show info messages"
#define FILTERED_MESSAGES       "filteredMessages"
#define PLC_SUSPEND             "suspend_plc"
#define PLC_SUSPENDED           "plc_suspended"
#define IS_PLC_IDLE             "is_plc_idle"
#define PLC_IS_IDLE            "plc_is_idle"
#define DELETE_FILE            "deleteFile" // depuis le socket
#define PLC_INTERNAL_SUSPEND    "interfaceSuspend" // message envoyé au javascript
#define FILE_SAVED             "fileSaved"  // Notifie le client web que le fichier a été sauvegardé
#define FILE_DELETED           "fileDeleted"  // Notifie le client web que le fichier a été supprimé
#define REASON                 "reason"
#define RTC_PRESENT           "RTC"
#define CURRENT_UNIX_TIME      "currentTimeUnix"  // Lorsque le js envoie l'heure actuelle au plc
#define GET_RTC_UNIX_TIME          "getCurrentTimeUnix"       // Lorsque le js demande l'heure du RTC au plc
#define GET_LOCAL_TIME         "getCurrentTime"
#define CURRENT_LOCAL_TIME      "currentTime"
#define ON_TAB_CHANGE           "onTabChange"

struct HARDWARE_ITEM{
    uint8_t id;
    uint8_t pin;
    bool direction; // if true -> output
};

struct HARDWARE_MODEL{
    uint8_t nbInputs;
    uint8_t nbOutputs;
    HARDWARE_ITEM nodes[32];
    char name[NAME_LENGHT];
};

struct WEB_SOCKET_MESSAGE{
    void * arg;
    uint8_t *data;
    size_t len; // 
    uint32_t timeOfOrigine;
    AsyncWebSocketClient *client;
};

struct NOTIF_MESSAGE{
    uint8_t severity;
    char *message;
};

// Suppresseur personnalisé pour NOTIF_MESSAGE
struct NotifMessageDeleter {
    void operator()(NOTIF_MESSAGE* m) const;  // ✅ DÉCLARATION SEULEMENT
};

// Suppresseur personnalisé pour WEB_SOCKET_MESSAGE
struct WebSocketMessageDeleter {
    void operator()(WEB_SOCKET_MESSAGE* c) const;  // ✅ Signature seulement
};

using NotifMessagePtr = std::unique_ptr<NOTIF_MESSAGE, NotifMessageDeleter>;
using WebSocketMessagePtr = std::unique_ptr<WEB_SOCKET_MESSAGE, WebSocketMessageDeleter>;

class BorneUniverselle{
	public:
        // Singleton
        static BorneUniverselle* getInstance();
        static bool initializeInstance();
        ~BorneUniverselle();
        static void resetForTests() {
            if (instance) {
                delete instance;  // Appelle ~BorneUniverselle() qui nettoie les nodes
                instance = nullptr;
            }
        }
        bool setName(const char *name, bool check = true);
        bool setNameImpl(const char *name, bool check = true);
        char *getName();
        char *getNameImpl();

        // Socket
        bool tryAcquireConnectionLock(uint32_t clientId);
        void releaseConnectionLock();
        bool isConnectionEventDuplicate(uint32_t clientId);
        void processClientCleanup();
        bool canAcceptNewClient();
        void setClientDisconnected(AsyncWebSocketClient* client);
        AsyncWebSocketClient* getConnectedClient();

        bool isWifiConnectionTimeout();
        WifiItem getNextNetwork();

        unsigned char getNbWifiItems();
        void setOTA_url(const char *url);
        void handleWebSocketMessage();
        void checkHeartbeat();
        void updateEarthbeatTimeout();
        bool checkForHeartbeatInQueue();
        void setClientConnected(bool status, AsyncWebSocketClient *client);
        bool clientQueueIsFull();
        void setShowHeartbeatMessages(bool status);
        bool isClientConnected();
        bool getWifiStatus() const;
        uint32_t getWifiStartupTime();
        void setWifiConnected(bool status);
        bool connectWifi(WifiItem item); 
        bool connectWifiImpl(WifiItem item); 
        void eraseWifis();
        void refresh();
        Node *findNode(const char *context, uint32_t hash);
        template <typename T>
        static T* findNodeSecure(const char* contexte, uint32_t hash, int expectedClassType);
        Node *findNodeImpl(const char *context, uint32_t hash);
        bool notifyWebClient(bool all = false);
        static void prepareMessage(uint8_t type, const char *text); // send message to web socket
        void prepareMessageImpl(uint8_t type, const char * text);
        bool getIsKinconyA8S();
        void printConfigFile();
        bool sendMessage();
        bool isPlcBroken();
        bool isPlcBrokenImpl();
        void setPlcBrokenImpl(const char *context);  
        static void setPlcBroken(const char *context);
        static bool enableInterface(bool status);
        void tooMuchClients(AsyncWebSocketClient *client);
        bool handleNodesChange(JsonDocument& socketDoc);
        void handleDirectoryRequest(JsonDocument& socketDoc);
        bool handleSaveFile(JsonDocument& socketDoc);
        bool handleUnixClockUpdateRequest(JsonDocument& socketDoc);
        bool handleLocalClockUpdateRequest(JsonDocument& socketDoc);
        bool handleDeleteFileRequest(JsonDocument& socketDoc);
        bool getIsWifiParsedOk();
        bool parseStaticIPFromJson(const JsonObject& wifiItem, WifiItem& config, const char* name);
        void handleWebSocket(void *_arg, unsigned char *_data, size_t _len, AsyncWebSocketClient *_client);
        void clearInputschanged(); // if no client connected
        static void refresHardwareInputs();
        void refresHardwareInputsImpl();
        bool isWebSocketMessagesListMoreThanHalf();
        bool isAllInputsReadOnce();
        bool isAllConditionsOk();
        bool sendTextToClientImpl(const char *text);
        static bool sendTextToClient(const char *text);
        float getConfigVersion(){
            return projectVersion;
        };
        void setShowModbusMessages(bool status);
        bool getModbusStatusMessages();
        void showMessage(Node *node, const char *text);
        static void modbusMessageHandler(uint8_t severity, const char* message);
        void setInitialStateLoadedCallback(std::function<void()> callback);
        void setTabChangeCallback(std::function<void(const char*)> callback); 
       

        char name[NAME_LENGHT];
        //static char defaultName[NAME_LENGHT];
        char ota_url[OTA_URL_SIZE];
        bool showModbusMessages = false;

		bool wifiParsedOk = true;
		static bool otaPending;

        bool heartbeatReceive = false;
        uint32_t lastHearbeatReceive = 0, lastHeartbeatSend = 0;
        uint8_t nbTimeouts = 0;
        bool clientconnected = false;
        bool showHeartbeatMessages = false;
        AsyncWebSocketClient *client = nullptr;
        uint32_t heartbeatTimeout = 0;
        bool isPLCSuspended();

        bool inputsCache[NB_DIGITAL_INPUTS] = {0};

        std::map<std::string, int> m;
        std::map<uint32_t, Node *> nodesMap;

       // static bool setHardware(JsonDocument& doc, const char *, bool check = true);
        void sendHeartbeat(bool reset = false);
        bool i2cInit(JsonDocument& contextDoc);
        bool RS485Init(JsonDocument& contextDoc);
        bool modbusInit(JsonDocument& contextDoc);
        void unableToFindKeyImpl(char *context, char *key);
        void unableToFindKey(char *context, char *key);
        bool addNode(Node* node, bool check = false);
        void deleteNodes();
        bool createRxBoolNode(char *name, char *parentName, uint16_t id, uint32_t *hash, JsonDocument& contextDoc, JsonObject &hardSection, char *type, bool inverted,uint16_t refreshInterval, uint16_t webRefreshInterval, bool check); // c'est ici que l'on créé les nodes !
        bool createTxBoolNode(char *name, char *parentName, uint16_t id, uint32_t *hash, JsonDocument& contextDoc, JsonObject &hardSection, char *type, bool inverted, uint16_t webRefreshInterval, bool check);
        bool createVirtualNode(char *name, char *sectionName, uint16_t id, char *type, bool inputsInverted, uint32_t *hash, bool check);
        bool createModbusNode(char *nodeName, char *sectionName, uint16_t id, uint32_t *hash, uint16_t slaveAddress, JsonDocument& contextDoc,
                                     JsonObject hardSection, JsonObject configNode, char *type, uint16_t refreshInterval,  uint16_t webRefreshInterval, bool check);
        // void modbusTask();
        bool parseConfig(JsonDocument& doc, bool check = true);
        static bool parseWifis(JsonDocument& doc, bool check);
        bool parseWifisImpl(JsonDocument& doc, bool check);
        
        bool parseHardwares(JsonDocument &doc, bool check, float version);
        uint8_t addWifiItem(const char *ssid, const char *pwd, const char *connexionName, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress mask, bool dhcp);
        uint8_t addWifiItem2(const char *ssid, const char *pwd, const char *connexionName, bool dhcp);
        bool saveParameters(const JsonDocument &configDoc);
        bool handleGetValue(uint32_t hash);
        bool handleGetAllValues();
        bool addNodeToNodeObject(Node *node, JsonObject *nodeObject);
        bool processMessage(WEB_SOCKET_MESSAGE *webSocketMessage);
        bool isI2CInitialised = false, isRs485Initialised = false, isModbusInitialised = false;
        HardwareSerial *myRS485 = nullptr;
        MyModbus& myModbus; // singleton 
        bool isKinconyA8S = true;
        bool plcBroken = false;
        bool inSetPlcBroken = false;  // Protection contre la récursion
        uint32_t clientConnectedAt;
        bool isLastMessageFatal;
        static SemaphoreHandle_t webSocketMutex;
        std::list<NotifMessagePtr> notifMessagesList;
        std::list<WebSocketMessagePtr> webSocketMessagesList;
        static SemaphoreHandle_t notifMutex;
        bool allInputsReadOnce = false;
        bool newClientConnected = false;
        bool isClientFullyConnected() const { return clientFullyInitialized;}
        float projectVersion = 3;
        bool noPLC_BrokenMessage = false;

        static const uint8_t MAX_MUTEX_ATTEMPTS = 10;  // Nb max de tentatives pour acquérir le mutex

           // Callback enregistrée
        static std::function<void()> initialStateLoadedCallback;
        static std::function<void(const char*)> tabChangeCallback; 

        char hearbeatChain[256];

        String messageBuffer; // buffer pour la réception de message du web socket en plusieurs morceaux
        size_t expectedSize = 0;
        void keepWebSocketMessage(const char *data, void *arg, size_t len, AsyncWebSocketClient *_client);

        void getClasseName(int classId, char* className);
        bool validatePointerType(void **ptr, int expectedClassType);
        bool validateNodeType(Node *node, int expectedClassType, const char *nodeName, uint32_t hash);
        bool isCompatibleClassType(int actualType, int expectedType);
        bool getLogOnDiagnosticFile(); // retourne true si on doit loguer les messages dans le fichier diagnostic
        void markSetupComplete();
        void clearAllClientNotifications();
        void forceUpdateVirtualInputNodes();

       
        
        bool logOnDiagnosticFile = false;  
        bool deleteDiagnosticFileAtStartup = false;
        bool showSuccessMessages = false;
        bool showInfoMessages = true;
        bool machineStateLogging = false;
        bool isMachineStateLogging(){
            return machineStateLogging;
        }

     /**
     * @brief Enregistre l'instance de BusinessLogic
     * @param logic Pointeur vers la logique métier
     * 
     * À appeler après création de la logique métier dans main.cpp
     * Enregistre aussi le callback de suspension
     */
    void setBusinessLogic(BusinessLogic* logic);
    
    /**
     * @brief Obtient l'instance de BusinessLogic
     */
    BusinessLogic* getBusinessLogic() const {
        return businessLogic;
    }
    
    // Callback appelé par BusinessLogic
    void onBusinessLogicSuspendEvent(bool suspend, bool accepted, const char* reason);
    
    // Méthodes pour gérer les demandes de suspension
    bool handleSuspendRequest(bool request);

    bool handleIsPLCIdleRequest() const;
    
    // Méthodes internes
    bool suspendedByJavaScript = false;
    bool suspendedByPLC = false;         
    void setPlcSuspended(bool status);  // Va devenir obsolète
    void suspendPLCServices();
    void resumePLCServices();
    void sendSuspendResponse(bool accepted);
    bool getPlcSuspended() const {
        return plcSuspended;
    }

    /**
     * @brief Suspend le PLC pour une opération interne
     * @param reason Raison de la suspension (pour log/debug)
     * @return true si la suspension a été activée (peut être déjà suspendu)
     */
    bool suspendForInternalOperation(const char* reason);
    
    /**
     * @brief Reprend le PLC après une opération interne
     * @return true si c'était bien suspendu par le PLC
     */
    bool resumeFromInternalOperation();
    void sendPlcSuspendNotification(bool suspended, const char* reason);

    bool getIsetupComplete() const {
        return setupComplete;
    }

    void clockUpdateRequest();

    private:
        BorneUniverselle();  // Constructeur privé pour le singleton
        static BorneUniverselle* instance; 
        static bool instanceCreated; 
        
        // ✅ RÉFÉRENCE À BusinessLogic (pas Formaca2 !)
        BusinessLogic* businessLogic = nullptr;
    
        // État de suspension (géré par BorneUniverselle)
        bool plcSuspended = false;
        bool heartbeatSuspended = false;
        uint32_t suspensionStartTime = 0;
        bool clientFullyInitialized = false;

        bool setupComplete = false;
        bool initializeConfig();
        bool parseAccessPointConfigFromJson(const JsonObject &wifiItem, WifiItem &config, const char *name);
        bool parseWifiItemFromJson(const JsonObject &wifiItem, WifiItem &config, const char *name);
        bool parseStationConfigFromJson(const JsonObject &wifiItem, WifiItem &config, const char *name);

        void addCustomDescriptor(Node *Node, JsonObject *nodeObject); 
        NodeInfo *nodeInfos = nullptr; // pour la logique metier 
       
        bool getDeleteDiagnosticFileAtStartup();
        void diagnosticConnectionState();
        void executeImmediateCleanup();
        void executeDelayedCleanup();
        const char*getClientStateString();
        void diagnosticHeartbeatState();

        // États de connexion améliorés
        enum class ClientState {
            DISCONNECTED,
            CONNECTING,
            CONNECTED,
            DISCONNECTING,
            CLEANUP_PENDING
        };

    
        ClientState clientState = ClientState::DISCONNECTED;
        uint32_t lastStateChange = 0;
        uint32_t cleanupDelay = 2000; // 2 secondes de délai
        bool forceCleanupOnNextCycle = false;
        
        // Compteurs pour diagnostic
        uint32_t consecutiveConnectionAttempts = 0;
        uint32_t lastConnectionAttempt = 0;

        uint32_t clientDisconnectionTime = 0;
        static const uint32_t CLIENT_CLEANUP_DELAY = 2000; // 2 secondes
        bool isCleanupPending = false;
        uint32_t lastClientCloseTime = 0;
        static const uint32_t MIN_TIME_BETWEEN_CLIENTS = 1000; // 1 seconde minimum

        uint32_t lastClientAcceptTime = 0;         // Timestamp de la dernière acceptation
        uint32_t lastAcceptedClientId = 0;         // ID du dernier client accepté
        static const uint32_t MIN_ACCEPT_INTERVAL = 1500; // 1.5 secondes minimum entre acceptations
        std::atomic<bool> isProcessingConnection{false};  // Flag atomique
        uint32_t lastConnectionEventTime = 0;             // Timestamp du dernier événement
        static const uint32_t MIN_EVENT_INTERVAL = 100;   // 100ms minimum entre événements

        bool configNeedsRestart(JsonDocument& newConfig);
        bool preValidateFile(const char* path, const JsonDocument& doc, String& errorMsg);
        bool preValidateConfigJson(const JsonDocument& doc, String& errorMsg);
        void onFileSavedHandler(const char* path, bool success);
        void reloadMinorConfigParameters(JsonDocument& newConfig);

        bool configNeedsRestartAfterSave = true;

        /**
         * @brief Calcule si le PLC doit être suspendu
         * @return true si au moins une source demande le suspend
         */
        bool isAnySuspendActive() const {
            return suspendedByJavaScript || suspendedByPLC;
        }
        uint8_t suspendCount = 0;  // Compteur de suspend imbriqués
        
        /**
         * @brief Met à jour l'état réel du PLC selon les flags
         */
        void updateSuspendState();
        void sendFileSavedNotification(bool result, const char* path = nullptr, const char* errorMsg = nullptr);
        void sendFileDeleteNotification(bool result, const char* path = nullptr, const char* errorMsg = nullptr);

        bool allConditionsOK = false;
        uint32_t lastInputReadWarningTime = 0;

        void keepAliveHeartbeat();
};

#endif
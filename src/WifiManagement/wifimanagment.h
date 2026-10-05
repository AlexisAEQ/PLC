#ifndef WIFI_MANAGMENT_LIB_H
#define WIFI_MANAGMENT_LIB_H

#include <map>
#include "Arduino.h"
#include <WiFi.h>
#include "WiFiGeneric.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include <functional>
#include "WiFiType.h"
#include "IPAddress.h"
#include "esp_smartconfig.h"
#include "wifi_provisioning/manager.h"
#include <WiFiServer.h>
#include <ESPmDNS.h>
#include "ArduinoJson.h"

#define TIMEOUT_WIFI	60000L
const uint32_t TIMEOUT_TIME = 20000; // 20 secondes

// Clés JSON pour le parsing
#define WIFI_MODE           "mode"
#define AP_NAME             "ap_name"
#define AP_PASSWORD         "ap_password"
#define AP_IP               "ap_ip"
#define AP_CHANNEL          "ap_channel"

// Valeurs des modes WiFi
#define WIFI_MODE_STATION       "station"
#define WIFI_MODE_ACCESS_POINT  "access_point"

// Définitions des réseaux prédéfinis
#define THIERRY_SSID      "Ella"
#define THIERRY_WIFI_PWD  "Oceanne1"
#define THIERRY_NAME      "Thierry"
#define THIERRY_ADDRESS   IPAddress(192,168,1,222)
#define THIERRY_DNS       IPAddress(192,168,1,107)
#define THIERRY_GATEWAY   IPAddress(192,168,1,254)
#define THIERRY_MASK      IPAddress(255,255,255,0)

#define THIERRY_CELL_SSID	"thiline"
#define THIERRY_CELL_WIFI_PWD  "Oceanne1"
#define THIERRY_CELL_NAME      "Cellulaire Thierry"

#define ALEXIS_SSID       "TELUSAFF7"
#define ALEXIS_WIFI_PWD   "h6vJ96UKmUr7"
#define ALEXIS_NAME       "Alexis"

#define ROUTER_ALEXIS_SSID	"dlink-FD70"
#define ROUTEUR_ALEXCI_PWD 	"yevvf68483"
#define ROUTEUR_ALEXIS_NAME "Routeur alexis"

#define CHRISTOPHE_SSID	  "CS maison"
#define CHRISTOPHE_WIFI_PWD	"maison%123"
#define CHRISTOPHE_NAME		"Saphari"

#define CHRISTOPHE_CELL_SSID	"IRTAG"
#define CHRISTOPHE_CELL_PWD		"IRTAG%123"
#define CHRISTOPHE_CELL_NAME	"Cellulaire Christophe"

#define MICHAEL_SSID		"BELL643"
#define MICHAEL_PWD			"F47227FE"
#define MICHAEL_NAME		"Michael"

#define NEREYDA_SSID		"Depa 72B"
#define NEREYDA_PWD			"72B20221"
#define NEREYDA_NAME		"NEREYDA"

#define MEXIQUE_SSID		"PosadaLaHacienda"
#define MEXIQUE_PWD			"Chispas2020"
#define MEXIQUE_NAME		"Mexique"

#define WIFIMANAGMENT_VERSION "WIFIMANAGMENT_VERSION_1.0"

#define NB_MAX_WIFI	10
#define SSID_LENGHT	30
#define PWD_LENGHT	30
#define CONNEXION_NAME_LENGHT 80
#define CHARS_SIZE	400

enum class WiFiError {
    WIFI_SUCCESS = 0,
    WIFI_JSON_PARSE_ERROR,
    WIFI_INVALID_IP,
    WIFI_INVALID_SSID,
    WIFI_INVALID_PASSWORD, 
    WIFI_CONNECTION_FAILED,
    WIFI_TIMEOUT,
    WIFI_HARDWARE_ERROR,
    WIFI_CONFIG_ERROR,
    WIFI_HOSTNAME_ERROR,
    WIFI_DHCP_ERROR
};

struct WiFiResult {
    WiFiError error;
    String message;
    
    WiFiResult(WiFiError err = WiFiError::WIFI_SUCCESS, const String& msg = "") 
        : error(err), message(msg) {}
    
    bool isSuccess() const { return error == WiFiError::WIFI_SUCCESS; }
    const char* getErrorString() const {
        switch(error) {
            case WiFiError::WIFI_SUCCESS: return "Succès";
            case WiFiError::WIFI_JSON_PARSE_ERROR: return "Erreur parsing JSON";
            case WiFiError::WIFI_INVALID_IP: return "Adresse IP invalide";
            case WiFiError::WIFI_INVALID_SSID: return "SSID invalide";
            case WiFiError::WIFI_INVALID_PASSWORD: return "Mot de passe invalide";
            case WiFiError::WIFI_CONNECTION_FAILED: return "Échec connexion";
            case WiFiError::WIFI_TIMEOUT: return "Timeout";
            case WiFiError::WIFI_HARDWARE_ERROR: return "Erreur matérielle";
            case WiFiError::WIFI_CONFIG_ERROR: return "Erreur configuration";
            case WiFiError::WIFI_HOSTNAME_ERROR: return "Erreur hostname";
            case WiFiError::WIFI_DHCP_ERROR: return "Erreur DHCP";
            default: return "Erreur inconnue";
        }
    }
};;

// Enum Arduino version compatibility
#if ESP_ARDUINO_VERSION < 2
	typedef enum {
		ARDUINO_EVENT_WIFI_READY = 0,
		ARDUINO_EVENT_WIFI_SCAN_DONE,
		ARDUINO_EVENT_WIFI_STA_START,
		ARDUINO_EVENT_WIFI_STA_STOP,
		ARDUINO_EVENT_WIFI_STA_CONNECTED,
		ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
		ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE,
		ARDUINO_EVENT_WIFI_STA_GOT_IP,
		ARDUINO_EVENT_WIFI_STA_GOT_IP6,
		ARDUINO_EVENT_WIFI_STA_LOST_IP,
		ARDUINO_EVENT_WIFI_AP_START,
		ARDUINO_EVENT_WIFI_AP_STOP,
		ARDUINO_EVENT_WIFI_AP_STACONNECTED,
		ARDUINO_EVENT_WIFI_AP_STADISCONNECTED,
		ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED,
		ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED,
		ARDUINO_EVENT_WIFI_AP_GOT_IP6,
		ARDUINO_EVENT_WIFI_FTM_REPORT,
		ARDUINO_EVENT_ETH_START,
		ARDUINO_EVENT_ETH_STOP,
		ARDUINO_EVENT_ETH_CONNECTED,
		ARDUINO_EVENT_ETH_DISCONNECTED,
		ARDUINO_EVENT_ETH_GOT_IP,
		ARDUINO_EVENT_ETH_GOT_IP6,
		ARDUINO_EVENT_WPS_ER_SUCCESS,
		ARDUINO_EVENT_WPS_ER_FAILED,
		ARDUINO_EVENT_WPS_ER_TIMEOUT,
		ARDUINO_EVENT_WPS_ER_PIN,
		ARDUINO_EVENT_WPS_ER_PBC_OVERLAP,
		ARDUINO_EVENT_SC_SCAN_DONE,
		ARDUINO_EVENT_SC_FOUND_CHANNEL,
		ARDUINO_EVENT_SC_GOT_SSID_PSWD,
		ARDUINO_EVENT_SC_SEND_ACK_DONE,
		ARDUINO_EVENT_PROV_INIT,
		ARDUINO_EVENT_PROV_DEINIT,
		ARDUINO_EVENT_PROV_START,
		ARDUINO_EVENT_PROV_END,
		ARDUINO_EVENT_PROV_CRED_RECV,
		ARDUINO_EVENT_PROV_CRED_FAIL,
		ARDUINO_EVENT_PROV_CRED_SUCCESS,
		ARDUINO_EVENT_MAX
	} arduino_event_id_t;
#endif

// Union pour conversion IP
typedef union{
    uint32_t ip;
    uint8_t bytes[4];
} converter;

// ========================================================================
// STRUCTURE WIFIITEM - DÉFINIE AVANT LA CLASSE
// ========================================================================
struct WifiItem {
    // Champs pour mode Station
    char SSID[80];
    char PWD[80];
    char connectionName[80];
    IPAddress ip;
    IPAddress dns;
    IPAddress gateway;
    IPAddress mask;
    bool dhcp;
    
    // Champs pour tous les modes
    String wifiMode;                // "station" ou "access_point"
    
    // Champs pour mode Access Point
    String apName;                  // Nom de l'AP
    String apPassword;              // Mot de passe AP
    IPAddress apIP;                 // IP de l'AP
    uint8_t apChannel;              // Canal WiFi (1-13)
    uint8_t apMaxClients;           // Nombre max de clients
    
    // Constructeur avec valeurs par défaut
    WifiItem() : dhcp(true), wifiMode(WIFI_MODE_STATION), 
                apName("ESP32-Formaca"), apPassword("FormacaPass123"),
                apIP(192, 168, 4, 1), apChannel(1), apMaxClients(1) {
        SSID[0] = '\0';
        PWD[0] = '\0';
        connectionName[0] = '\0';
        ip = IPAddress(0, 0, 0, 0);
        dns = IPAddress(0, 0, 0, 0);
        gateway = IPAddress(0, 0, 0, 0);
        mask = IPAddress(255, 255, 255, 0);
    }
    
    // Constructeur de copie
    WifiItem(const WifiItem& other) 
        : ip(other.ip), dns(other.dns), 
          gateway(other.gateway), mask(other.mask),
          dhcp(other.dhcp), wifiMode(other.wifiMode),
          apName(other.apName), apPassword(other.apPassword),
          apIP(other.apIP), apChannel(other.apChannel), 
          apMaxClients(other.apMaxClients) {
        strncpy(SSID, other.SSID, sizeof(SSID) - 1);
        SSID[sizeof(SSID) - 1] = '\0';
        strncpy(PWD, other.PWD, sizeof(PWD) - 1);
        PWD[sizeof(PWD) - 1] = '\0';
        strncpy(connectionName, other.connectionName, sizeof(connectionName) - 1);
        connectionName[sizeof(connectionName) - 1] = '\0';
    }
    
    // Opérateur d'assignation
    WifiItem& operator=(const WifiItem& other) {
        if (this != &other) {
            strncpy(SSID, other.SSID, sizeof(SSID) - 1);
            SSID[sizeof(SSID) - 1] = '\0';
            strncpy(PWD, other.PWD, sizeof(PWD) - 1);
            PWD[sizeof(PWD) - 1] = '\0';
            strncpy(connectionName, other.connectionName, sizeof(connectionName) - 1);
            connectionName[sizeof(connectionName) - 1] = '\0';
            
            ip = other.ip;
            dns = other.dns;
            gateway = other.gateway;
            mask = other.mask;
            dhcp = other.dhcp;
            wifiMode = other.wifiMode;
            apName = other.apName;
            apPassword = other.apPassword;
            apIP = other.apIP;
            apChannel = other.apChannel;
            apMaxClients = other.apMaxClients;
        }
        return *this;
    }
};

// ========================================================================
// CLASSE WIFIMANAGMENT - VERSION SINGLETON
// ========================================================================
class WifiManagment {
public:
    // ====================================================================
    // SINGLETON PATTERN
    // ====================================================================
    static WifiManagment& getInstance();
    
    // ====================================================================
    // MÉTHODES D'INSTANCE (NOUVELLES)
    // ====================================================================
    unsigned char getNbWifiItems() const;
    void clearNbWifiItems();
    void displayRegistredNetworks() const;
    
    // Ajout d'éléments
    unsigned char addItem(const char* ssid, const char* pwd);
    unsigned char addItem(const char* ssid, const char* pwd, const char* connexionName);
    unsigned char addItem(const char* ssid, const char* pwd, const char* connexionName, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress mask, bool dhcp = true);
    unsigned char addItem(const WifiItem& wifiConfig);

    // Gestion des données
    char* wifiMapstoCharPtr();
    void parseCharsMap();
    
    // Recherche et modification
    std::map<unsigned char, WifiItem>::iterator findWifiItemByName(const WifiItem& myItem);
    bool modifycurrentWifiConnection(const WifiItem& item);
    
    // Méthodes d'instance pour connexion
    WifiItem getNextNetwork();
    bool connectWifi(const WifiItem& item, const char* hostname = nullptr);
    WifiItem getCurrentNetwork();
    void setWifiConnected(bool status);
    bool getWifiStatus() const;
    void enableWifi(bool status);
    bool isWifiEnabled() const;
    long getWifiStartupTime() const;
    bool isWifiConnectionTimeout() const;
    
    // Nouvelles méthodes d'initialisation
    bool initialize(const WifiItem& wifiConfig);
    bool startAccessPointMode(const WifiItem& wifiConfig);
    bool validateConfiguration(const WifiItem& wifiConfig, String& errorMsg);
    String getCurrentConfigMode() const;
    
    // Callbacks pour les événements
    void setOnGotIPCallback(std::function<void()> callback) { onGotIPCallback = callback; }
    void setOnDisconnectedCallback(std::function<void()> callback) { onDisconnectedCallback = callback; }
    
    // Initialiser les event handlers (à appeler une seule fois)
    void initializeEventHandlers();
    
    // Monitoring et status
    String getDetailedStatus() const;
    bool isOperational() const;
    void shutdown();
    void update();
    void showCurrentStatus();
    void showIpAddress();
    IPAddress getCurrentIP() const;
    String getCurrentMode() const { return currentMode; }
    
    WiFiResult connectWifiWithResult(const WifiItem& currentWifi, const char* hostname = nullptr);
    WiFiResult validateWifiConfiguration(const WifiItem& config);
    WiFiResult parseWifiFromJson(const JsonObject& wifiItem, WifiItem& config, const char* name);

     // État détaillé
    WiFiError getLastError() const { return lastError; }
    String getLastErrorMessage() const { return lastErrorMessage; }
    
    // ====================================================================
    // MÉTHODES UTILITAIRES (STATIQUES)
    // ====================================================================
    static void printIPAddress(const char* name, IPAddress ip, bool ret = false);
    static void printWifiEvent(WiFiEvent_t event);

private:
    // ====================================================================
    // SINGLETON - CONSTRUCTEURS PRIVÉS
    // ====================================================================
    WifiManagment();
    
    // Empêcher la copie
    WifiManagment(const WifiManagment&) = delete;
    WifiManagment& operator=(const WifiManagment&) = delete;

     WiFiError lastError = WiFiError::WIFI_SUCCESS;
    String lastErrorMessage = "";
    
    void setError(WiFiError error, const String& message) {
        lastError = error;
        lastErrorMessage = message;
        Serial.printf("WiFi ERROR: %s - %s\n", getErrorString(error), message.c_str());
    }
    
    const char* getErrorString(WiFiError error) const;
    
    // ====================================================================
    // SINGLETON - INSTANCE STATIQUE
    // ====================================================================
    static WifiManagment* instance;
    
    // ====================================================================
    // VARIABLES D'INSTANCE (NOUVELLES)
    // ====================================================================
    String currentMode;
    bool isConnected;
    IPAddress currentIP;
    String currentSSID;
    String currentAPName;
    
    std::map<unsigned char, WifiItem> wifiItemsMap;
    unsigned char nbWifiItems;
    char currentNetworkId;
    bool wificonnected;
    volatile bool wifiEnabled;
    long wifiStartupTime;
    
    char mapAsCharPtr[CHARS_SIZE];
    
    // ====================================================================
    // MÉTHODES PRIVÉES D'AIDE
    // ====================================================================
    bool configureStaticIP(const WifiItem& wifiConfig);
    bool waitForConnection(uint32_t timeoutMs = 30000);
    void setupMDNS(const String& hostname);
    std::function<void()> onGotIPCallback = nullptr;
    std::function<void()> onDisconnectedCallback = nullptr;
    static bool eventHandlersInitialized;

    // Étape 1/3 correctif crash mDNS/AP : source unique de vérité sur l'état
    // mDNS, pour permettre un MDNS.end() propre avant tout changement de mode
    // (étape 2) et éviter les MDNS.begin() redondants (étape 3).
    bool mdnsStarted = false;
};

#endif // WIFI_MANAGMENT_LIB_H
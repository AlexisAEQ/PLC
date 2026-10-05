#include "WifiManagement/wifimanagment.h"
#include "wifimanagment.h"

// ========================================================================
// INITIALISATION DES VARIABLES STATIQUES DU SINGLETON
// ========================================================================
WifiManagment* WifiManagment::instance = nullptr;
bool WifiManagment::eventHandlersInitialized = false;

// ========================================================================
// IMPLÉMENTATION DU SINGLETON
// ========================================================================
WifiManagment& WifiManagment::getInstance() {
    if (instance == nullptr) {
        instance = new WifiManagment();
    }
    return *instance;
}

WifiManagment::WifiManagment() 
    : currentMode("none"), isConnected(false), currentIP(0, 0, 0, 0),
      currentSSID(""), currentAPName(""), nbWifiItems(0), currentNetworkId(0),
      wificonnected(false), wifiEnabled(false), wifiStartupTime(0) {
    
    Serial.println("WifiManagment Singleton créé");
    
    // Initialiser le buffer
    for (unsigned int i = 0; i < CHARS_SIZE; i++) {
        mapAsCharPtr[i] = 0;
    }
    
    // Vider la map
    wifiItemsMap.clear();
}

// ========================================================================
// MÉTHODES D'INSTANCE - GESTION DES ÉLÉMENTS
// ========================================================================
unsigned char WifiManagment::addItem(const char *ssid, const char *pwd) {
    return addItem(ssid, pwd, "");
}

unsigned char WifiManagment::addItem(const char *ssid, const char *pwd, const char* connexionName) {
    IPAddress ip = IPAddress(0,0,0,0);
    return addItem(ssid, pwd, connexionName, ip, ip, ip, ip, true); 
}

unsigned char WifiManagment::addItem(const char *ssid, const char *pwd, const char *connexionName, 
                                    IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress mask, bool dhcp) {
    if (nbWifiItems >= NB_MAX_WIFI) {
        Serial.println(F("Le nombre réseaux wifi enregistré est déjà au maximum !"));
        return nbWifiItems;
    }
    
    WifiItem myItem;
    strcpy(myItem.SSID, ssid);
    strcpy(myItem.PWD, pwd);
    strcpy(myItem.connectionName, connexionName);

    myItem.ip = ip;
    myItem.dns = dns;
    myItem.gateway = gateway;
    myItem.mask = mask;
    myItem.dhcp = dhcp;
    myItem.wifiMode = WIFI_MODE_STATION;
    
    wifiItemsMap[nbWifiItems++] = myItem;
    
    Serial.printf("SSID: %s successfuly added, connexion name: %s", myItem.SSID, connexionName);
    if (myItem.dhcp) {
        Serial.print(", dhcp");
    } else {
        printIPAddress(", ip", myItem.ip);
        printIPAddress(", dns", myItem.dns);
        printIPAddress(", gateway", myItem.gateway);
        printIPAddress(", mask", myItem.mask);
    }
    
    Serial.printf(", new nb network: %d\r\n", nbWifiItems);
    return nbWifiItems; 
}

unsigned char WifiManagment::getNbWifiItems() const {
    return nbWifiItems;
}

void WifiManagment::clearNbWifiItems() {
    Serial.println("Nb wifi = 0");
    nbWifiItems = 0;
    wifiItemsMap.clear();
}

// ========================================================================
// MÉTHODES D'INSTANCE - GESTION DES DONNÉES
// ========================================================================
char* WifiManagment::wifiMapstoCharPtr() {
    unsigned long index = 0;
    char *ptr = mapAsCharPtr;
    Serial.println("Convert the map into an chain");
    
    for (std::map<unsigned char,WifiItem>::iterator it = wifiItemsMap.begin(); it != wifiItemsMap.end(); ++it) {
        // SSID
        strcpy(ptr+index, it->second.SSID);
        Serial.printf("SSID: %s, ", it->second.SSID);
        index += strlen(it->second.SSID);
        index++;
        
        // PWD
        strcpy(ptr+index, it->second.PWD);
        Serial.printf("PWD: %s, ", it->second.PWD);
        index += strlen(it->second.PWD);
        index++;
        
        // CONNEXION_NAME
        strcpy(ptr+index, it->second.connectionName);
        Serial.printf("Connexion name: %s, ", it->second.connectionName);
        index += strlen(it->second.connectionName);
        index++;
        
        // ip
        converter conv;
        conv.ip = it->second.ip;
        strncpy(ptr + index, (char *)conv.bytes, sizeof(converter));
        index +=  sizeof(converter);	
        index++;
        
        // dns
        conv.ip = it->second.dns;
        strncpy(ptr + index, (char *)conv.bytes, sizeof(converter));
        index +=  sizeof(converter);
        index++;
        
        // gateway
        conv.ip = it->second.gateway;
        strncpy(ptr + index, (char *)conv.bytes, sizeof(converter));
        index +=  sizeof(converter);	
        index++;
        
        // mask
        conv.ip = it->second.mask;
        strncpy(ptr + index, (char *)conv.bytes, sizeof(converter));
        index +=  sizeof(converter);	
        index++;
        
        Serial.printf("index: %lu,", index);
        printf(" dhcp: %s", it->second.dhcp ? "true" : "false"); 
        
        if (!it->second.dhcp) {
            strncpy(ptr + index, "false", strlen("false")+1);
            index += strlen("false");
            printIPAddress("ip", it->second.ip);
            printIPAddress(", dns", it->second.dns);
            printIPAddress(", gateway", it->second.gateway);
            printIPAddress(", mask", it->second.mask);
        } else {
            strncpy(ptr + index, "true", strlen("true")+1);
            index += strlen("true");
            Serial.print(", DHCP");
        }
        
        index++;
        Serial.println();
    }
    
    Serial.printf("Size of map chain: %lu\r\n", index);
    return ptr;
}

void WifiManagment::parseCharsMap() {
    unsigned char itemId = 0;
    char *ptr = mapAsCharPtr;
    Serial.printf("Parsing chain, nb items: %d\r\n", nbWifiItems);
    
    unsigned char l = 0;
    unsigned long index = 0;
    char ssid[SSID_LENGHT];
    char pwd[PWD_LENGHT];
    char connexionName[CONNEXION_NAME_LENGHT];
    
    while (itemId < nbWifiItems) {
        l = 0;
        while (ptr[index] != 0  && l < SSID_LENGHT) {
            ssid[l++] = ptr[index++];
        }
        ssid[l] = 0;
        index++;
        
        l = 0;
        while (ptr[index] != 0  && l < PWD_LENGHT) {
            pwd[l++] = ptr[index++];
        }
        pwd[l] = 0;
        index++;
        
        l = 0;
        while (ptr[index] != 0 && l < CONNEXION_NAME_LENGHT) {
            connexionName[l++] = ptr[index++];
        }
        connexionName[l] = 0;
        index++;
        
        converter conv;
        strncpy((char *)conv.bytes, ptr+index, sizeof(converter));
        index +=  sizeof(converter);
        IPAddress ip = IPAddress(conv.ip);
        index++;
        
        strncpy((char *)conv.bytes, ptr+index, sizeof(converter));
        index +=  sizeof(converter);
        IPAddress dns = IPAddress(conv.ip);
        index++;
        
        strncpy((char *)conv.bytes, ptr+index, sizeof(converter));
        index +=  sizeof(converter);
        IPAddress gateway = IPAddress(conv.ip);
        index++;

        strncpy((char *)conv.bytes, ptr+index, sizeof(converter));
        index +=  sizeof(converter);
        IPAddress mask = IPAddress(conv.ip);
        index++;
        
        WifiItem myItem;
        
        if (!strncmp(ptr + index, "true", strlen("true"))) {
            myItem.dhcp = true;
            index += strlen("true");
        } else {
            if (!strncmp(ptr + index, "false", strlen("false"))) {
                myItem.dhcp = false;
                index += strlen("false");
            } else {
                Serial.println("ERREUR DHCP !!!!!!");
            }
        }
        index++;
        
        strcpy(myItem.SSID, ssid);
        strcpy(myItem.PWD, pwd);
        strcpy(myItem.connectionName, connexionName);
        myItem.ip = ip;
        myItem.dns = dns;
        myItem.gateway = gateway;
        myItem.mask = mask;
        myItem.wifiMode = WIFI_MODE_STATION;
        
        wifiItemsMap[itemId] = myItem;
        itemId++;
    }

    for (uint8_t i = 0; i < nbWifiItems; i++) {
        Serial.printf("%s\r\n", wifiItemsMap[i].connectionName);
    }
}

std::map<unsigned char,WifiItem>::iterator WifiManagment::findWifiItemByName(const WifiItem& myItem) {
    std::map<unsigned char,WifiItem>::iterator it;
    
    for (it = wifiItemsMap.begin(); it != wifiItemsMap.end(); ++it) {
        if (strcmp(it->second.connectionName, myItem.connectionName) == 0) {
            return it;
        }
    }
    
    Serial.printf("Unable to find connexion: %s\r\n", myItem.connectionName);
    return wifiItemsMap.end();
}

bool WifiManagment::modifycurrentWifiConnection(const WifiItem& myItem) {
    std::map<unsigned char,WifiItem>::iterator it = findWifiItemByName(myItem);
    
    if (it == wifiItemsMap.end()) {
        return false;
    }

    if (myItem.dhcp != it->second.dhcp) {
        if (myItem.dhcp) {
            it->second.dhcp = myItem.dhcp;
            Serial.println("DHCP will be activated for this connexion");
            return true;
        } else {
            IPAddress ip = myItem.ip;
            if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
                Serial.println("DHCP is deactivated and ip is not consistant !");
                return false;
            }
        }
    }
    
    Serial.println("Modify ip: current ip: "); 
    printIPAddress(it->second.connectionName, it->second.ip, true);
    Serial.println("New addresse: "); 
    printIPAddress(it->second.connectionName, myItem.ip, true);
    it->second.ip = myItem.ip;

    return true;	
}

void WifiManagment::displayRegistredNetworks() const {
    Serial.printf("WifiManagment::displayRegistredNetworks Nb registred networks: %d\r\n", nbWifiItems);
    for (std::map<unsigned char,WifiItem>::const_iterator it = wifiItemsMap.begin(); it != wifiItemsMap.end(); ++it) {
        Serial.println(it->second.connectionName);
    }
}

// ========================================================================
// MÉTHODES D'INSTANCE - CONNEXION ET STATUS
// ========================================================================
WifiItem WifiManagment::getNextNetwork() {
    Serial.println("Will get next wifi");
    
    // Afficher l'état actuel
    Serial.printf("Current network ID: %d/%d\n", currentNetworkId, nbWifiItems-1);
    
    // ✅ SAUVEGARDER L'INDEX ACTUEL avant incrémentation
    unsigned char networkToReturn = currentNetworkId;
    
    // ✅ INCRÉMENTER pour le prochain appel
    currentNetworkId++;
    
    if (currentNetworkId >= nbWifiItems) {
        Serial.println("Retour au debut de la liste des reseaux");
        currentNetworkId = 0;
    }

    Serial.printf("Returning wifi: %s (index: %d), next will be: %d\r\n", 
                  wifiItemsMap[networkToReturn].connectionName, 
                  networkToReturn,
                  currentNetworkId);
    
    return wifiItemsMap[networkToReturn];	
}

const char* WifiManagment::getErrorString(WiFiError error) const {
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

bool WifiManagment::connectWifi(const WifiItem& currentWifi, const char* hostname) {
    Serial.println("=== WifiManagment::connectWifi ===");

      // ✅ DÉTECTION MODE ACCESS POINT
    if (currentWifi.wifiMode == WIFI_MODE_ACCESS_POINT) {
        Serial.println("⚠️ connectWifi() appelé avec mode ACCESS_POINT - redirection vers initialize()");
        return initialize(currentWifi);
    }

    if (WiFi.getMode() != WIFI_OFF) {
        Serial.println("   Arrêt WiFi existant...");
        WiFi.disconnect(false, false);  // Disconnect doux
        delay(200);
    }
    
    // ✅ Reset propre de la stack WiFi
    WiFi.mode(WIFI_OFF);
    delay(300);  // Délai suffisant pour reset complet
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    delay(300);  // Délai pour stabilisation mode station
    
    Serial.printf("📶 Connexion au réseau: %s...\n", currentWifi.connectionName);
    Serial.printf("Will try to connect to: %s, SSID: %s, PWD: %s, DHCP: %s\r\n",
                  currentWifi.connectionName, currentWifi.SSID, currentWifi.PWD,
                  currentWifi.dhcp ? "YES" : "NO");

    // Fixé dès l'intention de connexion, avant l'attente bloquante ci-dessous : évite la
    // course avec l'event STA_GOT_IP qui peut se déclencher pendant le delay(500) de la
    // boucle d'attente, et qui lirait sinon un currentMode encore périmé.
    currentMode = WIFI_MODE_STATION;

    // 🔧 RESTE DU CODE EXISTANT INCHANGÉ
    if (currentWifi.dhcp) {
        Serial.println("Wifi is with DHCP");
       // WiFi.mode(WIFI_STA);
        Serial.println("Mode défini: station");
    } else {
        IPAddress primaryDNS = currentWifi.dns;
        IPAddress secondaryDNS = IPAddress(8, 8, 4, 4);
        
        if (!WiFi.config(currentWifi.ip, currentWifi.gateway, currentWifi.mask, 
                        primaryDNS, secondaryDNS)) {
            setError(WiFiError::WIFI_CONFIG_ERROR, "Échec configuration IP statique");
            return false;  // ✅ MODIFIÉ : retourner false au lieu de WiFiResult
        }
    }
    
    // Connexion
    WiFi.begin(currentWifi.SSID, currentWifi.PWD);
    wifiStartupTime = millis();
    
    // ✅ RESTE IDENTIQUE...
    uint32_t startTime = millis();
    const uint32_t TIMEOUT_TIME = 20000; // 20 secondes
    
    uint8_t noSsidCount = 0;
    const uint8_t NO_SSID_TOLERANCE = 3;  // tolère les faux négatifs transitoires (~1.5s)
    
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < TIMEOUT_TIME) {
        delay(500);
        
        if (WiFi.status() == WL_CONNECT_FAILED) {
            setError(WiFiError::WIFI_CONNECTION_FAILED, "Authentification échouée");
            return false;
        }
        if (WiFi.status() == WL_NO_SSID_AVAIL) {
            noSsidCount++;
            if (noSsidCount >= NO_SSID_TOLERANCE) {
                setError(WiFiError::WIFI_CONNECTION_FAILED, "SSID introuvable");
                return false;
            }
            continue;
        }
        noSsidCount = 0;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        currentIP = WiFi.localIP();
        currentSSID = WiFi.SSID();
        isConnected = true;
        currentMode = WIFI_MODE_STATION;
        setError(WiFiError::WIFI_SUCCESS, "Connexion réussie");
        Serial.printf("✅ CONNEXION RÉUSSIE: %s\n", currentIP.toString().c_str());
        return true;
    } else {
        setError(WiFiError::WIFI_TIMEOUT, "Timeout de connexion");
        return false;
    }
}// connnectWifi

WifiItem WifiManagment::getCurrentNetwork() {
    if (currentNetworkId >= nbWifiItems) {
        currentNetworkId = 0;  // Sécurité
    }
    
    Serial.printf("Current wifi (no increment): %s, item: %d\r\n", 
                  wifiItemsMap[currentNetworkId].connectionName, currentNetworkId);
    
    return wifiItemsMap[currentNetworkId];
}

bool WifiManagment::startAccessPointMode(const WifiItem& wifiConfig) {
    Serial.println("=== Démarrage Mode Access Point ===");
    
    // Étape 2/3 correctif crash mDNS/AP : arrêter mDNS avant tout changement
    // de mode WiFi. Sans ça, la tâche interne mDNS peut encore référencer
    // l'ancien netif STA au moment où WiFi.mode(WIFI_AP) le détruit, ce qui
    // provoque un LoadProhibited dans _mdns_service_task.
    if (mdnsStarted) {
        Serial.println("🛑 Arrêt mDNS avant bascule Access Point");
        MDNS.end();
        mdnsStarted = false;
        delay(100);  // Laisser la tâche mDNS se stabiliser après l'arrêt
    }
    
    WiFi.mode(WIFI_AP);
    delay(1000);  // Délai plus long
    
    IPAddress gateway = wifiConfig.apIP;
    IPAddress subnet(255, 255, 255, 0);
    
    Serial.printf("Configuration AP: IP=%s, Gateway=%s\n", 
                  wifiConfig.apIP.toString().c_str(), gateway.toString().c_str());
    
    if (!WiFi.softAPConfig(wifiConfig.apIP, gateway, subnet)) {
        Serial.println("❌ Erreur configuration IP Access Point");
        return false;
    }
    
    if (!WiFi.softAP(wifiConfig.apName.c_str(), wifiConfig.apPassword.c_str(), wifiConfig.apChannel)) {
        Serial.println("❌ Erreur démarrage Access Point");
        return false;
    }
    
    delay(3000);  // Délai plus long pour stabilisation
    
    // *** SOLUTION: FORCER L'IP CONFIGURÉE ***
    IPAddress retrievedIP = WiFi.softAPIP();
    Serial.printf("IP récupérée par WiFi.softAPIP(): %s\n", retrievedIP.toString().c_str());
    
    if (retrievedIP == IPAddress(0, 0, 0, 0)) {
        Serial.println("⚠️  WiFi.softAPIP() retourne 0.0.0.0, utilisation IP configurée");
        currentIP = wifiConfig.apIP;  // Forcer l'IP du config
    } else {
        currentIP = retrievedIP;
    }
    
    currentAPName = wifiConfig.apName;
    isConnected = true;
    
    // *** IMPORTANT: Synchroniser APRÈS avoir forcé l'IP ***
    setWifiConnected(true);
    
    Serial.printf("✅ Access Point démarré: %s\n", currentAPName.c_str());
    Serial.printf("✅ IP finale forcée: %s\n", currentIP.toString().c_str());
    Serial.printf("✅ Canal: %d\n", wifiConfig.apChannel);
    
    if (onGotIPCallback) {
        Serial.println("🌐 Démarrage manuel du serveur web pour Access Point");
        onGotIPCallback();
    }
    
    return true;
} //startAccessPointMode

void WifiManagment::enableWifi(bool status) {
    Serial.printf("WifiManagment::enableWifi, status: %s, wifienabled: %s\r\n", 
                  status ? "true" : "false", wifiEnabled ? "true" : "false");
    
    if (status && !wifiEnabled) {	
        setCpuFrequencyMhz(240);
        Serial.flush();
        Serial.println("Will set wifi mode to WIFI_STA");
        WiFi.mode(WIFI_STA);
        wifiEnabled = true;
        wifiStartupTime = millis();
        Serial.printf("%lu:: Wifi is now ENABLED\r\n", millis());
    }

    if (!status && wifiEnabled) {
        WiFi.disconnect(true, true);
        wifiEnabled = false;
        WiFi.mode(WIFI_OFF);
        Serial.println("Wifi is now DISABLED");
    }
    Serial.flush();
}

bool WifiManagment::isWifiEnabled() const {
    return wifiEnabled;
}

long WifiManagment::getWifiStartupTime() const {
    return wifiStartupTime;
}

bool WifiManagment::isWifiConnectionTimeout() const {
    // ✅ TIMEOUT RÉDUIT: 15 secondes au lieu de plus
    const unsigned long TIMEOUT_WIFI_REDUCED = 15000; // 15 secondes
    
    if (!wificonnected && millis() - wifiStartupTime > TIMEOUT_WIFI_REDUCED) {
        Serial.printf("%lu:: ⏰ Wifi connection timeout après %lu ms\r\n", 
                     millis(), millis() - wifiStartupTime);
        return true;
    }
    return false;
}

// ========================================================================
// MÉTHODES D'INSTANCE - NOUVELLES FONCTIONNALITÉS
// ========================================================================
bool WifiManagment::initialize(const WifiItem& wifiConfig) {
    Serial.println("=== WifiManagment::initialize ===");
    
    String errorMsg;
    if (!validateConfiguration(wifiConfig, errorMsg)) {
        Serial.printf("Configuration invalide: %s\n", errorMsg.c_str());
        return false;
    }

     // Étape 2/3 (renforcée) correctif crash mDNS/AP : arrêt inconditionnel de
    // mDNS avant TOUT changement de mode WiFi, y compris ce WIFI_OFF.
    // MDNS.end() est un no-op sûr si mDNS n'a pas été démarré. Appel
    // inconditionnel (pas gardé par mdnsStarted) car mdnsStarted ne reflète
    // pas un MDNS.begin() fait hors de cette classe (ex: WiFiGotIP() dans
    // main.cpp de Formaca). Voir chantier "crash mDNS/AP" du 30/07.
    Serial.println("🛑 Arrêt mDNS avant tout changement de mode WiFi");
    MDNS.end();
    mdnsStarted = false;
    delay(100);
    
    WiFi.mode(WIFI_OFF);
    delay(500);

    // Fixé dès l'intention, avant de démarrer le mode — même raison que dans connectWifi() :
    // startStationMode()/startAccessPointMode() appellent setWifiConnected() en interne,
    // avant que ce point ne soit atteint si on attend le retour de la fonction.
    currentMode = wifiConfig.wifiMode;

    bool success = false;
    if (wifiConfig.wifiMode == WIFI_MODE_STATION) {
        // startStationMode() supprimée (code mort confirmé — initialize()
        // n'est atteint qu'en mode Access Point, via la redirection de
        // connectWifi()). Le mode Station passe exclusivement par
        // connectWifi(), jamais par initialize().
        Serial.println("❌ initialize() ne supporte pas le mode Station - utiliser connectWifi()");
        success = false;
    } else if (wifiConfig.wifiMode == WIFI_MODE_ACCESS_POINT) {
        success = startAccessPointMode(wifiConfig);
    }

    if (success) {
        Serial.printf("WiFi initialisé en mode %s\n", currentMode.c_str());
    }

    return success;
}

bool WifiManagment::validateConfiguration(const WifiItem& wifiConfig, String& errorMsg) {
    if (wifiConfig.wifiMode == WIFI_MODE_STATION) {
        if (strlen(wifiConfig.SSID) == 0) {
            errorMsg = "SSID manquant pour mode Station";
            return false;
        }
        if (strlen(wifiConfig.PWD) == 0) {
            errorMsg = "Mot de passe manquant pour mode Station";
            return false;
        }
    } else if (wifiConfig.wifiMode == WIFI_MODE_ACCESS_POINT) {
        if (wifiConfig.apName.length() == 0) {
            errorMsg = "Nom Access Point manquant";
            return false;
        }
        if (wifiConfig.apPassword.length() < 8) {
            errorMsg = "Mot de passe Access Point trop court (min 8 caractères)";
            return false;
        }
    } else {
        errorMsg = "Mode WiFi invalide: " + wifiConfig.wifiMode;
        return false;
    }
    
    return true;
}

String WifiManagment::getDetailedStatus() const {
    String status = "WiFi Status: ";
    
    if (currentMode == "none") {
        status += "Non configuré";
    } else if (currentMode == WIFI_MODE_STATION) {
        status += "Station - ";
        if (isConnected) {
            status += "Connecté à " + currentSSID + " (" + currentIP.toString() + ")";
        } else {
            status += "Déconnecté";
        }
    } else if (currentMode == WIFI_MODE_ACCESS_POINT) {
        status += "Access Point - ";
        if (isConnected) {
            status += currentAPName + " actif (" + currentIP.toString() + ")";
        } else {
            status += "Inactif";
        }
    }
    
    return status;
}

bool WifiManagment::isOperational() const {
    // ✅ CORRECTION : Vérifier le vrai statut WiFi d'abord
    bool wifiHwConnected = WiFi.isConnected();
    
    if (currentMode == WIFI_MODE_STATION) {
        // Mode Station : se baser sur le hardware WiFi
        return wifiHwConnected;
    } else if (currentMode == WIFI_MODE_ACCESS_POINT) {
        // Mode AP : vérifier que nous avons une IP valide ET que le mode est correct
        return isConnected && 
               (currentIP != IPAddress(0, 0, 0, 0)) &&
               (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
    } else if (currentMode == "none" || currentMode.isEmpty()) {
        // ✅ NOUVEAU : Mode pas encore initialisé - se baser sur le hardware
        if (wifiHwConnected) {
            Serial.println("⚠️ Mode WiFi non défini mais hardware connecté");
            return true;  // Hardware connecté = opérationnel
        }
        return false;
    }
    return false;
}

void WifiManagment::shutdown() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    isConnected = false;
    currentMode = "none";
    Serial.println("WiFi arrêté");
}

void WifiManagment::update() {
    // Méthode appelée régulièrement pour maintenir la connexion
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 30000) { // Check toutes les 30 secondes
        lastCheck = millis();
        
        if (currentMode == WIFI_MODE_STATION && !WiFi.isConnected()) {
            Serial.println("Connexion WiFi perdue, tentative de reconnexion...");
            isConnected = false;
        }
    }
}

void WifiManagment::showCurrentStatus() {
    Serial.println(getDetailedStatus());
}

void WifiManagment::showIpAddress() {
    // ✅ Vérification cohérente de l'état, selon le mode courant
    // WiFi.isConnected() ne reflète QUE l'interface Station — non pertinent en mode AP.
    bool wifiHwConnected = WiFi.isConnected();
    bool localStatus = wificonnected;
    bool coherent = (currentMode == WIFI_MODE_ACCESS_POINT) ? localStatus
                                                              : (wifiHwConnected && localStatus);

    Serial.printf("=== STATUS WIFI DÉTAILLÉ ===\n");
    Serial.printf("WiFi.isConnected(): %s\n", wifiHwConnected ? "true" : "false");
    Serial.printf("wificonnected (local): %s\n", localStatus ? "true" : "false");
    Serial.printf("WiFi.status(): %d\n", WiFi.status());

    if (coherent) {
        if (currentMode == WIFI_MODE_STATION) {
            Serial.printf("✓ Mode Station - IP: %s, Réseau: %s, RSSI: %d dBm\n",
                         WiFi.localIP().toString().c_str(), WiFi.SSID().c_str(), WiFi.RSSI());
        } else if (currentMode == WIFI_MODE_ACCESS_POINT) {
            Serial.printf("✓ Access Point actif - IP: %s, SSID: %s, Client: %s\n",
                         WiFi.softAPIP().toString().c_str(), currentAPName.c_str(),
                         WiFi.softAPgetStationNum() > 0 ? "connecté" : "aucun");
        }
    } else {
        Serial.println("⚠️ WiFi non connecté ou état incohérent");
        // Auto-correction seulement pertinente hors AP (où WiFi.isConnected() fait foi) —
        // en AP, wifiHwConnected est structurellement toujours false, la comparaison n'a
        // pas de sens et ne doit pas déclencher de correction.
        if (currentMode != WIFI_MODE_ACCESS_POINT && wifiHwConnected != localStatus) {
            Serial.printf("   INCOHÉRENCE: Hardware=%s, Local=%s\n",
                         wifiHwConnected ? "connecté" : "déconnecté",
                         localStatus ? "connecté" : "déconnecté");
            // ✅ Auto-correction de l'état incohérent
            setWifiConnected(wifiHwConnected);
        }
    }
    Serial.println("===========================");
}

IPAddress WifiManagment::getCurrentIP() const {
    return currentIP;
}

void WifiManagment::setWifiConnected(bool status) {
    Serial.printf("WifiManagment::setWifiConnected appelé avec status=%s\n", 
                  status ? "true" : "false");
    
    if (wificonnected && !status) {
        wifiStartupTime = millis();
    }
    wificonnected = status;
    isConnected = status;
    
    if (status) {
        Serial.printf("Mode actuel: %s\n", currentMode.c_str());  // DEBUG
        
        if (currentMode == WIFI_MODE_ACCESS_POINT) {
            // En mode AP, currentIP est déjà définie dans startAccessPointMode()
            Serial.printf("Mode AP: IP conservée = %s, SSID AP = %s\n", 
                         currentIP.toString().c_str(), currentAPName.c_str());
        } else if (currentMode == WIFI_MODE_STATION) {
            // Mode Station uniquement
            currentIP = WiFi.localIP();
            currentSSID = WiFi.SSID();
            Serial.printf("Mode Station: IP = %s, SSID = %s, RSSI = %d dBm\n", 
                         currentIP.toString().c_str(), currentSSID.c_str(), WiFi.RSSI());
        }
    } else {
        Serial.println("WiFi déconnecté");
    }
}

void WifiManagment::printIPAddress(const char *name, IPAddress ip, bool ret) {
    Serial.printf("%s: %d.%d.%d.%d", name, ip[0], ip[1], ip[2], ip[3]);
    if (ret) {
        Serial.println();
    }
}

void WifiManagment::printWifiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_READY: 
            Serial.println("WiFi interface ready");
            break;
        case ARDUINO_EVENT_WIFI_SCAN_DONE:
            Serial.println("Completed scan for access points");
            break;
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.println("WiFi client started");
            break;
        case ARDUINO_EVENT_WIFI_STA_STOP:
            Serial.println("WiFi clients stopped");
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("Connected to access point");
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("Disconnected from WiFi access point");
            break;
        case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
            Serial.println("Authentication mode of access point has changed");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("Obtained IP address: ");
            Serial.println(WiFi.localIP());
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            Serial.println("Lost IP address and IP address is reset to 0");
            break;
        case ARDUINO_EVENT_WPS_ER_SUCCESS:
            Serial.println("WiFi Protected Setup (WPS): succeeded in enrollee mode");
            break;
        case ARDUINO_EVENT_WPS_ER_FAILED:
            Serial.println("WiFi Protected Setup (WPS): failed in enrollee mode");
            break;
        case ARDUINO_EVENT_WPS_ER_TIMEOUT:
            Serial.println("WiFi Protected Setup (WPS): timeout in enrollee mode");
            break;
        case ARDUINO_EVENT_WIFI_AP_START:
            Serial.println("WiFi access point started");
            break;
        case ARDUINO_EVENT_WIFI_AP_STOP:
            Serial.println("WiFi access point  stopped");
            break;
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            Serial.println("Client connected");
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            Serial.println("Client disconnected");
            break;
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
            Serial.println("Assigned IP address to client");
            break;
        case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
            Serial.println("Received probe request");
            break;
        case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
            Serial.println("AP IPv6 is preferred");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
            Serial.println("STA IPv6 is preferred");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP6:
            Serial.println("Ethernet IPv6 is preferred");
            break;
        case ARDUINO_EVENT_ETH_START:
            Serial.println("Ethernet started");
            break;
        case ARDUINO_EVENT_ETH_STOP:
            Serial.println("Ethernet stopped");
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("Ethernet connected");
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("Ethernet disconnected");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.println("Obtained IP address");
            break;
        default: break;
    }
}

WiFiResult WifiManagment::validateWifiConfiguration(const WifiItem& config) {
    // Validation SSID
    if (strlen(config.SSID) == 0) {
        return WiFiResult(WiFiError::WIFI_INVALID_SSID, "SSID vide");
    }
    if (strlen(config.SSID) > 32) {
        return WiFiResult(WiFiError::WIFI_INVALID_SSID, "SSID trop long (max 32 caractères)");
    }
    
    // Validation mot de passe
    if (strlen(config.PWD) == 0) {
        return WiFiResult(WiFiError::WIFI_INVALID_PASSWORD, "Mot de passe vide");
    }
    if (strlen(config.PWD) < 8) {
        return WiFiResult(WiFiError::WIFI_INVALID_PASSWORD, "Mot de passe trop court (min 8 caractères)");
    }
    
    // Validation IP statique
    if (!config.dhcp) {
        if (config.ip == IPAddress(0, 0, 0, 0)) {
            return WiFiResult(WiFiError::WIFI_INVALID_IP, "IP statique 0.0.0.0 invalide");
        }
        if (config.gateway == IPAddress(0, 0, 0, 0)) {
            return WiFiResult(WiFiError::WIFI_INVALID_IP, "Gateway 0.0.0.0 invalide");
        }
        
        // Validation cohérence réseau
        IPAddress network = IPAddress(config.ip[0] & config.mask[0], 
                                     config.ip[1] & config.mask[1],
                                     config.ip[2] & config.mask[2], 
                                     config.ip[3] & config.mask[3]);
        
        IPAddress gatewayNetwork = IPAddress(config.gateway[0] & config.mask[0],
                                           config.gateway[1] & config.mask[1], 
                                           config.gateway[2] & config.mask[2],
                                           config.gateway[3] & config.mask[3]);
        
        if (network != gatewayNetwork) {
            return WiFiResult(WiFiError::WIFI_INVALID_IP, 
                            "IP et Gateway dans des sous-réseaux différents");
        }
    }
    
    return WiFiResult(WiFiError::WIFI_SUCCESS);
}

WiFiResult WifiManagment::connectWifiWithResult(const WifiItem& currentWifi, const char* hostname) {
    Serial.println("=== WifiManagment::connectWifiWithResult ===");
    // Validation préalable
    WiFiResult validation = validateWifiConfiguration(currentWifi);
    if (!validation.isSuccess()) {
        setError(validation.error, validation.message);
        return validation;
    }
    
    Serial.printf("=== CONNEXION WiFi AVEC GESTION D'ERREUR ===\n");
    
    // Déconnexion propre
    WiFi.disconnect(true, true);
    delay(100);
    
    // Mode station
    WiFi.mode(WIFI_STA);
    delay(100);
    
    // Hostname avec validation
    if (hostname != nullptr && strlen(hostname) > 0) {
        if (strlen(hostname) > 63) {
            setError(WiFiError::WIFI_HOSTNAME_ERROR, "Hostname trop long");
            return WiFiResult(WiFiError::WIFI_HOSTNAME_ERROR, "Hostname trop long (max 63 caractères)");
        }
        
        // Nettoyage config IP
        if (!WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE)) {
            setError(WiFiError::WIFI_CONFIG_ERROR, "Impossible de nettoyer config IP");
            return WiFiResult(WiFiError::WIFI_CONFIG_ERROR, "Échec nettoyage config IP");
        }
        
        if (!WiFi.setHostname(hostname)) {
            Serial.printf("ATTENTION: Échec hostname %s (continue quand même)\n", hostname);
        }
    }
    
    // Configuration IP
    if (!currentWifi.dhcp) {
        IPAddress primaryDNS = (currentWifi.dns != IPAddress(0, 0, 0, 0)) ? 
                               currentWifi.dns : IPAddress(8, 8, 8, 8);
        IPAddress secondaryDNS = IPAddress(8, 8, 4, 4);
        
        if (!WiFi.config(currentWifi.ip, currentWifi.gateway, currentWifi.mask, 
                        primaryDNS, secondaryDNS)) {
            setError(WiFiError::WIFI_CONFIG_ERROR, "Échec configuration IP statique");
            return WiFiResult(WiFiError::WIFI_CONFIG_ERROR, "Impossible d'appliquer IP statique");
        }
    } else {
        if (!WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), 
                        IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0))) {
            Serial.println("ATTENTION: Impossible de réinitialiser vers DHCP");
        }
    }
    
    // Connexion
    WiFi.begin(currentWifi.SSID, currentWifi.PWD);
    wifiStartupTime = millis();
    
    // Attente avec timeout
    uint32_t startTime = millis();
    
    uint8_t noSsidCount = 0;
    const uint8_t NO_SSID_TOLERANCE = 3;

    while (WiFi.status() != WL_CONNECTED && millis() - startTime < TIMEOUT_TIME) {
        delay(500);
        
        if (WiFi.status() == WL_CONNECT_FAILED) {
            setError(WiFiError::WIFI_CONNECTION_FAILED, "Authentification échouée");
            return WiFiResult(WiFiError::WIFI_CONNECTION_FAILED, "Mot de passe incorrect");
        }
        if (WiFi.status() == WL_NO_SSID_AVAIL) {
            noSsidCount++;
            if (noSsidCount >= NO_SSID_TOLERANCE) {
                setError(WiFiError::WIFI_CONNECTION_FAILED, "SSID introuvable");
                return WiFiResult(WiFiError::WIFI_CONNECTION_FAILED, "Réseau introuvable");
            }
            continue;
        }
        noSsidCount = 0;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        currentIP = WiFi.localIP();
        currentSSID = WiFi.SSID();
        isConnected = true;
        setError(WiFiError::WIFI_SUCCESS, "Connexion réussie");
        Serial.printf("✓ CONNEXION RÉUSSIE: %s\n", currentIP.toString().c_str());
        return WiFiResult(WiFiError::WIFI_SUCCESS, "Connexion établie");
    } else {
        setError(WiFiError::WIFI_TIMEOUT, "Timeout de connexion");
        return WiFiResult(WiFiError::WIFI_TIMEOUT, "Délai d'attente dépassé");
    }
}

String WifiManagment::getCurrentConfigMode() const {
    // Supprimer la comparaison >= 0 car currentNetworkId est unsigned char
    if (currentNetworkId < nbWifiItems) {
        auto it = wifiItemsMap.find(currentNetworkId);
        if (it != wifiItemsMap.end()) {
            return it->second.wifiMode;
        }
    }
    return "unknown";
}

void WifiManagment::initializeEventHandlers() {
    if (eventHandlersInitialized) {
        Serial.println("Event handlers WiFi déjà initialisés");
        return;
    }
    
    Serial.println("=== INITIALISATION EVENT HANDLERS WiFi ===");
    
    // === EVENT: STATION GOT IP (Principal) ===
    WiFi.onEvent([](arduino_event_t *event) {
        Serial.println("📶 EVENT: WiFi Station Got IP");
        
        WifiManagment& instance = getInstance();
        
        // Mettre à jour le mode et les infos
        instance.currentMode = WIFI_MODE_STATION;
        instance.currentIP = WiFi.localIP();
        instance.currentSSID = WiFi.SSID();
        instance.isConnected = true;
        
        Serial.printf("Mode: %s, IP: %s, SSID: %s\n", 
                     instance.currentMode.c_str(),
                     instance.currentIP.toString().c_str(),
                     instance.currentSSID.c_str());
        
        // Synchroniser avec BorneUniverselle
        instance.setWifiConnected(true);
        
        // Callback utilisateur (serveur web, etc.)
        if (instance.onGotIPCallback) {
            Serial.println("📶 CALLBACK: Got IP - Démarrage serveur web");
            instance.onGotIPCallback();
        }
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    
    // === EVENT: STATION DISCONNECTED ===
    WiFi.onEvent([](arduino_event_t *event) {
        Serial.println("📶 EVENT: WiFi Station Disconnected");

        // Diagnostic : décoder la raison ESP-IDF de la déconnexion (chantier
        // "Ella ne se connecte pas" — sans ça on ne fait que deviner).
        uint8_t reason = event->event_info.wifi_sta_disconnected.reason;
        Serial.printf("   Raison ESP-IDF (code %d): ", reason);
        switch (reason) {
            case WIFI_REASON_UNSPECIFIED:
                Serial.println("Unspecified");
                break;
            case WIFI_REASON_AUTH_EXPIRE:
                Serial.println("Authentication expired");
                break;
            case WIFI_REASON_NOT_AUTHED:
                Serial.println("Not authenticated");
                break;
            case WIFI_REASON_NOT_ASSOCED:
                Serial.println("Not associated");
                break;
            case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
                Serial.println("Supported channel bad");
                break;
            case WIFI_REASON_MIC_FAILURE:
                Serial.println("MIC failure");
                break;
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                Serial.println("4-way handshake timeout");
                break;
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
                Serial.println("Handshake timeout");
                break;
            case WIFI_REASON_CONNECTION_FAIL:
                Serial.println("Connection failed");
                break;
            case WIFI_REASON_NO_AP_FOUND:
                Serial.println("No AP found");
                break;
            case WIFI_REASON_AUTH_FAIL:
                Serial.println("Authentication failed");
                break;
            case WIFI_REASON_ASSOC_FAIL:
                Serial.println("Association failed");
                break;
            case WIFI_REASON_BEACON_TIMEOUT:
                Serial.println("Beacon timeout");
                break;
            default:
                Serial.printf("Unknown (%d)\n", reason);
                break;
        }

        WifiManagment& instance = getInstance();

        // Mettre à jour le statut
        instance.isConnected = false;
        instance.currentSSID = "";
        instance.currentIP = IPAddress(0, 0, 0, 0);

        // Synchroniser avec BorneUniverselle
        instance.setWifiConnected(false);

        // Callback utilisateur (reconnexion)
        if (instance.onDisconnectedCallback) {
            Serial.println("📶 CALLBACK: Disconnected - Tentative reconnexion");
            instance.onDisconnectedCallback();
        }
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    eventHandlersInitialized = true;
    Serial.println("✅ Event handlers WiFi initialisés (Station uniquement)");
    Serial.println("   - Pour Access Point: démarrage manuel du serveur");
}

unsigned char WifiManagment::addItem(const WifiItem& wifiConfig) {
    if (nbWifiItems >= NB_MAX_WIFI) {
        Serial.println(F("Le nombre réseaux wifi enregistré est déjà au maximum !"));
        return nbWifiItems;
    }
    
    // Copier directement la configuration complète
    wifiItemsMap[nbWifiItems++] = wifiConfig;
    
    Serial.printf("Configuration WiFi ajoutée: %s (mode: %s)\n", 
                  wifiConfig.connectionName, wifiConfig.wifiMode.c_str());
    
    if (wifiConfig.wifiMode == WIFI_MODE_STATION) {
        Serial.printf("  SSID: %s, DHCP: %s\n", 
                      wifiConfig.SSID, wifiConfig.dhcp ? "OUI" : "NON");
    } else if (wifiConfig.wifiMode == WIFI_MODE_ACCESS_POINT) {
        Serial.printf("  AP Name: %s, IP: %s\n", 
                      wifiConfig.apName.c_str(), wifiConfig.apIP.toString().c_str());
    }
    
    return nbWifiItems;
}
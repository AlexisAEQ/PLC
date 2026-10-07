#define ARDUINOJSON_ENABLE_COMMENTS 1
#include "MyToolBox/MyToolBox.h" 
#include "ESPAsyncWebServer.h"
#include <ESPmDNS.h>
#include <map>
#include <vector>
#include "esp_wifi.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "BorneUniverselle/borneUniverselle.h"
#include "EthernetManager/EthernetManager.h"
#include "WifiManagement/wifimanagment.h"
#include "RessortRoyal2/RessortRoyal2.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ========================================
// DÉFINITIONS ET VERSIONS
// ========================================
#define MAIN_VERSION "RessortRoyal2 v1.0"
#define OTA_STARTED "HTTP update process started"
#define OTA_FINISHED "HTTP update process finished"

// ========================================
// VARIABLES GLOBALES
// ========================================
BorneUniverselle *bu;
RessortRoyal2 *ressortRoyal2;
MyToolBox toolbox;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
bool memoryMonitorBool = false;
MemoryMonitor memoryMonitor;
uint32_t lastMessageTime = 0;
uint32_t lastTime = 0;


long start;
bool scheduleReconnect = false;
uint32_t reconnectAt = 0;
bool wifiEverConnected = false;

// Déferrement du traitement WiFiGotIP hors du contexte arduino_events (pile réduite)
volatile bool pendingWifiGotIP = false;

// serverStarted et mdnsInitialized globalisés (nécessaire pour le split callback/loop)
bool serverStarted = false;
bool mdnsInitialized = false;

// Variables de performance LOOP (fréquence globale)
uint32_t loopCounter = 0;
uint32_t lastLoopStatsTime = 0;
uint32_t lastLoopCount = 0;
float currentLoopsPerSecond = 0.0f;  // Fréquence commune à toutes les mesures

// Variables de temps LOOP
uint32_t minLoopTime = UINT32_MAX;
uint32_t maxLoopTime = 0;
uint32_t totalLoopTime = 0;
uint32_t avgLoopTime = 0;

// Variables de temps REFRESH
uint32_t minRefreshTime = UINT32_MAX;
uint32_t maxRefreshTime = 0;
uint32_t totalRefreshTime = 0;
uint32_t avgRefreshTime = 0;
uint32_t refreshCounter = 0;

// Variables de temps LOGIC
uint32_t minLogicTime = UINT32_MAX;
uint32_t maxLogicTime = 0;
uint32_t totalLogicTime = 0;
uint32_t avgLogicTime = 0;
uint32_t logicCounter = 0;

// ========================================
// GESTION WIFI
// ========================================

void connectToNextNetwork(){
    static uint8_t timeoutCount = 0;

    // Première connexion ou vrai timeout → utilise la liste de réseaux configurés
    if (!wifiEverConnected || bu->isWifiConnectionTimeout()) {
        timeoutCount++;
        if (!wifiEverConnected) {
            Serial.println("🔄 Première connexion WiFi...");
        } else {
            Serial.printf("⚠️ Timeout #%u → essai réseau suivant\n", timeoutCount);
        }
        bool criterionOK = false;
        WifiItem currentWifi;
        while (!criterionOK) {
            currentWifi = bu->getNextNetwork();
            criterionOK = true;
        }
        bu->connectWifi(currentWifi);
    } else {
        // Déconnexion brève : reconnect() léger sans recréer le stack WiFi complet
        timeoutCount = 0;
        Serial.printf("🔄 Reconnexion rapide (heap: %lu)\n", ESP.getFreeHeap());
        WiFi.reconnect();
    }
} // connectToNextNetwork

// Callbacks WiFi
void WiFiStationConnected() {
    Serial.printf("%lu:: ✅ Connected to AP successfully\r\n", millis());
    Serial.printf("   📡 SSID: %s\r\n", WiFi.SSID().c_str());
    Serial.printf("   📶 Signal: %d dBm\r\n", WiFi.RSSI());
    Serial.printf("   📍 BSSID: %s\r\n", WiFi.BSSIDstr().c_str());
    Serial.printf("   📡 Channel: %ld\r\n", (long)WiFi.channel());
    Serial.println("   ⏳ Waiting for IP assignment...");
}

void turnOffBuzzer(){
    // Buzzer déclaré par le fichier matériel de la carte (KinCony A8S : GPIO2)
    int8_t pin = bu->getBuzzerPin();
    if (pin < 0) {
        return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, bu->isBuzzerActiveLow() ? HIGH : LOW);
}

// Adresse obtenue sur l'Ethernet : mDNS et serveur web comme pour le WiFi.
void processEthernetGotIP() {
    EthernetManager& eth = EthernetManager::getInstance();
    Serial.printf("🌐 Ethernet : adresse %s\n", eth.localIP().toString().c_str());
    if (!mdnsInitialized) {
        if (MDNS.begin(bu->getName())) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("MDNS responder started with name %s\n", bu->getName());
            mdnsInitialized = true;
        }
    }
    if (!serverStarted) {
        server.begin();
        Serial.println("Web server started (Ethernet) !");
        serverStarted = true;
    }
}

void processWifiGotIP() {
    turnOffBuzzer();
    bu->setWifiConnected(true);

    // Vérifier si MDNS est déjà initialisé (mdnsInitialized globalisée)
    if (!mdnsInitialized) {
        if (!MDNS.begin(bu->getName())) {
            Serial.printf("Error setting up MDNS responder with name %s\n", bu->getName());
        } else {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("MDNS responder started with name %s\n", bu->getName());
            mdnsInitialized = true;
        }
    }

    wifiEverConnected = true;  // Marque qu'on a eu au moins une connexion réussie
    scheduleReconnect = false; // Annuler toute reconnexion pending — on est connecté

    // Vérifier si le serveur est déjà démarré (serverStarted globalisée)
    if (!serverStarted) {
        server.begin();
        Serial.println("Web server started !");
        serverStarted = true;
    }

    // ✅ DÉTECTION DU MODE WiFi
    wifi_mode_t currentMode = WiFi.getMode();
    bool isAccessPoint = (currentMode == WIFI_AP || currentMode == WIFI_AP_STA);
    
    // ✅ AFFICHAGE AMÉLIORÉ - Adapté au mode
    Serial.println();
    Serial.println("████████████████████████████████████████████████████████████████");
    if (isAccessPoint) {
        Serial.println("█              ACCESS POINT DÉMARRÉ AVEC SUCCÈS                █");
    } else {
        Serial.println("█                    WIFI CONNECTÉ AVEC SUCCÈS                 █");
    }
    Serial.println("████████████████████████████████████████████████████████████████");
    
    if (isAccessPoint) {
        // Mode Access Point - utiliser les fonctions AP
        Serial.printf("📡 Access Point   : %s\n", WifiManagment::getInstance().getCurrentMode().c_str());
        Serial.printf("📍 Adresse IP     : %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("🎭 Masque         : 255.255.255.0\n");
        Serial.printf("📍 MAC Address    : %s\n", WiFi.softAPmacAddress().c_str());
        Serial.printf("🌐 Interface web  : http://%s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("🏷️  mDNS          : http://%s.local\n", bu->getName());
        Serial.printf("👥 Clients connectés : %d\n", WiFi.softAPgetStationNum());
    } else {
        // Mode Station - utiliser les fonctions STA
        Serial.printf("📡 Réseau WiFi    : %s\n", WiFi.SSID().c_str());
        Serial.printf("📍 Adresse IP     : %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("🚪 Passerelle     : %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("🔍 DNS            : %s\n", WiFi.dnsIP().toString().c_str());
        Serial.printf("🎭 Masque         : %s\n", WiFi.subnetMask().toString().c_str());
        Serial.printf("📶 Force signal   : %d dBm\n", WiFi.RSSI());
        Serial.printf("📍 MAC Address    : %s\n", WiFi.macAddress().c_str());
        Serial.printf("🌐 Interface web  : http://%s\n", WiFi.localIP().toString().c_str());
        Serial.printf("🏷️  mDNS          : http://%s.local\n", bu->getName());
    }
    
    Serial.println("████████████████████████████████████████████████████████████████");
    Serial.println();
} // processWifiGotIP

void WiFiGotIP() {
    // Déferrement volontaire : cette fonction est appelée depuis le contexte
    // arduino_events (pile réduite) via setOnGotIPCallback ET WiFi.onEvent
    // (ARDUINO_EVENT_WIFI_STA_GOT_IP). Le traitement réel est fait dans loop()
    // sur la tâche principale. Ne rien ajouter ici de plus lourd qu'un flag.
    pendingWifiGotIP = true;
} // WiFiGotIP

void WiFiStationDisconnected(){
    // NE JAMAIS bloquer ici — on est dans le contexte du task lwIP
    bu->setWifiConnected(false);
    Serial.println(F("🔴 WiFi déconnecté"));
    // Le mDNS reste actif tant que l'Ethernet a une adresse.
    if (!EthernetManager::getInstance().hasIP()) {
        MDNS.end();
        mdnsInitialized = false;  // permettre réinit au prochain GotIP
    }

    static uint32_t lastDisconnectTime = 0;
    static uint8_t failureCount = 0;
    uint32_t currentTime = millis();

    if (currentTime - lastDisconnectTime < 5000) {
        failureCount++;
    } else {
        failureCount = 1;
    }
    lastDisconnectTime = currentTime;

    uint32_t delayMs = 1000;
    if (failureCount >= 3) delayMs = 5000;
    else if (failureCount >= 2) delayMs = 2000;

    Serial.printf("⏰ Reconnexion dans %lums (via loop)\n", delayMs);
    reconnectAt = currentTime + delayMs;
    scheduleReconnect = true;
}// WiFiStationDisconnected

// ========================================
// GESTION OTA (OVER THE AIR UPDATE)
// ========================================

void update_started() {
    Serial.println(OTA_STARTED);
    bu->clearAllClientNotifications();
    BorneUniverselle::prepareMessage(INFO, OTA_STARTED);
}

void update_finished() {
    Serial.println(OTA_FINISHED);
    bu->clearAllClientNotifications();
    BorneUniverselle::prepareMessage(SUCCESS, OTA_FINISHED);
}

void update_progress(int cur, int total) {
    Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
}

void update_error(int err) {
    Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
    bu->clearAllClientNotifications();
    char errorMsg[100];
    snprintf(errorMsg, sizeof(errorMsg), "OTA Update Error: %d", err);
    BorneUniverselle::prepareMessage(ERROR, errorMsg);
}

// ========================================
// COMMANDES SÉRIE
// ========================================

void modifyLogs(){
    while (Serial.available()){
        Serial.read();
    }

    Serial.println("Logs");
    Serial.println("----");
    Serial.println("A: Activer les logs heartbeat");
    Serial.println("B: Désactiver les logs heartbeat");
    Serial.println("C: Activer les logs modbus");
    Serial.println("D: Désactiver les logs modbus");
    Serial.println("E: Moniteur de la mémoire");
    Serial.println("-----------------------------");
    Serial.println("Tapez sur une lettre...");

    while (!Serial.available()){
        delay(100);
    }
    
    char car = Serial.read();
    switch (car){
        case 'A':
            bu->setShowHeartbeatMessages(true);
            break;

        case 'B':
            bu->setShowHeartbeatMessages(false);
            break;

        case 'C':
            bu->setShowModbusMessages(true);
            break;

        case 'D':
            bu->setShowModbusMessages(false);
            break;

        case 'E':
            MemoryMonitor::printFlashStats("MemoryMonitor");
            MemoryMonitor::printStats("MemoryMonitor");
            break;

        default:
            Serial.printf("Command interpreter: Key: %c not attributed !!\r\n", car);
    }  
}

void showHelp(){
    toolbox.clearScreen();
    Serial.println("Help");
    Serial.println(F("-----------"));  
    const char compile_date[] = __DATE__ " " __TIME__;  
    Serial.printf("Version du firmware: %s, date de compilation: %s\r\n", MAIN_VERSION, compile_date);
    
    // ✅ CORRECTION : Vérifier le statut WiFi selon le mode
    wifi_mode_t currentMode = WiFi.getMode();
    bool isAccessPoint = (currentMode == WIFI_AP || currentMode == WIFI_AP_STA);
    bool wifiOperational = false;
    
    if (isAccessPoint) {
        // Mode Access Point : vérifier que l'AP est actif
        wifiOperational = (WiFi.softAPIP() != IPAddress(0, 0, 0, 0));
    } else {
        // Mode Station : vérifier la connexion matérielle
        wifiOperational = WiFi.isConnected();
    }
    
    bool buWifiStatus = bu->getWifiStatus();  // Status interne BU
    
    if (wifiOperational) {
        // ✅ WiFi opérationnel - afficher infos
        WifiManagment::getInstance().showIpAddress();
        
        // ⚠️ Signaler incohérence si nécessaire
        if (!buWifiStatus) {
            Serial.println("⚠️  Note: Synchronisation BorneUniverselle en cours...");
        }
    } else {
        // ❌ WiFi non opérationnel
        Serial.println("❌ STATUT WIFI : DÉCONNECTÉ");
        Serial.println("   🔄 Tentatives de reconnexion en cours...");
        Serial.printf("   📊 Réseaux configurés : %d\n", 4); // ou bu->getNbWifiItems() si accessible
    }
        
    Serial.println("Tapez sur 'A' ou 'a' pour effacer les réseaux wifi");
    Serial.println("Tapez sur 'B' pour forcer la copie du JSON par défaut dans la config");
    Serial.println("Tapez sur 'C' pour imprimer le fichier de config");
    Serial.println("Tapez sur 'D' pour envoyer un message de test");
    Serial.println("Tapez sur 'E' pour notifier le client web");
    Serial.println("Tapez sur 'F' pour imprimer le fichier de persistance");
    Serial.println("Tapez sur 'G' pour afficher la mémoire de manière cyclique");
    Serial.println("Tapez sur 'H' pour imprimer la liste des fichiers");
    Serial.println("Tapez sur 'Y' pour faire un reset");
    Serial.println("Tapez sur 'Z' pour afficher modifier les logs");
    Serial.println("Tapez sur '?' pour afficher l'aide");
} //showHelp

void showWiFiStatus() {
    Serial.println();
    
    // ✅ DÉTECTION DU MODE WiFi
    wifi_mode_t currentMode = WiFi.getMode();
    bool isAccessPoint = (currentMode == WIFI_AP || currentMode == WIFI_AP_STA);
    bool wifiOperational = false;
    
    if (isAccessPoint) {
        wifiOperational = (WiFi.softAPIP() != IPAddress(0, 0, 0, 0));
    } else {
        wifiOperational = WiFi.isConnected();
    }
    
    if (wifiOperational) {
        Serial.println("╔══════════════════════════════════════════════════════════════╗");
        if (isAccessPoint) {
            Serial.println("║                   STATUT ACCESS POINT ACTIF                 ║");
        } else {
            Serial.println("║                      STATUT WIFI ACTUEL                     ║");
        }
        Serial.println("╠══════════════════════════════════════════════════════════════╣");
        
        if (isAccessPoint) {
            // Mode Access Point
            Serial.printf("║ 📡 Access Point   : %-40s ║\n", WifiManagment::getInstance().getCurrentMode().c_str());
            Serial.printf("║ 📍 Adresse IP     : %-40s ║\n", WiFi.softAPIP().toString().c_str());
            Serial.printf("║ 🎭 Masque         : %-40s ║\n", "255.255.255.0");
            Serial.printf("║ 📍 MAC Address    : %-40s ║\n", WiFi.softAPmacAddress().c_str());
            Serial.printf("║ 🌐 Interface web  : http://%-32s ║\n", WiFi.softAPIP().toString().c_str());
            Serial.printf("║ 🏷️  mDNS          : http://%s.local%-*s ║\n", 
                         bu->getName(), 
                         (int)(32 - strlen(bu->getName()) - 6), "");
            Serial.printf("║ 👥 Clients conn.  : %-37d    ║\n", WiFi.softAPgetStationNum());
        } else {
            // Mode Station
            Serial.printf("║ 📡 Réseau WiFi    : %-40s ║\n", WiFi.SSID().c_str());
            Serial.printf("║ 📍 Adresse IP     : %-40s ║\n", WiFi.localIP().toString().c_str());
            Serial.printf("║ 🚪 Passerelle     : %-40s ║\n", WiFi.gatewayIP().toString().c_str());
            Serial.printf("║ 🔍 DNS            : %-40s ║\n", WiFi.dnsIP().toString().c_str());
            Serial.printf("║ 📶 Force signal   : %-37d dBm ║\n", WiFi.RSSI());
            Serial.printf("║ 📍 MAC Address    : %-40s ║\n", WiFi.macAddress().c_str());
            Serial.printf("║ 🌐 Interface web  : http://%-32s ║\n", WiFi.localIP().toString().c_str());
            Serial.printf("║ 🏷️  mDNS          : http://%s.local%-*s ║\n", 
                         bu->getName(), 
                         (int)(32 - strlen(bu->getName()) - 6), "");
        }
        
        // Calcul du temps de connexion
        static uint32_t connectionTime = millis();
        uint32_t uptime = (millis() - connectionTime) / 1000;
        uint32_t hours = uptime / 3600;
        uint32_t minutes = (uptime % 3600) / 60;
        uint32_t seconds = uptime % 60;
        Serial.printf("║ ⏱️  Temps actif   : %02lu:%02lu:%02lu%-25s ║\n", hours, minutes, seconds, "");
        
        Serial.println("╚══════════════════════════════════════════════════════════════╝");
    } else {
        Serial.println("╔══════════════════════════════════════════════════════════════╗");
        Serial.println("║                         WIFI DÉCONNECTÉ                     ║");
        Serial.println("╠══════════════════════════════════════════════════════════════╣");
        Serial.printf("║ 📊 Réseaux configurés : %-32d ║\n", bu->getNbWifiItems());
        Serial.println("║ 🔄 Statut            : Tentatives de reconnexion en cours   ║");
        Serial.println("║ ⚠️  Accès web         : Indisponible                        ║");
        Serial.println("╚══════════════════════════════════════════════════════════════╝");
    }
    Serial.println();
}

void diagnosticWiFiStatus() {
    Serial.println("\n=== DIAGNOSTIC WIFI COMPLET ===");
    
    // État hardware détaillé
    wl_status_t status = WiFi.status();
    Serial.printf("WiFi.status(): %d ", status);
    switch(status) {
        case WL_IDLE_STATUS: Serial.println("(IDLE - WiFi en attente)"); break;
        case WL_NO_SSID_AVAIL: Serial.println("(NO_SSID - Réseau introuvable)"); break;
        case WL_SCAN_COMPLETED: Serial.println("(SCAN_COMPLETED)"); break;
        case WL_CONNECTED: Serial.println("(CONNECTED - Connecté)"); break;
        case WL_CONNECT_FAILED: Serial.println("(CONNECT_FAILED - Échec connexion)"); break;
        case WL_CONNECTION_LOST: Serial.println("(CONNECTION_LOST - Connexion perdue)"); break;
        case WL_DISCONNECTED: Serial.println("(DISCONNECTED - Déconnecté)"); break;
        default: Serial.printf("(UNKNOWN STATUS: %d)\n", status); break;
    }
    
    // *** CORRECTION : Diagnostic intelligent selon le mode ***
    WifiManagment& wifiMgr = WifiManagment::getInstance();
    String currentMode = wifiMgr.getCurrentMode();
    
    Serial.printf("📡 Mode WiFi: %s\n", currentMode.c_str());
    
    if (currentMode == "access_point") {
        // Mode Access Point
        Serial.printf("WiFi.isConnected(): ❌ NON (normal en mode AP)\n");
        Serial.printf("🏠 Access Point: %s\n", wifiMgr.isOperational() ? "✅ ACTIF" : "❌ INACTIF");
        if (wifiMgr.isOperational()) {
            Serial.printf("📡 SSID AP: 'Formaca-Backup'\n");
            Serial.printf("📍 IP AP: %s\n", wifiMgr.getCurrentIP().toString().c_str());
            Serial.printf("🌐 Interface: http://%s\n", wifiMgr.getCurrentIP().toString().c_str());
        }
    } else if (currentMode == "station") {
        // Mode Station
        Serial.printf("WiFi.isConnected(): %s\n", WiFi.isConnected() ? "✅ OUI" : "❌ NON");
        if (WiFi.isConnected()) {
            Serial.printf("📡 SSID: '%s'\n", WiFi.SSID().c_str());
            Serial.printf("📍 IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("📶 Signal: %d dBm\n", WiFi.RSSI());
        }
    } else {
        Serial.printf("WiFi.isConnected(): %s\n", WiFi.isConnected() ? "✅ OUI" : "❌ NON");
        Serial.println("⚠️  Mode WiFi non configuré");
    }
    
    // *** CORRECTION : Vérification hardware améliorée ***
    wifi_mode_t wifiMode = WiFi.getMode();
    bool hardwareOn = (wifiMode == WIFI_STA || wifiMode == WIFI_AP || wifiMode == WIFI_AP_STA);
    Serial.printf("🔧 WiFi Hardware: %s", hardwareOn ? "✅ ON" : "❌ OFF");
    if (hardwareOn) {
        const char* modeStr = (wifiMode == WIFI_STA) ? "Station" : 
                             (wifiMode == WIFI_AP) ? "Access Point" : 
                             (wifiMode == WIFI_AP_STA) ? "Station+AP" : "Inconnu";
        Serial.printf(" (%s)", modeStr);
    }
    Serial.println();
    
    Serial.printf("🔧 BorneUniverselle: %s\n", bu->getWifiStatus() ? "✅ ON" : "❌ OFF");
    Serial.printf("📋 Réseaux configurés: %d\n", bu->getNbWifiItems());
    
    Serial.println("==============================\n");
}

void listFiles() {
    Serial.println("=== Fichiers à la racine (LittleFS) ===");
     Serial.printf("Total: %u bytes, Used: %u bytes, Free: %u bytes\r\n",
        LittleFS.totalBytes(), LittleFS.usedBytes(),
        LittleFS.totalBytes() - LittleFS.usedBytes());
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("Erreur: impossible d'ouvrir la racine");
        return;
    }
    File file = root.openNextFile();
    while (file) {
        Serial.printf("  %-40s %8u bytes\r\n", file.name(), file.size());
        file = root.openNextFile();
    }
    Serial.println("=======================================");
}

void commandInterpretor(char car){
  switch (car){
    case 'A':     if (bu != NULL && bu != nullptr){
                    bu->eraseWifis();
                  } else {
                    Serial.println("BorneUniverselle non initialisée !");
                  }
      break;

    case 'C':     if (bu != NULL && bu != nullptr){
                    bu->printConfigFile();
                  }
      break;

    case 'D':     if (bu != NULL && bu != nullptr){
                    Serial.println("Envoi d'un message de test sur le web socket");
                    BorneUniverselle::prepareMessage(WARNING, "Message de test");
                  }
      break;
    
    case 'E':     bu->notifyWebClient(true);
      break;

    case 'F':     if (ressortRoyal2 != NULL && ressortRoyal2 != nullptr){
                    ressortRoyal2->printPersistance();
                  }
      break;

    case 'G':     memoryMonitorBool = true;
      break;

    case 'H':       listFiles();
                  break;
    case 'I':
    case 'i':
            Serial.println("=== INFORMATIONS WIFI DÉTAILLÉES ===");
            showWiFiStatus();
            diagnosticWiFiStatus();
            break;

    case 'T':
    {  // ← AJOUTER cette accolade ouvrante
        Serial.println("=== DIAGNOSTIC PSRAM COMPLET ===");
        
        // Test de la fonction utilisée par l'API
        bool apiResult = PLC_Tools::isPSRAM_available();
        Serial.printf("PLC_Tools::isPSRAM_available(): %s\n", apiResult ? "TRUE" : "FALSE");
        
        // Tests individuels
        Serial.printf("ESP.getPsramSize(): %lu bytes\n", ESP.getPsramSize());
        Serial.printf("ESP.getFreePsram(): %lu bytes\n", ESP.getFreePsram());
        Serial.printf("psramFound(): %s\n", psramFound() ? "TRUE" : "FALSE");
        
        // Test d'allocation comme dans l'API
        void* testAlloc = ps_malloc(1024);
        Serial.printf("ps_malloc(1024): %s\n", testAlloc ? "SUCCÈS" : "ÉCHEC");
        if (testAlloc) free(testAlloc);
        
        // Simuler exactement le code de l'API
        Serial.println("--- Simulation du code API ---");
        if (PLC_Tools::isPSRAM_available()) {
            Serial.println("✅ API devrait afficher psram_available: true");
            Serial.printf("   psram_total: %lu\n", ESP.getPsramSize());
            Serial.printf("   psram_free: %lu\n", ESP.getFreePsram());
        } else {
            Serial.println("❌ API va afficher psram_available: false");
        }
        Serial.println("========================");
    }  // ← AJOUTER cette accolade fermante
    break;

    case 'Y':     Serial.println("ESP32 will reset");
                  ESP.restart();
      break;       

    case 'Z':     modifyLogs();
      break;

    case '?':     showHelp();
      break;

    case '\n':
     break;

    default:      Serial.printf("Command interrpretor: Key: %c not attributed !!\r\n", car);
  } 
}

// ========================================
// WEBSOCKET
// ========================================

void notFound(AsyncWebServerRequest *request) {
    Serial.printf("%lu:: Page not found !, requested url: %s\r\n", millis(), request->url().c_str());
    request->send(404, "text/plain", "Not found");
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, 
               void *arg, uint8_t *data, size_t len) {
    
    switch (type) {
          case WS_EVT_CONNECT: {
            uint32_t now = millis();
            uint32_t clientId = (unsigned long)client->id();
            
            Serial.printf("%lu:: New WebSocket client connected: %lu\n", now, clientId);
            
            // ========================================
            // ETAPE 2: Protection contre double evenement WS_EVT_CONNECT
            // ========================================
            
            // Protection atomique: verifier si un traitement est deja en cours
            if (!bu->tryAcquireConnectionLock(clientId)) {
                Serial.printf("%lu:: [ETAPE 2] Ignoring duplicate WS_EVT_CONNECT for client %lu\n", 
                             now, clientId);
                return;
            }
            
            // ========================================
            // ETAPE 1: Verifications de securite renforcees
            // ========================================
            
            // Verification 1: Peut-on accepter un nouveau client ?
            if (!bu->canAcceptNewClient()) {
                Serial.printf("%lu:: [ETAPE 1] Rejecting client %lu - canAcceptNewClient() returned false\n", 
                             now, clientId);
                bu->releaseConnectionLock();  // IMPORTANT: liberer le verrou avant de rejeter
                client->close(1008, "Server busy - try again later");
                return;
            }
            
            // Verification 2: Y a-t-il deja un client connecte ?
            if (bu->isClientConnected()) {
                Serial.printf("%lu:: [ETAPE 1] Cleaning up existing client before accepting new one\n", now);
                bu->setClientDisconnected(bu->getConnectedClient());
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            
           
            bu->setClientConnected(true, client);

            // Le verrou est libéré ICI, après que tout le traitement soit terminé
            // Cela empêche un 2ème événement WS_EVT_CONNECT de passer pendant
            // que le 1er événement est encore en cours de traitement
            bu->releaseConnectionLock();
            break;
        }
       
        case WS_EVT_DISCONNECT: {
            Serial.printf("%lu:: Client %lu disconnected\r\n", 
                         millis(), (unsigned long)client->id());
            
            // BUG FIX: Vérifier que c'est bien notre client actuel
            AsyncWebSocketClient* currentClient = bu->getConnectedClient();
            if (currentClient == client) {
                bu->setClientDisconnected(client);
            } else {
                Serial.printf("%lu:: Ignoring disconnect from non-active client %lu\r\n", 
                             millis(), (unsigned long)client->id());
            }
            break;
        }
        
        case WS_EVT_DATA: {
            // BUG FIX: Vérifications strictes avant traitement
            AsyncWebSocketClient* currentClient = bu->getConnectedClient();
            if (currentClient != client) {
                Serial.printf("%lu:: Ignoring data from non-active client %lu\r\n", 
                             millis(), (unsigned long)client->id());
                return;
            }
            
            if (!bu->isClientConnected()) {
                Serial.printf("%lu:: Ignoring data - no client connected\r\n", millis());
                return;
            }
            
            if (client->status() != WS_CONNECTED) {
                Serial.printf("%lu:: Ignoring data from client %lu - status: %d\r\n", 
                             millis(), (unsigned long)client->id(), client->status());
                return;
            }
            
            bu->handleWebSocket(arg, data, len, client);
            break;
        }
        
        case WS_EVT_ERROR: {
            Serial.printf("%lu:: WebSocket error from client %lu\r\n", 
                         millis(), (unsigned long)client->id());
            
            // BUG FIX: Seulement si c'est notre client actuel
            AsyncWebSocketClient* currentClient = bu->getConnectedClient();
            if (currentClient == client) {
                bu->setClientDisconnected(client);
            }
            break;
        }

        case WS_EVT_PING: {
            // ===================================
            // PING REÇU - Répondre automatiquement avec PONG
            // ===================================
            
            Serial.printf("[WS] Ping reçu du client #%lu depuis %s\n", 
                         client->id(), 
                         client->remoteIP().toString().c_str());
            
            // Statistiques de connexion
           static std::map<uint32_t, uint32_t> pingCounts;
            pingCounts[client->id()]++;
            
            // Log périodique de santé
            if (pingCounts[client->id()] % 10 == 0) {
                // Vérifier l'état de la queue avec les méthodes disponibles
                bool queueFull = client->queueIsFull();
                size_t canSend = client->canSend() ? 1 : 0;
                
                Serial.printf("[WS] Client #%lu : %lu pings reçus, Queue pleine: %s, Peut envoyer: %s, Heap: %lu\n",
                            client->id(), 
                            pingCounts[client->id()],
                            queueFull ? "OUI" : "NON",
                            canSend ? "OUI" : "NON",
                            ESP.getFreeHeap());
                            
                // Alerte si la queue est pleine
                if (queueFull) {
                    Serial.printf("⚠️ [WS] Queue client #%lu saturée!\n", client->id());
                }
            }
            
            break;
        }
        
        case WS_EVT_PONG: {
            // Vérifier le contenu du PONG si présent
            if (len > 0 && data) {
                // On peut vérifier que les données correspondent à ce qu'on a envoyé
                #ifdef DEBUG_WEBSOCKET
                Serial.printf("[WS] Pong contient %u bytes de données\n", len);
                #endif
                
                // Exemple : si on encode un timestamp dans le ping
                if (len >= 4) {
                    uint32_t timestamp = *((uint32_t*)data);
                    uint32_t now = millis();
                    uint32_t roundTrip = now - timestamp;
                    
                    Serial.printf("[WS] Round-trip time: %lu ms\n", roundTrip);
                }
            }
            
            break;
        }
    }
}

// ========================================
// SERVEUR WEB
// ========================================

void setupServer(){
    Serial.println("----------------------------------------------SERVER");
    Serial.println("Setting up server...");

    
    
    ws.onEvent(onWsEvent);
    ws.enable(true);
    server.addHandler(&ws);
    
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    //DefaultHeaders::Instance().addHeader("Connection", "close"); // Thierry le 7 avril 2026
    
    // Serve main app files from root
    // setFilter() sert de hook générique : appelé pour CHAQUE requête GET sur un
    // fichier .json servi depuis la racine, avant que serveStatic ne l'envoie. Ça
    // garantit que l'éditeur (qui peut ouvrir n'importe quel fichier de la liste)
    // voit toujours l'état à jour, sans dépendre d'une route dédiée par fichier.
    server.serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html")
        .setFilter([](AsyncWebServerRequest *request) {
            if (request->url().endsWith(".json")) {
                PLC_Persistence::getInstance().flushAll();
            }
            return true; // ne bloque jamais la requête, sert juste de hook
        });

    /* server.on("/index.js", HTTP_GET, [](AsyncWebServerRequest *request){ // Thierry le 7 avril 2026
        request->send(LittleFS, "/index.js", "application/javascript");
    });
    */

    // Serve assets directory with longer cache time
    server.serveStatic("/assets/", LittleFS, "/assets/").setCacheControl("max-age=31536000");

    // Specifically handle icon files
    server.serveStatic("/assets/icons/", LittleFS, "/assets/icons/").setCacheControl("max-age=31536000");
    
    // Explicitly serve CSS files with correct mime type
    server.serveStatic("/static/css/", LittleFS, "/static/css/").setCacheControl("max-age=600");
    
    // Serve the config.json with shorter cache time
    // Bug corrigé : servait "/configRessortRoyal2.json", un fichier qui n'existe pas.
    // Le vrai fichier persisté par BorneUniverselle est CONFIG_PATH_FILE ("/config.json").
    server.serveStatic("/config", LittleFS, CONFIG_PATH_FILE).setCacheControl("max-age=30");

    // ✅ Diagnostic complet (heap, PSRAM, perf loop, wifi, plc, websocket, modbus, persistance, santé)
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        uint32_t now = millis();

        doc["timestamp"] = now;
        doc["uptime_seconds"] = now / 1000;

        JsonObject memory = doc["memory"].to<JsonObject>();
        memory["heap_free"] = ESP.getFreeHeap();
        memory["heap_min"] = ESP.getMinFreeHeap();
        memory["heap_max_alloc"] = ESP.getMaxAllocHeap();
        memory["heap_total"] = ESP.getHeapSize();
        memory["heap_usage_percent"] = (float)(ESP.getHeapSize() - ESP.getFreeHeap()) * 100.0f / ESP.getHeapSize();
        float fragmentation = PLC_Tools::getHeapFragmentation();
        memory["heap_fragmentation_percent"] = fragmentation;

        if (PLC_Tools::isPSRAM_available()) {
            memory["psram_available"] = true;
            memory["psram_free"] = ESP.getFreePsram();
            memory["psram_total"] = ESP.getPsramSize();
        } else {
            memory["psram_available"] = false;
        }

        JsonObject performance = doc["performance"].to<JsonObject>();
        performance["loops_per_second"] = currentLoopsPerSecond;
        performance["total_loops"] = loopCounter;
        JsonObject timing = performance["timing"].to<JsonObject>();
        timing["average_loop_time_ms"] = avgLoopTime;
        timing["min_loop_time_ms"] = (minLoopTime == UINT32_MAX) ? 0 : minLoopTime;
        timing["max_loop_time_ms"] = maxLoopTime;

        JsonObject wifi = doc["wifi"].to<JsonObject>();
        wifi_mode_t currentMode = WiFi.getMode();
        bool isAccessPoint = (currentMode == WIFI_AP || currentMode == WIFI_AP_STA);
        bool wifiOperational = isAccessPoint ? (WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) : WiFi.isConnected();
        wifi["connected"] = wifiOperational;
        wifi["mode"] = WifiManagment::getInstance().getCurrentMode();
        wifi["configured_networks"] = bu->getNbWifiItems();
        if (!isAccessPoint && wifiOperational) {
            wifi["ssid"] = WiFi.SSID();
            wifi["rssi"] = WiFi.RSSI();
            wifi["ip_address"] = WiFi.localIP().toString();
        }

        JsonObject plc = doc["plc"].to<JsonObject>();
        plc["broken"] = bu->isPlcBroken();
        plc["name"] = bu->getName();
        plc["nodes_total"] = bu->nodesMap.size();
        plc["all_inputs_read_once"] = bu->isAllConditionsOk();

        JsonObject websocket = doc["websocket"].to<JsonObject>();
        websocket["client_connected"] = bu->isClientConnected();
        websocket["clients_count"] = ws.count();
        websocket["queue_full"] = bu->clientQueueIsFull();

        JsonObject health = doc["health"].to<JsonObject>();
        int healthScore = 100;
        JsonArray issues = health["issues"].to<JsonArray>();
        if (bu->isPlcBroken()) {
            healthScore = 0;
            issues.add("PLC is broken");
        } else {
            if (ESP.getFreeHeap() < 50000) { healthScore -= 20; issues.add("Low heap memory"); }
            if (fragmentation > 50) { healthScore -= 15; issues.add("High heap fragmentation"); }
            if (!wifiOperational) { healthScore -= 30; issues.add("WiFi not operational"); }
            if (!bu->isClientConnected()) { healthScore -= 5; issues.add("No WebSocket client"); }
        }
        health["score"] = healthScore;

        doc["response"]["version"] = MAIN_VERSION;

        String response;
        serializeJsonPretty(doc, response);
        AsyncWebServerResponse *apiResponse = request->beginResponse(200, "application/json", response);
        apiResponse->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        apiResponse->addHeader("Access-Control-Allow-Origin", "*");
        request->send(apiResponse);
    });

    // ✅ Redémarrage sécurisé (debug)
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Redemarrage programme dans 2 secondes...");
        delay(100);
        ESP.restart();
    });

    // ✅ Vidage des caches WebSocket
    server.on("/api/websocket/cleanup", HTTP_POST, [](AsyncWebServerRequest *request) {
        ws.cleanupClients();
        request->send(200, "text/plain", bu->isClientConnected() ? "WebSocket cleanup effectue" : "Aucun client a nettoyer");
    });

    // ✅ Limiter la taille des requêtes body (protection DoS basique)
    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (total > 8192) {
            request->send(413, "text/plain", "Request too large");
        }
    });

    server.onNotFound(notFound);

    server.begin();
    Serial.println("Server started");
}

// ========================================
// UTILITAIRES
// ========================================

void printTaskInfo() {
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    Serial.printf("[TaskInfo] Number of tasks: %u at %lu s\n", (unsigned int)uxArraySize, millis() / 1000);
}

// ========================================
// SETUP
// ========================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("=== VERSION CHECK ===");
    Serial.printf("Arduino-ESP32 version: %d.%d.%d\n", 
                  ESP_ARDUINO_VERSION_MAJOR,
                  ESP_ARDUINO_VERSION_MINOR, 
                  ESP_ARDUINO_VERSION_PATCH);
    
    // Version ESP-IDF (sans planter)
    Serial.printf("ESP-IDF version: %d.%d.%d\n",
                  ESP_IDF_VERSION_MAJOR,
                  ESP_IDF_VERSION_MINOR,
                  ESP_IDF_VERSION_PATCH);
    
    Serial.println("====================");

    Serial.println("Will start BorneUniverselle library...");
    printTaskInfo();
    
    // ========================================
    // INITIALISATION BORNEUNIVERSELLE
    // ========================================
    // Initialiser PLC_Tools (buffer diagnostic, etc.)
    PLC_Tools::initialize();

    if (!BorneUniverselle::initializeInstance()) {
        Serial.println("ERREUR CRITIQUE: Échec de l'initialisation de BorneUniverselle, will restart");
        ESP.restart();
    }

    bu = BorneUniverselle::getInstance();
    if (bu == nullptr) {  // ← AJOUTER CETTE VÉRIFICATION
        Serial.println("ERREUR CRITIQUE: getInstance() retourne null après initializeInstance()");
        ESP.restart();
        return;
    }

    Serial.println("Before check plc broken");
    if (!heap_caps_check_integrity_all(false)) {
        Serial.println("❌ HEAP CORROMPU DÉTECTÉ ");
    }

    if (bu->isPlcBroken()) {
        Serial.println("Erreur: PLC cassé après initialisation de BorneUniverselle");
        return;
    }

    Serial.println("Before check map empty");
    if (!heap_caps_check_integrity_all(false)) {
        Serial.println("❌ HEAP CORROMPU DÉTECTÉ ");
    }

    if (bu->nodesMap.empty()) {
        bu->setPlcBrokenImpl("No nodes were created in BorneUniverselle");
        Serial.println("Erreur: Aucun nœud créé dans BorneUniverselle");
        return;
    }

    Serial.println("Before printTaskInfo");
    if (!heap_caps_check_integrity_all(false)) {
        Serial.println("❌ HEAP CORROMPU DÉTECTÉ ");
    }
    
    printTaskInfo();
    
    // ========================================
    // CRÉATION RESSORTROYAL2
    // ========================================
    Serial.println("========================================");
    Serial.println("Creating RessortRoyal2 instance...");
    Serial.println("========================================");

    Serial.printf("✅ RessortRoyal2 size: %u bytes\n", sizeof(RessortRoyal2));
    Serial.println("Before RessortRoyal2 constructor, checking heap integrity...");
    if (!heap_caps_check_integrity_all(false)) {
        Serial.println("❌ HEAP CORROMPU DÉTECTÉ AVANT constrcuteur RessortRoyal !");
    }
    
    ressortRoyal2 = new RessortRoyal2();
    if (ressortRoyal2 == nullptr) {
        Serial.println("Erreur critique: Échec de la création de l'instance RessortRoyal2");
        bu->setPlcBrokenImpl("Failed to create RessortRoyal2 instance");
        return;
    }

    Serial.println("Registering BusinessLogic with BorneUniverselle...");
    bu->setBusinessLogic(ressortRoyal2);
  
    // ========================================
    // INITIALISATION DES POINTEURS
    // ========================================
    Serial.println("Appel à initializePointers");
    if (!ressortRoyal2->initializePointers("main::setup")) {
        Serial.println("Erreur critique: Échec de initializePointers");
        bu->setPlcBrokenImpl("Failed to initialize pointers");
        return;
    }

    // ========================================
    // DÉMARRAGE STATE MACHINE
    // ========================================
    Serial.println("========================================");
    Serial.println("Starting state machine...");
    Serial.println("========================================");
    
    if (!ressortRoyal2->startStateMachine()) {
        Serial.println("Erreur: Échec du démarrage de la state machine");
        bu->setPlcBrokenImpl("Failed to start state machine");
        return;
    }

    // ========================================
    // CONFIGURATION WIFI
    // ========================================
    WiFi.onEvent([](arduino_event_t *event) {
        WiFiStationConnected();
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
    
    WiFi.onEvent([](arduino_event_t *event) {
        WiFiGotIP();
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    
    WiFi.onEvent([](arduino_event_t *event) {
        WiFiStationDisconnected();
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    // ========================================
    // CONFIGURATION OTA
    // ========================================
    httpUpdate.onStart(update_started);
    httpUpdate.onEnd(update_finished);
    httpUpdate.onProgress(update_progress);
    httpUpdate.onError(update_error);

    WifiManagment::getInstance().setOnGotIPCallback([]() {
        WiFiGotIP();  // Réutilise la même fonction
    });

    // ========================================
    // CONNEXION WIFI
    // ========================================
    if (bu->getIsWifiParsedOk()) {
        connectToNextNetwork();
    } else {
        Serial.println("Not starting wifi because config is not correct !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    }

    // ========================================
    // ETHERNET (cartes qui en ont, si le projet l'active)
    // ========================================
    EthernetManager::getInstance().begin(bu->getName());

    // ========================================
    // DÉMARRAGE SERVEUR WEB
    // ========================================
    setupServer();
    
    // ========================================
    // AFFICHAGE AIDE
    // ========================================
    showHelp();
    MyToolBox::emptySerialIn(); 

    bu->markSetupComplete();
    
    Serial.println("========================================");
    Serial.printf("%lu:: RessortRoyal2::setup() finished !\n", (unsigned long)millis());
    Serial.println("========================================");
    // ⭐ LOG après markSetupComplete pour que le fichier diagnostic soit créé
    PLC_Tools::logDiagnostic("PLC is up - RessortRoyal2 ready");
}

// ========================================
// LOOP PRINCIPALE
// ========================================

void loop() {
  long start = millis();

  if (pendingWifiGotIP) {
      pendingWifiGotIP = false;
      processWifiGotIP();
  }

  if (EthernetManager::getInstance().takeGotIP()) {
      processEthernetGotIP();
  }

  // Reconnexion WiFi non-bloquante (planifiée par WiFiStationDisconnected)
  if (scheduleReconnect && millis() >= reconnectAt) {
      scheduleReconnect = false;
      connectToNextNetwork();
  }

  if (Serial.available()){
      commandInterpretor(Serial.read());
  }

  // check if it is time to send heartbeat
  bu->checkHeartbeat();
  
  if (!bu->isPlcBroken()){
    // =====================================
    // MESURE PERFORMANCE REFRESH
    // =====================================
    uint32_t refreshStart = millis();
    
    if (!bu->clientQueueIsFull() && !bu->getPlcSuspended()){
      bu->refresh();
    }
    
    uint32_t refreshDuration = millis() - refreshStart;

#ifdef PERF_MONITOR
    if (ressortRoyal2 && ressortRoyal2->isPmActive()) {
        ressortRoyal2->pmAccumulateRefresh(refreshDuration);
    }
#endif

    // Mise à jour des stats refresh
    if (refreshDuration < minRefreshTime) minRefreshTime = refreshDuration;
    if (refreshDuration > maxRefreshTime) maxRefreshTime = refreshDuration;
    totalRefreshTime += refreshDuration;
    refreshCounter++;
    
    if (refreshCounter > 0) {
        avgRefreshTime = totalRefreshTime / refreshCounter;
    }
    // =====================================
 
    bu->handleWebSocketMessage();
    
    if (bu->isAllConditionsOk()){
          // =====================================
          // MESURE PERFORMANCE LOGIC EXECUTOR
          // =====================================
          uint32_t logicStart = millis();
          
          if (!bu->getPlcSuspended() && bu->isClientFullyConnected()){
            if (!bu->getBusinessLogic()->logicExecutor()){  // ← Polymorphisme
                bu->setPlcBrokenImpl("LogicalExecutor");
            }
          }
         
          uint32_t logicDuration = millis() - logicStart;
          
          // Mise à jour des stats logic
          if (logicDuration < minLogicTime) minLogicTime = logicDuration;
          if (logicDuration > maxLogicTime) maxLogicTime = logicDuration;
          totalLogicTime += logicDuration;
          logicCounter++;
          
          if (logicCounter > 0) {
              avgLogicTime = totalLogicTime / logicCounter;
          }
        
    } else {
      if (millis() - lastMessageTime > 5000){
        lastMessageTime = millis();
        Serial.printf("%lu::Not calling logic executor because not all inputs are read once\r\n", millis());
      }
    }       
  } else {
    // PLC broken — mode diagnostic : l'interpréteur reste accessible
    static uint32_t lastBrokenMsg = 0;
    if (millis() - lastBrokenMsg > 5000) {
        Serial.println("PLC is broken — mode diagnostic actif");
        lastBrokenMsg = millis();
    }
    if (Serial.available()) {
        commandInterpretor(Serial.read());
    }
  }

    if (!bu->clientQueueIsFull() && !bu->isWebSocketMessagesListMoreThanHalf()){
        bu->notifyWebClient();
    }
  
  bu->sendMessage();
  ws.cleanupClients();

  // =====================================
  // STATISTIQUES PERFORMANCE LOOP
  // =====================================
  uint32_t loopDuration = millis() - start;
  if (loopDuration > 1500){
    Serial.printf("⚠️ Long loop time: %lu\r\n", loopDuration);
  }
  
  // Mise à jour des stats loop
  if (loopDuration < minLoopTime) minLoopTime = loopDuration;
  if (loopDuration > maxLoopTime) maxLoopTime = loopDuration;
  totalLoopTime += loopDuration;
  loopCounter++;
  
  if (loopCounter > 0) {
      avgLoopTime = totalLoopTime / loopCounter;
  }
  
  // Calcul de la fréquence globale (une seule fois)
  uint32_t now = millis();
  if (now - lastLoopStatsTime >= 1000) {
      uint32_t loopsInPeriod = loopCounter - lastLoopCount;
      uint32_t timeElapsed = now - lastLoopStatsTime;
      
      if (timeElapsed > 0) {
          currentLoopsPerSecond = (float)loopsInPeriod * 1000.0f / (float)timeElapsed;
      }
      
      lastLoopStatsTime = now;
      lastLoopCount = loopCounter;
  }
  
  // =====================================
  // AFFICHAGE PERFORMANCES (toutes les 30 sec)
  // =====================================
  static uint32_t lastPerfDisplay = 0;
  if (now - lastPerfDisplay > 30000) {
      Serial.printf("🔄 Performance globale: %.1f cycles/sec\n", currentLoopsPerSecond);
      Serial.printf("   Loop:    avg=%lums, min=%lums, max=%lums\n",
                   avgLoopTime, minLoopTime, maxLoopTime);
      
      Serial.printf("   Refresh: avg=%lums, min=%lums, max=%lums\n",
                   avgRefreshTime, 
                   (minRefreshTime == UINT32_MAX) ? 0 : minRefreshTime, 
                   maxRefreshTime);
      
      Serial.printf("   Logic:   avg=%lums, min=%lums, max=%lums\n",
                   avgLogicTime, 
                   (minLogicTime == UINT32_MAX) ? 0 : minLogicTime, 
                   maxLogicTime);
      
      lastPerfDisplay = now;
  }
  
  // =====================================
  // MONITORING MÉMOIRE
  // =====================================
  MemoryMonitor::trackStats();

  if (memoryMonitorBool){
    memoryMonitor.printStats("MemoryMonitor");
  }

  // =====================================
    // FLUSH PÉRIODIQUE DU BUFFER DIAGNOSTIC
    // =====================================
    // Solution générique: flush automatique toutes les 500ms
    // pour garantir que les logs sont écrits même si aucun nouveau log n'arrive
    static uint32_t lastDiagnosticFlush = 0;
    if (now - lastDiagnosticFlush >= 1000) {
        PLC_Tools::flushDiagnosticBuffer();
        lastDiagnosticFlush = now;
    }
}

// =====================================
// FONCTION DE DIAGNOSTIC ADDITIONNELLE
// =====================================

// Fonction à appeler depuis les commandes série pour diagnostic
void diagnosticLoop() {
    static uint32_t loopCount = 0;
    static uint32_t totalLoopTime = 0;
    static uint32_t maxLoopTime = 0;
    static uint32_t lastReport = 0;
    
    loopCount++;
    uint32_t now = millis();
    
    if (now - lastReport > 10000) { // Rapport toutes les 10 secondes
        uint32_t avgLoopTime = totalLoopTime / loopCount;
        
        Serial.println("\n=== DIAGNOSTIC LOOP ===");
        Serial.printf("Loops exécutées: %lu\n", loopCount);
        Serial.printf("Temps moyen: %lu ms\n", avgLoopTime);
        Serial.printf("Temps max: %lu ms\n", maxLoopTime);
        Serial.printf("Fréquence: %.1f Hz\n", (float)loopCount / 10.0f);
        
        // États du système
        // ✅ Vérification WiFi selon le mode
        wifi_mode_t currentMode = WiFi.getMode();
        bool isAP = (currentMode == WIFI_AP || currentMode == WIFI_AP_STA);
        bool wifiOk = isAP ? (WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) : WiFi.isConnected();
        Serial.printf("WiFi opérationnel: %s\n", wifiOk ? "OUI" : "NON");
        Serial.printf("Client WebSocket: %s\n", bu->isClientConnected() ? "OUI" : "NON");
        Serial.printf("PLC cassé: %s\n", bu->isPlcBroken() ? "OUI" : "NON");
        Serial.printf("Toutes entrées lues: %s\n", bu->isAllConditionsOk() ? "OUI" : "NON");
        Serial.printf("Queue WebSocket: %s\n", bu->clientQueueIsFull() ? "PLEINE" : "OK");
        
        Serial.println("=====================\n");
        
        // Reset des compteurs
        loopCount = 0;
        totalLoopTime = 0;
        maxLoopTime = 0;
        lastReport = now;
    }
}
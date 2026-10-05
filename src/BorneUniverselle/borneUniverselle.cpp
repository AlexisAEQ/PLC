#include "BorneUniverselle/borneUniverselle.h"
#include <typeinfo>

void WebSocketMessageDeleter::operator()(WEB_SOCKET_MESSAGE* c) const {
    if (c) {
        if (c->data) {
            PLC_Tools::freeMemory(c->data);  // ✅ PLC_Tools complet ici
            c->data = nullptr;
        }
        delete c;
    }
}

void NotifMessageDeleter::operator()(NOTIF_MESSAGE* m) const {
    if (m) {
        if (m->message) {
            PLC_Tools::freeMemory(m->message);  // ✅ PLC_Tools complet ici
            m->message = nullptr;
        }
        delete m;
    }
}

BorneUniverselle* BorneUniverselle::instance = nullptr;
bool BorneUniverselle::instanceCreated = false;

SemaphoreHandle_t BorneUniverselle::webSocketMutex = NULL;
SemaphoreHandle_t BorneUniverselle::notifMutex = NULL;

// Initialisation des callbacks à nullptr
std::function<void()> BorneUniverselle::initialStateLoadedCallback = nullptr;
std::function<void(const char*)> BorneUniverselle::tabChangeCallback = nullptr;

BorneUniverselle::BorneUniverselle() : myModbus(MyModbus::getInstance()) {
    Serial.printf("Start at %lu ms, tasks: %u, heap: %u, stack free: %u\n",
                  millis(), uxTaskGetNumberOfTasks(), xPortGetFreeHeapSize(), uxTaskGetStackHighWaterMark(NULL) * 4);
    MemoryMonitor::printFlashStats("BorneUniverselle constructor");

    instance = this;
    instanceCreated = true;

    // Initialisation des mutex
    webSocketMutex = xSemaphoreCreateRecursiveMutex();
    if (webSocketMutex == NULL) {
        setPlcBrokenImpl("webSocketMutex was not created successfully");
        return;
    }
    
    notifMutex = xSemaphoreCreateRecursiveMutex();
    if (notifMutex == NULL) {
        setPlcBrokenImpl("notifMutex was not created successfully");
        return;
    }

    // Initialisation de Modbus
    myModbus.setMessageCallback(modbusMessageHandler);

    // Initialisation de nodesMap
    nodesMap.clear();
    nbTimeouts = 0;

    if (!initializeConfig()) {
        setPlcBrokenImpl("Failed to initialize configuration");
        return;
    }

    // Préparer la chaîne heartbeat
    JsonDocument doc;
    doc[HEARTBEAT] = TRUE_J;
    serializeJson(doc, hearbeatChain);

    // ========================================================================
    // ENREGISTREMENT CALLBACK PRÉ-VALIDATION (pour vérifier le fichier de configuration si on le modifie)
    // ========================================================================
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    persistence.registerPreValidateCallback([this](const char* path, const JsonDocument& doc, String& errorMsg) {
        return this->preValidateFile(path, doc, errorMsg);
    });
    Serial.println("BorneUniverselle: Pre-validate callback registered");


    persistence.registerSaveCallback([this](const char* path, bool success) {
        this->onFileSavedHandler(path, success);
    });

    if (!heap_caps_check_integrity_all(true)) {
        Serial.println("❌ HEAP CORROMPU DÉTECTÉ AVANT registerSaveCallback !");
        return;
    }

    Serial.println("BorneUniverselle: Post-save callback registered");

    Serial.println(F(BORNE_UNIVERSELLE_VERSION));
    Serial.printf("BorneUniverselle constructed, nodesMap size: %u\n", nodesMap.size());
}

void BorneUniverselle::onFileSavedHandler(const char* path, bool success) {
    if (!success) {
        return;
    }
    
    Serial.printf("========================================\r\n");
    Serial.printf("BorneUniverselle::onFileSavedHandler: %s\r\n", path);
    Serial.printf("========================================\r\n");
    
    // ========================================================================
    // CAS SPÉCIAL: config.json
    // ========================================================================
    if (strcmp(path, CONFIG_PATH_FILE) == 0) {
        Serial.println("✅ config.json saved successfully");
        
        // Utiliser le flag déterminé AVANT la sauvegarde
        if (configNeedsRestartAfterSave) {
            Serial.println("🔄 Critical changes detected, restart required");
            prepareMessage(SUCCESS, "config.json saved, restarting...");
            if (businessLogic) {
                businessLogic->requestRestart(10000, "config.json modifié (changements critiques)");
                prepareMessage(INFO, "System will restart in 10 seconds...");
            }
        } else {
            Serial.println("♻️ Only minor changes, hot-reload without restart");
            
            // Lire la nouvelle config pour rechargement à chaud
            PLC_Persistence& persistence = PLC_Persistence::getInstance();
            SafeJsonDocument newConfig;
            
            if (persistence.readJsonFromFile(CONFIG_PATH_FILE, newConfig)) {
                reloadMinorConfigParameters(newConfig);
                prepareMessage(SUCCESS, "config.json reloaded without restart");
            } else {
                Serial.println("⚠️ Failed to read config for hot-reload, but file was saved");
                prepareMessage(WARNING, "config.json saved but hot-reload failed");
            }
        }
    }
    
    Serial.printf("onFileSavedHandler: not config.json\r\n");
    // ========================================================================
    // DÉLÉGUER AU BUSINESSLOGIC pour les autres fichiers
    // ========================================================================
    if (businessLogic) {
        Serial.printf("Delegating onFileSaved to BusinessLogic for %s\r\n", path);
        businessLogic->onFileSaved(path, success);
    }

    // ========================================
    // NOTIFIER LE CLIENT DES CHANGEMENTS
    // ========================================
    // 🔥 CRITIQUE: Notifier AVANT de reprendre les opérations
    // Le prochain loop() va appeler refresh() qui va clear updateNeeded
    // Donc on doit notifier MAINTENANT pendant la suspension
    Serial.println("Notification des changements au client...");
    notifyWebClient(false);
}

BorneUniverselle* BorneUniverselle::getInstance() {
    return instance;
}

bool BorneUniverselle::initializeInstance() {
    if (instanceCreated && instance) {
        return true;
    }
    
    instance = new BorneUniverselle();
    return (instance != nullptr && instanceCreated);
}

void BorneUniverselle::modbusMessageHandler(uint8_t severity, const char* message) {
    BorneUniverselle::prepareMessage(severity, message);  
}

bool BorneUniverselle::isPlcBroken(){
    BorneUniverselle* instance = getInstance();
    if (instance != nullptr) {
        return instance->isPlcBrokenImpl();
    } else {
        Serial.println("Pas d'instance BorneUniverselle !!!");
        return true;
    }
}
   
bool BorneUniverselle::isPlcBrokenImpl(){
    return plcBroken;
}

void BorneUniverselle::setPlcBroken(const char *context) {
    BorneUniverselle* instance = getInstance();
    if (instance != nullptr) {
        instance->setPlcBrokenImpl(context);
    }
}

bool BorneUniverselle::enableInterface(bool status){
    JsonDocument doc;
    doc[ENABLE_INTERFACE] = status;
    char json[256];
    uint16_t len = serializeJson(doc, json, sizeof(json));
    json[len +1] = 0;

    //serializeJsonPretty(doc, Serial);
    bool success = sendTextToClient(json);
    // Serial.printf("ATTENTION ! Interface %s\r\n", status ? "activée" : "désactivée");
    return success;
}

void BorneUniverselle::setPlcBrokenImpl(const char *context) {
    if (!plcBroken) {

        if (inSetPlcBroken) {
        Serial.printf("⚠️ setPlcBroken déjà en cours, ignorant: %s\r\n", context);
        return;
        }

        inSetPlcBroken = true;

        size_t len = strlen(context) + strlen("Plc is broken, context: ") + 10;
        char *text = (char *)malloc(len);
      
        if (text == nullptr) {
            Serial.println("setPlcBrokenImpl:: Unable to allocate memory for error message");
            return;
        }
     
        snprintf(text, len, "Plc is broken, context: %s", context);
        prepareMessage(FATAL, text);
        isLastMessageFatal = true;
        Serial.println(text);
          
        free(text);
      
    }
    plcBroken = true;
    inSetPlcBroken = false;

    // Halte sécurisée si fatal AU BOOT (setupComplete == false).
    // Sinon le boot continue vers initializePointers() qui déréférence des
    // pointeurs de nœuds nuls -> LoadProhibited -> reboot en boucle.
    // Le delay() nourrit le watchdog IDLE : un while(true){} nu relancerait
    // justement le reboot via le Task WDT.
    if (!setupComplete) {
        Serial.println("⛔ FATAL au boot : arret securise du PLC (pas de reboot). Corrigez la cause puis redemarrez.");
        Serial.flush();
        while (true) {
            delay(1000);
        }
    }
}

void BorneUniverselle::unableToFindKey(char *_context, char *_key){
    BorneUniverselle* instance = getInstance();
    if (instance != nullptr) {
        instance->unableToFindKeyImpl(_context, _key);
    }
}

void BorneUniverselle::unableToFindKeyImpl(char *_context, char *_key){
    char message[256];
    sprintf(message, "unableToFindKey:: In context: %s, Unable to find key: %s\r\n", _context, _key);
    prepareMessage(ERROR, message);
    setPlcBrokenImpl(message);
}

void BorneUniverselle::setShowModbusMessages(bool status){
    showModbusMessages = status;
    // flag tous les nodes modbus pour afficher les messages
    std::map<uint32_t, Node *>::iterator it;
    for (it = nodesMap.begin(); it != nodesMap.end(); it++){
        Node *node = it->second;
        if (node->classType() >= FIRST_MODBUS_CLASS && node->classType() <= LAST_MODBUS_CLASS){
            node->setShowMessages(status);
        }
    }
}

bool BorneUniverselle::getModbusStatusMessages(){
    return showModbusMessages;
}

void BorneUniverselle::showMessage(Node *node, const char *text){
    if (showHeartbeatMessages && node->getShowMessages()){
        Serial.println(text);
    }
}

void BorneUniverselle::refresh(){
    if (isPlcBroken()){
        Serial.println("No refresh because PLC is broken !!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            return;
    }

    uint32_t start = millis();
    uint32_t lastYield = start;            // ✅ Pour les yields
    
    // ===== RESET COMPTEURS MODBUS =====
    myModbus.resetRefreshCounters();

    std::map<uint32_t, Node *>::iterator it;
    for (it = nodesMap.begin(); it != nodesMap.end(); it++){
        if (PF8574BooleanInputNode::isInterrupt()  && allConditionsOK){
            return;
        }
        //Serial.printf("Will refresh node key: %u\r\n", it->first);
        Node *node = it->second;

        if (node->getMode() == INPUT_MODE){
            InputNode *inputNode = (InputNode *)node;
            if (millis() - inputNode->getLastRefresh() < inputNode->getRefreshInterval()){
                char mess[256];
                snprintf(mess, sizeof(mess), "On saute le node %s\r\n", node->getName());
                showMessage(node, mess);
                continue;
            } 
        }

        uint32_t hash;
        if (node->getShowMessages()){
            if ((node->classType() >= FIRST_MODBUS_CLASS && node->classType() <= LAST_MODBUS_CLASS) ){
               myModbus.setShowMessages(true);
            }  
        }
        
        // ===== COMPTAGE NODES MODBUS =====
        bool isModbusNode = (node->classType() >= FIRST_MODBUS_CLASS && node->classType() <= LAST_MODBUS_CLASS);

        bool e = node->refresh();

        myModbus.setShowMessages(false);

        if (!e){
            bool shouldShowMessage = true;
            if (isModbusNode) {
                uint8_t slaveAddr = node->getModbusAddress();
                if (slaveAddr == 0) slaveAddr = myModbus.getLastTransactionAddress();
                shouldShowMessage = myModbus.isSlaveConsideredDead(slaveAddr);
            }

            if (shouldShowMessage) {
                hash = node->getHash();
                char mess[256];
                uint8_t mbAddr = node->getModbusAddress();
                if (mbAddr > 0) {
                    snprintf(mess, sizeof(mess), "Refresh error addr:%u node:%s hash:%lu",
                        mbAddr, node->getName(), (long unsigned)hash);
                } else {
                    snprintf(mess, sizeof(mess), "Refresh error node:%s hash:%lu",
                        node->getName(), (long unsigned)hash);
                }
                PLC_Tools::sendPeriodicMessage(true, WARNING, mess);
            }
        }
            
        // ✅ Maintenir la connexion active pendant le refresh
        keepAliveHeartbeat(); 
        
        // ===== YIELD PÉRIODIQUE (tous les 50ms) =====
        if (millis() - lastYield > 50) {
            vTaskDelay(pdMS_TO_TICKS(1));
            lastYield = millis();  // ✅ Reset sa propre variable
        }
    } // for 
    
    // ===== VERIFICATION SANTE BUS MODBUS =====
    uint32_t realTransactions = myModbus.getTransactionsThisRefresh();
    uint32_t realErrors = myModbus.getTransactionErrorsThisRefresh();

    // Exige plusieurs passages consecutifs de refresh() a 100% d'echec avant
    // de declarer le bus mort. Un seul passage a 100% peut arriver par pur
    // hasard de timing (ex: passage entierement consomme par une cascade
    // REFUSEE, n'ayant eu le temps de tenter qu'une seule transaction) sans
    // que le bus soit reellement mort.
    static const uint32_t BUS_DEAD_CONSECUTIVE_THRESHOLD = 5;
    static uint32_t consecutiveFailedRefreshCycles = 0;

    if (realTransactions > 0) {
        if (realErrors == realTransactions) {
            consecutiveFailedRefreshCycles++;
        } else {
            consecutiveFailedRefreshCycles = 0;
        }
    }

    if (consecutiveFailedRefreshCycles >= BUS_DEAD_CONSECUTIVE_THRESHOLD) {
        if (!myModbus.isModbusDead()) {
            Serial.printf("MODBUS: %lu passages consecutifs a 100%% d'echec -> Declaration bus MORT\n",
                consecutiveFailedRefreshCycles);
            myModbus.forceModbusDead();
        }
        consecutiveFailedRefreshCycles = 0;
    }

    // ===== GESTION RECUPERATION MODBUS =====
    myModbus.checkModbusHealthAndRecover();

    uint32_t totalDuration = millis() - start;
    if (totalDuration > 1000) {
        Serial.printf("⚠️ %lu::Total refresh time: %lu ms\r\n", millis(), totalDuration);
    }
}

bool BorneUniverselle::isAllConditionsOk(){
    if (!allConditionsOK){
        std::map<uint32_t, Node *>::iterator it;
        for (it = nodesMap.begin(); it != nodesMap.end(); it++){
            Node *node = it->second;
            if (node->getMode() == INPUT_MODE){
                // 🔥 NOUVEAU : Skip les Virtual INPUT nodes
                // Ils sont contrôlés par le client, pas besoin d'attendre qu'ils soient "lus"
                int classType = node->classType();
                if (classType == CLASS_VIRTUAL_BOOLEAN_INPUT_NODE ||
                    classType == CLASS_VIRTUAL_UINT32_INPUT_NODE ||
                    classType == CLASS_VIRTUAL_FLOAT_INPUT_NODE ||
                    classType == CLASS_VIRTUAL_TEXT_INPUT_NODE) {
                    // Virtual input : on ne l'attend pas
                    continue;
                }
                
                InputNode *inputNode = (InputNode *)node;
                if (inputNode->getLastRefresh() == 0){
                    allConditionsOK = false;
                    uint32_t now = millis();

                    if (now - lastInputReadWarningTime >= INPUT_READ_WARNING_INTERVAL_MS){
                        Serial.printf("isAllInputsReadOnce: at least node %s has not been read once\r\n", node->getName());
                        lastInputReadWarningTime = millis();
                    }
                    return false;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
      
        allConditionsOK = true;
        // 🔥 NOUVEAU : Log pour confirmer
        Serial.printf("%lu:: ✅ All hardware inputs read once! Logic executor can start.\r\n", millis());
        return !myModbus.isModbusDead();
    }

    return !myModbus.isModbusDead();
}

void BorneUniverselle::refresHardwareInputs(){
    BorneUniverselle* instance = getInstance();
    if (instance) {
        instance->refresHardwareInputsImpl();
    }
}

void BorneUniverselle::refresHardwareInputsImpl(){
    std::map<uint32_t, Node *>::iterator it;
    for (it = nodesMap.begin(); it != nodesMap.end(); it++){
        //Serial.printf("Will refresh node key: %d\r\n", it->first);
        Node *node = it->second;
        if (node->getMode() == INPUT_MODE && node->classType() == CLASS_PFC8574_BOOLEAN_INPUT_NODE){
            //InputNode *inputNode = (InputNode *)node;
            node->refresh();
        }
     }
}

bool BorneUniverselle::getWifiStatus() const {
    WifiManagment& wifiMgr = WifiManagment::getInstance();
    return wifiMgr.isOperational();
}

void BorneUniverselle::setWifiConnected(bool status) {
    WifiManagment::getInstance().setWifiConnected(status);
}

bool BorneUniverselle::isWifiConnectionTimeout(){
    return WifiManagment::getInstance().isWifiConnectionTimeout();
}

uint32_t BorneUniverselle::getWifiStartupTime(){
    return WifiManagment::getInstance().getWifiStartupTime();
}

WifiItem BorneUniverselle::getNextNetwork(){
    return WifiManagment::getInstance().getNextNetwork();
}

bool BorneUniverselle::connectWifi(WifiItem currentWifi){
    BorneUniverselle* instance = getInstance();
    if (instance != nullptr) {
        return instance->connectWifiImpl(currentWifi);
    } else {
        return false;
    }
}

unsigned char BorneUniverselle::getNbWifiItems(){
    return WifiManagment::getInstance().getNbWifiItems();
}      

void BorneUniverselle::setOTA_url(const char *url){
	Serial.printf("New url for fimrware update will be saved in persistant parameters: %s\r\n", url);
	//saveParameters();
}

bool BorneUniverselle::saveParameters(const JsonDocument &configDoc) {
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    if (persistence.saveJsonToFile(CONFIG_PATH_FILE, configDoc)) {
        Serial.println(F("Config file saved with success"));
        return true;
    } else {
        Serial.println(F("Error saving config file"));
        Serial.println(persistence.getLastError());
        PLC_Tools::logDiagnostic(persistence.getLastError(), false);
        return false;
    }
}

void BorneUniverselle::setClientConnected(bool status, AsyncWebSocketClient* _client) {
    uint32_t now = millis();
    
    MutexGuard webSocketGuard(webSocketMutex, "webSocketMutex", __FUNCTION__);
    if (!webSocketGuard.isAcquired()) {
        setPlcBroken("setClientConnected:: unable to acquire the webSocket Mutex");
        return;
    }

    if (status) {
        // ===== CLIENT SE CONNECTE =====
        if (clientconnected && client != nullptr) {
            Serial.println("Client already connected, rejecting new connection");
            tooMuchClients(_client);
            return;
        }
        
        // ========================================
        // ETAPE 1: Enregistrer l'acceptation du client
        // ========================================
        lastClientAcceptTime = now;
        lastAcceptedClientId = _client->id();
        Serial.printf("%lu:: [ETAPE 1] Client %lu accepted and registered\r\n", now, lastAcceptedClientId);
        
        client = _client;
        clientconnected = true;
        clientConnectedAt = now;
        
        // 🔥 FIX CRITIQUE : Réinitialiser les timers heartbeat
        lastHeartbeatSend = now;
        lastHearbeatReceive = now;
        heartbeatTimeout = now + HEARTHBEAT_TIMEOUT; // now + 10000ms
        nbTimeouts = 0;
        heartbeatReceive = false;
        
        Serial.printf("%lu:: ✅ Heartbeat timers initialized:\r\n", now);
        Serial.printf("  - lastHeartbeatSend: %lu\r\n", lastHeartbeatSend);
        Serial.printf("  - heartbeatTimeout: %lu (dans %lu ms)\r\n", 
                     heartbeatTimeout, HEARTHBEAT_TIMEOUT);
        
        // Vider les queues au cas où
        webSocketMessagesList.clear();
        
        {
            MutexGuard notifGuard(notifMutex, "notifMutex", __FUNCTION__);
            if (notifGuard.isAcquired()) {
                notifMessagesList.clear();
            }
        }
        
        newClientConnected = true;
        clientState = ClientState::CONNECTED;
        lastStateChange = now; 
    } else {
        // ===== CLIENT SE DÉCONNECTE =====
        // 🔥 PRÉSERVATION DU CODE ORIGINAL - AUCUNE SIMPLIFICATION
        
        Serial.printf("%lu:: Client %lu disconnected\r\n", 
                      now, _client ? (unsigned long)_client->id() : 0);
        
        clientconnected = false;
        
        // Invalider le pointeur seulement si c'est le bon client
        if (client == _client || _client == nullptr) {
            client = nullptr;
        }
        
        // Vider les queues
        webSocketMessagesList.clear();
        
        {
            MutexGuard notifGuard(notifMutex, "notifMutex", __FUNCTION__);
            if (notifGuard.isAcquired()) {
                notifMessagesList.clear();
            }
        }
        
        // Reset des compteurs heartbeat
        nbTimeouts = 0;
        heartbeatReceive = false;
        lastHearbeatReceive = 0;
        lastHeartbeatSend = 0;
        heartbeatTimeout = 0;
        
        // Marquer pour nettoyage différé
        clientState = ClientState::CLEANUP_PENDING;
        lastStateChange = now;
        isCleanupPending = true;
        clientDisconnectionTime = now;
        
        Serial.println("Client disconnected - cleanup initiated");
    }
} // setClientConnected

void BorneUniverselle::setClientDisconnected(AsyncWebSocketClient* disconnectedClient) {
    uint32_t now = millis();
    
    Serial.printf("%lu:: Client %lu déconnecté - nettoyage\r\n", 
                  now, disconnectedClient ? (unsigned long)disconnectedClient->id() : 0);
    

    // Préparer le message de diagnostic AVANT de modifier l'état
    char diagnosticMsg[250];
    if (disconnectedClient) {
        const char* reason = "Unknown";
        switch (disconnectedClient->status()) {
            case WS_DISCONNECTED: reason = "Client closed connection normally"; break;
            case WS_DISCONNECTING: reason = "Client is disconnecting"; break;
            default: reason = "Network error or server timeout"; break;
        }
        snprintf(diagnosticMsg, sizeof(diagnosticMsg),
                "WEBSOCKET_CLIENT_DISCONNECT: Client %lu - %s",
                (unsigned long)disconnectedClient->id(), reason);
    } else {
        snprintf(diagnosticMsg, sizeof(diagnosticMsg), "WEBSOCKET_SERVER_DISCONNECT: Server-initiated disconnection");
    }

    // Marquer le client comme déconnecté EN PREMIER
    // (doit précéder logDiagnostic pour que suspendForInternalOperation ne tente pas d'envoyer à un client mort)
    {
        MutexGuard webSocketGuard(webSocketMutex, "webSocketMutex", __FUNCTION__);
        if (webSocketGuard.isAcquired()) {
            clientconnected = false;
            clientFullyInitialized = false;
            if (this->client == disconnectedClient || disconnectedClient == nullptr) {
                this->client = nullptr;
            }
            webSocketMessagesList.clear();
        }
    }
    
    {
        MutexGuard notifGuard(notifMutex, "notifMutex", __FUNCTION__);
        if (notifGuard.isAcquired()) {
            notifMessagesList.clear();
        }
    }

    // Journaliser APRÈS que clientconnected=false, sinon suspendForInternalOperation
    // croit encore que le client est actif et envoie interfaceSuspend à un client mort
    PLC_Tools::logDiagnostic(diagnosticMsg);

    // Reset des compteurs heartbeat
    nbTimeouts = 0;
    heartbeatReceive = false;
    lastHearbeatReceive = 0;
    lastHeartbeatSend = 0;
    
    // ✅ NOUVEAU : Reset seulement de la suspension JavaScript
    if (suspendedByJavaScript) {
        Serial.printf("%lu:: ⚠️ Client déconnecté, reset suspension JavaScript (count: %d -> %d)\r\n", 
                      millis(), suspendCount, suspendCount > 0 ? suspendCount - 1 : 0);
        
        // Décrémenter le compteur pour la suspension JavaScript
        if (suspendCount > 0) {
            suspendCount--;
        }
        
        suspendedByJavaScript = false;
        
        // Mettre à jour l'état (le PLC peut rester suspendu si opération en cours)
        updateSuspendState();
    }
    
    // Marquer pour nettoyage différé
    clientState = ClientState::CLEANUP_PENDING;
    lastStateChange = now;
    isCleanupPending = true;
    clientDisconnectionTime = now;
    consecutiveConnectionAttempts = 0;  // Reset pour permettre la reconnexion

    Serial.println("Nettoyage immédiat terminé");
} // setClientDisconnected

void BorneUniverselle::diagnosticConnectionState() {
    Serial.println("\n=== DIAGNOSTIC WEBSOCKET ===");
    Serial.printf("🔗 Client BU connecté: %s\r\n", clientconnected ? "OUI" : "NON");
    Serial.printf("💾 Heap libre: %lu bytes\r\n", ESP.getFreeHeap());
    
    if (client) {
        Serial.printf("📡 Statut WebSocket: %u\r\n", client->status());
        Serial.printf("🔔 Queue pleine: %s\r\n", client->queueIsFull() ? "OUI" : "NON");
    }
    
    Serial.printf("⏱️  Dernier heartbeat: %lu ms ago\r\n", millis() - lastHearbeatReceive);
    Serial.printf("🔄 Tentatives timeout: %u/%u\r\n", nbTimeouts, MAX_HEARTBEAT_FAILURES);
    Serial.printf("📨 Messages en attente: %zu\r\n", webSocketMessagesList.size());
    Serial.println("========================");
}

void BorneUniverselle::executeImmediateCleanup() {
    Serial.printf("%lu:: Executing immediate cleanup\r\n", millis());
    
    {
        MutexGuard webSocketGuard(webSocketMutex, "webSocketMutex", __FUNCTION__);
        if (webSocketGuard.isAcquired()) {
            client = nullptr;
            clientconnected = false;
            webSocketMessagesList.clear();
        }
    }
    
    {
        MutexGuard notifGuard(notifMutex, "notifMutex", __FUNCTION__);
        if (notifGuard.isAcquired()) {
            notifMessagesList.clear();
        }
    }
    
    // BUG FIX: Reset complet des états
    clientState = ClientState::DISCONNECTED;
    lastStateChange = millis();
    isCleanupPending = false;
    nbTimeouts = 0;
    heartbeatReceive = false;
    consecutiveConnectionAttempts = 0;
    
    Serial.println("Immediate cleanup completed - ready for new connections");
}

void BorneUniverselle::executeDelayedCleanup() {
        Serial.println("🧹 Executing delayed cleanup");
        executeImmediateCleanup();
}

bool BorneUniverselle::isClientConnected() {
    MutexGuard webSocketGuard(webSocketMutex, "webSocketMutex", __FUNCTION__);
    if (!webSocketGuard.isAcquired()) {
        return false;
    }
    
    // BUG FIX: Vérifications en cascade avec correction automatique
    if (!clientconnected) {
        return false;
    }
    
    if (client == nullptr) {
        // BUG FIX: Corriger l'incohérence
        clientconnected = false;
        return false;
    }
    
    // BUG FIX: Vérifier l'état réel du WebSocket
    if (client->status() != WS_CONNECTED) {
        Serial.printf("%lu:: Client status changed to %d, marking as disconnected\r\n", 
                     millis(), client->status());
        clientconnected = false;
        return false;
    }
    
    return true;
}

void BorneUniverselle::sendHeartbeat(bool reset) {
    bool success = sendTextToClient(hearbeatChain);
    
    if (!success) {
        Serial.println("⚠️  Heartbeat send failed - client may be disconnecting");
        heartbeatReceive = false;
        return;
    }
    
    lastHeartbeatSend = millis();
    
    if (showHeartbeatMessages) {
        Serial.printf("%lu:: Heartbeat sent successfully\r\n", millis());
        Serial.printf("Next heartbeat in: %lu ms\r\n", HEARTBEAT_DELAY);
    }

    // ===== FIX: Reset du flag seulement si demandé =====
    if (reset && heartbeatReceive) {
        heartbeatReceive = false;
    }
    
    // ===== FIX: Ne pas réinitialiser le timeout ici ! =====
    // Le timeout ne doit être réinitialisé que dans updateEarthbeatTimeout()
    // quand on reçoit effectivement une réponse du client
    
    // ===== DIAGNOSTIC: Vérifier si on est en retard =====
    if (millis() > heartbeatTimeout) {
        Serial.printf("⚠️  Heartbeat envoyé en retard de %lu ms\r\n", 
                     millis() - heartbeatTimeout);
    }
}

void BorneUniverselle::clearAllClientNotifications(){
    JsonDocument doc;
    doc[NOTIFICATION] = CLEAR_ALL;
    char chain[200];
    uint16_t len = sizeof(chain);
    serializeJson(doc, chain, len);
    sendTextToClient(chain);
    
    MutexGuard guard(notifMutex, "notifMutex", __FUNCTION__);
    if (!guard.isAcquired()) {
        Serial.println("clearAllClientNotifications:: unable to get mutex");
        return;
    }
    
    notifMessagesList.clear();
}

// =============================================================================
// MÉTHODE STATIQUE : Point d'entrée public (dans .h)
// =============================================================================
bool BorneUniverselle::sendTextToClient(const char *text) {
    BorneUniverselle* instance = getInstance();
    if (instance != nullptr) {
        return instance->sendTextToClientImpl(text);
    } 
    return false;
}

bool BorneUniverselle::sendTextToClientImpl(const char *text) {
    uint32_t startTime = millis();
    const uint32_t maxWaitTime = 5000;
    
    if (strlen(text) > SOCKET_MESSAGE_MAX_SIZE) {
        Serial.printf("Message trop long: %u bytes (max: %u)\r\n", 
                     strlen(text), SOCKET_MESSAGE_MAX_SIZE);
        return false;
    }
    
    // ========================================================================
    // ✅ BOUCLE AVEC LIBÉRATION DU MUTEX PENDANT L'ATTENTE
    // ========================================================================
    while (true) {
        {
            // Prendre le mutex seulement pour vérifier et envoyer
            MutexGuard guard(webSocketMutex, "webSocketMutex", __FUNCTION__); 
            if (!guard.isAcquired()) {
                setPlcBrokenImpl("sendTextToClientImpl:: unable to acquire mutex");
                return false;
            }
            
            // Vérification état client
            if (client == nullptr || !clientconnected || client->status() != WS_CONNECTED) {
                return false;
            }
            
            // ✅ Si la queue N'EST PAS pleine, envoyer immédiatement
            if (!client->queueIsFull()) {
                bool success = client->text(text);
                if (!success) {
                    Serial.println("⚠️ Échec envoi message WebSocket");
                }
                
                uint32_t totalTime = millis() - startTime;
                if (totalTime > 100) {
                    Serial.printf("⏱️ Envoi message WebSocket: %lu ms\n", totalTime);
                }
                
                return success;
            }
            
            // Queue pleine - vérifier timeout
            uint32_t waitTime = millis() - startTime;
            if (waitTime > maxWaitTime) {
                Serial.printf("⚠️ Queue pleine après %lu ms - déconnexion\n", waitTime);
                setClientConnected(false, client);
                return false;
            }
            
            // Log périodique
            if (waitTime == 0 || (waitTime % 500) < 10) {
                if (waitTime == 0) {
                    Serial.println("⏳ Queue WebSocket pleine - attente de libération...");
                } else {
                    Serial.printf("⏳ Attente queue... (%lu ms)\n", waitTime);
                }
                Serial.printf("Message que l'on voulait envoyer: %s\r\n", text);
            }
            
            // ✅ IMPORTANT: Le mutex sera LIBÉRÉ à la fin de ce scope
        }
        
        // ========================================================================
        // ✅ ICI LE MUTEX EST LIBÉRÉ - Les autres fonctions peuvent l'acquérir!
        // ========================================================================
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Recommencer la boucle (re-acquérir le mutex, re-vérifier)
    }
}

void BorneUniverselle::updateEarthbeatTimeout() {
    uint32_t now = millis();
    
    lastHearbeatReceive = now;
    heartbeatTimeout = now + HEARTHBEAT_TIMEOUT; // ← Seul endroit légitime
    heartbeatReceive = true; // Flag pour checkHeartbeat()
    
    // ===== FIX: Reset du compteur d'échecs =====
    nbTimeouts = 0; // Important: reset quand on reçoit une réponse
    
    if (showHeartbeatMessages) {
        Serial.printf("%lu:: ✅ Heartbeat reçu! Timeout reset à %lu ms\r\n", 
                     now, heartbeatTimeout);
    }
}

void BorneUniverselle::checkHeartbeat() {
    // ===== GUARD CLAUSE =====
    if (!isClientConnected() || heartbeatSuspended) {
        return;
    }
    
    uint32_t now = millis();
    
    // ===== 1. VÉRIFICATION DE LA RÉCEPTION =====
    uint32_t timeSinceLastReceive = now - lastHearbeatReceive;
    
    // Variables static pour éviter l'incrément multiple dans les boucles
    static uint32_t lastTimeoutCheck = 0;
    static uint8_t previousTimeouts = 0;
    
    // Vérifier timeout seulement une fois par période HEARTBEAT_DELAY
    if (timeSinceLastReceive > HEARTHBEAT_TIMEOUT && 
        (now - lastTimeoutCheck) > HEARTBEAT_DELAY) {
        
        // 🔍 NOUVEAU : Vérifier la queue AVANT d'incrémenter le timeout
        // Le heartbeat est peut-être arrivé mais pas encore traité
        if (checkForHeartbeatInQueue()) {
            // ✅ Heartbeat trouvé dans la queue !
            nbTimeouts = 0;
            previousTimeouts = 0;
            lastTimeoutCheck = now;
            
            if (showHeartbeatMessages) {
                Serial.printf("%lu:: ✅ Timeout évité - heartbeat trouvé dans la queue\r\n", now);
            }
            return;
        }
        
        // ❌ Vraiment aucun heartbeat, c'est un vrai timeout
        // Incrémenter SEULEMENT si on n'a pas déjà compté ce timeout
        if (nbTimeouts == previousTimeouts) {
            nbTimeouts++;
            previousTimeouts = nbTimeouts;
            lastTimeoutCheck = now;
            
            if (showHeartbeatMessages) {
                Serial.printf("%lu:: ❌ Vrai timeout - Pas de heartbeat depuis %lu ms - timeout #%u/%u\r\n", 
                             now, timeSinceLastReceive, nbTimeouts, MAX_HEARTBEAT_FAILURES);
            }
            
            // Déconnexion après trop de timeouts consécutifs
            if (nbTimeouts >= MAX_HEARTBEAT_FAILURES) {
                Serial.printf("%lu:: 💔 DÉCONNEXION - %u timeouts consécutifs (pas de réception depuis %lu ms)\r\n", 
                             now, nbTimeouts, timeSinceLastReceive);
                setClientDisconnected(client);
                return;
            }
            
            // Tentative de récupération - envoyer un heartbeat
            Serial.printf("%lu:: 🔄 Tentative récupération #%u/%u\r\n", 
                         now, nbTimeouts, MAX_HEARTBEAT_FAILURES);
            sendHeartbeat(false);
        }
    }
    
    // ===== 2. RESET SI HEARTBEAT REÇU (via traitement normal) =====
    if (heartbeatReceive) {
        nbTimeouts = 0;
        previousTimeouts = 0;  // Reset aussi la variable static
        heartbeatReceive = false;
        
        if (showHeartbeatMessages) {
            Serial.printf("%lu:: 💚 Heartbeat reçu via traitement normal\r\n", now);
        }
    }

    // ===== 3. ENVOI PÉRIODIQUE =====
    if (now - lastHeartbeatSend > HEARTBEAT_DELAY) {
        if (showHeartbeatMessages) {
            Serial.printf("%lu:: ⏰ Envoi heartbeat périodique\r\n", now);
        }
        sendHeartbeat(true);
    }
} // checkHeartbeat

void BorneUniverselle::keepAliveHeartbeat() {
    if (!isClientConnected() || heartbeatSuspended) return;
    uint32_t now = millis();
    if (now - lastHeartbeatSend > HEARTBEAT_DELAY) {
        sendHeartbeat(true);
    }
}

bool BorneUniverselle::checkForHeartbeatInQueue() {
    MutexGuard webSocketGuard(webSocketMutex, "webSocketMutex", __FUNCTION__);
    if (!webSocketGuard.isAcquired()) {
        return false;
    }
    
    if (webSocketMessagesList.empty()) {
        return false;
    }
    
    // Parcourir la queue pour trouver un heartbeat
    auto it = webSocketMessagesList.begin();
    while (it != webSocketMessagesList.end()) {
        WEB_SOCKET_MESSAGE* msg = it->get();
        
        if (!msg || !msg->data || msg->len == 0) {
            ++it;  // Message invalide, passer au suivant
            continue;
        }
        
        // Parser le message pour détecter un heartbeat
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, (const char*)msg->data, msg->len);
        
        if (!error && !doc[HEARTBEAT].isNull()) {
            // ✅ Heartbeat trouvé !
            if (showHeartbeatMessages) {
                Serial.printf("%lu:: 💚 Heartbeat trouvé dans la queue (pas encore traité)\r\n", millis());
            }
            
            // 1. Traiter le heartbeat immédiatement
            updateEarthbeatTimeout();
            heartbeatReceive = true;
            lastHearbeatReceive = millis();
            
            // 2. ⚠️ CRITIQUE : Retirer de la queue pour éviter double traitement !
            //    erase() retourne un itérateur vers l'élément suivant
            it = webSocketMessagesList.erase(it);
            
            if (showHeartbeatMessages) {
                Serial.printf("%lu:: 🗑️ Heartbeat supprimé de la queue\r\n", millis());
            }
            
            // 3. Retourner immédiatement (on a trouvé ce qu'on cherchait)
            return true;
        }
        
        // Pas un heartbeat, passer au suivant
        ++it;
    }
    
    // Aucun heartbeat trouvé dans la queue
    return false;
}

void BorneUniverselle::setShowHeartbeatMessages(bool status){
    showHeartbeatMessages = status;
}

bool BorneUniverselle::sendMessage() { 
     // Le MutexGuard assurera la libération du mutex à la sortie de la fonction
    MutexGuard guard(notifMutex, "notifMutex", __FUNCTION__);
    if (!guard.isAcquired()) {
        setPlcBrokenImpl("sendMessage: unable to acquire the mutex");
        return false;
    }
    if (!isClientConnected() || notifMessagesList.empty()) {
        return true;
    }
    
    static uint32_t lastMessageTime = 0;
    const uint32_t MIN_MESSAGE_INTERVAL = 250; // 250ms minimum entre messages
    
    // Vérifier si assez de temps s'est écoulé depuis le dernier message
    if (millis() - lastMessageTime < MIN_MESSAGE_INTERVAL) {
        return true; // On reportera l'envoi au prochain appel
    }

    if (notifMessagesList.empty()) {
        return true;
    }

    NOTIF_MESSAGE *notifMessage = notifMessagesList.front().get();

    if (!notifMessage) {
        Serial.println(F("BorneUniverselle::sendMessage, invalid notification message"));
        notifMessagesList.pop_front();
        return false;
    }

        // Vérification supplémentaire du message
    if (!notifMessage->message) {
        Serial.println(F("BorneUniverselle::sendMessage, invalid message content"));
        notifMessagesList.pop_front();
        return false;
    }

    SafeJsonDocument doc;
    JsonObject notif = doc["notification"].to<JsonObject>();
    notif[STATUS] = 200;

    switch (notifMessage->severity) {
        case ERROR:     notif[TYPE] = "error"; break;
        case WARNING:   notif[TYPE] = "warning"; break;
        case INFO:      notif[TYPE] = "info"; break;
        case SUCCESS:   notif[TYPE] = "success"; break;
        case DEBUG:     notif[TYPE] = "debug"; break;
    }

    size_t msgLen = strlen(notifMessage->message);

    if (msgLen > MAX_MESSAGE_LENGTH) {
        Serial.printf("BorneUniverselle::sendMessage, message too long (%d chars), truncating\r\n", msgLen);
        char tempMessage[MAX_MESSAGE_LENGTH + 1];  // Buffer sur la pile
    
        // Copier en limitant à MAX_MESSAGE_LENGTH caractères
        strncpy(tempMessage, notifMessage->message, MAX_MESSAGE_LENGTH);
        tempMessage[MAX_MESSAGE_LENGTH] = '\0';  // S'assurer que la chaîne est terminée
    
        notif[DESCRIPTION] = tempMessage;
    } else {
        notif[DESCRIPTION] = notifMessage->message;
    }

    bool success = PLC_Tools::sendJsonDocument(doc, "Notification");

    if (success){
        lastMessageTime = millis();
    } else {
        Serial.println("SendMessage: unable to send text to client"); 
    }

    // Toujours retirer le message de la liste (succès ou échec)
    notifMessagesList.pop_front();
    return success;
}

void BorneUniverselle::tooMuchClients(AsyncWebSocketClient *_client){
    if (!_client) {
        Serial.println(F("Invalid client pointer in tooMuchClients"));
        return;
    }

    JsonDocument doc;
    doc[TOO_MUCH_CLIENTS] = true;
    char chain[256];
    serializeJson(doc, chain);
    Serial.printf("%lu:: Too much clients: %lu: will be closed...\r\n", millis(), (unsigned long)_client->id());

    // Envoi du message et fermeture
    if (_client->status() == WS_CONNECTED) {
        _client->text(chain);
        vTaskDelay(pdMS_TO_TICKS(1000));  // Utiliser vTaskDelay au lieu de delay
        _client->close();
    }
}

void BorneUniverselle::prepareMessage(uint8_t type, const char *text){
    BorneUniverselle* instance = getInstance();
    if (instance != nullptr) {
        instance->prepareMessageImpl(type, text);
    } 
}

void BorneUniverselle::prepareMessageImpl(uint8_t type, const char * text){
    // Protection contre les pointeurs nuls ou textes vides
    if (!text || !*text) {
        Serial.println(F("Empty message discarded"));
        return;
    }

    // Calculer la longueur finale pour une tronquage sécurisé
    size_t messageLength = strlen(text);
    size_t finalLength = messageLength < MAX_MESSAGE_LENGTH ? messageLength : MAX_MESSAGE_LENGTH - 1;

    // Créer une copie sécurisée du message
    char safeMessage[MAX_MESSAGE_LENGTH];
    strncpy(safeMessage, text, MAX_MESSAGE_LENGTH - 1);
    safeMessage[MAX_MESSAGE_LENGTH - 1] = '\0';

    if (messageLength > MAX_MESSAGE_LENGTH) {
        Serial.printf("prepareMessage:: message too long (%u chars), truncating\r\n", messageLength);
    }

    switch (type) {
        case ERROR:     Serial.printf("\033[31m%lu:: ERROR: %s\033[0m\r\n", millis(), safeMessage);
                        PLC_Tools::logDiagnostic(safeMessage);
                        break;

        case WARNING:   Serial.printf("\033[33m%lu:: WARNING: %s\033[0m\r\n", millis(), safeMessage);
                        break;

        case INFO:      Serial.printf("\033[34m%lu:: INFO: %s\033[0m\r\n", millis(), safeMessage);
                        break;

        case SUCCESS:   Serial.printf("\033[32m%lu:: SUCCESS: %s\033[0m\r\n", millis(), safeMessage);
                        break;

        case FATAL:     Serial.printf("\033[31m\033[1m%lu:: FATAL: %s\033[0m\r\n", millis(), safeMessage);
                        //                    ^^^^^ gras en plus pour FATAL
                        PLC_Tools::logDiagnostic(safeMessage);
                        break;
    }

    // Filtre config (filteredMessages) : le log Serial ci-dessus reste toujours visible,
    // seule la notification au client HMI est coupée pour les messages filtrés.
    if (PLC_Tools::isMessageFiltered(safeMessage)) {
        return;
    }

    if (!isClientConnected()) {
        // Serial.println("⚠️ Client not connected, skipping notification");
        return;
    }

    /*
    if (!clientFullyInitialized) {
        // Le client n'est pas encore prêt
        // On n'envoie RIEN (même pas les heartbeats sont gérés ici)
        static uint32_t lastSkipLog = 0;
        if (millis() - lastSkipLog > 5000) {  // Log max 1x/5s
            Serial.println("⚠️ Client not fully initialized, skipping notification");
            lastSkipLog = millis();
        }
        return;
    }
        */

    if (type == SUCCESS && !showSuccessMessages) {
        Serial.println("Success messages are disabled, skipping notification");
        return;
    }

    if (type == INFO && !showInfoMessages) {
        Serial.println("Info messages are disabled, skipping notification");
        return;
    }
    
    MutexGuard guard(notifMutex, "notifyMutex", __FUNCTION__); // Le MutexGuard assurera la libération du mutex à la sortie de la fonction
    if (!guard.isAcquired()) {
        setPlcBroken("prepareMessageImpl:: unable to acquire the mutex notifyMutes");
        return;
    }

    if (notifMessagesList.size() >= MAX_MESSAGES_PENDING) {
        Serial.println(F("Notification queue full, dropping oldest message"));
        notifMessagesList.pop_front();
    }

    auto notif = std::unique_ptr<NOTIF_MESSAGE, NotifMessageDeleter>(new NOTIF_MESSAGE());
    notif->message = static_cast<char*>(PLC_Tools::allocateMemory(finalLength + 1));
    if (!notif->message) {
        return;
    }

    notif->severity = type;
    strncpy(notif->message, text, finalLength);
    notif->message[finalLength] = '\0';
    notifMessagesList.push_back(std::move(notif));
    //Serial.println(F("Notification prepared and added to queue"));
    if (notifMessagesList.size() > MAX_MESSAGES_PENDING / 2) {
        Serial.printf("%lu:: prepareMessage:: There are %u / %u notifications on the queue\r\n", 
                     millis(), notifMessagesList.size(), MAX_MESSAGES_PENDING);
    }
}

bool BorneUniverselle::setName(const char *_name, bool check) {
    return instance ? instance->setNameImpl(_name, check) : false;
}

bool BorneUniverselle::setNameImpl(const char *_name, bool check){
    bool status = false;
    if (strlen(_name) < NAME_LENGHT && strlen(_name) > 0 && !strstr(_name, " ")){
        Serial.printf("Project name: %s\r\n", _name);
        if (!check){
            strcpy(name, _name);
        }
        status = true;
    } else {
        char text[256];
        sprintf(text, "The name: ""%s"" is misconfigured (must be not to long and not contains space)", _name);
        prepareMessage(ERROR, text); 
        setPlcBroken(text);  
    }
    return status;
}

char *BorneUniverselle::getName(){
   return instance ? instance->getNameImpl() : nullptr;
}

char *BorneUniverselle::getNameImpl(){
    return name;
}

bool BorneUniverselle::isWebSocketMessagesListMoreThanHalf() { 
    if (!isClientConnected()){
        return false;
    }

    MutexGuard guard(webSocketMutex, "webSocketMutex", __FUNCTION__);
    
    if (!guard.isAcquired()) {
        setPlcBroken("isWebSocketMessagesListMoreThanHalf:: unable to acquire webSocketMutex");
        return true; // Par sécurité, on retourne true pour indiquer que la liste est "pleine"
    }

    bool isMoreThanHalf = webSocketMessagesList.size() > MAX_MESSAGES_PENDING / 2;

    if (isMoreThanHalf) {
        Serial.println(F("isWebSocketMessagesList greater than half capacity"));
    }

    return isMoreThanHalf;
}

bool BorneUniverselle::isAllInputsReadOnce(){
    bool status = true;
    if (!allInputsReadOnce){
        std::map<uint32_t, Node *>::iterator it;
        for (it = nodesMap.begin(); it != nodesMap.end(); it++){
            Node *node = it->second;
            if (node->getMode() == INPUT_MODE){
                InputNode *inputNode = (InputNode *)node;
                if (inputNode->getLastRefresh() == 0){
                    status = false;
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (status){
            allInputsReadOnce = true;
        }
    }

    return allInputsReadOnce;
}

void BorneUniverselle::handleWebSocket(void *_arg, unsigned char *_data, size_t _len, AsyncWebSocketClient *_client){
    // Vérifie simplement que c'est un message texte
    AwsFrameInfo *info = (AwsFrameInfo*)_arg;
    if (info->opcode != WS_TEXT) {
        Serial.println(F("Not a text message"));
        return;
    }

    // WS-2 : le <10 (vieilles erreurs WS) jetait les petits chunks de continuation
    //         d'un message fragmenté → trou dans messageBuffer → corruption. Supprimé.
    //         La borne haute reste : protège le chemin mono-frame (un seul gros frame).
    if (_len > WEB_SOCKET_MESSAGE_MAX_SIZE) { 
        Serial.printf("handleWebSocket:: webSocketMessage chunk too large: %u [bytes]\r\n", _len);
        return;
    }  

    if (info->final && info->index == 0 && info->len == _len && info->opcode == WS_TEXT) {
        // 🔍 DIAGNOSTIC: Logger les messages bruts reçus
        char preview[150];
        size_t previewLen = (_len < 149) ? _len : 149;
        memcpy(preview, _data, previewLen);
        preview[previewLen] = '\0';
        //Serial.printf("%lu:: 📨 WS_RAW: %.140s\r\n", millis(), preview);
        
        keepWebSocketMessage((const char *)_data, _arg, _len, _client);
        return;
    }

    // Premier fragment
    if (info->index == 0) {
        messageBuffer = "";
        expectedSize = info->len;
        Serial.printf("handleWebSocket:: start collecting message, expected size: %u\n", expectedSize);
    }

        // Ajouter le fragment
    // WS-3 : vérifier que la concaténation a bien ajouté _len octets.
    //        Un += sur String peut échouer silencieusement sous pression heap.
    size_t beforeLen = messageBuffer.length();
    messageBuffer += String((char*)_data, _len);
    if (messageBuffer.length() != beforeLen + _len) {
        Serial.printf("\r\n🟥🟥🟥 handleWebSocket:: CONCAT INCONSISTANTE (heap?) "
                      "avant=%u + chunk=%u attendu=%u obtenu=%u | heap libre=%lu 🟥🟥🟥\r\n",
                      (unsigned)beforeLen, (unsigned)_len, (unsigned)(beforeLen + _len),
                      (unsigned)messageBuffer.length(), (unsigned long)ESP.getFreeHeap());
        messageBuffer.clear();
        expectedSize = 0;
        return;
    }

    if (info->final && (info->index + _len) == info->len) {
        // WS-1 : garde-fou d'intégrité — ne livrer QUE si le payload est complet.
        // Tout écart length()/expectedSize = message tronqué → on jette, jamais de sauvegarde d'incomplet (fail-closed).
        if (messageBuffer.length() != expectedSize) {
            Serial.printf("handleWebSocket:: message INCOMPLET rejete: recu %u / attendu %u [octets]\r\n",
                          messageBuffer.length(), expectedSize);
            messageBuffer.clear();
            expectedSize = 0;
            return;
        }
        Serial.println("handleWebSocket: processing complete reassembled message");
        keepWebSocketMessage(messageBuffer.c_str(), _arg, messageBuffer.length(), _client);
        
        // ✅ Nettoyage agressif du buffer
        size_t bufferSize = messageBuffer.length();
        messageBuffer.clear();
        messageBuffer.~String();  // Détruit l'objet
        new (&messageBuffer) String();  // Reconstruit un String vide
        expectedSize = 0;
        
        Serial.printf("🧹 messageBuffer cleaned, heap free: %lu\n", ESP.getFreeHeap());
        // ✅ GC si gros message (> 10 KB)
        if (bufferSize > 10000) {
            Serial.printf("🧹 Large message processed (%u bytes), triggering GC\n", bufferSize);
            PLC_Tools::forceGarbageCollection();
        }
    } else {
        Serial.println("handleWebSocket:: collecting more data");
    }
} // handleWebSocket

void BorneUniverselle::keepWebSocketMessage(const char *data, void *arg, size_t len, AsyncWebSocketClient *_client) {
    if (isPlcBroken()){
        if (!noPLC_BrokenMessage){
            Serial.println(F("Because the PLC is broken, we don't read the websocket message"));
            noPLC_BrokenMessage = true;
        }
        return;
    }

    if (!isClientConnected()) {
        Serial.println(F("Discarding messages from client disconnected or timed out"));
        return; 
    }

    // Add yield to prevent watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(1));

    MutexGuard webSocketGuard(webSocketMutex, "webSocketMutex", __FUNCTION__);
    if (!webSocketGuard.isAcquired()) {
        setPlcBroken("keepWebSocketMessage:: unable to acquire the webSocket mutex");
        return;
    }

     //Limite la taille de la queue en enlevant les messages les plus vieux
    uint16_t webSocketMessageLength;
    while ((webSocketMessageLength = webSocketMessagesList.size()) > MAX_MESSAGES_PENDING) {
        webSocketMessagesList.pop_front();
        Serial.println("Oldest webSocketMessage removed because queue is full");
    }
   
    // Copie des données
    unsigned char* dataCopy = (unsigned char*)PLC_Tools::allocateMemory(len + 1);
    if (!dataCopy) {
        webSocketGuard.~MutexGuard();  // Libération explicite
        prepareMessage(ERROR, "keepWebSocketMessage:: unable to allocate memory for webSocket message");
        return;
    }

    memcpy(dataCopy, data, len);
    dataCopy[len] = 0;

    // Allouer la structure WEB_SOCKET_MESSAGE avec nothrow pour éviter exception
    WEB_SOCKET_MESSAGE* rawMessage = new (std::nothrow) WEB_SOCKET_MESSAGE();
    if (!rawMessage) {
        // ✅ Libérer dataCopy avant de sortir pour éviter fuite mémoire
        PLC_Tools::freeMemory(dataCopy);
        webSocketGuard.~MutexGuard();
        prepareMessage(ERROR, "keepWebSocketMessage:: unable to allocate WEB_SOCKET_MESSAGE structure");
        return;
    }

    // Créer le unique_ptr avec la structure allouée
    auto webSocketMessage = std::unique_ptr<WEB_SOCKET_MESSAGE, WebSocketMessageDeleter>(rawMessage);
    webSocketMessage->arg = arg;
    webSocketMessage->len = len;
    webSocketMessage->client = _client;
    webSocketMessage->data = dataCopy;
    webSocketMessage->timeOfOrigine = millis();
    webSocketMessagesList.push_back(std::move(webSocketMessage));
} //keepWebSocketMessage

bool BorneUniverselle::clientQueueIsFull() {
    MutexGuard guard(webSocketMutex, "webSocketMutex", __FUNCTION__);
    if (!guard.isAcquired()) {
        setPlcBroken("clientQueueIsFull:: unable to acquire webSocket mutex");
        return true;
    }

    // Ne pas forcer le cleanup ici, juste retourner false
    if (!isClientConnected()) {
        return false; // Pas de client = pas de problème de queue
    }
    
    // Vérifier l'état sans forcer le cleanup
    if (client && client->status() != WS_CONNECTED) {
        // ANCIEN CODE BUGUÉ : forceCleanupOnNextCycle = true;
        // NOUVEAU CODE : Juste retourner true pour empêcher l'envoi
        Serial.println("⚠️  Client WebSocket déconnecté détecté dans queue check");
        return true; // Empêcher l'envoi de nouvelles données
    }
    
    // Vérification normale de la queue
    return client && client->queueIsFull();
}

void BorneUniverselle::handleWebSocketMessage() {
    static uint32_t lastMessageTime = millis();
#ifdef SHOW_MESSAGE_QUEUE_SIZE
    if (isClientConnected()) {
        
        uint32_t now = millis();
        if ((now - lastMessageTime > 1000) && webSocketMessagesList.size() > 0) {
            MutexGuard guard(webSocketMutex, "webSocketMutex", __FUNCTION__);
            if (guard.isAcquired()) {
                Serial.printf("%lu:: 📊 webSocketMessagesList.size() = %u\r\n", now, webSocketMessagesList.size());
            }
            lastMessageTime = now;
        }
    }
#endif
    
    if (!isAllConditionsOk()){
        if (millis() - lastMessageTime > 5000){
            lastMessageTime = millis();
        }
    }

    uint32_t messagesProcessedThisCall = 0;
    uint32_t queueSizeAtEntry = 0;
    bool firstIteration = true;
    static uint32_t consecutiveFailures = 0;  // Compteur d'échecs consécutifs

    while (true) {
        if (PF8574BooleanInputNode::isInterrupt()) {
            if (messagesProcessedThisCall > 0 || queueSizeAtEntry > 5) {
                Serial.printf("%lu:: ⚠️ handleWebSocketMessage EXIT (interrupt): processed %lu messages\r\n", 
                             millis(), messagesProcessedThisCall);
            }
            return;
        }

        if (!isClientConnected()) {
            if (messagesProcessedThisCall > 0 || queueSizeAtEntry > 5) {
                Serial.printf("%lu:: ⚠️ handleWebSocketMessage EXIT (not connected): processed %lu messages\r\n", 
                             millis(), messagesProcessedThisCall);
            }
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
        
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
        WEB_SOCKET_MESSAGE workingMessage = {0};
#pragma GCC diagnostic pop
        WEB_SOCKET_MESSAGE *webSocketMessageOrig = nullptr;
        bool messageExtracted = false;

        {   
            MutexGuard webSocketGuard(webSocketMutex, "webSocketMutex", __FUNCTION__);
            if (!webSocketGuard.isAcquired()) {
                setPlcBroken("handleWebSocketMessage:: unable to acquire the webSocket Mutex");
                return;
            }

            if (webSocketMessagesList.empty()) {
                if (messagesProcessedThisCall > 5 || queueSizeAtEntry > 5) {
                    Serial.printf("%lu:: ✅ handleWebSocketMessage EXIT (queue empty): processed %lu messages\r\n", 
                                 millis(), messagesProcessedThisCall);
                }
                return;
            }

            if (firstIteration) {
                queueSizeAtEntry = webSocketMessagesList.size();
                firstIteration = false;
                
                if (queueSizeAtEntry > 5) {
                    Serial.printf("%lu:: 🔴 handleWebSocketMessage ENTER: queue size = %lu\r\n", 
                                 millis(), queueSizeAtEntry);
                }
            }

            webSocketMessageOrig = webSocketMessagesList.front().get();
            if (!webSocketMessageOrig) {
                Serial.println("handleWebSocketMessage:: webSocketMessage Null !!");
                continue;
            }
        
            workingMessage.data = (uint8_t*)PLC_Tools::allocateMemory(webSocketMessageOrig->len + 1);
            if (!workingMessage.data) {
                // ✅ Retirer le message pour éviter retry infini
                webSocketMessagesList.pop_front();
                char text[128];
                snprintf(text, sizeof(text), "handleWebSocketMessage: failed to allocate %u bytes for message may be receiving large file ?", webSocketMessageOrig->len + 1);
                prepareMessage(ERROR, text);

                consecutiveFailures++;
                
                if (consecutiveFailures >= 3) {
                    webSocketGuard.~MutexGuard();
                    setPlcBroken("handleWebSocketMessage: repeated memory allocation failures");
                    return;
                }
                
                // Continuer avec le message suivant
                continue;
            }
            consecutiveFailures = 0;

            workingMessage.len = webSocketMessageOrig->len;
            memcpy(workingMessage.data, webSocketMessageOrig->data, webSocketMessageOrig->len);
            workingMessage.data[webSocketMessageOrig->len] = 0;
            workingMessage.timeOfOrigine = webSocketMessageOrig->timeOfOrigine;
            workingMessage.client = webSocketMessageOrig->client;

            messageExtracted = true;
            
            if (queueSizeAtEntry > 5 && messagesProcessedThisCall < 3) {
                char preview[81];
                size_t previewLen = (workingMessage.len < 80) ? workingMessage.len : 80;
                memcpy(preview, workingMessage.data, previewLen);
                preview[previewLen] = '\0';
                
                uint32_t messageAge = millis() - webSocketMessageOrig->timeOfOrigine;
                Serial.printf("%lu:: 📨 Processing msg #%lu (age: %lu ms): %.80s\r\n", 
                             millis(), messagesProcessedThisCall + 1, messageAge, preview);
            }

            webSocketMessagesList.pop_front();
            messagesProcessedThisCall++;
            
        } // Fin du mutex

        if (!messageExtracted) {
            continue;
        }

        uint32_t processStart = millis();
        vTaskDelay(pdMS_TO_TICKS(1));
        processMessage(&workingMessage);   
        vTaskDelay(pdMS_TO_TICKS(1));

        uint32_t processDuration = millis() - processStart;
        
        if (processDuration > 50 || (queueSizeAtEntry > 5 && messagesProcessedThisCall <= 3)) {
            Serial.printf("%lu:: ⏱️ Message processing took %lu ms\r\n", 
                         millis(), processDuration);
        }

        if (workingMessage.data) {
            PLC_Tools::freeMemory(workingMessage.data);
            workingMessage.data = nullptr;
        }
        
        keepAliveHeartbeat();
    }
}

bool BorneUniverselle::initializeConfig() {
    Serial.println(F("Initializing configuration..."));

    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    PLC_Tools plcTools;

    // Diagnostic
    plcTools.printDiagnosticFile(); 

    // Lecture du fichier JSON
    SafeJsonDocument configDoc;
    if (!persistence.readJsonFromFile(CONFIG_PATH_FILE, configDoc)) {
        setPlcBrokenImpl(persistence.getLastError());
        return false;
    }

    // Parsing de la configuration
    if (configDoc[CONFIG].is<JsonArray>()) {
        if (!parseConfig(configDoc, false)) {
            setPlcBrokenImpl("The config file doesn't contain the key config");
            return false;
        }
    } else {
        unableToFindKey((char *)"config.json file", (char *)TOSTRING(CONFIG));
        return false;
    }

     // Log du reboot
    if (!plcTools.logReboot()) {
        setPlcBrokenImpl("Failed to log reboot");
        return false;
    }
    
    Serial.println(F("Reboot history"));
    Serial.println(F("*************"));
    Serial.println(plcTools.getRebootLog());
    Serial.println(F("End of reboot history"));
    Serial.println();

    // Traitement du fichier d'interface
    if (getConfigVersion() >= 2) {
        PLC_InterfaceMenu* menuInterface = new PLC_InterfaceMenu();
        Serial.println("Will parse the interface file");
        if (!menuInterface->parseFile(INTERFACE_PATH_FILE)) {
            char buff[128];
            sprintf(buff, "Unable to parse the interface file with the path: %s\r\n", INTERFACE_PATH_FILE);
            delete menuInterface;
            setPlcBrokenImpl(buff);
            return false;
        }
        Serial.println("Interface file parsed with success");

        //Serial.println("Loop on nodes to set node name from the interface file");
        for (const MenuNode& menuNode : *menuInterface) {
            //Serial.printf("MenuNode: %s in section %s, hash: %lu\r\n", menuNode.name.c_str(), menuNode.sectionName.c_str(), (long unsigned)menuNode.hash);
            Node* node = findNodeImpl("Loop on nodes to set node name from the interface file", menuNode.hash);
            if (node != nullptr) {
                if (node->setName(menuNode.name.c_str(), menuNode.sectionName.c_str())) {
                    //Serial.printf("Node name set: %s\r\n", node->getName());
                } else {
                    String fullName = String(menuNode.sectionName.c_str()) + "/" + String(menuNode.name.c_str());
                    char buff[128];
                    sprintf(buff, "Unable to set node name: %s \r\n", fullName.c_str());
                    setPlcBrokenImpl(buff);
                    delete menuInterface;
                    return false;
                }
            }

            if (isPlcBroken()) {
                delete menuInterface;
                return false;
            }
        }

        delete menuInterface;
    } else {
        Serial.printf("Config version: %2.2f\r\n", getConfigVersion());
    }

    // Lister les fichiers
    auto files = persistence.getFilteredFiles("/", "*.*");
    Serial.println("List of files in the root directory:");
    for (const String& fileName : files) {
        Serial.println(fileName);
    }

    Serial.println(F("Configuration initialized successfully"));

    return true;
}

bool BorneUniverselle::processMessage(WEB_SOCKET_MESSAGE *webSocketMessage) {
    // processMessage doit retourner false si on veut garder le message dans la liste.

    SafeJsonDocument socketDoc(65536); 
    DeserializationError error = deserializeJson(socketDoc, (const char*)webSocketMessage->data);

    if (error) {
        char buff[256];
        sprintf(buff, "%lu:: processMessage: deserializeJson() failed: ", millis());
        strcpy_P(buff + strlen(buff) , (const prog_char*) error.f_str());
        Serial.println(buff);
        Serial.printf("Raw message length: %u, raw data: %.50s\n", strlen((char *)webSocketMessage->data), (char*)webSocketMessage->data);
        prepareMessage(ERROR, JSON_NOT_VALID);
        return true;
    }

    // ===== FIX: Traitement spécial pendant l'initialisation =====
    if (!setupComplete) {
        
        // Les heartbeats sont toujours acceptés
        if (!socketDoc[HEARTBEAT].isNull()) {
            updateEarthbeatTimeout();
            heartbeatReceive = true;
            lastHearbeatReceive = millis();
            if (showHeartbeatMessages && client != nullptr) {
                Serial.printf("%lu:: Heartbeat from client during startup: %s\r\n", 
                    millis(), socketDoc[HEARTBEAT] ? "true": "false");   
            }
            return true;
        }
        
        // allStatesRequest pendant startup : reporter
        if (!socketDoc[ALL_STATES_REQUEST].isNull()) {
            Serial.printf("%lu:: allStatesRequest deferred - setup not complete\r\n", millis());
            prepareMessage(INFO, "Initialisation en cours...");
            return false; // Garder le message pour le retraiter plus tard
        }
        
        // initialStatesLoaded pendant startup : reporter aussi
        if (!socketDoc[INITIAL_STATE_LOADED].isNull()) {
            Serial.printf("%lu:: initialStatesLoaded deferred - setup not complete\r\n", millis());
            return false;
        }
        
        // Autres messages non critiques : reporter
        Serial.printf("%lu:: Non-critical message deferred during startup\r\n", millis());
        return false;
    }

    // ===== Setup complet : traitement normal =====
    updateEarthbeatTimeout(); // n'importe quel message compte comme un heartbeat reçu
    
    if (!socketDoc[HEARTBEAT].isNull()){
        heartbeatReceive = true;
        lastHearbeatReceive = millis();     
        if (showHeartbeatMessages && client != nullptr){
            Serial.printf("%lu:: Heartbeat from client id: %lu received, value: %s\r\n", millis(), (unsigned long)client->id(), socketDoc[HEARTBEAT] ? "true": "false");   
        }
    } else if (!socketDoc[CONFIG].isNull()){
        if (!BorneUniverselle::parseConfig(socketDoc)){
            Serial.println(F("Config is not valid !!!"));
        } else {
            Serial.println(F("BorneUniverselle::processMessage:: Will update config and restart"));
            saveParameters(socketDoc);
            delay(1000);
            ESP.restart();
        } 
    } else if (!socketDoc[NOTIFY_STATES_CHANGED].isNull()){
        //Serial.println(F("BorneUniverselle::processMessage: receive a NOTIFY_STATES_CHANGED"));
        handleNodesChange(socketDoc);
    } else if (!socketDoc[GET_VALUE].isNull()){
        //uint32_t hash = socketDoc[GET_VALUE];
        //Serial.printf("%lu::BorneUniverselle::processMessage: receive a get value for node: %lu\r\n", millis(), (unsigned long)hash);
        if (!handleGetValue(socketDoc[GET_VALUE])){
            return false;
        }
    } else if (!socketDoc[ALL_STATES_REQUEST].isNull()){
        Serial.println(F("BorneUniverselle::processMessage: receive get all states request"));
        if (!handleGetAllValues()){
            return false;
        }
    } else  if (!socketDoc[DIRECTORY].isNull()){
        Serial.println(F("BorneUniverselle::processMessage: receive a directory request"));
        handleDirectoryRequest(socketDoc);
    } else if (!socketDoc[SAVE_FILE].isNull()){
        Serial.println(F("BorneUniverselle::processMessage: receive a set save file request"));
        size_t heapBefore = ESP.getFreeHeap();
        handleSaveFile(socketDoc);
        // 🧹 GC après sauvegarde de fichier volumineux
        size_t heapAfter = ESP.getFreeHeap();
        if (heapBefore - heapAfter > 5000) {  // Si on a perdu > 5KB
            Serial.printf("🧹 Memory consumed: %d bytes, triggering GC\n", 
                        heapBefore - heapAfter);
            PLC_Tools::forceGarbageCollection();
        } 
    } else if (!socketDoc[DELETE_FILE].isNull()){
        Serial.println(F("BorneUniverselle::processMessage: receive a delete file request"));
        handleDeleteFileRequest(socketDoc); 
    } else if (!socketDoc[INITIAL_STATE_LOADED].isNull()){
        Serial.println(F("BorneUniverselle::processMessage: initial state loaded"));
        if (initialStateLoadedCallback) {
             clientFullyInitialized = true; // avant la callback car la callback va envoie le message enableInterface
            // Appel de la callback
            initialStateLoadedCallback();
            forceUpdateVirtualInputNodes();

            Serial.println("Calling notifyWebClient after initialStateLoadedHandler");
            notifyWebClient(false);  // Notify changed nodes only
           
        } else {
            Serial.println(F("No callback set for initial state loaded"));
        }
    } else if (!socketDoc[PLC_SUSPEND].isNull()){
       bool request = socketDoc[PLC_SUSPEND].as<bool>();
        handleSuspendRequest(request);
    } else if (!socketDoc[IS_PLC_IDLE].isNull()){
        handleIsPLCIdleRequest();
    } else  if (!socketDoc[CURRENT_UNIX_TIME].isNull()){
        Serial.println(F("BorneUniverselle::processMessage: receive a unix clock update request"));
        handleUnixClockUpdateRequest(socketDoc);
    } else if (!socketDoc[CURRENT_LOCAL_TIME].isNull()){
        Serial.println(F("BorneUniverselle::processMessage: receive a local clock update request"));
        handleLocalClockUpdateRequest(socketDoc);
    } else if (!socketDoc[ON_TAB_CHANGE].isNull()) {
        const char* tabName = socketDoc[ON_TAB_CHANGE] | "unknown";
        Serial.printf("BorneUniverselle::processMessage: Tab change detected -> %s\r\n", tabName);
    
        if (businessLogic) {
            businessLogic->onTabChange(tabName);
        } else {
            Serial.println("BorneUniverselle::processMessage: No BusinessLogic registered");
        }
    } else {
        String out1, out2;
        out1 = "Json root key received by web socket is unknow !, json received: ";
        serializeJsonPretty(socketDoc, out2);
        out1 = out1 + out2;
        prepareMessage(ERROR, out1.c_str());
    }
    return true;
} // processMeSSAGE

void BorneUniverselle::setBusinessLogic(BusinessLogic* logic) {
    if (logic == nullptr) {
        Serial.println("❌ setBusinessLogic: logic is null!");
        return;
    }
    
    // ✅ Garder la référence
    businessLogic = logic;
    
    // ✅ Enregistrer le callback
    // Utilise une lambda qui capture 'this'
    logic->setSuspendCallback([this](bool suspend, bool accepted, const char* reason) {
        this->onBusinessLogicSuspendEvent(suspend, accepted, reason);
    });
    
    Serial.println("✅ BusinessLogic registered with callback");
}

/**
 * @brief Gère la demande de suspension venant du WebSocket
 */
bool BorneUniverselle::handleSuspendRequest(bool request) {
    if (request) {
        // ===== DEMANDE DE SUSPENSION PAR JAVASCRIPT =====
        if (suspendedByJavaScript) {
            Serial.printf("%lu:: ✅ JavaScript suspend request - already suspended, acknowledging\r\n", millis());
            sendSuspendResponse(true);  // Accepté (déjà fait)
            return true;
        }
        
        Serial.printf("%lu:: 📩 JavaScript requests suspend\r\n", millis());
        suspendedByJavaScript = true;
        
        // Incrémenter le compteur
        suspendCount++;
        
        // Si c'est la première suspension, vraiment suspendre
        if (suspendCount == 1) {
            updateSuspendState();  // ✅ délègue la logique
        }
        
        sendSuspendResponse(true);
        return true;
        
    } else {
        // ===== DEMANDE DE REPRISE PAR JAVASCRIPT =====
        if (!suspendedByJavaScript) {
            Serial.printf("%lu:: ✅ JavaScript resume request - already resumed, acknowledging\r\n", millis());
            sendSuspendResponse(false);
            return false;
        }
        
        Serial.printf("%lu:: 📩 JavaScript requests resume\r\n", millis());
        suspendedByJavaScript = false;
        
        // Décrémenter le compteur
        if (suspendCount > 0) {
            suspendCount--;
        }
        
        // Si compteur == 0, vraiment reprendre
        if (suspendCount == 0) {
            updateSuspendState();  // ✅ délègue la logique
        }
        
        sendSuspendResponse(true);
        return true;
    }
} // handleSuspendRequest

bool BorneUniverselle::handleIsPLCIdleRequest() const {
    Serial.println("\r\n🔵 === IS_IDLE REQUEST from WebSocket ===");
    // Sauvegarde forcée des fichiers différé
    PLC_Persistence::getInstance().flushAll();

    // ✅ APPEL POLYMORPHIQUE - fonctionne avec n'importe quelle BusinessLogic !
    businessLogic->handleIsPLCIdleRequest();
    
    return true;
}

/**
 * @brief Callback appelé par BusinessLogic
 */
void BorneUniverselle::onBusinessLogicSuspendEvent(bool suspend, bool accepted, const char* reason) {
    Serial.printf("%lu:: 📞 Callback from BusinessLogic: suspend=%d, accepted=%d\r\n", 
                  millis(), suspend, accepted);
    
    if (!accepted) {
        prepareMessage(ERROR, reason ? reason : "Suspension request denied by BusinessLogic");
        sendSuspendResponse(false);
        return;
    }
    
    // Demande acceptée par BusinessLogic
    if (suspend) {
        // ========================================
        // SUSPENSION
        // ========================================
        if (plcSuspended) {
            Serial.println("⚠️ PLC already suspended, ignoring");
            sendSuspendResponse(true);
            return;
        }
        
        suspendPLCServices();
        plcSuspended = true;
        suspensionStartTime = millis();
        sendSuspendResponse(true);
        
        Serial.printf("%lu:: 🔴 PLC is now SUSPENDED\r\n", millis());
        
    } else {
        // ========================================
        // REPRISE
        // ========================================
        if (!plcSuspended) {
            Serial.println("⚠️ PLC not suspended, ignoring resume");
            sendSuspendResponse(false);
            return;
        }
        
        uint32_t duration = millis() - suspensionStartTime;
        
        resumePLCServices();
        sendHeartbeat(); // Envoyer un heartbeat immédiat  thierry
        plcSuspended = false;
        
        sendSuspendResponse(false);
        
        Serial.printf("%lu:: 🟢 PLC RESUMED (was suspended %lu ms)\r\n", millis(), duration);
    }
}

void BorneUniverselle::suspendPLCServices() {
    Serial.println("  🔴 Suspending PLC services...");
    
    if (isClientConnected()){
        heartbeatSuspended = true;
        Serial.println("    ✓ Heartbeat suspended");
    }
    
    
    myModbus.suspend();
    Serial.println("    ✓ Modbus suspended");
}

void BorneUniverselle::resumePLCServices() {
    Serial.println("  🟢 Resuming PLC services...");
    
    myModbus.resume();
    Serial.println("    ✓ Modbus resumed");
    
    // Toujours remettre à false pour éviter l'incohérence
    heartbeatSuspended = false;
    
    if (isClientConnected()){
        sendHeartbeat();
        heartbeatTimeout = millis() + HEARTHBEAT_TIMEOUT;
        Serial.println("    ✓ Heartbeat resumed");
    }
}

void BorneUniverselle::sendSuspendResponse(bool accepted) {
    JsonDocument doc;
    doc[PLC_SUSPENDED] = accepted;
    
    char chain[200];
    serializeJson(doc, chain, sizeof(chain));
    sendTextToClient(chain);
}

void BorneUniverselle::setPlcSuspended(bool status) {
    plcSuspended = status;
    if (plcSuspended) {
        Serial.printf("%lu:: PLC is now suspended\r\n", millis());
    } else {
        Serial.printf("%lu:: PLC is now resumed\r\n", millis());
    }
}

void BorneUniverselle::forceUpdateVirtualInputNodes() {
    Serial.printf("%lu:: forceUpdateVirtualInputNodes - Début du parcours (client nouvellement connecté)\r\n", millis());
    
    int virtualNodesCount = 0;
    int updatedNodesCount = 0;
    
    // Parcourir tous les nodes de la map
    for (const auto& pair : nodesMap) {
        Node* node = pair.second;
        
        if (node == nullptr) {
            continue;  // Sécurité : ignorer les pointeurs null
        }
        
        // Récupérer le type du node
        int nodeType = node->classType();
        
        // Vérifier si c'est un node virtuel input
        bool isVirtualInput = (nodeType == CLASS_VIRTUAL_BOOLEAN_INPUT_NODE) ||
                             (nodeType == CLASS_VIRTUAL_UINT32_INPUT_NODE) ||
                             (nodeType == CLASS_VIRTUAL_FLOAT_INPUT_NODE) ||
                             (nodeType == CLASS_VIRTUAL_TEXT_INPUT_NODE);
        
        if (isVirtualInput) {
            virtualNodesCount++;
            
            // Cast en InputNode pour accéder à setChangeEvent()
            InputNode* inputNode = dynamic_cast<InputNode*>(node);
            
            if (inputNode != nullptr) {
                inputNode->forceIsChanged();  // Cette méthode met updateNeeded à true
                updatedNodesCount++;
#ifdef DEBUG
                Serial.printf("  - Node virtuel input mis à jour: %s (hash: %lu)\r\n", node->getName(), (unsigned long)node->getHash() );
#endif
            }
        }
    }
    
    Serial.printf("%lu:: forceUpdateVirtualInputNodes - Terminé. %d nodes virtuels trouvés, %d mis à jour\r\n", 
                 millis(), virtualNodesCount, updatedNodesCount);
}

void BorneUniverselle::markSetupComplete() {
    setupComplete = true;
    Serial.printf("%lu:: Setup complete - normal WebSocket processing enabled\r\n", millis());
    
    // Informer le client que le système est prêt
    if (isClientConnected()) {
        prepareMessage(SUCCESS, "Système initialisé");
    }
}

// Implémentation de la méthode pour enregistrer la callback
void BorneUniverselle::setInitialStateLoadedCallback(std::function<void()> callback) {
    initialStateLoadedCallback = callback;
}

void BorneUniverselle::setTabChangeCallback(std::function<void(const char*)> callback) {
    tabChangeCallback = callback;
}

bool BorneUniverselle::handleSaveFile(JsonDocument& socketDoc) {
    const char* path = socketDoc["saveFile"][PATH];
    const char* data = socketDoc["saveFile"][DATA];

    // Guard Clause: Vérifier path et data
    if (!path || !data) {
        Serial.println("handleSaveFile:: path or data empty!");
        prepareMessage(ERROR, "Path or data is empty");
        return false;
    }

    Serial.printf("BorneUniverselle::handleSaveFile: path: %s, data length: %u\r\n", 
                  path, strlen(data));

    PLC_Persistence& persistence = PLC_Persistence::getInstance();

    // ==========================================================================
    // ROUTER SELON L'EXTENSION DU FICHIER
    // ==========================================================================
    
    // Détection de l'extension .json
    size_t pathLen = strlen(path);
    bool isJsonFile = (pathLen > 5 && strcmp(path + pathLen - 5, ".json") == 0);
    
    bool success = false;
    
    if (isJsonFile) {
        // 📄 Fichier JSON : validation obligatoire (syntaxe + métier via callback)
        Serial.printf("handleSaveFile:: Fichier JSON détecté, validation activée\r\n");
        success = persistence.saveJsonToFile(path, data);
        // ✅ La pré-validation a été faite automatiquement !
        
    } else {
        // 📁 Autre type de fichier : sauvegarde directe
        Serial.printf("handleSaveFile:: Fichier non-JSON, sauvegarde directe\r\n");
        success = persistence.saveToFile(path, data);
    }
    
    // ==========================================================================
    // GESTION DES RÉSULTATS
    // ==========================================================================
    sendFileSavedNotification(success, path, persistence.getLastError());
    if (success) {
        char buff[256];
        sprintf(buff, "File: %s saved with success", path);
        prepareMessage(SUCCESS, buff);
        return true;
    } else {
        PLC_Tools::logDiagnostic(persistence.getLastError());
        prepareMessage(ERROR, persistence.getLastError());
        return false;
    }
} // handleSaveFile

bool BorneUniverselle::handleDeleteFileRequest(JsonDocument& socketDoc) {
    const char* path = socketDoc[DELETE_FILE];
    char pathBuff[PATH_MAX_SIZE];
    if (strstr(path, "/") == nullptr) {
        
        sprintf(pathBuff, "/%s", path);
    } else {
        strcpy(pathBuff, path);
    }  
    Serial.printf("BorneUniverselle::handleDeleteFileRequest: path: %s\r\n", pathBuff);

    // ⭐ VALIDATION MÉTIER AVANT SUPPRESSION
    if (businessLogic && !businessLogic->canDeleteFile(pathBuff)) {
        Serial.printf("⛔ File deletion refused by business logic: %s\r\n", pathBuff);
        sendFileDeleteNotification(false, pathBuff, "Deletion refused: protected file");
        
        char buff[256];
        snprintf(buff, sizeof(buff), "File Deletion REFUSED: %s (protected file)", pathBuff);
        PLC_Tools::logDiagnostic(buff);
        
        return false;
    }

    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    bool status = persistence.deleteFile(pathBuff);
    sendFileDeleteNotification(status, pathBuff, persistence.getLastError());

    char buff[256];
    snprintf(buff, sizeof(buff), "File Deletion: %s, Status: %s, Error: %s", pathBuff, status ? "Success" : "Failure", persistence.getLastError());
    PLC_Tools::logDiagnostic(buff); 
    
    return status;
}

void BorneUniverselle::handleDirectoryRequest(JsonDocument& socketDoc){
    Serial.println(F("BorneUniverselle::handleDirectoryRequest"));
    if (socketDoc[FILTER].isNull()){
        Serial.printf("BorneUniverselle::handleDirectoryRequest: %s key is missing\r\n", FILTER);
      return; 
    }

    String filter = socketDoc[FILTER].as<String>();
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    SafeJsonDocument doc = persistence.getFilteredFilesAsJson("/", filter.c_str());
    //serializeJson(doc, Serial);
    if (isClientConnected() && client != nullptr){
        PLC_Tools::sendJsonDocument(doc, "Directory");
    }
}

bool BorneUniverselle::handleGetAllValues(){
    if (!isAllConditionsOk()){
        Serial.printf("%lu::ℹ️ handleGetAllValues: sending all states (virtual inputs auto-skipped)\r\n", millis());
    }

    suspendForInternalOperation("get all values");
    bool success = notifyWebClient(true);
    resumeFromInternalOperation();
    return success;
}

bool BorneUniverselle::handleGetValue(uint32_t hash){
    // La fonction doit retrourner false pour garder l'événement sur la pile
    Node *node = findNodeImpl("handleGetValue", hash);
    if (node == nullptr){
        return true;// node not found, on veux supprimer ce message (de toute façon on va avoir plc broken...)
    }

    // 🔥 Vérifier seulement si CE node a été lu
    if (node->getMode() == INPUT_MODE) {
        InputNode *inputNode = (InputNode *)node;
        if (inputNode->getLastRefresh() == 0) {
            // Ce node input n'a jamais été rafraîchi, on garde le message
            Serial.printf("%lu:: handleGetValue: node %s not yet read, deferring request\r\n", millis(), node->getName());
            return false; // Garder le message pour le retraiter plus tard
        }
    }

    Serial.printf("handleGetValue: receive a getValue for node: %s, hash: %lu\r\n", node->getName(), (unsigned long)hash);

    SafeJsonDocument notifyDoc;
    JsonArray array = notifyDoc[NOTIFY_STATES_CHANGED].to<JsonArray>();
    JsonObject nodeObject = array.add<JsonObject>();
    bool success = addNodeToNodeObject(node, &nodeObject);
    if (!success){
        Serial.println(F("handleGetValue: addNodeToNodeObject failed"));
        return false; // on supprime ma requête
    } 
   
    success = PLC_Tools::sendJsonDocument(notifyDoc, "GetValue");
    if (!success){
        Serial.println(F("Unable to send message to client"));
       
    }
    return success;
} // handleGetValue

bool BorneUniverselle::handleNodesChange(JsonDocument& socketDoc){
    // On accepte tous les nodes virtuelles et les nodes output
    // Rappel on ne peux pas modifier une entrée, c'est le monde physique qui modifie une entrée !!!!
    uint32_t start = millis();
  
    //Serial.printf("%lu:: handleNodesChange: Web socket nodes change received. ", millis());
    //serializeJsonPretty(socketDoc, Serial);
    
    char mess[512];
    for (JsonObject state : socketDoc["states"].as<JsonArray>()) {
         // is it a hardware input interrupt ?
        if (PF8574BooleanInputNode::isInterrupt()){
            return true;
        }     

        uint32_t hash = state[HASH].as<uint32_t>();
        Node *node = findNodeImpl("handleNodesChange", hash);
        if (node == nullptr){
            return false;
        }

        // Obtenir le classType une seule fois
        int classType = node->classType();
        
        // ========================================
        // SWITCH/CASE AU LIEU DE IF/ELSE
        // ========================================
        switch (classType) {
            
            // === BOOLEAN VIRTUAL INPUT ===
            case CLASS_VIRTUAL_BOOLEAN_INPUT_NODE: {
                VirtualBooleanInputNode *b = (VirtualBooleanInputNode*) node;
                b->setValue(state[VALUE]);
                Serial.printf("Node: %s new value: %s\r\n", node->getName(), b->getValue() ? "true" : "false");
                break;
            }
            
            // === BOOLEAN VIRTUAL OUTPUT ===
            case CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE: {
                VirtualBooleanOutputNode *b = (VirtualBooleanOutputNode *) node;
                b->setValue(state[VALUE]);
                Serial.printf("Node: %s new value: %s\r\n", node->getName(), b->getValue() ? "true" : "false");
                break;
            }
            
            // === BOOLEAN HARDWARE OUTPUT ===
            case CLASS_HW_BOOLEAN_OUTPUT_NODE: {
                HardwareBooleanOutputNode *h = (HardwareBooleanOutputNode *) node;
                h->setValue(state[VALUE]);
                Serial.printf("Node: %s new value: %s\r\n", node->getName(), h->getValue() ? "true" : "false");
                break;
            }
            
            // === BOOLEAN PF8574 OUTPUT ===
            case CLASS_PFC8574_BOOLEAN_OUTPUT_NODE: {
                PF8574BooleanOutputNode *p = (PF8574BooleanOutputNode *) node;
                p->setValue(state[VALUE]);
                Serial.printf("Node: %s, new value: %s\r\n", node->getName(), p->getValue() ? "true" : "false");
                break;
            }

            // === BOOLEAN PCA9554 OUTPUT ===
            case CLASS_PCA9554_BOOLEAN_OUTPUT_NODE: {
                PCA9554BooleanOutputNode *p = (PCA9554BooleanOutputNode *) node;
                p->setValue(state[VALUE]);
                Serial.printf("Node: %s, new value: %s\r\n", node->getName(), p->getValue() ? "true" : "false");
                break;
            }

            // === MODBUS WRITE COIL ===
            case CLASS_MODBUS_WRITE_COIL_NODE: {
                ModbusWriteCoilNode *m = (ModbusWriteCoilNode *)node;
                m->setValue(state[VALUE]);
                Serial.printf("Node: %s new value: %s\r\n", node->getName(), m->getValue() ? "true" : "false");
                break;
            }
            
            // === UINT32 VIRTUAL INPUT ===
            case CLASS_VIRTUAL_UINT32_INPUT_NODE: {
                VirtualUint32InputNode *v = (VirtualUint32InputNode *)node;
                //Serial.printf("Received value: %d\r\n", state[VALUE].as<unsigned int>());
                v->setValue(state[VALUE].as<unsigned int>());
                Serial.printf("Node: %s new value: %lu\r\n", node->getName(), (unsigned long)v->getValue());
                break;
            }
            
            // === UINT32 VIRTUAL OUTPUT ===
            case CLASS_VIRTUAL_UINT32_OUTPUT_NODE: {
                VirtualUint32OutputNode *v = (VirtualUint32OutputNode *)node;
                v->setValue(state[VALUE]);
                Serial.printf("Node: %s new value: %lu\r\n", node->getName(), (unsigned long)v->getValue());
                break;
            }
            
            // === MODBUS WRITE HOLDING REGISTER ===
            case CLASS_MODBUS_WRITEHOLDINGREGISTER: {
                ModbusWriteHoldingRegister *w = (ModbusWriteHoldingRegister *)node;
                w->setValue(state[VALUE]);
                Serial.printf("Node: %s new value: %s\r\n", node->getName(), w->getValue() ? "true" : "false");
                break;
            }
            
            // === MODBUS WRITE DOUBLE HOLDING REGISTER ===
            case CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER: {
                ModbusWriteDoubleHoldingRegister *w = (ModbusWriteDoubleHoldingRegister *)node;
                w->setValue(state[VALUE]);
                Serial.printf("Node: %s new value: %s\r\n", node->getName(), w->getValue() ? "true" : "false");
                break;
            }
            
            // === FLOAT VIRTUAL INPUT ===
            case CLASS_VIRTUAL_FLOAT_INPUT_NODE: {
                VirtualFloatInputNode *k = (VirtualFloatInputNode *)node;
                k->setValue(state[VALUE]);
                Serial.printf("Node: %s new value: %3.3f\r\n", node->getName(), k->getValue());
                break;
            }
            
            // === FLOAT VIRTUAL OUTPUT ===
            case CLASS_VIRTUAL_FLOAT_OUTPUT_NODE: {
                VirtualFloatOutputNode *w = (VirtualFloatOutputNode *)node;
                w->setValue(state[VALUE]);
                Serial.printf("Node: %s new value: %3.3f\r\n", node->getName(), w->getValue());
                break;
            }
            
            // === TEXT VIRTUAL INPUT ===
            case CLASS_VIRTUAL_TEXT_INPUT_NODE: {
                Serial.println(F("BorneUniverselle::handleNodesChange: CLASS_VIRTUAL_TEXT_INPUT_NODE"));
                if (!state[VALUE].isNull()) {
                    VirtualTextInputNode *t = static_cast<VirtualTextInputNode *>(node);
                    
                    // Gérer différents types de valeurs
                    if (state[VALUE].is<const char*>()) {
                        // Cas normal : chaîne de caractères
                        const char* textValue = state[VALUE].as<const char*>();
                        t->setValue(textValue);
                    } else if (state[VALUE].is<int>() || state[VALUE].is<unsigned int>() || state[VALUE].is<long>()) {
                        // Cas nombre entier : convertir en chaîne
                        char buffer[32];
                        snprintf(buffer, sizeof(buffer), "%d", state[VALUE].as<int>());
                        t->setValue(buffer);
                    } else if (state[VALUE].is<float>() || state[VALUE].is<double>()) {
                        // Cas nombre décimal : convertir en chaîne
                        char buffer[32];
                        snprintf(buffer, sizeof(buffer), "%.2f", state[VALUE].as<double>());
                        t->setValue(buffer);
                    } else {
                        // Cas par défaut : utiliser la sérialisation JSON
                        String valueStr;
                        serializeJson(state[VALUE], valueStr);
                        t->setValue(valueStr.c_str());
                    }
                    
                    Serial.printf("Node: %s new value: %s\r\n", node->getName(), t->getValue());
                } else {
                    sprintf(mess, "handleNodesChange: CLASS_VIRTUAL_TEXT_INPUT_NODE Null value for node: %s, hash: %lu\r\n", node->getName(), (unsigned long)hash);
                    Serial.println("State received from web socket:\n");
                    PLC_Tools::printJsonObject(state);
                    setPlcBroken(mess);
                    return false;
                }
                break;
            }
            
            // === TEXT VIRTUAL OUTPUT ===
            case CLASS_VIRTUAL_TEXT_OUTPUT_NODE: {
                if (!state[VALUE].isNull()) {
                    VirtualTextOutputNode *w = static_cast<VirtualTextOutputNode *>(node);
                    
                    // Gérer différents types de valeurs
                    if (state[VALUE].is<const char*>()) {
                        // Cas normal : chaîne de caractères
                        const char* textValue = state[VALUE].as<const char*>();
                        w->setValue(textValue);
                    } else if (state[VALUE].is<int>() || state[VALUE].is<unsigned int>() || state[VALUE].is<long>()) {
                        // Cas nombre entier : convertir en chaîne
                        char buffer[32];
                        snprintf(buffer, sizeof(buffer), "%d", state[VALUE].as<int>());
                        w->setValue(buffer);
                    } else if (state[VALUE].is<float>() || state[VALUE].is<double>()) {
                        // Cas nombre décimal : convertir en chaîne
                        char buffer[32];
                        snprintf(buffer, sizeof(buffer), "%.2f", state[VALUE].as<double>());
                        w->setValue(buffer);
                    } else {
                        // Cas par défaut : utiliser la sérialisation JSON
                        String valueStr;
                        serializeJson(state[VALUE], valueStr);
                        w->setValue(valueStr.c_str());
                    }
                    
                    Serial.printf("Node: %s new value: %s\r\n", node->getName(), w->getValue());
                } else {
                    sprintf(mess, "handleNodesChange: CLASS_VIRTUAL_TEXT_OUTPUT_NODE Null value for node: %s, hash: %lu\r\n", node->getName(), (unsigned long)hash);
                    Serial.println("State received from web socket:\n");
                    PLC_Tools::printJsonObject(state);
                    setPlcBroken(mess);
                    return false;
                }
                break;
            }
            
            // === TYPE NON GÉRÉ ===
            default: {
                sprintf(mess, "handleNodesChange:: Unable to find class of node with hash: %lu, classType %d", (unsigned long)hash, node->classType());
                setPlcBroken(mess);
                return false;
            }
        }
        
        yield();
    }

    // Serial.println(F("End Web socket nodes change received\r\n"));

    if (millis() - start > 100){
        Serial.printf("Handle message time: %lu\r\n", millis() - start);
    }
    
    return true;
}

bool BorneUniverselle::getLogOnDiagnosticFile(){
    return logOnDiagnosticFile;
}

bool BorneUniverselle::getDeleteDiagnosticFileAtStartup() {
    return deleteDiagnosticFileAtStartup;
}

bool BorneUniverselle::parseConfig( JsonDocument& doc,bool check){
    bool status = true;
    projectVersion = 0;
    Serial.println("Will parse config");

    if (!doc[NAME].isNull()){
        String name = doc[NAME]; // project name
        if (!setName(name.c_str(), check)){
           status = false; 
        }
    } else {
       prepareMessage(ERROR, NO_NAME_KEY);         //         "The key Name is not found"
       status = false;
    }

    if (!doc[LOG_ON_DIAGNOSTIC_FILE].isNull()){
        logOnDiagnosticFile = doc[LOG_ON_DIAGNOSTIC_FILE].as<bool>();
    } else {
        Serial.println(F("The key logOnDiagnosticFile is not found, will use default value: true"));
        logOnDiagnosticFile = true; // Valeur par défaut
    }

    if (!doc[SHOW_SUCCESS_MESSAGES].isNull()){
        showSuccessMessages = doc[SHOW_SUCCESS_MESSAGES].as<bool>();
    } else {
        Serial.println(F("The key showSuccessMessages is not found, will use default value: true"));
        showSuccessMessages = true; // Valeur par défaut
    }

    if (!doc[SHOW_INFO_MESSAGES].isNull()){
        showInfoMessages = doc[SHOW_INFO_MESSAGES].as<bool>();
    } else {
        Serial.println(F("The key showInfoMessages is not found, will use default value: true"));
        showInfoMessages = true; // Valeur par défaut
    }

    if (doc[FILTERED_MESSAGES].is<JsonArray>()){
        std::vector<String> patterns;
        for (JsonVariant v : doc[FILTERED_MESSAGES].as<JsonArray>()) {
            if (v.is<const char*>() || v.is<String>()) {
                patterns.push_back(v.as<String>());
            }
        }
        PLC_Tools::setFilteredMessagePatterns(patterns);
    } else {
        Serial.println(F("The key filteredMessages is not found, no message will be filtered"));
        PLC_Tools::setFilteredMessagePatterns({});
    }

    if (!doc[DELETE_DIAGNOSTIC_FILE_AT_STARTUP].isNull()){
        deleteDiagnosticFileAtStartup = doc[DELETE_DIAGNOSTIC_FILE_AT_STARTUP].as<bool>();
    } else {
        Serial.println(F("The key deleteDiagnosticFileAtStartup is not found, will use default value: false"));
        deleteDiagnosticFileAtStartup = false; // Valeur par défaut
    }

    if (!doc[MACHINE_STATE_LOGGING].isNull()){
        machineStateLogging = doc[MACHINE_STATE_LOGGING].as<bool>();
    } else {
         Serial.println(F("The key log historry and transition is not found, will use default value: false"));
         machineStateLogging = false;   
    }

    if (getDeleteDiagnosticFileAtStartup()) {
        PLC_Persistence& persistence = PLC_Persistence::getInstance();
        if (persistence.fileExists(DIAGNOSTIC_FILE)) {
            if (persistence.deleteFile(DIAGNOSTIC_FILE)) {
                Serial.println(F("Diagnostic file deleted at startup"));
            } else {
                Serial.println(F("Warning: Failed to delete diagnostic file at startup"));
                // Continuer malgré l'échec de la suppression
            }
        }
    }

    if (!doc[TYPE].isNull()){
        if (doc[TYPE].is<const char*>() || doc[TYPE].is<String>()) {
            if (!strcmp(doc[TYPE], PROJECT_TYPE_V0)){
                prepareMessage(ERROR, "The project type is not BorneUniverselle");
                status = false;
            } else {
                projectVersion = 0;
            }
        }

        if (doc[TYPE].is<int>() || doc[TYPE].is<float>()) {
               projectVersion = doc[TYPE].as<float>();
        }   
    } else {
        prepareMessage(ERROR, "The key type is not found");
        status = false;
    }

    if (status && !check) {
        PLC_Persistence& persistence = PLC_Persistence::getInstance();
        
        if (persistence.fileExists(CONFIG_PATH_FILE)) {
            if (persistence.createBackup(CONFIG_PATH_FILE)) {
                Serial.println(F("Created backup of config.json before applying changes"));
            } else {
                Serial.println(F("Warning: Failed to create backup of config.json"));
                // Continuer malgré l'échec de la sauvegarde
            }
        }
    }

    if (status){
        Serial.printf("Project version: %2.2f\r\n", projectVersion);
        status = parseWifis(doc, check);
        if (!status && !check){
            wifiParsedOk = false;
        }
    }

    if (status){
        //Serial.println ("Wifi's parsed");
        status = parseHardwares(doc, check, projectVersion);
    }

    return status;
}

bool BorneUniverselle::getIsWifiParsedOk(){
    return wifiParsedOk;
}

bool BorneUniverselle:: parseWifis(JsonDocument& doc, bool check){
    BorneUniverselle* instance = getInstance();
    if (instance != nullptr) {
        return instance->parseWifisImpl(doc, check);
    } else {
        return false;
    }
}

bool BorneUniverselle::parseWifisImpl(JsonDocument& doc, bool check) {
    for (JsonObject item : doc[CONFIG].as<JsonArray>()) {
        if (!item[WIFI].isNull()) {
            JsonArray wifiArray = item[WIFI].as<JsonArray>();
            
            for (const JsonObject& wifiItem : wifiArray) {
                // === PARSING BASIQUE ===
                const char* name = nullptr;  // ✅ Changez char* en const char*
                char message[800] = {0};
                
                if (!wifiItem[NAME].isNull()) {
                    name = wifiItem[NAME];  // ✅ Maintenant ça fonctionne !
                    Serial.printf("Configuration WiFi: %s\n", name);
                } else {
                    strcpy(message, "parseWifis:: Configuration sans nom");
                    prepareMessage(ERROR, message);
                    return false;
                }
                
                // === DÉLÉGATION DU PARSING COMPLEXE À WIFIMANAGEMENT ===
                if (!check) {
                    WifiItem parsedConfig;
                    if (!parseWifiItemFromJson(wifiItem, parsedConfig, name)) {
                        return false;
                    }
                    
                    if (parsedConfig.wifiMode == WIFI_MODE_STATION) {
                        if (parsedConfig.dhcp) {
                            WifiManagment::getInstance().addItem(parsedConfig);
                        } else {
                            WifiManagment::getInstance().addItem(parsedConfig);
                        }
                    } else if (parsedConfig.wifiMode == WIFI_MODE_ACCESS_POINT) {
                        WifiManagment::getInstance().addItem(parsedConfig);
}
                }
            } // for
        } else if (!item[PARAMETERS].isNull()) {
            // Votre code parameters existant...
            JsonObject params = item[PARAMETERS];    
            if (!params[OTA_URL].isNull()) {
                strcpy(ota_url, params[OTA_URL]);
                Serial.printf("ota_url: %s\r\n", ota_url);
            }
        }
    }
    return true;
}

bool BorneUniverselle::parseWifiItemFromJson(const JsonObject& wifiItem, WifiItem& config, const char* name) {
    char message[800];
    
    // === PARSING DU MODE ===
    String mode = WIFI_MODE_STATION; // Mode par défaut
    if (!wifiItem[WIFI_MODE].isNull()) {
        mode = wifiItem[WIFI_MODE].as<String>();
    }
    config.wifiMode = mode;
    config.connectionName[0] = '\0';
    strcpy(config.connectionName, name);
    
    Serial.printf("Mode WiFi: %s\n", mode.c_str());
    
    // === PARSING SELON LE MODE ===
    if (mode == WIFI_MODE_STATION) {
        return parseStationConfigFromJson(wifiItem, config, name);
    } else if (mode == WIFI_MODE_ACCESS_POINT) {
        return parseAccessPointConfigFromJson(wifiItem, config, name);
    } else {
        sprintf(message, "parseWifis:: Mode WiFi invalide '%s' pour %s", mode.c_str(), name);
        prepareMessage(ERROR, message);
        return false;
    }
}

bool BorneUniverselle::parseStationConfigFromJson(const JsonObject& wifiItem, WifiItem& config, const char* name) {
    char message[800];
    
    // SSID obligatoire
    if (!wifiItem[SSID_].isNull()) {
        const char* ssid = wifiItem[SSID_];
        strcpy(config.SSID, ssid);
    } else {
        sprintf(message, "parseWifis:: Station %s sans SSID", name);
        prepareMessage(ERROR, message);
        return false;
    }
    
    // Password obligatoire
    if (!wifiItem[PWD_].isNull()) {
        const char* pwd = wifiItem[PWD_];
        strcpy(config.PWD, pwd);
    } else {
        sprintf(message, "parseWifis:: Station %s sans mot de passe", name);
        prepareMessage(ERROR, message);
        return false;
    }
    
    // DHCP (défaut: true)
    config.dhcp = true;
    if (!wifiItem[DHCP].isNull()) {
        config.dhcp = wifiItem[DHCP].as<bool>();
    }
    
    // Si IP statique, parser les paramètres réseau
    if (!config.dhcp) {
        return parseStaticIPFromJson(wifiItem, config, name);
    }
    
    return true;
}

bool BorneUniverselle::parseStaticIPFromJson(const JsonObject& wifiItem, WifiItem& config, const char* name) {
    char message[800];
    
    // 1. VALIDATION JSON STRUCTURE AVANT PARSING
    if (wifiItem.isNull()) {
        sprintf(message, "parseStaticIPFromJson:: Configuration WiFi nulle pour %s", name);
        prepareMessage(ERROR, message);
        return false;
    }
    
    // 2. EXPLICITEMENT DÉSACTIVER DHCP AVANT CONFIGURATION
    config.dhcp = false;
    Serial.printf("parseStaticIPFromJson:: DHCP explicitement désactivé pour %s\n", name);
    
    // 3. VALIDATION ET PARSING IP AVEC fromString() (validation ESP32 intégrée)
    if (!wifiItem[IP].isNull()) {
        const char* ipChar = wifiItem[IP];
        if (!config.ip.fromString(String(ipChar))) {
            sprintf(message, "parseStaticIPFromJson:: IP invalide '%s' pour %s", ipChar, name);
            prepareMessage(ERROR, message);
            return false;
        }
        
        // Validation supplémentaire : IP ne doit pas être 0.0.0.0
        if (config.ip == IPAddress(0, 0, 0, 0)) {
            sprintf(message, "parseStaticIPFromJson:: IP 0.0.0.0 non autorisée pour %s", name);
            prepareMessage(ERROR, message);
            return false;
        }
    } else {
        sprintf(message, "parseStaticIPFromJson:: IP manquante pour configuration statique de %s", name);
        prepareMessage(ERROR, message);
        return false;
    }

    // 4. VALIDATION GATEWAY (OBLIGATOIRE POUR IP STATIQUE)
    if (!wifiItem[GATEWAY_].isNull()) {
        const char* gatewayChar = wifiItem[GATEWAY_];
        if (!config.gateway.fromString(String(gatewayChar))) {
            sprintf(message, "parseStaticIPFromJson:: Gateway invalide '%s' pour %s", gatewayChar, name);
            prepareMessage(ERROR, message);
            return false;
        }
    } else {
        sprintf(message, "parseStaticIPFromJson:: Gateway manquante pour %s", name);
        prepareMessage(ERROR, message);
        return false;
    }

    // 5. VALIDATION SUBNET MASK
    if (!wifiItem[MASK_].isNull()) {
        const char* maskChar = wifiItem[MASK_];
        if (!config.mask.fromString(String(maskChar))) {
            sprintf(message, "parseStaticIPFromJson:: Masque invalide '%s' pour %s", maskChar, name);
            prepareMessage(ERROR, message);
            return false;
        }
    } else {
        sprintf(message, "parseStaticIPFromJson:: Masque manquant pour %s", name);
        prepareMessage(ERROR, message);
        return false;
    }

    // 6. VALIDATION DNS (AVEC FALLBACK VERS 8.8.8.8)
    if (!wifiItem[DNS_].isNull()) {
        const char* dnsChar = wifiItem[DNS_];
        if (!config.dns.fromString(String(dnsChar))) {
            sprintf(message, "parseStaticIPFromJson:: DNS invalide '%s' pour %s, utilisation 8.8.8.8", dnsChar, name);
            prepareMessage(WARNING, message);
            config.dns = IPAddress(8, 8, 8, 8); // Fallback vers Google DNS
        }
    } else {
        config.dns = IPAddress(8, 8, 8, 8); // DNS par défaut
        Serial.printf("parseStaticIPFromJson:: DNS par défaut (8.8.8.8) pour %s\n", name);
    }

    // 7. VALIDATION COHÉRENCE RÉSEAU (IP et Gateway dans le même sous-réseau)
    IPAddress networkIP = IPAddress(config.ip[0] & config.mask[0], 
                                   config.ip[1] & config.mask[1],
                                   config.ip[2] & config.mask[2], 
                                   config.ip[3] & config.mask[3]);
    
    IPAddress gatewayNetwork = IPAddress(config.gateway[0] & config.mask[0],
                                        config.gateway[1] & config.mask[1], 
                                        config.gateway[2] & config.mask[2],
                                        config.gateway[3] & config.mask[3]);
    
    if (networkIP != gatewayNetwork) {
        sprintf(message, "parseStaticIPFromJson:: IP %s et Gateway %s ne sont pas dans le même sous-réseau pour %s", 
                config.ip.toString().c_str(), config.gateway.toString().c_str(), name);
        prepareMessage(ERROR, message);
        return false;
    }

    // 8. LOG DE VALIDATION RÉUSSIE
    Serial.printf("parseStaticIPFromJson:: Configuration statique validée pour %s:\n", name);
    Serial.printf("  IP: %s\n", config.ip.toString().c_str());
    Serial.printf("  Gateway: %s\n", config.gateway.toString().c_str()); 
    Serial.printf("  Mask: %s\n", config.mask.toString().c_str());
    Serial.printf("  DNS: %s\n", config.dns.toString().c_str());
    
    return true;
}

bool BorneUniverselle::connectWifiImpl(WifiItem currentWifi) {
    return WifiManagment::getInstance().connectWifi(currentWifi, BorneUniverselle::getName());
}

bool BorneUniverselle::parseAccessPointConfigFromJson(const JsonObject& wifiItem, WifiItem& config, const char* name) {
    // Nom AP (défaut: ESP32-Formaca)
    config.apName = "ESP32-Formaca";
    if (!wifiItem[AP_NAME].isNull()) {
        config.apName = wifiItem[AP_NAME].as<String>();
    }
    
    // Password AP (défaut: FormacaPass123)
    config.apPassword = "FormacaPass123";
    if (!wifiItem[AP_PASSWORD].isNull()) {
        config.apPassword = wifiItem[AP_PASSWORD].as<String>();
    }
    
    // IP AP (défaut: 192.168.4.1)
    config.apIP = IPAddress(192, 168, 4, 1);
    if (!wifiItem[AP_IP].isNull()) {
        String apIpStr = wifiItem[AP_IP].as<String>();
        if (!config.apIP.fromString(apIpStr)) {
            char message[800];
            sprintf(message, "parseWifis:: IP Access Point invalide '%s' pour %s", apIpStr.c_str(), name);
            prepareMessage(ERROR, message);
            return false;
        }
    }
    
    // Canal (défaut: 1)
    config.apChannel = 1;
    if (!wifiItem[AP_CHANNEL].isNull()) {
        config.apChannel = wifiItem[AP_CHANNEL].as<uint8_t>();
        if (config.apChannel < 1 || config.apChannel > 13) {
            char message[800];
            sprintf(message, "parseWifis:: Canal WiFi invalide %d pour %s (doit être 1-13)", config.apChannel, name);
            prepareMessage(ERROR, message);
            return false;
        }
    }
    
    return true;
}

unsigned char BorneUniverselle::addWifiItem2(const char *ssid, const char *pwd, const char *connexionName, bool dhcp){
    return WifiManagment::getInstance().addItem(ssid, pwd, connexionName);
}

unsigned char BorneUniverselle::addWifiItem(const char *ssid, const char *pwd, const char *connexionName, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress mask, bool dhcp){
    return WifiManagment::getInstance().addItem(ssid, pwd, connexionName, ip, dns, gateway, mask, dhcp);
}

void BorneUniverselle::eraseWifis(){
    WifiManagment::getInstance().clearNbWifiItems();
}

bool BorneUniverselle::parseHardwares(JsonDocument& doc, bool check, float projectVersion){
    bool needToSaveConfig = false;
    char buff[1000];
    //Serial.println("parseHardwares");

    // Est-ce que l'on a un hardware ?
    if (doc[CHILDREN].isNull()){
        setPlcBroken(NO_CHILDREN_KEY_CONFIG);
        return false;
    }

    //Serial.println("Children parsing");

    for (JsonObject children : doc[CHILDREN].as<JsonArray>()) {
        char sectionName[128], hardware[80], type[80], text[200];
        if (children[NAME].isNull()){
            sprintf(buff, "parseHardwares:: Section ? doesn't contains key %s", NAME);
            prepareMessage(ERROR, buff);
            return false;
        }

        strcpy(sectionName, children[NAME]);
        Serial.printf("Section name: %s\r\n", sectionName);
           
        if (children[HARDWARE].isNull()){
            sprintf(text, "parseHardwares:: Section %s doesnt contains key hardware", name);
            setPlcBroken(text);
            return false;
        }
        strcpy(hardware, children[HARDWARE]);
        Serial.printf("hardware: %s\r\n", hardware);
        char fileName[250];

        if (children[TYPE].isNull()){
            sprintf(buff, "parseHardwares:: Section %s doesnt contains key type", name);
            Serial.println(buff);
            setPlcBroken(buff);
            return false;
        }

        strcpy(type, children[TYPE]);  // type du fichier de conf
        Serial.printf("type: %s\r\n", type);

        // no descriptor file for virtual nodes!
        if (!strcmp(hardware, VIRTUAL)){
            JsonArray childrenArray = children[CHILDREN].as<JsonArray>();
            for (JsonObject c : childrenArray) {
                char nodeName[128];
                if (c[NAME].isNull()){
                    sprintf(buff, "ERREUR CONFIG: Section VIRTUAL '%s' - Un nœud n'a pas de clé 'name'", sectionName);
                    setPlcBroken(buff);
                    Serial.println(buff);
                    Serial.println("=== Contenu du nœud problématique ===");
                    PLC_Tools::printJsonObject(c);
                    Serial.println("=====================================");
                    return false;  
                }
                strcpy(nodeName, c[NAME]);
               
                //Serial.printf("node name: %s\r\n", nodeName);
                uint32_t hash, hashLocal;

                if (c[HASH].isNull()){
                    Serial.printf("Hash key note found for node name: %s\r\n", nodeName);
                    return false;
                } else {
                    hash = c[HASH].as<unsigned int>();
                    hashLocal = hash;

                    /*
                    if (hash == 0) {
                        Serial.println();
                        Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
                        Serial.printf("🚨 ATTENTION: HASH = 0 dans config.json ! 🚨\n");
                        Serial.printf("🚨 Node: %s dans section: %s                🚨\n", nodeName, sectionName);
                        Serial.printf("🚨 Ceci peut causer des dysfonctionnements ! 🚨\n");
                        Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
                        Serial.println();
                        
                        // Message pour l'interface web
                        sprintf(buff, "ATTENTION: Hash=0 détecté pour le node %s/%s dans config.json", 
                            sectionName, nodeName);
                        setPlcBroken(buff);
                    }
                    */
                } // fin

                if (c[ID].isNull()){
                    sprintf(buff, "parseHardwares:: node %s has no key %s", nodeName, ID);
                    prepareMessage(ERROR, buff);
                return false;  
                }
                uint8_t addr = c[ID].as<int>();

                bool inverted = false;
                if (!c[INPUT_INVERTED].isNull()){
                    inverted = c[INPUT_INVERTED].as<bool>();
                }

                if (!createVirtualNode(nodeName, sectionName, addr, type, inverted, &hashLocal, check)){
                    sprintf(buff, "Error on creating virtual node");
                    prepareMessage(ERROR, buff);
                    return false;
                }

                if (hash != hashLocal){
                    if (c[HASH].isNull()){
                        JsonObject hashObject;
                        hashObject[HASH] = hash;
                        c.set(hashObject);
                    } 

                    c[HASH] = hashLocal;
                    hash = hashLocal;
                    //Serial.println("Hash updated");
                    needToSaveConfig = true;
                } else {
                    //Serial.println("Hash is corresponding VIRTUAL NODE");
                }
            } // boucle sur la section de nodes virtuels
     
            continue;  // on a créer les node de cette section, on passe a la section suivante
        }  // virtual nodes

        // check if corresponding json file exist
        sprintf(fileName, "%s/%s.json", HARDWARE_FOLDER, hardware);
        SafeJsonDocument nodesDoc; // The hardware document
        PLC_Persistence& persistence = PLC_Persistence::getInstance();
        if (!persistence.readJsonFromFile(fileName, nodesDoc)) {
            setPlcBroken(persistence.getLastError());
            return false;
        }
        
        if (strstr(fileName, "A8S") && !isKinconyA8S){
            Serial.println(F("Card is an Kincony A8S, pin 2 must be set after go ip"));
            isKinconyA8S = true;
        } 
        

        if (nodesDoc[HARDWARE].isNull()){
            sprintf(buff,"parseHardwares:: Key: %s not found", HARDWARE);
            prepareMessage(ERROR, buff);
            return false;
        }

        if (strcmp(nodesDoc[HARDWARE], hardware)){
            sprintf(buff, "parseHardwares:: Hardware from config file doesn't match with hardware file: %s", hardware);
            prepareMessage(ERROR, buff);
            return false;
        }

        // Serial.println("Hardware match")

        if (i2cInit(nodesDoc)){
            Serial.println(F("Bus I2C initialized"));
        } 


        if (modbusInit(nodesDoc)){
            Serial.println(F("Modbus initialized"));
        }
    
        //Serial.println("Lectures des sections du hardware");

        if (nodesDoc[type].isNull()){
            sprintf(buff, "Hardware file doesnt contains key %s", type);
            prepareMessage(ERROR, buff);
            return false;
        }

        if (children[CHILDREN].isNull()){
            sprintf(buff, "parseHardwares:: section %s doesnt contains key %s from config file", name, CHILDREN);
            setPlcBroken(buff);
            return false; 
        }

        JsonArray childrenArray = children[CHILDREN].as<JsonArray>();
        for (JsonObject c : childrenArray) {
            char nodeName[128];
            if (c[NAME].isNull()){
                sprintf(buff, "ERREUR CONFIG: Section '%s' (hardware: %s) - Un nœud n'a pas de clé 'name'", sectionName, hardware);
                prepareMessage(ERROR, buff);
                Serial.println(buff);
                Serial.println("=== Contenu du nœud problématique ===");
                PLC_Tools::printJsonObject(c);
                Serial.println("=====================================");
                return false;  
            }
            strcpy(nodeName, c[NAME]);
        
            uint32_t hash = 0, hashLocal;
            //Serial.printf("node name: %s\r\n", nodeName);

            //if (!c.containsKey(HASH)){
            if (c[HASH].isNull()){
               //Serial.printf("Hash key not found for node name: %s\r\n", nodeName);
            } else {
                hash = c[HASH].as<unsigned int>();
                hashLocal = hash;
                //Serial.printf("Node name: %s, hash found: %lu\r\n", nodeName, hash);
                /*
                if (hash == 0) {
                    Serial.println();
                    Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
                    Serial.printf("🚨 ATTENTION: HASH = 0 dans config.json ! 🚨\n");
                    Serial.printf("🚨 Node: %s dans section: %s                🚨\n", nodeName, sectionName);
                    Serial.printf("🚨 Ceci peut causer des dysfonctionnements ! 🚨\n");
                    Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
                    Serial.println();
                    
                    // Log dans le diagnostic aussi
                    char warningMsg[512];
                    snprintf(warningMsg, sizeof(warningMsg), "ATTENTION: Hash=0 pour node '%s' dans section '%s' du fichier config.json", nodeName, sectionName);
                    PLC_Tools::logDiagnostic(warningMsg);
                    
                    // Message pour l'interface web
                    sprintf(buff, "ATTENTION: Hash=0 détecté pour le node %s/%s dans config.json", sectionName, nodeName);
                    setPlcBroken(buff);
                }
                */
            }

            //if (!c.containsKey(ID)){
            if (c[ID].isNull()){
                sprintf(buff, "parseHardwares:: node %s has no key %s", nodeName, ID);
                prepareMessage(ERROR, buff);
                return false;  
            }

            uint8_t addr = c[ID].as<int>();

            bool found = false;
            for (JsonObject hardSection : nodesDoc[type].as<JsonArray>()) {
                //if (!hardSection.containsKey(ID)){
                if (hardSection[ID].isNull()){
                    sprintf(buff, "parseHardwares:: Hardware file key %s has no key id", fileName);
                    prepareMessage(ERROR, buff);
                    return false; 
                }

                //Serial.printf("type confirm: %s\r\n", type);

                uint8_t id = hardSection[ID].as<int>();
                //Serial.printf("Hardware node id: %d\r\n", id);

                if (addr == id){ 
                    //Serial.println("Find correspondig id <-> addr");
                    // on doit créer le node avec le bon type. On cherche le node creator !

                    uint16_t webRefreshInterval = 0;
                    //if (c.containsKey(WEB_UPDATE_INTERVAL)){
                    if (!c[WEB_UPDATE_INTERVAL].isNull()){
                        webRefreshInterval = c[WEB_UPDATE_INTERVAL].as<uint16_t>();
                    } 

                     uint16_t refreshInterval = 0;
                    //if (c.containsKey(REFRESH_INTERVAL)){
                    if (!c[REFRESH_INTERVAL].isNull()){
                        refreshInterval = c[REFRESH_INTERVAL].as<uint16_t>();
                        //Serial.printf("Refreh interval: %u\r\n", refreshInterval);
                    } 
                    
                    if (!strcmp(type, RX_BOOL) || !strcmp(type, PF8574_RX_BOOL)){
                        bool inverted = false;
                        if (!c[INPUT_INVERTED].isNull()){
                            inverted = c[INPUT_INVERTED].as<bool>();
                        } else {
                            inverted = false;
                        }

                        if (!createRxBoolNode(nodeName, sectionName, addr, &hashLocal, nodesDoc, hardSection, type, inverted, refreshInterval, webRefreshInterval, check)){
                            sprintf(buff, "parseHardwares:: Error on creating RxBoolNode with file %s\r\n", fileName);
                            prepareMessage(ERROR, buff);
                            return false;
                        }
                        found = true;
                    }  else  if (!strcmp(type, TX_BOOL) || !strcmp(type, PF8574_TX_BOOL) || !strcmp(type, PCA9554_TX_BOOL)){
                            bool inverted = false;
                            if (!c[INPUT_INVERTED].isNull()){
                                inverted = c[INPUT_INVERTED].as<bool>();
                            }

                            if (!createTxBoolNode(nodeName, sectionName, addr, &hashLocal, nodesDoc, hardSection, type, inverted, webRefreshInterval, check)){
                                sprintf(buff, "parseHardwares:: Error on creating TxBoolNode with file %s\r\n", fileName);
                                prepareMessage(ERROR, buff);
                                return false;
                            }
                            found = true;
                    } else if (!strcmp(type, MODBUS_READ_COIL) || !strcmp(type, MODBUS_WRITE_COIL) || !strcmp(type, MODBUS_READ_HOLDING_REGISTER) ||
                               !strcmp(type, MODBUS_WRITE_HOLDING_REGISTER) || !strcmp(type, MODBUS_READ_DOUBLE_HOLDING_REGISTER) || !strcmp(type, MODBUS_WRITE_DOUBLE_HOLDING_REGISTER) ||
                               !strcmp(type, MODBUS_READ_INPUT_REGISTER) || !strcmp(type, MODBUS_WRITE_MULTIPLE_COILS) || !strcmp(type, MODBUS_READ_MULTIPLE_INPUTS_STATUS)){
                                //Serial.println("Modbus");
                                //if (children.containsKey(ADDRESS)){
                                if (!children[ADDRESS].isNull()){
                                    uint16_t slaveAddress = children[ADDRESS].as<uint16_t>();
                            
                                    //Serial.printf("Modbus slave address: %u\r\n", slaveAddress);
                                    if (!createModbusNode(nodeName, sectionName, addr, &hashLocal, slaveAddress, nodesDoc, hardSection, c, type, refreshInterval, webRefreshInterval, check)){
                                        sprintf(buff, "parseHardwares:: Error on creating Modbus node with file %s\r\n", fileName);
                                        prepareMessage(ERROR, buff);
                                    }
                                    found = true; 
                                } else {
                                    sprintf(buff, "parseHardwares:: Section doesn't contains key %s\r\n", ADDRESS);
                                    prepareMessage(ERROR, buff);
                                    return false;
                                }
                    } else {
                            sprintf(buff, "parseHardwares:: Unable to match type %s\r\n", type);
                            prepareMessage(ERROR, buff);
                    }  
                    break;
                } 
            } // loop on hardware

            //Serial.println("fin de la sous section du fichier de config");
            if (found){
                //Serial.printf("Found !! hash lu: %u, hash produced: %u\r\n", hash, hashLocal);
                if (hash != hashLocal){
                    Serial.println(F("Must save the file with new hash"));
                    //if (!c.containsKey(HASH)){
                    if (c[HASH].isNull()){
                        // Add object
                        JsonObject hashObject;
                        c.set(hashObject);
                    } 

                    c[HASH] = hashLocal;
                    hash = hashLocal;
                    Serial.println(F("Hash updated"));
                    needToSaveConfig = true;
                }
            } else {
                    sprintf(buff, "parseHardwares:: Unable to find coresponding id to %d\r\n", addr);
                    prepareMessage(ERROR, buff);
                    return false; 
            }   
                     
        } //Serial.println("fin de la section du fichier de config"); 
    } // loop on children from config filecreateRxBoolNode

    if (needToSaveConfig){
        Serial.println(F("Need to save config file"));
        saveParameters(doc);
        ESP.restart();
    }
    return true;
 // setHardware
}

bool BorneUniverselle::createModbusNode(char *nodeName, char *sectionName, uint16_t id, uint32_t *hash, uint16_t slaveAddress, JsonDocument& contextDoc,
                                     JsonObject hardSection, JsonObject configNode, char *type, uint16_t refreshInterval,  uint16_t webRefreshInterval, bool check){
    
    if (!isModbusInitialised){
        prepareMessage(ERROR, MODBUS_NOT_INITIALISED);
        return false;
    }
    
    Node *node;
    uint16_t offset;
    char message[256];
    int8_t registersOffset = 0;

    //if (contextDoc.containsKey(REGISTERS_OFFSET)){
    if (!contextDoc[REGISTERS_OFFSET].isNull()){
        registersOffset = contextDoc[REGISTERS_OFFSET].as<int8_t>();
        // Serial.printf("Registers offset: %d\r\n", registersOffset);
    }

    //if (hardSection.containsKey(OFFSET)){
    if (!hardSection[OFFSET].isNull()){
        offset = hardSection[OFFSET].as<uint16_t>();
        //Serial.printf("Offset: %u\r\n", offset);
    } else {
        sprintf(message, "createModbusNode::modbus hardware file doesn't contains key %s\r\n", OFFSET);
        prepareMessage(ERROR, message);
        return false; 
    }

    offset += registersOffset;

    if (!strcmp(type, MODBUS_READ_COIL)){
        bool inputsInverted = false;
        //if (contextDoc.containsKey(INPUT_INVERTED)){
        if (!contextDoc[INPUT_INVERTED].isNull()){
            inputsInverted = contextDoc[INPUT_INVERTED].as<bool>();
        }

        if (PLC_Tools::isPSRAM_available()){
            void* a = ps_malloc(sizeof(ModbusReadCoilNode));
            if (a != nullptr) {
                // Utiliser le placement new avec la mémoire PSRAM allouée
                node = new (a) ModbusReadCoilNode(nodeName, sectionName, id, *hash, slaveAddress, offset, inputsInverted, refreshInterval, webRefreshInterval);
            } else {
                // Fallback sur l'allocation mémoire standard avec gestion d'erreur
                node = new (std::nothrow) ModbusReadCoilNode(nodeName, sectionName, id, *hash, slaveAddress, offset, inputsInverted, refreshInterval, webRefreshInterval);
                if (node == nullptr) {
                    // Gestion de l'échec d'allocation
                    setPlcBroken("Erreur d'allocation mémoire pour ModbusReadCoilNode");
                    return false;
                }
            }
        } else {
            // Allocation mémoire standard avec gestion d'erreur
            node = new (std::nothrow) ModbusReadCoilNode(nodeName, sectionName, id, *hash, slaveAddress, offset, inputsInverted, refreshInterval, webRefreshInterval);
            if (node == nullptr) {
                // Gestion de l'échec d'allocation
                setPlcBroken("Erreur d'allocation mémoire pour ModbusReadCoilNode");
                return false;
            }
        }
        Serial.printf("Node %s created with succes with type ModbusReadCoilNode, hash: %lu, address: %d, offset: %d, refresh interval: %u, web refresh interval %u\r\n", node->getName(), (unsigned long)*hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
    } else if (!strcmp(type, MODBUS_WRITE_COIL)){
        if (PLC_Tools::isPSRAM_available()) {
            void* a = ps_malloc(sizeof(ModbusWriteCoilNode));
            if (a != nullptr) {
                node = new (a) ModbusWriteCoilNode(nodeName, sectionName, id, *hash, slaveAddress, offset, webRefreshInterval);
            } else {
                // Gestion du cas où ps_malloc échoue
                node = new (std::nothrow) ModbusWriteCoilNode(nodeName, sectionName, id, *hash, slaveAddress, offset, webRefreshInterval);
                if (node == nullptr) {
                    // Gestion de l'échec d'allocation
                   setPlcBroken("Erreur d'allocation mémoire pour ModbusWriteCoilNode");
                   return false;
                }
            }
        } else {
            node = new (std::nothrow) ModbusWriteCoilNode(nodeName, sectionName, id, *hash, slaveAddress, offset, webRefreshInterval);
            if (node == nullptr) {
                setPlcBroken("Erreur d'allocation mémoire pour ModbusWriteCoilNode");
                return false;
            }
        }
        Serial.printf("Node %s created with succes with type ModbusReadCoilNode, hash: %lu, address: %d, offset: %d\r\n", node->getName(), (unsigned long)*hash, slaveAddress, offset);
    } else if (!strcmp(type, MODBUS_WRITE_MULTIPLE_COILS)){
        uint8_t nbValues = 1;
        //if (hardSection.containsKey(NB_VALUES)){
        if (!hardSection[NB_VALUES].isNull()){
            nbValues = hardSection[NB_VALUES].as<uint8_t>();
            Serial.printf("createModbusNode:: nbValues: %d\r\n", nbValues);
        } else {
            char texte[250];
            sprintf(texte, "Unable to find key: %s on section %s\r\n", NB_VALUES, MODBUS_WRITE_MULTIPLE_COILS);
            setPlcBroken(texte);
        }

        if (PLC_Tools::isPSRAM_available()) {
            void* a = ps_malloc(sizeof(ModbusWriteMultipleCoilslNode));
            if (a != nullptr) {
                // Utiliser le placement new avec la mémoire PSRAM allouée
                node = new (a) ModbusWriteMultipleCoilslNode(nodeName, sectionName, id, *hash, slaveAddress, offset, nbValues, webRefreshInterval);
            } else {
                // Fallback sur l'allocation mémoire standard avec gestion d'erreur
                node = new (std::nothrow) ModbusWriteMultipleCoilslNode(nodeName, sectionName, id, *hash, slaveAddress, offset, nbValues, webRefreshInterval);
                if (node == nullptr) {
                    // Gestion de l'échec d'allocation
                    setPlcBroken("Erreur d'allocation mémoire pour ModbusWriteMultipleCoilslNode");
                    return false;
                }
            }
        } else {
            // Allocation mémoire standard avec gestion d'erreur
            node = new (std::nothrow) ModbusWriteMultipleCoilslNode(nodeName, sectionName, id, *hash, slaveAddress, offset, nbValues, webRefreshInterval);
            if (node == nullptr) {
                // Gestion de l'échec d'allocation
                setPlcBroken("Erreur d'allocation mémoire pour ModbusWriteMultipleCoilslNode");
                return false;
            }
        }
        Serial.printf("Node %s created with succes with type ModbusWriteMultipleCoilslNode, hash: %lu, address: %d, offset: %d\r\n", node->getName(), (unsigned long)*hash, slaveAddress, offset); 
    } else if (!strcmp(type, MODBUS_READ_HOLDING_REGISTER)){
        if (PLC_Tools::isPSRAM_available()) {
            void* a = ps_malloc(sizeof(ModbusReadHoldingRegister));
            if (a != nullptr) {
                // Utiliser le placement new avec la mémoire PSRAM allouée
                node = new (a) ModbusReadHoldingRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
            } else {
                // Fallback sur l'allocation mémoire standard avec gestion d'erreur
                node = new (std::nothrow) ModbusReadHoldingRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
                if (node == nullptr) {
                    // Gestion de l'échec d'allocation
                    setPlcBroken("Erreur d'allocation mémoire pour ModbusReadHoldingRegister");
                    return false;
                }
            }
        } else {
            // Allocation mémoire standard avec gestion d'erreur
            node = new (std::nothrow) ModbusReadHoldingRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
            if (node == nullptr) {
                // Gestion de l'échec d'allocation
                setPlcBroken("Erreur d'allocation mémoire pour ModbusReadHoldingRegister");
                return false;
            }
        }
        Serial.printf("Node %s created with succes with type ModbusReadHoldingRegister, hash: %lu, address: %d, offset: %d, refresh interval: %u, web refresh interval %u\r\n", node->getName(), (unsigned long)*hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
    } else if (!strcmp(type, MODBUS_WRITE_HOLDING_REGISTER)){
        if (PLC_Tools::isPSRAM_available()) {
            void* a = ps_malloc(sizeof(ModbusWriteHoldingRegister));
            if (a != nullptr) {
                // Utiliser le placement new avec la mémoire PSRAM allouée
                node = new (a) ModbusWriteHoldingRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, webRefreshInterval);
            } else {
                // Fallback sur l'allocation mémoire standard avec gestion d'erreur
                node = new (std::nothrow) ModbusWriteHoldingRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, webRefreshInterval);
                if (node == nullptr) {
                    // Gestion de l'échec d'allocation
                    setPlcBroken("Erreur d'allocation mémoire pour ModbusWriteHoldingRegister");
                    return false;
                }
            }
        } else {
            // Allocation mémoire standard avec gestion d'erreur
            node = new (std::nothrow) ModbusWriteHoldingRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, webRefreshInterval);
            if (node == nullptr) {
                // Gestion de l'échec d'allocation
                setPlcBroken("Erreur d'allocation mémoire pour ModbusWriteHoldingRegister");
                return false;
            }
        }
        Serial.printf("Node %s created with succes with type ModbusWriteHoldingRegister, hash: %lu, address: %u, offset: %d\r\n", node->getName(), (unsigned long)*hash, slaveAddress, offset);
    } else if (!strcmp(type, MODBUS_READ_DOUBLE_HOLDING_REGISTER)){
        if (PLC_Tools::isPSRAM_available()) {
            void* a = ps_malloc(sizeof(ModbusReadDoubleHoldingRegisters));
            if (a != nullptr) {
                // Utiliser le placement new avec la mémoire PSRAM allouée
                node = new (a) ModbusReadDoubleHoldingRegisters(nodeName, sectionName, id, *hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
            } else {
                // Fallback sur l'allocation mémoire standard avec gestion d'erreur
                node = new (std::nothrow) ModbusReadDoubleHoldingRegisters(nodeName, sectionName, id, *hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
                if (node == nullptr) {
                    // Gestion de l'échec d'allocation
                    setPlcBroken("Erreur d'allocation mémoire pour ModbusReadDoubleHoldingRegisters");
                    return false;
                }
            }
        } else {
            // Allocation mémoire standard avec gestion d'erreur
            node = new (std::nothrow) ModbusReadDoubleHoldingRegisters(nodeName, sectionName, id, *hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
            if (node == nullptr) {
                // Gestion de l'échec d'allocation
                setPlcBroken("Erreur d'allocation mémoire pour ModbusReadDoubleHoldingRegisters");
                return false;
            }
        }
        Serial.printf("Node %s created with succes with type ModbusReadDoubleHoldingRegisters, hash: %lu, address: %u, offset: %d, refresh interval: %u, web refresh interval %u\r\n", node->getName(), (unsigned long)*hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
    } else if (!strcmp(type, MODBUS_WRITE_DOUBLE_HOLDING_REGISTER)){
        if (PLC_Tools::isPSRAM_available()) {
            void* a = ps_malloc(sizeof(ModbusWriteDoubleHoldingRegister));
            if (a != nullptr) {
                // Utiliser le placement new avec la mémoire PSRAM allouée
                node = new (a) ModbusWriteDoubleHoldingRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, webRefreshInterval);
            } else {
                // Fallback sur l'allocation mémoire standard avec gestion d'erreur
                node = new (std::nothrow) ModbusWriteDoubleHoldingRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, webRefreshInterval);
                if (node == nullptr) {
                    // Gestion de l'échec d'allocation
                    setPlcBroken("Erreur d'allocation mémoire pour ModbusWriteDoubleHoldingRegister");
                    return false;
                }
            }
        } else {
            // Allocation mémoire standard avec gestion d'erreur
            node = new (std::nothrow) ModbusWriteDoubleHoldingRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, webRefreshInterval);
            if (node == nullptr) {
                // Gestion de l'échec d'allocation
                setPlcBroken("Erreur d'allocation mémoire pour ModbusWriteDoubleHoldingRegister");
                return false;
            }
        }
        Serial.printf("Node %s created with succes with type ModbusWriteDoubleHoldingRegister, hash: %lu, address: %u, offset: %d\r\n", node->getName(), (unsigned long)*hash, slaveAddress, offset);
    } else if (!strcmp(type, MODBUS_READ_INPUT_REGISTER)){
        if (PLC_Tools::isPSRAM_available()) {
            void* a = ps_malloc(sizeof(ModbusReadInputRegister));
            if (a != nullptr) {
                // Utiliser le placement new avec la mémoire PSRAM allouée
                node = new (a) ModbusReadInputRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
            } else {
                // Fallback sur l'allocation mémoire standard avec gestion d'erreur
                node = new (std::nothrow) ModbusReadInputRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
                if (node == nullptr) {
                    // Gestion de l'échec d'allocation
                    setPlcBroken("Erreur d'allocation mémoire pour ModbusReadInputRegister");
                    return false;
                }
            }
        } else {
            // Allocation mémoire standard avec gestion d'erreur
            node = new (std::nothrow) ModbusReadInputRegister(nodeName, sectionName, id, *hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
            if (node == nullptr) {
                // Gestion de l'échec d'allocation
                setPlcBroken("Erreur d'allocation mémoire pour ModbusReadInputRegister");
                return false;
            }
        } 
        Serial.printf("Node %s created with succes with type ModbusReadInputRegister, hash: %lu, address: %u, offset: %u, refresh interval: %u, web refresh interval %u\r\n", node->getName(), (unsigned long)*hash, slaveAddress, offset, refreshInterval, webRefreshInterval);
    } else if (!strcmp(type, MODBUS_READ_MULTIPLE_INPUTS_STATUS)){
        uint8_t nbValues = 1;
        //if (hardSection.containsKey(NB_VALUES)){
        if (!hardSection[NB_VALUES].isNull()){
            nbValues = hardSection[NB_VALUES].as<uint8_t>();
            Serial.printf("createModbusNode:: nbValues: %d\r\n", nbValues);
        } else {
            char texte[250];
            sprintf(texte, "Unable to find key: %s on section %s\r\n", NB_VALUES, MODBUS_WRITE_MULTIPLE_COILS);
            setPlcBroken(texte);
        }

        // "inverse": tableau de bool, un par bit lu (même clé que BooleanInputNode, version bit à bit)
        bool invertedBits[64] = {};
        if (!configNode[INPUT_INVERTED].isNull() && configNode[INPUT_INVERTED].is<JsonArray>()){
            JsonArray invArray = configNode[INPUT_INVERTED].as<JsonArray>();
            uint8_t i = 0;
            for (JsonVariant v : invArray){
                if (i >= nbValues || i >= 64) break;
                invertedBits[i] = v.as<bool>();
                i++;
            }
        }

        if (PLC_Tools::isPSRAM_available()) {
            void* a = ps_malloc(sizeof(ModbusReadMultipleInputsRegistersNode));
            if (a != nullptr) {
                // Utiliser le placement new avec la mémoire PSRAM allouée
                node = new (a) ModbusReadMultipleInputsRegistersNode(nodeName, sectionName, id, (unsigned long)*hash, slaveAddress, offset, nbValues, refreshInterval, webRefreshInterval, invertedBits);
            } else {
                // Fallback sur l'allocation mémoire standard avec gestion d'erreur
                node = new (std::nothrow) ModbusReadMultipleInputsRegistersNode(nodeName, sectionName, id, (unsigned long)*hash, slaveAddress, offset, nbValues, refreshInterval, webRefreshInterval, invertedBits);
                if (node == nullptr) {
                    // Gestion de l'échec d'allocation
                    setPlcBroken("Erreur d'allocation mémoire pour ModbusReadMultipleInputsRegistersNode");
                    return false;
                }
            }
        } else {
            // Allocation mémoire standard avec gestion d'erreur
            node = new (std::nothrow) ModbusReadMultipleInputsRegistersNode(nodeName, sectionName, id, (unsigned long)*hash, slaveAddress, offset, nbValues, refreshInterval, webRefreshInterval, invertedBits);
            if (node == nullptr) {
                // Gestion de l'échec d'allocation
                setPlcBroken("Erreur d'allocation mémoire pour ModbusReadMultipleInputsRegistersNode");
                return false;
            }
        }
    } else {
        sprintf(message, "createModbusNode:: Unable to identifiy node type with class %s\r\n", type);
        prepareMessage(ERROR, message);
        return false;
    }

    *hash = node->getHash();

   return addNode(node, check);
}

bool BorneUniverselle::i2cInit(JsonDocument& contextDoc){
    char buff[128];
    // if (contextDoc.containsKey(I2C)){
    if (!contextDoc[I2C].isNull()){
        if (!isI2CInitialised){
            uint8_t sda, scl;
            if (!contextDoc[I2C][BU_SDA].isNull()){
                sda =  contextDoc[I2C][BU_SDA].as<uint8_t>();
                if (!contextDoc[I2C][BU_SCL].isNull()){
                    scl = contextDoc[I2C][BU_SCL].as<uint8_t>();
                    Wire.begin(sda, scl);
                    Serial.println("I2c bus initialised");
                } else {
                    sprintf(buff, "initI2C:: Unable to find key %s", BU_SCL);
                    prepareMessage(ERROR, buff);
                    return false; 
                } 
            }  else {
                sprintf(buff, "InitI2C:: Unable to find key %s", BU_SDA);
                prepareMessage(ERROR, buff);
                return false;
            } 
            Wire.begin(sda, scl);
            isI2CInitialised = true;
            if (!contextDoc[I2C][RTC_PRESENT].isNull()){
                RTC_Service* rtc = RTC_Service::getInstance();
                bool success = rtc->begin(true, sda, scl);
                if (!success) {
                    setPlcBroken("i2cInit:: Unable to initialise DS3231 RTC over I2C");
                    return false;
                }
            }
        }
        Serial.println("I2C bus is initialised");
        return true;
    } 
    Serial.printf("i2cInit:: key %s is missing\r\n", I2C);
    return false;
}

bool BorneUniverselle::RS485Init(JsonDocument& contextDoc) {
  
    if (isRs485Initialised) {
        return true;
    }
  
    isRs485Initialised = false;

    if (contextDoc[RS485].isNull()) {
        setPlcBroken("RS485Init:: No RS485 key or null");
        return false;
    }
   
    JsonObject rs485 = contextDoc[RS485];
    if (rs485[RX].isNull()) {
        setPlcBroken("RS485Init:: Unable to find key rx");
        return false;
    }
    if (rs485[TX].isNull()) {
        setPlcBroken("RS485Init:: Unable to find key tx");
        return false;
    }
    if (rs485[SPEED].isNull()) {
        setPlcBroken("RS485Init:: Unable to find key speed");
        return false;
    } 
    if (rs485[CONFIG].isNull()) {
        setPlcBroken("RS485Init:: Unable to find key config");
        return false;
    }

    uint8_t rxPin = rs485[RX].as<uint8_t>();
    uint8_t txPin = rs485[TX].as<uint8_t>();
    uint32_t speed = rs485[SPEED].as<int>();
    uint32_t config;

    const char* configStr = rs485[CONFIG];

    if (!strcmp(configStr, "SERIAL_8N1")) {
        config = SERIAL_8N1;
    } else if (!strcmp(configStr, "SERIAL_8N2")) {
        config = SERIAL_8N2;
    } else if (!strcmp(configStr, "SERIAL_8E1")) {
        config = SERIAL_8E1;
    } else if (!strcmp(configStr, "SERIAL_8E2")) {
        config = SERIAL_8E2;
    } else {
        setPlcBroken("RS485Init:: Unrecognised protocol");
        return false;
    }

    Serial.printf("RS485 speed: %lu, rx pin: %d, tx pin: %d, mode: %s\r\n", speed, rxPin, txPin, configStr);

    if (myRS485 != nullptr) {
        delete myRS485;
    }

    myRS485 = new HardwareSerial(1);
   
    myRS485->setRxBufferSize(256);
    myRS485->begin(speed, config, rxPin, txPin); 

    if (!myRS485->availableForWrite()) {
        setPlcBroken("RS485Init:: Failed to initialize HardwareSerial");
        delete myRS485;
        myRS485 = nullptr;
        return false;
    }

    isRs485Initialised = true;
    Serial.println("RS485 bus is initialised");
    return true;
}

// Dans borneUniverselle.cpp

bool BorneUniverselle::modbusInit(JsonDocument& contextDoc){
    if (isModbusInitialised){
        return true;
    }

    Serial.println("Try to initialise Modbus");
    
    if (!isRs485Initialised){
        if (!RS485Init(contextDoc)){
            prepareMessage(ERROR, "modbusInit:: unable to initalise RS485 bus");
            return false;
        }
    } 
    
    if (contextDoc[MODBUS].isNull()){
        char buff[128];
        sprintf(buff, "Unable to find key %s in context file", MODBUS);
        prepareMessage(ERROR, buff);
        return false;
    }

    if (!contextDoc[MODBUS][TIMEOUT].isNull()){
        uint16_t timeout = contextDoc[MODBUS][TIMEOUT].as<uint16_t>();
        Serial.printf("Modbus RTU timeout: %u\r\n", timeout);
        myModbus.setTimeout(timeout);
    } else {
        myModbus.setTimeout(DEFAULT_TIMEOUT);
    }

    ModbusRTU *modbus = new ModbusRTU();
    if (!modbus->begin(myRS485)){
        prepareMessage(ERROR, "modbusInit:: unable to initalise Modbus");
        return false;
    }
    modbus->master();

    myModbus.setModbus(modbus);
    
    isModbusInitialised = true;
    
    // 🔧 NOUVEAU : Informer MyModbus qu'il est initialisé
    myModbus.setInitialized(true);
    
    Serial.println("Modbus initialised"); 
    
    return true;
}

bool BorneUniverselle::createRxBoolNode(char *name,  char *parentName, uint16_t id, uint32_t *hash, JsonDocument& contextDoc, JsonObject& hardSection, char * type, bool inverted, uint16_t refreshInterval,
                                        uint16_t webRefreshInterval, bool check){
    // Cree un input node boolean hardware
    char buff[256];

    //Serial.println("createRxBoolNode");
    uint8_t pin;
    //if (hardSection.containsKey(PIN)){
    if (!hardSection[PIN].isNull()){
        pin = hardSection[PIN].as<int>();
    } else {
        sprintf(buff, "createRxBoolNode:: Unable to find key %s in hardware file for section %s", PIN, parentName);
        prepareMessage(ERROR, buff);

        return false;
    }

    Node *node;
    if (!strcmp(type, RX_BOOL)){
        //Serial.println("type RX_BOOL");

        if (PLC_Tools::isPSRAM_available()){
            void * a = ps_malloc(sizeof(HardwareBooleanInputNode));
            node = new (a) HardwareBooleanInputNode(name, parentName, id, *hash, pin, inverted, refreshInterval, webRefreshInterval); 
        } else {
            node = new HardwareBooleanInputNode(name, parentName, id, *hash, pin, inverted, refreshInterval, webRefreshInterval);
        }
        
        Serial.printf("Node %s created with succes (ESP32 pin %d), with type HardwareBooleanInputNode and hash: %lu, inverted: %s, refresh interval: %u, web refresh interval: %u\r\n", node->getName(), pin, (unsigned long)*hash, inverted ? "true" : "false", refreshInterval, webRefreshInterval);
    } else  if (!strcmp(type, PF8574_RX_BOOL)){
        if (isI2CInitialised){
            //if (contextDoc[I2C].containsKey(RX_ADDR)){
            if (!contextDoc[I2C][RX_ADDR].isNull()){
                uint8_t i2cAddr = contextDoc[I2C][RX_ADDR].as<uint8_t>();
                if (PLC_Tools::isPSRAM_available()){
                    void * a = ps_malloc(sizeof(PF8574BooleanInputNode));
                    node = new (a) PF8574BooleanInputNode(name, parentName, id, *hash, i2cAddr, pin, inverted, refreshInterval, webRefreshInterval);
                } else {
                    node = new PF8574BooleanInputNode(name, parentName, id, *hash, i2cAddr, pin, inverted, refreshInterval, webRefreshInterval);
                }
                Serial.printf("Node %s created with succes with type PF8574BooleanInputNode and hash: %lu, inverted: %s, refresh interval: %u, web refresh interval: %u\r\n", node->getName(), (unsigned long)*hash, inverted ? "true" : "false", refreshInterval, webRefreshInterval);
            } else {
                sprintf(buff, "createRxBoolNode:: Unable to find key %s in hardware file for section %s", RX_ADDR, parentName);
                prepareMessage(ERROR, buff);
                return false;
            }
        }   else {
            sprintf(buff, "createRxBoolNode:: Use PF8574, but I2Cbus is not initialised");
            prepareMessage(ERROR, buff);
            return false;
        } 

    } else {
        sprintf(buff, "createRxBoolNode:: Unable to match type  %s from config with type for hardware file.\r\n", type);
        prepareMessage(ERROR, buff);
        return false;
    }

    *hash = node->getHash();

   return addNode(node, check);
}

bool BorneUniverselle::createTxBoolNode(char *name, char *parentName, uint16_t id,  uint32_t *hash, JsonDocument& contextDoc, JsonObject& hardSection, char * type,
                                        bool inverted, uint16_t webRefreshInterval, bool check){
    // Cree un input node boolean hardware
    uint8_t pin;
    char buff[128];
    //if (hardSection.containsKey(PIN)){
    if (!hardSection[PIN].isNull()){
        pin = hardSection[PIN].as<int>();
    } else {
        sprintf(buff, "createTxBoolNode:: Unable to find key %s in hardware file for section %s, web refresh interval: %u", PIN, parentName, webRefreshInterval);
        prepareMessage(ERROR, buff);
        return false;
    }

    Node *node;
    if (!strcmp(type, TX_BOOL)){
        //Serial.println("type TX bool"); 
        if (PLC_Tools::isPSRAM_available()){
            void * a = ps_malloc(sizeof(HardwareBooleanOutputNode));
            node = new (a) HardwareBooleanOutputNode(name, parentName, id, *hash, pin, webRefreshInterval);
        } else {  
            node = new HardwareBooleanOutputNode(name, parentName, id, *hash, pin, webRefreshInterval);
        }

        Serial.printf("Node %s created with succes (ESP32 pin %d) with type HardwareBooleanOutputNode and hash: %lu\r\n", node->getName(), pin, (unsigned long)*hash); 
    } else if (!strcmp(type, PF8574_TX_BOOL)){
        //Serial.println("type PF8574");
        if (isI2CInitialised){
            //if (contextDoc[I2C].containsKey(TX_ADDR)){
            if (!contextDoc[I2C][TX_ADDR].isNull()){
                uint8_t i2cAddr = contextDoc[I2C][TX_ADDR].as<uint8_t>();

                if (PLC_Tools::isPSRAM_available()){
                    void * a = ps_malloc(sizeof(PF8574BooleanOutputNode));
                    node = new (a) PF8574BooleanOutputNode(name, parentName, id, *hash, i2cAddr, pin, webRefreshInterval);
                } else {
                    node = new PF8574BooleanOutputNode(name, parentName, id, *hash, i2cAddr, pin, webRefreshInterval);
                }

                Serial.printf("Node %s created with succes with type PF8574BooleanOutputNode and hash: %lu\r\n", node->getName(), (unsigned long)*hash); 
            } else {
                sprintf(buff, "createTxBoolNode:: Unable to find key %s in hardware file for section %s, web refresh interval: %u", TX_ADDR, parentName, webRefreshInterval);
                prepareMessage(ERROR, buff);
                return false;
            }    
        } else {
            sprintf(buff, "createTxBoolNode:: Use PF8574, but I2Cbus is not initialised");
            prepareMessage(ERROR, buff);
            return false;
        }
    } else if (!strcmp(type, PCA9554_TX_BOOL)){
        if (isI2CInitialised){
            if (!contextDoc[I2C][TX_ADDR].isNull()){
                uint8_t i2cAddr = contextDoc[I2C][TX_ADDR].as<uint8_t>();

                if (PLC_Tools::isPSRAM_available()){
                    void * a = ps_malloc(sizeof(PCA9554BooleanOutputNode));
                    node = new (a) PCA9554BooleanOutputNode(name, parentName, id, *hash, i2cAddr, pin, inverted, webRefreshInterval);
                } else {
                    node = new PCA9554BooleanOutputNode(name, parentName, id, *hash, i2cAddr, pin, inverted, webRefreshInterval);
                }

                Serial.printf("Node %s created with succes with type PCA9554BooleanOutputNode and hash: %lu\r\n", node->getName(), (unsigned long)*hash);
            } else {
                sprintf(buff, "createTxBoolNode:: Unable to find key %s in hardware file for section %s, web refresh interval: %u", TX_ADDR, parentName, webRefreshInterval);
                prepareMessage(ERROR, buff);
                return false;
            }
        } else {
            sprintf(buff, "createTxBoolNode:: Use PCA9554, but I2Cbus is not initialised");
            prepareMessage(ERROR, buff);
            return false;
        }
    } else {
        sprintf(buff, "createTxBoolNode:: Unable to match type  %s from config with type for hardware file.\r\n", type);
        prepareMessage(ERROR, buff);
        return false;
    }

    *hash = node->getHash();

    //Serial.printf("Node %s, pin %d created with succes with type BooleanOutputNode, hash: %u\r\n", name, pin, *hash);
    return addNode(node, check);
}

bool BorneUniverselle::createVirtualNode(char *name, char *sectionName, uint16_t id, char *type, bool inverted, uint32_t *hash, bool check){
    char buff[128];
    bool found = false;
    //Serial.printf("createVirtualNode section: %s,  node name: %s, \r\n", sectionName, name);
    Node *node;
    // No web refresh interval constraint...
    if (!strcmp(type, RX_BOOL)){      
        if (PLC_Tools::isPSRAM_available()){
            void * a = ps_malloc(sizeof(VirtualBooleanInputNode));
            if (!a) {
                Serial.println("❌ ps_malloc FAILED!");
                return false;  // Ou fallback vers malloc normal
            }
            node = new (a) VirtualBooleanInputNode(name, sectionName, id, *hash, inverted, 0);   
        } else {
            node = new VirtualBooleanInputNode(name, sectionName, id, *hash, inverted, 0);
        }
        Serial.printf("Node %s created (VirtualBooleanInputNode) with succes, hash: %lu\r\n", node->getName(), (unsigned long)node->getHash());
        found = true;
    } else if (!strcmp(type, TX_BOOL)) {
        if (PLC_Tools::isPSRAM_available()){
            void * a = ps_malloc(sizeof(VirtualBooleanOutputNode));
            if (!a) {
                Serial.println("❌ ps_malloc FAILED!");
                return false;  // Ou fallback vers malloc normal
            }
            node = new (a) VirtualBooleanOutputNode(name, sectionName, id, *hash, 0);
        } else {
            node = new VirtualBooleanOutputNode(name, sectionName, id, *hash, 0);
        }
        
        Serial.printf("Node %s created (VirtualBooleanOutputNode) with succes, hash: %lu\r\n", node->getName(), (unsigned long)node->getHash());
        found = true;
    } else if (!strcmp(type, RX_UINT32)){
        if (PLC_Tools::isPSRAM_available()){
            void * a = ps_malloc(sizeof(VirtualUint32InputNode));
            if (!a) {
                Serial.println("❌ ps_malloc FAILED!");
                return false;  // Ou fallback vers malloc normal
            }
            node = new (a) VirtualUint32InputNode(name, sectionName, id, *hash, 0);
        } else {
            node = new VirtualUint32InputNode(name, sectionName, id, *hash, 0);
        }
        
        Serial.printf("Node %s created (VirtualUint32InputNode) with succes, hash: %lu\r\n", node->getName(), (unsigned long)node->getHash());
        found = true;
    } else if (!strcmp(type, TX_UINT32)){
        if (PLC_Tools::isPSRAM_available()){
            void * a = ps_malloc(sizeof(VirtualUint32OutputNode));
            if (!a) {
                Serial.println("❌ ps_malloc FAILED!");
                return false;  // Ou fallback vers malloc normal
            }
            node = new (a) VirtualUint32OutputNode(name, sectionName, id, *hash, 0);
        } else {
            node = new VirtualUint32OutputNode(name, sectionName, id, *hash, 0);
        }
        
        Serial.printf("Node %s created (VirtualUint32OutputNode) with succes, hash: %lu\r\n", node->getName(), (unsigned long)node->getHash());
        found = true;
    } else if (!strcmp(type, TX_FLOAT)){
        if (PLC_Tools::isPSRAM_available()){
            void * a = ps_malloc(sizeof(VirtualFloatOutputNode));
            if (!a) {
                Serial.println("❌ ps_malloc FAILED!");
                return false;  // Ou fallback vers malloc normal
            }
            node = new (a) VirtualFloatOutputNode(name, sectionName, id, *hash, 0); 
        } else {
            node = new VirtualFloatOutputNode(name, sectionName, id, *hash, 0);
        }
        
        Serial.printf("Node %s created (VirtualFloatOutputNode) with succes, hash: %lu\r\n", node->getName(), (unsigned long)node->getHash());
        found = true;
    } else if (!strcmp(type, RX_FLOAT)){
        if (PLC_Tools::isPSRAM_available()){
            void * a = ps_malloc(sizeof(VirtualFloatInputNode));
            if (!a) {
                Serial.println("❌ ps_malloc FAILED!");
                return false;  // Ou fallback vers malloc normal
            }
            node = new (a) VirtualFloatInputNode(name, sectionName, id, *hash, 0); 
        } else {
            node = new VirtualFloatInputNode(name, sectionName, id, *hash, 0);
        }
        
        Serial.printf("Node %s created (VirtualFloatInputNode) with succes, hash: %lu\r\n", node->getName(), (unsigned long)node->getHash());
        found = true;
    } else if (!strcmp(type, TX_TEXT)){
        if (PLC_Tools::isPSRAM_available()){
            void * a = ps_malloc(sizeof(VirtualTextOutputNode));
            if (!a) {
                Serial.println("❌ ps_malloc FAILED!");
                return false;  // Ou fallback vers malloc normal
            }
            node = new (a) VirtualTextOutputNode(name, sectionName, id, *hash, 0); 
        } else {
            node = new VirtualTextOutputNode(name, sectionName, id, *hash, 0);
        }
        
        Serial.printf("Node %s created (VirtualTextOutputNode) with succes, hash: %lu\r\n", node->getName(), (unsigned long)node->getHash());
        found = true;
    } else if (!strcmp(type, RX_TEXT)){
       found = true;
        if (PLC_Tools::isPSRAM_available()){
            void * a = ps_malloc(sizeof(VirtualTextInputNode));
            if (!a) {
                Serial.println("❌ ps_malloc FAILED!");
                return false;  // Ou fallback vers malloc normal
            }
            node = new (a) VirtualTextInputNode(name, sectionName, id, *hash, 0); 
        } else {
            node = new VirtualTextInputNode(name, sectionName, id, *hash, 0);
        }   
        Serial.printf("Node %s created (VirtualTextInputNode) with succes, hash: %lu\r\n", node->getName(), (unsigned long)node->getHash());
    } else {
        sprintf(buff, "createVirtualNode:: Unable to create virtual node %s/%s with type %s", sectionName, name, type);
        prepareMessage(ERROR, buff);
        setPlcBroken(buff);
        return false;
    }

    *hash = node->getHash();

    if (found){
        return addNode(node, check);
    } else {
        return false;
    }
}

bool BorneUniverselle::getIsKinconyA8S(){
    return isKinconyA8S;
}

bool BorneUniverselle::notifyWebClient(bool sendAllStates){
    // Sortie immédiate si pas de client — évite l'allocation SafeJsonDocument en PSRAM
    if (!isClientConnected()) {
        clearInputschanged();
        return true;
    }
    if (clientQueueIsFull()){
        Serial.println("No client connected or client queue full");
        clearInputschanged();
        return true;
    }

    uint32_t start = millis();
    uint32_t lastCheck = millis();
    bool hasTosend = false;

    SafeJsonDocument notifyDoc;
    JsonArray array = notifyDoc[NOTIFY_STATES_CHANGED].to<JsonArray>();

    uint32_t nodesSkippedVirtualInputs = 0;
    //uint32_t nodesSent = 0;

  std::map<uint32_t, Node *>::iterator it;
    for (it = nodesMap.begin(); it != nodesMap.end(); it++){
        // Ne pas interrompre l'envoi de ALL_STATES par une interruption hardware
        if (!sendAllStates && PF8574BooleanInputNode::isInterrupt()){
            return true; 
        }

        Node *node = it->second;

        // Skip les nodes temporairement désactivés (erreurs Modbus répétées)
        if (node->isNodeDisabled()){
            continue; 
        }

        // 🔥 TOUJOURS skip les Virtual INPUT nodes
        // Le client les contrôle, pas besoin de les lui renvoyer
        if (node->getMode() == INPUT_MODE) {
            int classType = node->classType();
            if (classType == CLASS_VIRTUAL_BOOLEAN_INPUT_NODE ||
                classType == CLASS_VIRTUAL_UINT32_INPUT_NODE ||
                classType == CLASS_VIRTUAL_FLOAT_INPUT_NODE ||
                classType == CLASS_VIRTUAL_TEXT_INPUT_NODE) {
                
                // Skip SAUF si le node a été modifié
                // CRITIQUE : Permet au clearIsChanged() d'être appelé même pour les VirtualInputs
                if (!node->getIsChanged()) {
                    nodesSkippedVirtualInputs++;
                    continue;
                }
                // Si le VirtualInput a changé (rare), on continue pour le notifier
            }
        }

        // Pour un OutputNode : notifier seulement si l'écriture hardware a réussi (_notifyPending)
        // Pour un InputNode  : notifier si la valeur a changé (getIsChanged)
        // Dans les deux cas, sendAllStates force l'envoi sauf OutputNode en attente d'écriture
        bool shouldNotify = false;
        if (node->getMode() == OUTPUT_MODE){
            OutputNode *outputNode = static_cast<OutputNode*>(node);
            shouldNotify = outputNode->getNotifyPending();
            // sendAllStates : on envoie l'état courant uniquement si l'écriture est confirmée
            if (sendAllStates && !outputNode->getIsChanged()){
                shouldNotify = true;  // valeur stable, déjà écrite sur le hardware
            }
        } else {
            shouldNotify = node->getIsChanged() || sendAllStates;
        }

        if (shouldNotify){
            // On limite le refresh du client web
            if (!sendAllStates){
                if (millis() - node->getLastWebUpdate() < node->getWebUpdateInterval()){
                    continue;
                }
                node->setLastWebUpdate(millis());
            }

            hasTosend = true;
            if (node->getMode() == DEFAULT_MODE){
                char message[128];
                sprintf(message, "Node %s is not input and not output !!!\r\n", node->getName());
                setPlcBroken(message);
                return false;
            }

            JsonObject nodeObject = array.add<JsonObject>();

            if (!addNodeToNodeObject(node, &nodeObject)){
                Serial.printf("notifyWebClient: addNodeToNodeObject for node: %s failed\r\n", node->getName());
                return false;
            }

            if (node->getMode() == INPUT_MODE){
                node->clearIsChanged();
            } else {
                // OutputNode : écriture confirmée notifiée, on remet le flag à zéro
                static_cast<OutputNode*>(node)->clearNotifyPending();
            }

            // Serial.printf("Will notify for node: %s\r\n", node->getName()); // LOG_PLC
        }

        if (millis() - lastCheck > ABNORMAL_REFRESH_TIME){   
            //Serial.printf("%lu::Refresh:: will check heartbeat and Websocket message\r\n", millis());
           keepAliveHeartbeat();
            lastCheck = millis();
            vTaskDelay(pdMS_TO_TICKS(10)); // Délai plus approprié que yield()
        }
    } 

      // 🔥 Log diagnostique
    if (nodesSkippedVirtualInputs > 0 && sendAllStates) {
        // Serial.printf("%lu:: 📊 notifyWebClient: sent %lu nodes, skipped %lu virtual input nodes\r\n", millis(), nodesSent, nodesSkippedVirtualInputs);
    }

    //Serial.printf("%lu:: notifyWebClient END: hasTosend=%d, sendAllStates=%d\r\n", millis(), hasTosend, sendAllStates);


    if (hasTosend){
        if (!isClientConnected()){
           return true; 
        }
       
       bool success = PLC_Tools::sendJsonDocument(notifyDoc, "NotifyStates");
        if (!success){
            Serial.println("Failed to send notify document to client");
            return false;
        }
        
        if (millis() - start > 300){ 
            Serial.printf("\r\n%lu:: End notifyWebClient %s: duration: %lu\r\n", millis(), sendAllStates ? "all states": "on state", millis() - start);
        }
    }
    //Serial.printf("%lu:: notifyWebClient: End web notify\r\n", millis());
    return true;
} // notifyWebClient


bool BorneUniverselle::addNodeToNodeObject(Node *node, JsonObject *nodeObject){
    uint32_t hash = node->getHash();

    //Serial.printf("addNodeToNodeObject: node: %s, hash: %lu\r\n", node->getName(), hash);  

    (*nodeObject)[HASH] = hash;

    if (node->classType() == CLASS_HW_BOOLEAN_INPUT_NODE || node->classType() == CLASS_PFC8574_BOOLEAN_INPUT_NODE ||
        node->classType() == CLASS_VIRTUAL_BOOLEAN_INPUT_NODE || node->classType() == CLASS_MODBUS_READ_COIL){
        BooleanInputNode *b = (BooleanInputNode*) node;
        (*nodeObject)[VALUE] = b->getValue();
 
        Serial.printf("%lu:: Node notify: %s new value: %s\r\n", millis(), node->getName(), b->getValue() ? "true" : "false");    
    } else  if (node->classType() == CLASS_HW_BOOLEAN_OUTPUT_NODE || node->classType() == CLASS_PFC8574_BOOLEAN_OUTPUT_NODE || node->classType() == CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE ||
        node->classType() == CLASS_PCA9554_BOOLEAN_OUTPUT_NODE || node->classType() == CLASS_MODBUS_WRITE_COIL_NODE){
        BooleanOutputNode *b = (BooleanOutputNode*) node;
        (*nodeObject)[VALUE] = b->getValue();

        Serial.printf("%lu:: Node notify: %s new value: %s\r\n", millis(), node->getName(), b->getValue() ? "true" : "false");
    } else if (node->classType() == CLASS_MODBUS_READHOLDINGREGISTER || node->classType() == CLASS_MODBUS_READINPUTREGISTER){
        Uint16InputNode *u = (Uint16InputNode*) node;
        (*nodeObject)[VALUE] = u->getValue();
 
        Serial.printf("%lu:: Node notify (Uint16InputNode): %s new value: %u\r\n", millis(), node->getName(), u->getValue());
    } else if (node ->classType() == CLASS_MODBUS_WRITEHOLDINGREGISTER){
        Uint16OutputNode *w = (Uint16OutputNode*) node;
        (*nodeObject)[VALUE] = w->getValue();
 
        Serial.printf("%lu:: Node notify (Uint16OutputNode): %s new value: %u\r\n", millis(), node->getName(), w->getValue());
    } else if (node->classType() == CLASS_VIRTUAL_UINT32_INPUT_NODE || node->classType() == CLASS_MODBUS_READ_DOUBLE_INPUTREGISTER || node->classType() == CLASS_MODBUS_READ_DOUBLE_HOLDING_REGISTER){
        Uint32InputNode *v = (Uint32InputNode*) node;
        (*nodeObject)[VALUE] = v->getValue();
        Serial.printf("%lu:: Node notify: %s (Uint32InputNode) new value: %lu\r\n", millis(), node->getName(), (unsigned long)v->getValue());
    } else if (node->classType() == CLASS_VIRTUAL_UINT32_OUTPUT_NODE || node->classType() == CLASS_MODBUS_WRITE_DOUBLE_INPUTREGISTER || node->classType() == CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER) {
        Uint32OutputNode *u = (Uint32OutputNode *) node;
        (*nodeObject)[VALUE] = u->getValue();

        Serial.printf("%lu:: Node notify (uint32 output): %s new value: %lu\r\n", millis(), node->getName(), (unsigned long)u->getValue());
    } else if (node->classType() == CLASS_VIRTUAL_FLOAT_OUTPUT_NODE) {
        FloatOutputNode *u = (FloatOutputNode *) node;
        (*nodeObject)[VALUE] = u->getValue();

        Serial.printf("%lu:: Node notify (float output): %s new value: %.3f\r\n", millis(), node->getName(), u->getValue());
    } else if (node->classType() == CLASS_VIRTUAL_FLOAT_INPUT_NODE) {
        FloatInputNode *i = (FloatInputNode *) node;
        (*nodeObject)[VALUE] = i->getValue();

        Serial.printf("%lu:: Node notify (float input): %s new value: %.3f\r\n", millis(), node->getName(), i->getValue());
    } else if (node->classType() == CLASS_MODBUS_WRITE_MULTIPLE_COILS){
        ModbusWriteMultipleCoilslNode *m = (ModbusWriteMultipleCoilslNode *) node;
        char text[32];
        uint8_t z;
        for (z = 0; z < m->getNbValues(); z++){
            if (m->getValue(z)){
               text[z] = '1';
            } else {
                 text[z] = '0';
            }
        }
   
        text[m->getNbValues()] = 0;
        (*nodeObject)[VALUE] = text;
        Serial.printf("node: %s is a ModbusWriteMultipleCoilslNode, it can be represented as a unique number. The 'value': %s is all binary bits converted to an texte\r\n", node->getName(), text);
    } else if (node->classType() == CLASS_MODBUS_READ_MULTIPLE_INPUTS_STATUS){  
        ModbusReadMultipleInputsRegistersNode *t = (ModbusReadMultipleInputsRegistersNode *) node;
        char text[32];
        uint8_t z;
        for (z = 0; z < t->getNbValues(); z++){
            if (t->bits[z].getValue()){
               text[z] = '1';
            } else {
                 text[z] = '0';
            }
        }
        text[t->getNbValues()] = 0;
        (*nodeObject)[VALUE] = text;
        Serial.printf("ModbusReadMultipleInputsRegistersNode notify: %s = %s\r\n", node->getName(), text);              
    } else if (node->classType() == CLASS_VIRTUAL_TEXT_INPUT_NODE){
        VirtualTextInputNode *t = (VirtualTextInputNode *) node;
        (*nodeObject)[VALUE] = t->getValue(); 
        Serial.printf("%lu:: Node notify: %s new value: %s\r\n", millis(), node->getName(), t->getValue());
    } else if (node->classType() == CLASS_VIRTUAL_TEXT_OUTPUT_NODE) {
        VirtualTextOutputNode *v = (VirtualTextOutputNode *) node;
        (*nodeObject)[VALUE] = v->getValue();
        Serial.printf("%lu:: Node notify: %s new value: %s\r\n", millis(), node->getName(), v->getValue());
    } else {
        char buff[256];
        sprintf(buff, "addNodeToJsonObject:: Unable to find node class from node: %s with class type: %u\r\n", node->getName(), node->classType());
        prepareMessage(WARNING, buff);
        setPlcBroken(buff);
        return false;
    }

    if (node->descriptorCallback) {
        //Serial.printf("addNodeToNodeObject:: Node %s with hash %lu has a descriptor callback\r\n", node->getName(), (long unsigned int)node->getHash());
        addCustomDescriptor(node, nodeObject);
        //Serial.printf("addNodeToNodeObject:: Node %s with hash %lu custom descriptor added\r\n", node->getName(), (long unsigned int)node->getHash());
        //Serial.printf("NodeObject au retour de addCustomDescriptor:\r\n");
        //PLC_Tools::printJsonObject(*nodeObject);
    }

    checkHeartbeat();
    return true;
}

void BorneUniverselle::addCustomDescriptor(Node *node, JsonObject *nodeObject) {
    // Appeler la callback pour récupérer le descripteur personnalisé
    JsonDocument customDescriptor = node->descriptorCallback();
    
    // Si le document n'est pas vide, l'ajouter au nodeObject
    if (!customDescriptor.isNull()) {
        Serial.printf("BorneUniverselle::addCustomDescriptor: hash from node: %lu, hash from nodeObject: %lu\r\n", (long unsigned int)node->getHash(), (*nodeObject)[HASH].as<uint32_t>());
        if (!customDescriptor[VALUE].isNull()) {    
            //La clef value ne va pas dans descritor, mais dans nodeObject
            //On va donc supprimer la clef pour la réintroduire un étage plus haut.
             
            // Supprimer la clé hash du descripteur personnalisé
            Serial.println("Custom descriptor contains a key 'value', will superside it with nodeObject value");
            (*nodeObject)[VALUE] = customDescriptor[VALUE];
            // Supprimer la clé "value" du descripteur pour éviter la duplication
            customDescriptor.remove("value");
        }
        
        // Vérifier si nodeObject a déjà une clé descriptor
        if (!(*nodeObject)[DESCRIPTOR].isNull()) {
            Serial.println("Descriptor key found in nodeObject, will merge custom descriptor with existing one");
            // Récupérer l'objet descriptor existant
            JsonObject existingDescriptor = (*nodeObject)[DESCRIPTOR].as<JsonObject>();
            // Fusionner le nouveau descripteur avec l'existant
            // Copier toutes les paires clé-valeur de customDescriptor dans existingDescriptor
            for (JsonPair p : customDescriptor.as<JsonObject>()) {
                existingDescriptor[p.key().c_str()] = p.value();
            }
        } else {
            // Si pas de descriptor existant, ajouter simplement le nouveau
            Serial.println("No descriptor key found in nodeObject, will add custom descriptor");
            (*nodeObject)[DESCRIPTOR] = customDescriptor;
        }

        
        // Serial.printf("%lu:: Added custom descriptor to node: %s, with hash: %lu\r\n", millis(), node->getName(), (long unsigned int)node->getHash());
    } else {
        Serial.printf("%lu:: Custom descriptor is null for node: %s\r\n", millis(), node->getName());
    }

    //Serial.printf("After descriptorcallback from node: %lu, hash from nodeObject: %lu\r\n", (long unsigned int)node->getHash(), (*nodeObject)[HASH].as<uint32_t>());
    //PLC_Tools::printJsonObject(*nodeObject);
}

void BorneUniverselle::clearInputschanged(){
    std::map<uint32_t, Node *>::iterator it;

    for (it = nodesMap.begin(); it != nodesMap.end(); it++){
        Node *node = it->second;
 
        if (node->getMode() == INPUT_MODE){
            if (node->getIsChanged()){
                //Serial.printf("Node %s change will be cleared\r\n", node->getName());
                node->clearIsChanged();
            }
        } 
    }
} // if no client connected

Node *BorneUniverselle::findNode(const char *context, uint32_t hash){
    BorneUniverselle* instance = getInstance();
    if (instance != nullptr) {
        return instance->findNodeImpl(context, hash);
    } else {
        // Cas où aucune instance n'est disponible
        char text[256];
        snprintf(text, sizeof(text), "findNode:: No instance available. Searching for node with hash: %lu, with context: %s", (unsigned long)hash, context);
        Serial.println(text);
        return nullptr;
    }
}

Node *BorneUniverselle::findNodeImpl(const char *context, uint32_t hash) {
    auto iter = nodesMap.find(hash);

    if (iter != nodesMap.end()) {
        return iter->second;
    } else {
        char text[256];
        snprintf(text, sizeof(text), "findNodeImpl:: Searching for node with hash: %lu, with context: %s but not found!", (unsigned long)hash, context);
        setPlcBroken(text);
        return nullptr;
    }
}

void BorneUniverselle::printConfigFile(){
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    
    SafeJsonDocument configDoc;
    if (!persistence.readJsonFromFile("/config.json", configDoc)) {
      Serial.printf("Config file open error: %s\n", persistence.getLastError());
      setPlcBroken(persistence.getLastError());
      return;
    }
    
    serializeJsonPretty(configDoc, Serial);
}

bool BorneUniverselle::isCompatibleClassType(int actualType, int expectedType) {
    if (actualType == expectedType) return true;
    switch (expectedType) {
        case CLASS_BOOLEAN_INPUT_NODE:
            return actualType == CLASS_HW_BOOLEAN_INPUT_NODE || 
                   actualType == CLASS_PFC8574_BOOLEAN_INPUT_NODE || 
                   actualType == CLASS_VIRTUAL_BOOLEAN_INPUT_NODE || 
                   actualType == CLASS_MODBUS_READ_COIL;
        case CLASS_BOOLEAN_OUTPUT_NODE:
            return actualType == CLASS_HW_BOOLEAN_OUTPUT_NODE ||
                   actualType == CLASS_PFC8574_BOOLEAN_OUTPUT_NODE ||
                   actualType == CLASS_PCA9554_BOOLEAN_OUTPUT_NODE ||
                   actualType == CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE ||
                   actualType == CLASS_MODBUS_WRITE_COIL_NODE;
        case CLASS_UINT16_INPUT_NODE:
            return actualType == CLASS_MODBUS_READHOLDINGREGISTER || 
                   actualType == CLASS_MODBUS_READINPUTREGISTER;
        case CLASS_UINT16_OUTPUT_NODE:
            return actualType == CLASS_MODBUS_WRITEHOLDINGREGISTER;
        case CLASS_UINT32_INPUT_NODE:
            return actualType == CLASS_VIRTUAL_UINT32_INPUT_NODE || 
                   actualType == CLASS_MODBUS_READ_DOUBLE_INPUTREGISTER || 
                   actualType == CLASS_MODBUS_READ_DOUBLE_HOLDING_REGISTER;
        case CLASS_UINT32_OUTPUT_NODE:
            return actualType == CLASS_VIRTUAL_UINT32_OUTPUT_NODE || 
                   actualType == CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER;
        case CLASS_FLOAT_INPUT_NODE:
            return actualType == CLASS_VIRTUAL_FLOAT_INPUT_NODE;
        case CLASS_FLOAT_OUTPUT_NODE:
            return actualType == CLASS_VIRTUAL_FLOAT_OUTPUT_NODE;
        case CLASS_TEXT_INPUT_NODE:
            return actualType == CLASS_VIRTUAL_TEXT_INPUT_NODE;
        case CLASS_TEXT_OUTPUT_NODE:
            return actualType == CLASS_VIRTUAL_TEXT_OUTPUT_NODE;
        default:
            return false;
    }
}

bool BorneUniverselle::validateNodeType(Node* node, int expectedClassType, const char* nodeName, uint32_t hash) {
    if (!node) {
        char errorMsg[256];
        snprintf(errorMsg, sizeof(errorMsg), "Nœud null pour %s (hash %lu)", nodeName, hash);
        Serial.println(errorMsg);
        setPlcBrokenImpl(errorMsg);
        return false;
    }
    
    int actualType = node->classType();
    if (!isCompatibleClassType(actualType, expectedClassType)) {
        char expectedName[50], actualName[50];
        getClasseName(expectedClassType, expectedName);
        getClasseName(actualType, actualName);
        
        char errorMsg[512];
        snprintf(errorMsg, sizeof(errorMsg), 
                "ERREUR TYPE: Le nœud %s (hash %lu) est défini comme %s dans config.json, "
                "mais est utilisé comme %s dans Formaca.h", 
                nodeName, hash, expectedName, actualName);
        
        Serial.println(errorMsg);
        setPlcBrokenImpl(errorMsg);
        return false;
    }
    return true;
}

bool BorneUniverselle::validatePointerType(void** ptr, int expectedClassType) {
    if (!ptr) {
        char expectedName[50];
        BorneUniverselle::getClasseName(expectedClassType, expectedName);
        Serial.printf("Pointeur nul pour classType %s (%d)\n", expectedName, expectedClassType);
        BorneUniverselle::setPlcBroken("Pointeur nul");
        return false;
    }
    char expectedName[50];
    BorneUniverselle::getClasseName(expectedClassType, expectedName);
    return true; // Retourne true pour permettre l'assignation, mais logue si besoin
}

void BorneUniverselle::getClasseName(int classId, char* className) {
    switch (classId) {
        case CLASS_NODE: strcpy(className, "Node"); break;
        case CLASS_INPUT_NODE: strcpy(className, "InputNode"); break;
        case CLASS_OUTPUT_NODE: strcpy(className, "OutputNode"); break;
        case CLASS_BOOLEAN_INPUT_NODE: strcpy(className, RX_BOOL); break;
        case CLASS_BOOLEAN_OUTPUT_NODE: strcpy(className, TX_BOOL); break;
        case CLASS_UINT16_INPUT_NODE: strcpy(className, RX_UINT16); break;
        case CLASS_UINT16_OUTPUT_NODE: strcpy(className, TX_UINT16); break;
        case CLASS_UINT32_INPUT_NODE: strcpy(className, RX_UINT32); break;
        case CLASS_UINT32_OUTPUT_NODE: strcpy(className, TX_UINT32); break;
        case CLASS_HW_BOOLEAN_INPUT_NODE: strcpy(className, RX_BOOL); break;
        case CLASS_HW_BOOLEAN_OUTPUT_NODE: strcpy(className, TX_BOOL); break;
        case CLASS_VIRTUAL_BOOLEAN_INPUT_NODE: strcpy(className, RX_BOOL); break;
        case CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE: strcpy(className, TX_BOOL); break;
        case CLASS_PFC8574_BOOLEAN_INPUT_NODE: strcpy(className, RX_BOOL); break;
        case CLASS_PFC8574_BOOLEAN_OUTPUT_NODE: strcpy(className, TX_BOOL); break;
        case CLASS_PCA9554_BOOLEAN_OUTPUT_NODE: strcpy(className, TX_BOOL); break;
        case CLASS_VIRTUAL_UINT32_INPUT_NODE: strcpy(className, RX_UINT32); break;
        case CLASS_VIRTUAL_UINT32_OUTPUT_NODE: strcpy(className, TX_UINT32); break;
        case CLASS_MODBUS_READ_COIL: strcpy(className,RX_BOOL); break;
        case CLASS_MODBUS_WRITE_COIL_NODE: strcpy(className, TX_BOOL); break;
        case CLASS_MODBUS_READHOLDINGREGISTER: strcpy(className,RX_UINT16); break;
        case CLASS_MODBUS_WRITEHOLDINGREGISTER: strcpy(className,TX_UINT16); break;
        case CLASS_MODBUS_READINPUTREGISTER: strcpy(className, RX_UINT16); break;
        case CLASS_MODBUS_READ_DOUBLE_INPUTREGISTER: strcpy(className, RX_UINT32); break;
        case CLASS_MODBUS_WRITE_DOUBLE_INPUTREGISTER: strcpy(className, TX_UINT32); break;
        case CLASS_MODBUS_READ_DOUBLE_HOLDING_REGISTER: strcpy(className, RX_UINT32); break;
        case CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER: strcpy(className, TX_UINT32); break;
        case CLASS_MODBUS_WRITE_MULTIPLE_COILS: strcpy(className, MODBUS_WRITE_MULTIPLE_COILS); break;
        case CLASS_MODBUS_READ_MULTIPLE_INPUTS_STATUS: strcpy(className, MODBUS_READ_MULTIPLE_INPUTS_STATUS); break;
        case CLASS_FLOAT_INPUT_NODE: strcpy(className, RX_FLOAT); break;
        case CLASS_FLOAT_OUTPUT_NODE: strcpy(className, TX_FLOAT); break;
        case CLASS_VIRTUAL_FLOAT_INPUT_NODE: strcpy(className, RX_FLOAT); break;
        case CLASS_VIRTUAL_FLOAT_OUTPUT_NODE: strcpy(className, TX_FLOAT); break;
        case CLASS_TEXT_INPUT_NODE: strcpy(className, RX_TEXT); break;
        case CLASS_VIRTUAL_TEXT_INPUT_NODE: strcpy(className, RX_TEXT); break;
        case CLASS_TEXT_OUTPUT_NODE: strcpy(className, TX_TEXT); break;
        case CLASS_VIRTUAL_TEXT_OUTPUT_NODE: strcpy(className, TX_TEXT); break;
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "UNKNOWN(%d)", classId);
            strcpy(className, buf);
            break;
        }
    }
}

static const char* getClassNameForType(int classType) {
    static char className[50];
    BorneUniverselle* instance = BorneUniverselle::getInstance();
    if (instance) {
        instance->getClasseName(classType, className);
        return className;
    }
    strcpy(className, "UNKNOWN");
    return className;
}

// Définition de findNodeSecure
template <typename T>
T* BorneUniverselle::findNodeSecure(const char* contexte, uint32_t hash, int expectedClassType) {
    if (!instance) {
        Serial.println("❌ ERREUR CRITIQUE: BorneUniverselle instance non disponible");
        return nullptr;
    }
    
    // ========================================
    // VÉRIFICATION HASH VALIDE
    // ========================================
    if (hash == 0) {
        char errorMsg[256];
        snprintf(errorMsg, sizeof(errorMsg), 
                "findNodeSecure ERREUR: Hash=0 détecté dans contexte '%s' - Vérifiez votre config.json", 
                contexte ? contexte : "NULL");
        
        Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
        Serial.printf("🚨 ATTENTION: HASH = 0 dans findNodeSecure !              🚨\n");
        Serial.printf("🚨 Context: %s                                            🚨\n", contexte ? contexte : "NULL");
        Serial.printf("🚨 Template type: %s                                      🚨\n", typeid(T).name());
        Serial.printf("🚨 Vérifiez vos #define dans le header de la classe !     🚨\n");
        Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
        
        instance->setPlcBroken(errorMsg);
        return nullptr;
    }
    
    // ========================================
    // RECHERCHE DU NŒUD
    // ========================================
    Node* node = instance->findNode(contexte, hash);
    if (!node) {
        // 🔍 MESSAGE DÉTAILLÉ POUR NŒUD NON TROUVÉ
        char errorMsg[400];
        snprintf(errorMsg, sizeof(errorMsg), 
                "findNodeSecure ERREUR: Nœud NON TROUVÉ\n"
                "┌─ Détails de recherche:\n"
                "├─ Contexte: %s\n"
                "├─ Hash recherché: %lu (0x%08lX)\n"
                "├─ Type attendu: %s (%d)\n"
                "├─ Nombre total de nœuds: %u\n"
                "└─ Template: %s", 
                contexte ? contexte : "NULL",
                (unsigned long)hash, 
                (unsigned long)hash,
                getClassNameForType(expectedClassType),
                expectedClassType,
                instance->nodesMap.size(),
                typeid(T).name());
        
        Serial.println("❌ NŒUD NON TROUVÉ - Détails:");
        Serial.printf("   Contexte: %s\n", contexte ? contexte : "NULL");
        Serial.printf("   Hash: %lu (0x%08lX)\n", (unsigned long)hash, (unsigned long)hash);
        Serial.printf("   Type attendu: %s\n", getClassNameForType(expectedClassType));
        Serial.printf("   Nœuds disponibles: %u\n", instance->nodesMap.size());
        
        instance->setPlcBroken(errorMsg);
        return nullptr;
    }
    
    // ========================================
    // VÉRIFICATION TYPE DE NŒUD
    // ========================================
    if (!instance->isCompatibleClassType(node->classType(), expectedClassType)) {
        char errorMsg[400];
        snprintf(errorMsg, sizeof(errorMsg), 
                "findNodeSecure ERREUR: Type de nœud INCOMPATIBLE\n"
                "┌─ Détails du nœud trouvé:\n"
                "├─ Nom: %s\n"
                "├─ Hash: %lu (0x%08lX)\n"
                "├─ Type trouvé: %s (%d)\n"
                "├─ Type attendu: %s (%d)\n"
                "├─ Contexte: %s\n"
                "└─ Template: %s", 
                node->getName() ? node->getName() : "UNNAMED",
                (unsigned long)hash,
                (unsigned long)hash,
                getClassNameForType(node->classType()),
                node->classType(),
                getClassNameForType(expectedClassType),
                expectedClassType,
                contexte ? contexte : "NULL",
                typeid(T).name());
        
        Serial.println("❌ TYPE DE NŒUD INCOMPATIBLE:");
        Serial.printf("   Nœud: %s\n", node->getName() ? node->getName() : "UNNAMED");
        Serial.printf("   Type trouvé: %s (%d)\n", getClassNameForType(node->classType()), node->classType());
        Serial.printf("   Type attendu: %s (%d)\n", getClassNameForType(expectedClassType), expectedClassType);
        Serial.printf("   Hash: %lu\n", (unsigned long)hash);
        
        instance->setPlcBroken(errorMsg);
        return nullptr;
    }
    
    // ✅ SUCCÈS
    return static_cast<T*>(node);
}

bool BorneUniverselle::canAcceptNewClient() {
    uint32_t now = millis();

    // Reset du compteur après une pause suffisante depuis la dernière déconnexion
    if (now - clientDisconnectionTime > 10000) {
        consecutiveConnectionAttempts = 0;
    }

    // Protection: Empêcher les reconnexions trop rapides (< 1s) sauf si pas de client actif
    if (!isClientConnected() && now - clientDisconnectionTime < 1000) {
        consecutiveConnectionAttempts++;
        Serial.printf("%lu:: [ETAPE 1] Connection attempt #%lu too soon (%lu ms)\r\n",
                     now, consecutiveConnectionAttempts, now - clientDisconnectionTime);
        if (consecutiveConnectionAttempts > 3) {
            Serial.printf("%lu:: [ETAPE 1] Too many rapid attempts, delaying\r\n", now);
            return false;
        }
    }

    // ✅ Accepter la connexion (même si isClientConnected() — le handler WS_EVT_CONNECT gère le nettoyage)
    lastConnectionAttempt = now;
    Serial.printf("%lu:: [ETAPE 1] Accepting new client (attempt #%lu)\r\n",
                 now, consecutiveConnectionAttempts);
    return true;
}

void BorneUniverselle::processClientCleanup() {
    uint32_t now = millis();
    
    // BUG FIX: Traitement du nettoyage forcé
    if (forceCleanupOnNextCycle) {
        Serial.printf("%lu:: Processing forced cleanup\r\n", now);
        executeImmediateCleanup();
        forceCleanupOnNextCycle = false;
        return;
    }
    
    // BUG FIX: Traitement du nettoyage différé
    if (isCleanupPending && (now - clientDisconnectionTime >= CLIENT_CLEANUP_DELAY)) {
        Serial.printf("%lu:: Processing delayed client cleanup\r\n", now);
        executeDelayedCleanup();
        isCleanupPending = false;
    }
    
    // BUG FIX: Détection de blocage dans les états de transition
    if (clientState == ClientState::CLEANUP_PENDING && 
        (now - lastStateChange > 10000)) { // 10 secondes max
        Serial.printf("%lu:: Cleanup timeout - forcing immediate cleanup\r\n", now);
        executeImmediateCleanup();
    }
}

const char* BorneUniverselle::getClientStateString() {
    switch (clientState) {
        case ClientState::DISCONNECTED: return "🔴 DISCONNECTED";
        case ClientState::CONNECTING: return "🟡 CONNECTING";
        case ClientState::CONNECTED: return "🟢 CONNECTED";
        case ClientState::DISCONNECTING: return "🟠 DISCONNECTING";
        case ClientState::CLEANUP_PENDING: return "🧹 CLEANUP_PENDING";
        default: return "❓ UNKNOWN";
    }
}

void BorneUniverselle::diagnosticHeartbeatState() {
    if (!isClientConnected()) return;
    
    uint32_t now = millis();
    uint32_t timeSinceLastSend = now - lastHeartbeatSend;
    uint32_t timeSinceLastReceive = now - lastHearbeatReceive;
    uint32_t timeUntilTimeout = (heartbeatTimeout > now) ? (heartbeatTimeout - now) : 0;
    
    Serial.println("\n=== DIAGNOSTIC HEARTBEAT ===");
    Serial.printf("⏰ Maintenant: %lu ms\r\n", now);
    Serial.printf("📤 Dernier envoi: il y a %lu ms\r\n", timeSinceLastSend);
    Serial.printf("📥 Dernière réception: il y a %lu ms\r\n", timeSinceLastReceive);
    Serial.printf("⏳ Timeout dans: %lu ms\r\n", timeUntilTimeout);
    Serial.printf("❌ Échecs consécutifs: %u/6\r\n", nbTimeouts);
    Serial.printf("💓 Flag reçu: %s\r\n", heartbeatReceive ? "OUI" : "NON");
    Serial.println("============================\n");
}

AsyncWebSocketClient* BorneUniverselle::getConnectedClient() {
    MutexGuard guard(webSocketMutex, "webSocketMutex", __FUNCTION__);
    if (!guard.isAcquired()) {
        return nullptr;
    }
    
    // BUG FIX: Retourner le client seulement s'il est vraiment connecté
    if (clientconnected && client && client->status() == WS_CONNECTED) {
        return client;
    }
    
    // BUG FIX: Si l'état est incohérent, le corriger
    if (clientconnected && (client == nullptr || client->status() != WS_CONNECTED)) {
        clientconnected = false;
    }
    
    return nullptr;
}

BorneUniverselle::~BorneUniverselle() {
    Serial.println("=== Destruction de BorneUniverselle === (utilisé pour les tests unitaires)");
    
    deleteNodes();
}

bool BorneUniverselle::addNode(Node* node, bool check) {
    if (!node) return false;
    
    uint32_t hash = node->getHash();
    
    // ========================================================================
    // MODE CHECK: Validation seulement, pas d'ajout à nodesMap
    // ========================================================================
    if (check) {
        // Node créé et validé avec succès
        // Le supprimer pour libérer la mémoire
        delete node;
        Serial.printf("  [check] Node %s validated and deleted (hash: %lu)\r\n", 
                     node->getName(), (unsigned long)hash);
        return true;
    }
    
    // ========================================================================
    // MODE NORMAL: Ajout à nodesMap avec vérification de collision
    // ========================================================================
    
    // Vérifier si le hash existe déjà (collision)
    auto result = nodesMap.insert(std::make_pair(hash, node));
    
    if (!result.second) {
        // L'insertion a échoué car le hash existe déjà
        char errMsg[128];
        sprintf(errMsg, "Hash collision detected for node %s, hash: %lu", 
                node->getName(), (unsigned long)hash);
        Serial.printf("ERROR: %s\r\n", errMsg);
        setPlcBroken(errMsg);
        return false;
    }
    
    // Succès
    //Serial.printf("Node %s added with hash %lu\r\n", node->getName(), (unsigned long)hash);
    return true;
}

void BorneUniverselle::deleteNodes(){
    Serial.println("=== Destruction des nodes === (utilisé pour les tests unitaires)");
    
    // Compter les nodes avant destruction
    size_t nodeCount = nodesMap.size();
    size_t psramNodes = 0;
    size_t ramNodes = 0;
    
    // Parcourir la map et détruire tous les nodes
    for (auto it = nodesMap.begin(); it != nodesMap.end(); ) {
        Node* node = it->second;
        
        if (node) {
            // Vérifier si le node est en PSRAM
            bool inPsram = esp_ptr_external_ram(node);
            if (inPsram) {
                psramNodes++;
            } else {
                ramNodes++;
            }
            
            //Serial.printf("  Destruction node: %s (hash: %lu) [%s]\n", node->getName(), node->getHash(), inPsram ? "PSRAM" : "RAM");
            
            // Utiliser l'operator delete qui appellera le destructeur
            delete node;
            
            // Effacer l'entrée de la map
            it = nodesMap.erase(it);
        } else {
            ++it;
        }
    }
    
    Serial.printf("=== %zu nodes détruits (%zu PSRAM, %zu RAM) ===\n", 
        nodeCount, psramNodes, ramNodes);
    
    // Nettoyer les autres ressources si nécessaire
    nodesMap.clear();
}

bool BorneUniverselle::tryAcquireConnectionLock(uint32_t clientId) {
    uint32_t now = millis();
    
    // Tentative d'acquisition atomique du verrou
    bool expected = false;
    if (isProcessingConnection.compare_exchange_strong(expected, true)) {
        // Verrou acquis avec succes
        lastConnectionEventTime = now;
        Serial.printf("%lu:: [ETAPE 2] Connection lock acquired for client %lu\n", now, clientId);
        return true;
    }
    
    // Le verrou est deja pris - verifier si c'est un evenement duplique
    uint32_t timeSinceLastEvent = now - lastConnectionEventTime;
    if (timeSinceLastEvent < MIN_EVENT_INTERVAL) {
        Serial.printf("%lu:: [ETAPE 2] Duplicate WS_EVT_CONNECT detected for client %lu (only %lu ms since last event)\n",
                     now, clientId, timeSinceLastEvent);
        return false;
    }
    
    // Evenement legitime mais verrou encore actif (probleme)
    Serial.printf("%lu:: [ETAPE 2] WARNING: Lock held for %lu ms, forcing release\n", 
                 now, timeSinceLastEvent);
    isProcessingConnection.store(false);
    return false;
}

void BorneUniverselle::releaseConnectionLock() {
    uint32_t now = millis();
    uint32_t lockDuration = now - lastConnectionEventTime;
    
    isProcessingConnection.store(false);
    Serial.printf("%lu:: [ETAPE 2] Connection lock released (held for %lu ms)\n", 
                 now, lockDuration);
}

bool BorneUniverselle::isConnectionEventDuplicate(uint32_t clientId) {
    uint32_t now = millis();
    uint32_t timeSinceLastEvent = now - lastConnectionEventTime;
    
    // Si le dernier evenement etait il y a moins de MIN_EVENT_INTERVAL ms
    // ET pour le meme client ID, c'est un doublon
    if (timeSinceLastEvent < MIN_EVENT_INTERVAL && lastAcceptedClientId == clientId) {
        Serial.printf("%lu:: [ETAPE 2] Duplicate event for client %lu (last event %lu ms ago)\n",
                     now, clientId, timeSinceLastEvent);
        return true;
    }
    
    return false;
}

bool BorneUniverselle::isPLCSuspended(){
    return plcSuspended;
}

bool BorneUniverselle::configNeedsRestart(JsonDocument& newConfig) {
    // Charger la configuration actuelle
    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    SafeJsonDocument currentConfig;
    
    if (!persistence.readJsonFromFile(CONFIG_PATH_FILE, currentConfig)) {
        Serial.println(F("configNeedsRestart: Cannot read current config, restart required"));
        return true;
    }
    
    // ========================================================================
    // 1. Comparer le nom du projet
    // ========================================================================
    const char* currentName = currentConfig[NAME] | "";
    const char* newName = newConfig[NAME] | "";
    if (strcmp(currentName, newName) != 0) {
        Serial.printf("configNeedsRestart: Project name changed '%s' -> '%s'\r\n", currentName, newName);
        return true;
    }
    
    // ========================================================================
    // 2. Comparer les configurations WiFi
    // ========================================================================
    JsonArray currentConfigArray = currentConfig[CONFIG].as<JsonArray>();
    JsonArray newConfigArray = newConfig[CONFIG].as<JsonArray>();
    
    // Extraire les sections WiFi
    JsonArray currentWifi, newWifi;
    for (JsonObject item : currentConfigArray) {
        if (!item[WIFI].isNull()) {
            currentWifi = item[WIFI].as<JsonArray>();
            break;
        }
    }
    for (JsonObject item : newConfigArray) {
        if (!item[WIFI].isNull()) {
            newWifi = item[WIFI].as<JsonArray>();
            break;
        }
    }
    
    // Comparer les WiFi en sérialisant (méthode simple mais efficace)
    String currentWifiStr, newWifiStr;
    serializeJson(currentWifi, currentWifiStr);
    serializeJson(newWifi, newWifiStr);
    
    if (currentWifiStr != newWifiStr) {
        Serial.println(F("configNeedsRestart: WiFi configuration changed"));
        return true;
    }
    
    // ========================================================================
    // 3. Comparer les configurations Hardware (sections avec "children")
    // ========================================================================
    for (JsonObject newItem : newConfigArray) {
        if (!newItem[CHILDREN].isNull()) {
            // C'est une section hardware, chercher la correspondante dans current
            const char* sectionName = newItem[NAME] | "";
            bool found = false;
            
            for (JsonObject currentItem : currentConfigArray) {
                const char* currentSectionName = currentItem[NAME] | "";
                if (strcmp(sectionName, currentSectionName) == 0) {
                    found = true;
                    
                    // Comparer les children
                    String currentChildrenStr, newChildrenStr;
                    serializeJson(currentItem[CHILDREN], currentChildrenStr);
                    serializeJson(newItem[CHILDREN], newChildrenStr);
                    
                    if (currentChildrenStr != newChildrenStr) {
                        Serial.printf("configNeedsRestart: Hardware section '%s' changed\r\n", sectionName);
                        return true;
                    }
                    break;
                }
            }
            
            if (!found) {
                Serial.printf("configNeedsRestart: New hardware section '%s' added\r\n", sectionName);
                return true;
            }
        }
    }
    
    // Vérifier si des sections hardware ont été supprimées
    for (JsonObject currentItem : currentConfigArray) {
        if (!currentItem[CHILDREN].isNull()) {
            const char* sectionName = currentItem[NAME] | "";
            bool found = false;
            
            for (JsonObject newItem : newConfigArray) {
                const char* newSectionName = newItem[NAME] | "";
                if (strcmp(sectionName, newSectionName) == 0) {
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                Serial.printf("configNeedsRestart: Hardware section '%s' removed\r\n", sectionName);
                return true;
            }
        }
    }
    
    // ========================================================================
    // 4. Comparer les PARAMETERS machine (lus au démarrage par BusinessLogic)
    // ========================================================================
    JsonObject currentParams, newParams;
    for (JsonObject item : currentConfigArray) {
        if (!item[PARAMETERS].isNull()) {
            currentParams = item[PARAMETERS];
            break;
        }
    }
    for (JsonObject item : newConfigArray) {
        if (!item[PARAMETERS].isNull()) {
            newParams = item[PARAMETERS];
            break;
        }
    }
    
    // Comparer les parameters en sérialisant
    String currentParamsStr, newParamsStr;
    serializeJson(currentParams, currentParamsStr);
    serializeJson(newParams, newParamsStr);
    
    if (currentParamsStr != newParamsStr) {
        Serial.println(F("configNeedsRestart: Machine parameters changed"));
        return true;
    }
    
    // ========================================================================
    // 5. Si on arrive ici, seuls des paramètres mineurs ont changé
    // ========================================================================
    Serial.println(F("configNeedsRestart: Only minor parameters changed, no restart needed"));
    return false;
}

bool BorneUniverselle::preValidateFile(const char* path, const JsonDocument& doc, String& errorMsg) {
    Serial.printf("BorneUniverselle::preValidateFile - Fichier: %s\r\n", path);
    
    // ========================================================================
    // 1. CONFIG.JSON - Validation par BorneUniverselle
    // ========================================================================
    if (strcmp(path, CONFIG_PATH_FILE) == 0) {
        return preValidateConfigJson(doc, errorMsg);
    }
    
    // ========================================================================
    // 2. AUTRES FICHIERS - Déléguer à BusinessLogic
    // ========================================================================
    if (businessLogic) {
        return businessLogic->validateConfigFile(path, doc, errorMsg);
    }
    
    // Pas de BusinessLogic ou fichier non reconnu : accepter par défaut
    return true;
}

bool BorneUniverselle::preValidateConfigJson(const JsonDocument& doc, String& errorMsg) {
    Serial.println("BorneUniverselle::preValidateConfigJson - Début validation");
    
    // Créer une copie temporaire pour parseConfig
    SafeJsonDocument docCopy;
    docCopy.set(doc);
    
    // Valider avec parseConfig (check=true)
    if (!parseConfig(docCopy, true)) {
        errorMsg = "config.json validation failed - parseConfig returned false";
        Serial.printf("❌ %s\r\n", errorMsg.c_str());
        return false;
    }
    
    // ========================================================================
    // DÉTECTER SI REDÉMARRAGE NÉCESSAIRE (AVANT sauvegarde)
    // ========================================================================
    if (configNeedsRestart(docCopy)) {
        Serial.println("⚠️ Critical changes detected - restart will be required after save");
        // Stocker l'info pour la callback POST-sauvegarde
        configNeedsRestartAfterSave = true;
    } else {
        Serial.println("ℹ️ Only minor changes - hot-reload will be possible");
        configNeedsRestartAfterSave = false;
    }
    
    Serial.println("✅ config.json validation successful");
    return true;
}

void BorneUniverselle::reloadMinorConfigParameters(JsonDocument& newConfig) {
    Serial.println("========================================");
    Serial.println("reloadMinorConfigParameters: Début du rechargement à chaud");
    PLC_Tools::logDiagnostic("reloadMinorConfigParameters: Début du rechargement à chaud", false);
    Serial.println("========================================");
    
    JsonArray configArray = newConfig[CONFIG].as<JsonArray>();
    if (configArray.isNull()) {
        Serial.println("❌ reloadMinorConfigParameters: Pas de section CONFIG trouvée");
        return;
    }
    
    // ========================================================================
    // PARCOURIR LES SECTIONS DU CONFIG
    // ========================================================================
    for (JsonObject item : configArray) {
        const char* sectionName = item[NAME] | "unknown";
        Serial.printf("  Traitement section: %s\r\n", sectionName);
        
        // ====================================================================
        // 3. RECHARGER LES PARAMÈTRES DE LOGGING/DEBUG
        // ====================================================================
        if (!item["logLevel"].isNull()) {
            const char* newLogLevel = item["logLevel"];
            Serial.printf("    - Log level: %s\r\n", newLogLevel);
            // PLC_Tools::logParamChange("logLevel", oldLogLevel, newLogLevel);
        }
    }
    
    Serial.println("========================================");
    Serial.println("✅ Rechargement à chaud terminé avec succès");
    PLC_Tools::logDiagnostic("Rechargement à chaud terminé avec succès", false);
    Serial.println("========================================");
} // reloadMinorConfigParameters

void BorneUniverselle::updateSuspendState() {
    bool shouldBeSuspended = isAnySuspendActive();
    bool wasAlreadySuspended = plcSuspended;
    
    plcSuspended = shouldBeSuspended;
    
    // ===== TRANSITION : PAS SUSPENDU → SUSPENDU =====
    if (shouldBeSuspended && !wasAlreadySuspended) {
        Serial.printf("%lu:: 🔴 PLC SUSPENSION ACTIVATED (JS=%d, PLC=%d)\r\n", 
                      millis(), suspendedByJavaScript, suspendedByPLC);
        suspensionStartTime = millis();
        suspendPLCServices();  // ✅ Utilise l'existant
    }
    
    // ===== TRANSITION : SUSPENDU → PAS SUSPENDU =====
    else if (!shouldBeSuspended && wasAlreadySuspended) {
        uint32_t duration = millis() - suspensionStartTime;
        Serial.printf("%lu:: 🟢 PLC SUSPENSION RELEASED (duration: %lu ms)\r\n", 
                      millis(), duration);
        resumePLCServices();  // ✅ Utilise l'existant
    }
    
    // ===== PAS DE TRANSITION =====
    else if (shouldBeSuspended && wasAlreadySuspended) {
        Serial.printf("%lu:: 🟡 PLC remains suspended (JS=%d, PLC=%d)\r\n", 
                      millis(), suspendedByJavaScript, suspendedByPLC);
    }
}

bool BorneUniverselle::suspendForInternalOperation(const char* reason) {
    if (!isClientConnected()) {
        Serial.println("Do not suspend PLC: client is not connected\r\n");
        suspendCount = 0; // Reset du compteur pour éviter incohérences
        return true;
    }

    suspendCount++;
    
    if (suspendCount == 1) {
        MyModbus::getInstance().waitUntilFree(50);  // ← attendre bus libre
        // Première suspension → vraie suspension
        Serial.printf("%lu:: 🔧 PLC requests suspend for: %s\r\n", millis(), reason);
        suspendedByPLC = true;  // Flag pour traçage
        updateSuspendState();
        sendPlcSuspendNotification(true, reason);
    } else {
        // Suspension imbriquée (normal, pour debug)
        Serial.printf("%lu:: 🔧 PLC nested suspend (count=%d) for: %s\r\n", millis(), suspendCount, reason);
    }
    
    return true;
}

bool BorneUniverselle::resumeFromInternalOperation() {
    if (!isClientConnected()) {
        Serial.printf("%lu:: ⚠️ Do not resume suspended PLC: client is not connected\r\n", millis());
        suspendCount = 0; // Reset du compteur pour éviter incohérences
        return true;
    }


    // Décrémenter le compteur (avec protection contre underflow)
    if (suspendCount > 0) {
        suspendCount--;
    } else {
        Serial.printf("%lu:: ⚠️ PLC resume called but suspendCount already 0\r\n", millis());
        return false;
    }
    
    // Si compteur == 0, on peut vraiment reprendre
    if (suspendCount == 0) {
        Serial.printf("%lu:: 🔧 PLC releases suspend\r\n", millis());
        suspendedByPLC = false;  // Flag pour traçage
        updateSuspendState();
        sendPlcSuspendNotification(false, "operation completed");
        return true;
    } else {
        // Reprise imbriquée (normal, pour debug)
        Serial.printf("%lu:: 🔧 PLC nested resume (count=%d remaining)\r\n", millis(), suspendCount);
        return true;
    }
}

void BorneUniverselle::sendPlcSuspendNotification(bool suspended, const char* reason) {
    JsonDocument doc;
    doc[PLC_INTERNAL_SUSPEND] = suspended;
    
    char chain[128];
    serializeJson(doc, chain, sizeof(chain));
    sendTextToClient(chain);

    //snprintf(chain, sizeof(chain), "PLC internal suspend notification, status %s, reason %s", suspended ? "SUSPENDED" : "RESUMED", reason ? reason : "none");
    //prepareMessage(DEBUG, chain);
}

void BorneUniverselle::sendFileSavedNotification(bool success, const char* path, const char *reason) {
    JsonDocument doc;
    
    // Créer l'objet fileSaved
    JsonObject fileSaved = doc[FILE_SAVED].to<JsonObject>();
    fileSaved["success"] = success;
    
    // Optionnel : ajouter le path pour debug/info
    if (path) {
        fileSaved[PATH] = path;
    }

    if (reason) {
        fileSaved[REASON] = reason;
    }
    
    char chain[400];
    serializeJson(doc, chain, sizeof(chain));
    sendTextToClient(chain);
    
    Serial.printf("%lu:: 📤 Sent file saved notification: success=%s, path=%s\r\n", millis(), success ? "true" : "false", path ? path : "unknown");
}

void BorneUniverselle::sendFileDeleteNotification(bool result, const char* path, const char* reason) {
    JsonDocument doc;
    
    // Créer l'objet fileDeleted
    JsonObject fileDeleted = doc[FILE_DELETED].to<JsonObject>();
    fileDeleted["success"] = result;
    
    // Optionnel : ajouter le path pour debug/info
    if (path) {
        fileDeleted[PATH] = path;
    }

    if (reason && !result) {
        fileDeleted[REASON] = reason;
    }
    
    char chain[400];
    serializeJson(doc, chain, sizeof(chain));
    sendTextToClient(chain);
    
    Serial.printf("%lu:: 📤 Sent file delete notification: success=%s, path=%s\r\n", millis(), result ? "true" : "false", path ? path : "unknown");
}

bool BorneUniverselle::handleUnixClockUpdateRequest(JsonDocument& socketDoc) {
    Serial.println("BorneUniverselle::handleUnixClockUpdateRequest - Updating RTC from client request (unix time)");
    
    // Vérifications
    if (socketDoc[CURRENT_UNIX_TIME].isNull()) {
        Serial.println("❌ Erreur: clé CURRENT_TIME_UNIX absente");
        return false;
    }
    
    // Récupération timestamp JavaScript (millisecondes)
    uint64_t unixTimeMs = socketDoc[CURRENT_UNIX_TIME].as<uint64_t>();
    Serial.printf("⏰ Timestamp JavaScript (ms): %llu\r\n", unixTimeMs);
    
    return true;
}

bool BorneUniverselle::handleLocalClockUpdateRequest(JsonDocument& socketDoc) {
    Serial.println("BorneUniverselle::handleLocalClockUpdateRequest - Updating RTC from client request (local time)");
    
    // Vérifications
    if (socketDoc[CURRENT_LOCAL_TIME].isNull()) {
        Serial.println("❌ Erreur: clé CURRENT_LOCAL_TIME absente");
        return false;
    }
     
    // Récupération timestamp JavaScript (ISO 8601)
    const char* iso8601_str = socketDoc[CURRENT_LOCAL_TIME].as<const char*>();
    Serial.printf("⏰ Timestamp ISO 8601 reçu: %s\n", iso8601_str); 
    
    // Mise à jour du RTC via RTC_Service
    bool success = RTC_Service::getInstance()->setTimeFromISO8601(String(iso8601_str));
    
    if (success) {
        PLC_Tools::logDiagnostic("Horloge locale mis à jour avec succès", true);
        // Optionnel : relire l'heure pour confirmation
        char currentTime[128];
        RTC_Service::getInstance()->getTimestamp(currentTime, sizeof(currentTime));
        Serial.printf("🕐 Heure RTC après mise à jour: %s\n", currentTime);
    } else {
       PLC_Tools::logDiagnostic("Échec de mise à jour de l'horloge locale", true);
    }
    
    return success;
}

void BorneUniverselle::clockUpdateRequest(){
    Serial.println("⏰ Synchronisation de l'horloge PLC avec l'heure du java script");  
    JsonDocument doc;
    doc[GET_LOCAL_TIME] = true;
    
    char chain[128];
    serializeJson(doc, chain, sizeof(chain));
    sendTextToClient(chain); 
    Serial.println("✓ Commande de synchronisation envoyée");
}
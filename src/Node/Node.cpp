#include "Node/Node.h"
#include "BorneUniverselle/BorneUniverselle.h"
#include "Pca9554Driver/Pca9554Driver.h"
#include "IoExpander/IoExpander.h"
#include "driver/gpio.h"

// Les PCF8574 (et autres expandeurs I2C) sont partagés par adresse via IoExpanderManager.
static IoExpander* getLegacyPcf8574(uint8_t i2cAddr) {
    IoExpander* expander = IoExpanderManager::getOrCreate("PCF8574", i2cAddr);
    if (expander == nullptr) {
        BorneUniverselle::getInstance()->setPlcBroken(PFC_INIT_ERROR);
    }
    return expander;
}

// Gestionnaire centralisé de PCA9554 par adresse I2C
class PCA9554Manager {
private:
    static std::map<uint8_t, Pca9554Driver*> instances;

public:
    static Pca9554Driver* getOrCreate(uint8_t i2cAddr) {
        auto it = instances.find(i2cAddr);

        if (it != instances.end()) {
            Serial.printf("ℹ️  PCA9554 at 0x%02X already initialized, reusing\n", i2cAddr);
            return it->second;
        }

        Serial.printf("🔧 Initializing PCA9554 at 0x%02X...\n", i2cAddr);
        Pca9554Driver* newDriver = new Pca9554Driver(i2cAddr);

        if (!newDriver->begin()) {
            Serial.printf("❌ PCA9554 initialization FAILED at 0x%02X\n", i2cAddr);
            delete newDriver;
            BorneUniverselle::getInstance()->setPlcBroken(PCA9554_INIT_ERROR);
            return nullptr;
        }

        Serial.printf("✅ PCA9554 initialized successfully at 0x%02X\n", i2cAddr);
        instances[i2cAddr] = newDriver;
        return newDriver;
    }
};

std::map<uint8_t, Pca9554Driver*> PCA9554Manager::instances;

Node::Node(char *name, char *parentName, uint16_t id,  uint32_t _hash, uint16_t webRefreshInterval){
    //Serial.println("constructeur Node");
    setName(name, parentName);
    lastChange = millis();
    char text[256];
    sprintf(text, "%d%s", id, parentName);
    hash = (~crc32_le((uint32_t)~(0xffffffff), (const uint8_t*)text, strlen(text)))^0xffffffFF;
    if (_hash != hash){
        if (_hash != 0) {
            // Hash différent ET non-nul dans le fichier config = ERREUR CRITIQUE
            Serial.println("\033[1;31m"); // Rouge gras
            Serial.println("╔═══════════════════════════════════════════════════════════════╗");
            Serial.println("║                    PLC CONFIGURATION ERROR                    ║");
            Serial.println("╠═══════════════════════════════════════════════════════════════╣");
            Serial.printf("║ Node: %-54s ║\n", name);
            Serial.printf("║ Parent: %-52s ║\n", parentName);
            Serial.printf("║ ID: %-56d ║\n", id);
            Serial.println("╠═══════════════════════════════════════════════════════════════╣");
            Serial.printf("║ Hash calculé: %lu (attendu)                          ║\n", (unsigned long)hash);
            Serial.printf("║ Hash fichier: %lu (reçu)                             ║\n", (unsigned long)_hash);
            Serial.println("╠═══════════════════════════════════════════════════════════════╣");
            Serial.println("║ CAUSE: Hash incohérent dans le fichier de configuration      ║");
            Serial.println("║ ACTION: Vérifier les hash codes dans le fichier JSON         ║");
            Serial.println("╚═══════════════════════════════════════════════════════════════╝");
            Serial.println("\033[0m"); // Reset couleur
            
            // PLC broken
            char errorMsg[128];
            snprintf(errorMsg, sizeof(errorMsg), "Node hash error: '%s' (ID:%d), mismatch between config and calculated hash", name, id);
            BorneUniverselle::getInstance()->setPlcBroken(errorMsg);
            
        } else {
            // Hash à 0 dans le fichier = mise à jour normale (comportement actuel)
            Serial.printf("CRC32 updated. Text: %s, new hash: %lu, old %lu\r\n", 
                         text, (unsigned long)hash, (unsigned long)_hash);
        }
    }
    setWebUpdateInterval(webRefreshInterval);
    mode = DEFAULT_MODE;
}

bool Node::setName(const char *_name, const char *_parentName){
    if ((strlen(_name) + strlen(_parentName))< NAME_LENGHT){
        if (_name == nullptr || _parentName == nullptr){
           char buffer[256];
           snprintf(buffer, sizeof(buffer), "Node::setName name (or parent name) is null for node with hash: %lu", hash);
           BorneUniverselle::getInstance()->setPlcBroken(buffer);
        return false;
        }
        
        sprintf(name, "%s/%s", _parentName, _name);
    } else {
        char mess[1024];
        sprintf(mess,  "Node::setName name (or parent name) too long %s/%s", _parentName, _name);
        BorneUniverselle::getInstance()->setPlcBroken(mess);
        return false;
    } 

    return true;
}

char * Node::getName() {
    // Toujours retourner une chaîne valide
    if (name[0] == '\0') {
        static char defaultName[] = "[UNNAMED]";
        return defaultName;
    }
    return name;
}

void Node::setShowMessages(bool _showMessage){
    Serial.printf("Node:: show messages: %s for node: %s\r\n", _showMessage ? "true" : "false", getName());
    isShowMessage = _showMessage;
}

uint32_t Node::getHash(){
    if(isnan(hash)){
        Serial.printf("WARNING ! the node: %s is not initialised !!!!!!\r\n", name);
        BorneUniverselle::getInstance()->setPlcBroken(HASH_NOT_FOUND);
        return 0;
    }
    return hash;
}

uint32_t Node::getLastChange(){
    return lastChange;
}

uint32_t Node::getLastRefresh() {
     return lastRefresh; 
}

void Node::setChangeEvent(){
    lastChange = millis();
}

bool Node::getIsChanged(){
    return updateNeeded;
}

void Node::forceIsChanged(){
    updateNeeded = true;
}  

void Node::clearIsChanged(){
    updateNeeded = false;
}

uint8_t Node::getMode(){
    if (mode == DEFAULT_MODE){
        Serial.println("WARNING MODE WAS NOT SETTED !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    }
    return mode;
}

uint32_t Node::getWebUpdateInterval(){
    return webUpdateInterval;
}

uint32_t Node::getLastWebUpdate(){
    return lastWebUpdate;
}

void Node::setLastWebUpdate(uint32_t lastUpdate){
    lastWebUpdate = lastUpdate;
}

void Node::setWebUpdateInterval(uint16_t _interval){
    webUpdateInterval = _interval;
    // Serial.printf("Web refresh interval for node %s is %u\r\n", getName(), webUpdateInterval);
}

void Node::showMessage(const char *text){
    if (isShowMessage){
        Serial.printf("%lu:: %s\r\n", millis(), text);
    }
}  

bool Node::refresh(){
    uint32_t startTime = millis();

    bool isModbusNode = (classType() >= FIRST_MODBUS_CLASS && classType() <= LAST_MODBUS_CLASS);
    uint8_t mbAddr = getModbusAddress();  // 0 si non-Modbus (via virtuelle dans Node)

    if (tempDisabled) {
        if (millis() - disabledTime >= disableDuration) {
            if (isModbusNode && mbAddr > 0) {
                bool slaveDead = MyModbus::getInstance().isSlaveConsideredDead(mbAddr);
                if (MyModbus::getInstance().isModbusDead() || slaveDead) {
                    disabledTime = millis();
                    return true;
                }
            }
            tempDisabled = false;
            Serial.printf("%lu:: Reactivation du noeud %s apres %lu ms\n",
                (long unsigned int)millis(), getName(), (long unsigned int)disableDuration);
            consecutiveErrors = 1;
        } else {
            return true;
        }
    }

    // Rafraichissement normal
    bool success = specificRefresh();

    if (!success) {
        char buff[256];
        consecutiveErrors++;
        snprintf(buff, 256, "%lu:: Noeud %s: erreur %d consecutive(s)\n", millis(), getName(), consecutiveErrors);
        showMessage(buff);

        if (consecutiveErrors >= 10 && !isModbusNode) {
            tempDisabled = true;
            disabledTime = millis();
            disableDuration = min(disableDuration * 2, maxDisableDuration);
            snprintf(buff, 256, "Desactivation temporaire du noeud %s pour %lu ms\n",
                getName(), (long unsigned int)disableDuration);
            BorneUniverselle::prepareMessage(WARNING, buff);
        }
    } else {
        if (consecutiveErrors > 0) consecutiveErrors--;
        if (consecutiveErrors == 0 && disableDuration > 5000) disableDuration /= 2;
        setLastRefresh(millis());
    }

    if (millis() - startTime > 100) {
        uint32_t elapsed = millis() - startTime;
        if (mbAddr > 0) {
            Serial.printf("⏱️ Modbus lent addr:%u %s %lums %s\r\n",
                mbAddr, name, (unsigned long)elapsed, success ? "OK" : "FAIL");
        } else {
            Serial.printf("⏱️ Refresh lent: %s %lums\r\n", name, (unsigned long)elapsed);
        }
    }

    return success;
}

void Node::setLastRefresh(uint32_t lastUpdate){
    lastRefresh = lastUpdate;
}

void Node::setMode(uint8_t _mode){
    //Serial.printf("Node::setMode for node: %s\r\n", getName());
    mode = _mode;
    char text[64];
    switch (mode){
        case DEFAULT_MODE: strcpy(text, "DEFAULT_MODE !!!!");
        break;

        case INPUT_MODE: strcpy(text, "INPUT_MODE");
        break;

        case OUTPUT_MODE: strcpy(text, "OUTPUT_MODE");
        break;

        default: strcpy(text, "NOT SETTED !!!!");
        break;
    }
    //Serial.printf("Node %s is a node: %s\r\n", getName(), text);
}

InputNode:: InputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t refreshInterval, uint16_t webRefreshInterval): Node(name, parentName, id, hash, webRefreshInterval){
    //Serial.println("Constructeur de la classe InputNode");
    setMode(INPUT_MODE);
    setRefreshInterval(refreshInterval);
}

void InputNode::setRefreshInterval(uint16_t _interval){
    refreshInterval = _interval;
    //Serial.printf("InputNode::Refresh interval for node %s is %u\r\n", getName(), refreshInterval);
}

uint32_t InputNode::getRefreshInterval(){
    return refreshInterval;
}

OutputNode:: OutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval): Node(name, parentName, id, hash, webRefreshInterval){
    // Serial.printf("Constructeur de la classe OutputNode for node: %s\r\n", getName());
    setMode(OUTPUT_MODE);
    // Serial.printf("End of constructeur de la classe OutputNode for node: %s, mode: %u\r\n", getName(), getMode());
}

BooleanInputNode:: BooleanInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, bool inverted, uint16_t refreshInterval, uint16_t webRefreshInterval): InputNode(name, parentName, id, hash, refreshInterval, webRefreshInterval){
    inputInverted = inverted;
    /*
    if (inverted){
        Serial.printf("Node %s is inverted\r\n", getName());
    }
    */
}

bool BooleanInputNode::specificRefresh(){
    showMessage("specificRefresh for BooleanInputNode");
    bool tempVal;
    bool succes = getNewValue(tempVal);

    if (!succes){
        updateNeeded = false;
        Serial.printf("BooleanInputNode::specificRefresh: getNewValue failed for node: %s\r\n", getName());
        return false;
    }

    bool val = inputInverted ? !tempVal : tempVal;

    if (val != hideValue){
        hideValue = val;
        updateNeeded = true;
        lastChange = millis();
        if (val){
            lastUp = lastChange;
        } else {
            lastDown = lastChange;
        } 
        Serial.printf("%lu:: node: %s, new value: %s\r\n", millis(), name, hideValue ? "true" : "false");   
    }
    return true;
}

bool BooleanInputNode::getValue(){
    return hideValue;
}

uint32_t BooleanInputNode::getLastDown(){
    return lastDown;
}

uint32_t BooleanInputNode::getLastUp(){
    return lastUp;
}

bool BooleanInputNode::isRisingEdge(){
    return hideValue && getIsChanged();
}

bool BooleanInputNode::isFallingEdge(){
    return !hideValue && getIsChanged();
}

bool BooleanInputNode::isUpFrom(uint32_t time){
    return hideValue && (millis() - lastChange > time);
}

bool BooleanInputNode::isDownFrom(uint32_t time){
    return !hideValue && (millis() - lastChange > time);
}  

BooleanOutputNode::BooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval): OutputNode(name, parentName, id, hash, webRefreshInterval){
    // Serial.printf("Constructeur de la classe BooleanOutputNode for node %s\r\n", getName());
}

bool BooleanOutputNode::specificRefresh(){
    showMessage("specificRefresh for BooleanOutputNode");
    if (updateNeeded){
        if (!setNewValue(hideValue)){
            return false;
        }

        _notifyPending = true;
        lastChange = millis();
        updateNeeded = false; 
        if (hideValue){
            lastUp = lastChange;
        } else {
            lastDown = lastChange;
        }

        Serial.printf("%lu:: OutputNode::node: %s, new value: %s\r\n", millis(),  name, hideValue ? "up": "down");   
    }
    return true;
}

bool BooleanOutputNode::getValue(){
    return hideValue;
}

void BooleanOutputNode::setValue(bool _value, bool force){
    if (force || _value != hideValue){
        // Serial.printf("setValue: will update value for node %s, new value: %s, hash: %lu\r\n", getName(), _value ? "true" : "false", (unsigned long)getHash());
        updateNeeded = true;
        hideValue = _value;
    }
}

uint32_t BooleanOutputNode::getLastDown(){
    return lastDown;
}

uint32_t BooleanOutputNode::getLastUp(){
    return lastUp;
}


Uint16InputNode::Uint16InputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t refreshInterval, uint16_t webRefreshInterval): InputNode(name, parentName, id, hash, refreshInterval, webRefreshInterval){
}

uint16_t Uint16InputNode::getValue(){
    return hideValue;
}

bool Uint16InputNode::specificRefresh(){
    showMessage("specificRefresh for Uint16InputNode");

    uint16_t val = hideValue;
    bool success = getNewValue(val);

    if (!success){
        updateNeeded = false;
        return false;
    }
    
    if (val != hideValue){
        hideValue = val;
        updateNeeded = true;
        lastChange = millis();
        Serial.printf("%lu:: node: %s, new value: %u\r\n", millis(),  name, hideValue);   
    }
    return true;
}

Uint32InputNode::Uint32InputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t refreshInterval, uint16_t webRefreshInterval): InputNode(name, parentName, id, hash, refreshInterval, webRefreshInterval){
}

uint32_t Uint32InputNode::getValue(){
    return hideValue;
}

bool Uint32InputNode::specificRefresh(){
    showMessage("specificRefresh for Uint32InputNode");
    uint32_t val = hideValue;
    bool succes = getNewValue(val);
    if (!succes){
        updateNeeded = false;
        return false;
    }

    if (val != hideValue){
        hideValue = val;
        updateNeeded = true;
        lastChange = millis();
        Serial.printf("%lu:: node: %s, new value: %lu\r\n", millis(),  name, (unsigned long)hideValue);   
    }
    return true;
}

FloatInputNode::FloatInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t refreshInterval, uint16_t webRefreshInterval): InputNode(name, parentName, id, hash, refreshInterval, webRefreshInterval){
}

float FloatInputNode::getValue(){
    return hideValue;
}

bool FloatInputNode::specificRefresh(){
    showMessage("specificRefresh for FloatInputNode");
    float val = hideValue;
    bool succes = getNewValue(val);

    if (!succes){
        updateNeeded = false;
        return false;
    }

    if (val != hideValue){
        hideValue = val;
        updateNeeded = true;
        lastChange = millis();
        Serial.printf("%lu:: refresh FloatInputNode: %s, new value: %f\r\n", millis(),  name, hideValue);   
    }
    return true;
}

Uint16OutputNode::Uint16OutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval): OutputNode(name, parentName, id, hash, webRefreshInterval){
}

bool Uint16OutputNode::specificRefresh(){
    showMessage("specificRefresh for Uint16OutputNode");
    if (updateNeeded){
        if (!setNewValue(hideValue)){
            return false;
        }

        _notifyPending = true;
        lastChange = millis();
        updateNeeded = false;
        Serial.printf("%lu:: Uint16OutputNode::node: %s, new value: %u\r\n", millis(),  name, hideValue);  
    } 
    return true;
}

uint16_t Uint16OutputNode::getValue(){
    return hideValue;
}

void Uint16OutputNode::setValue(uint16_t _value, bool force){
    Serial.printf("%lu::setValue for node: %s, with value: %u\r\n", millis(), getName(), _value);
    if (force || _value != hideValue){
        updateNeeded = true;
        hideValue = _value;
    }
}

Uint32OutputNode::Uint32OutputNode(char *name, char *parentName, uint16_t id, uint32_t hash,  uint16_t webRefreshInterval): OutputNode(name, parentName, id, hash,  webRefreshInterval){
}

bool Uint32OutputNode::specificRefresh(){
    showMessage("specificRefresh for Uint32OutputNode");
    if (updateNeeded){
        if (!setNewValue(hideValue)){
           return false;
        }

        _notifyPending = true;
        lastChange = millis();
        updateNeeded = false;
        Serial.printf("%lu:: Uint32OutputNode::node: %s, new value: %lu\r\n", millis(),  name, (unsigned long)hideValue);     
    }
    return true;
}

void Uint32OutputNode::setValue(uint32_t _value){
    char *name_ = getName(); 
    if (name_== nullptr || name_[0] == '\0') {
        char buff[256];
        sprintf(buff, "Uint32OutputNode::setValue: getName is NULL\r\n");
        BorneUniverselle::getInstance()->setPlcBroken(buff);
        return;
    }

    Serial.printf("%lu:: setValue for node: %s: %lu\r\n", millis(), getName(), (long unsigned int)_value);
    if (_value != hideValue){
        updateNeeded = true;
        hideValue = _value;
    }
}

uint32_t Uint32OutputNode::getValue(){
    return hideValue;
}

FloatOutputNode::FloatOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash,  uint16_t webRefreshInterval): OutputNode(name, parentName, id, hash,  webRefreshInterval){
}

bool FloatOutputNode::specificRefresh(){
    showMessage("specificRefresh for FloatOutputNode");
    if (updateNeeded){
        if (!setNewValue(hideValue)){
           return false;
        }

        _notifyPending = true;
        lastChange = millis();
        updateNeeded = false;
        // Serial.printf("%lu:: refresh (clear updateNeeded) FloatOutputNode::node: %s, new value: %f\r\n", millis(),  name, hideValue);     
    }
    return true;
}

float FloatOutputNode::getValue(){
    return hideValue;
}

void FloatOutputNode::setValue(float _value){
    Serial.printf("%lu::setValue for FloatOutputNode: %s, with value: %.2f\r\n", (unsigned long) millis(), getName(), _value);
    // 🔧 DEBUG: Afficher hideValue et la condition
    Serial.printf("  🔍 DEBUG: hideValue=%.2f, _value=%.2f, equal=%d\r\n", hideValue, _value, (_value == hideValue));

   if (_value != hideValue){
        updateNeeded = true;
        hideValue = _value;
        Serial.printf("  ✅ updateNeeded = TRUE\r\n");  // ← CE LOG MANQUE !
    } else {
        Serial.printf("  ❌ SKIP: valeurs identiques\r\n");
    }
}
TextInputNode::TextInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, 
                             uint16_t refreshInterval, uint16_t webRefreshInterval)  // 6 paramètres - DOIT CORRESPONDRE
    : InputNode(name, parentName, id, hash, refreshInterval, webRefreshInterval) {
    
    hideValue = (char*)PLC_Tools::allocateMemory(MAX_TEXT_SIZE, MEMORY_PREFER_PSRAM);
    tempBuffer = (char*)PLC_Tools::allocateMemory(MAX_TEXT_SIZE, MEMORY_PREFER_PSRAM);
}

TextInputNode::~TextInputNode() {
    if (hideValue) {
        PLC_Tools::freeMemory(hideValue);  // ✅ Fonctionne pour PSRAM et RAM
        hideValue = nullptr;
    }
    if (tempBuffer) {
        PLC_Tools::freeMemory(tempBuffer);
        tempBuffer = nullptr;
    }
}

char * TextInputNode::getValue() {
    // ⚠️ SI hideValue est null, PLC_Tools l'aura déjà signalé lors de l'allocation
    return hideValue ? hideValue : (char*)"";
}

bool TextInputNode::specificRefresh() {
    if (!hideValue) return false;

    if (!tempBuffer) return false;  // ✅ Sécurité
    
    showMessage("specificRefresh for TextInputNode");
    
    strcpy(tempBuffer, hideValue);
    bool succes = getNewValue(tempBuffer);
    
    if (!succes) {
        updateNeeded = false;
        return false;
    }

    if (strcmp(tempBuffer, hideValue)) {
        Serial.println("Refresh: value is different");
        strcpy(hideValue, tempBuffer);
        updateNeeded = true;
        lastChange = millis();
        Serial.printf("%lu:: node: %s, new value: %s\r\n", millis(), name, hideValue);   
    }
    
    return true;
}

TextOutputNode::TextOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval): OutputNode(name, parentName, id, hash, webRefreshInterval) { 
    hideValue = (char*)PLC_Tools::allocateMemory(MAX_TEXT_SIZE, MEMORY_PREFER_PSRAM);
}

TextOutputNode::~TextOutputNode() {
    if (hideValue) {
        PLC_Tools::freeMemory(hideValue);
        hideValue = nullptr;
    }
}

bool TextOutputNode::specificRefresh() {
    if (!hideValue) return false; // Échec silencieux
    
    showMessage("specificRefresh for TextOutputNode");
    if (updateNeeded) {
        if (!setNewValue(hideValue)) {
           return false;
        }

        _notifyPending = true;
        lastChange = millis();
        updateNeeded = false;
    }
    return true;
}

char * TextOutputNode::getValue() {
    // ⚠️ SI hideValue est null, PLC_Tools l'aura déjà signalé lors de l'allocation
    return hideValue ? hideValue : (char*)"";
}

void TextOutputNode::setValue(const char * _value) {
    const char* safeName = getName() ? getName() : "[UNNAMED]";
    
    if (!hideValue) {
        Serial.printf("ERROR: TextOutputNode::setValue() - hideValue is null for node %s\r\n", safeName);
        return;
    }
    
    if (!_value) {
        Serial.printf("ERROR: setValue called with null value for node: %s\r\n", safeName);
        return;
    }
    
    if (strlen(_value) >= MAX_TEXT_SIZE) {
        Serial.printf("ERROR: setValue value too long for node: %s, ignoring\r\n", safeName);
        return;
    }
    
    // Logging sécurisé
    Serial.printf("%lu:: TextOutputNode: setValue for node: %s, with value: %s\r\n", millis(), safeName, _value);
    
    // Logique métier sécurisée
    if (strcmp(_value, hideValue) != 0) {
        updateNeeded = true;
        strcpy(hideValue, _value);
    }
}

// GPIO déjà préparés en sortie : les nœuds sont recréés lors de la vérification d'une nouvelle
// configuration et ne doivent pas remettre au repos une sortie en service.
static uint64_t hardwareOutputsReady = 0;

HardwareBooleanInputNode::HardwareBooleanInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint8_t _pin, bool _inputInverted, uint16_t refreshInterval, uint16_t webRefreshInterval,
                                                   bool _activeLow, uint8_t pullMode): BooleanInputNode(name, parentName, id, hash, _inputInverted, refreshInterval, webRefreshInterval){
    //Serial.println("Constructeur de HardwareBoolanInputNode");
    pin = _pin;
    activeLow = _activeLow;
    Serial.printf("Pin %d will be set as input (%s, active %s)\r\n", pin,
                  pullMode == INPUT_PULLUP ? "pull-up" : (pullMode == INPUT_PULLDOWN ? "pull-down" : "no pull"), activeLow ? "low" : "high");
    pinMode(pin, pullMode);
    if (pin < 64) {
        hardwareOutputsReady &= ~(1ULL << pin);
    }
    //Serial.println("Fin du constructeur HardwareBooleanInputNode");
}

bool HardwareBooleanInputNode::getNewValue(bool& value){
    char text[256];
    sprintf(text, "HardwareBooleanInputNode::getNewValue() for node: %s, pin: %u, inverted: %s", name, pin, inputInverted ? "true" : "false");
    showMessage(text);
    bool level = digitalRead(pin);
    value = activeLow ? !level : level;
    return true; // Pas d'erreur possible....
}

bool PF8574BooleanInputNode::interruptFlag = false;
long PF8574BooleanInputNode::timeOfInterrupt;
int8_t PF8574BooleanInputNode::interruptPin = -1;

PF8574BooleanInputNode::PF8574BooleanInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint8_t  i2cAddr, uint8_t _pin, bool _inputInverted, uint16_t refreshInterval, uint16_t webRefreshInterval)
    : PF8574BooleanInputNode(name, parentName, id, hash, getLegacyPcf8574(i2cAddr), _pin, true, _inputInverted, refreshInterval, webRefreshInterval) {
    // Historique : PCF8574 de la KinCony A8S, entrées actives à l'état bas.
}

PF8574BooleanInputNode::PF8574BooleanInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, IoExpander *_expander, uint8_t _pin, bool _activeLow, bool _inputInverted, uint16_t refreshInterval, uint16_t webRefreshInterval)
    : BooleanInputNode(name, parentName, id, hash, _inputInverted, refreshInterval, webRefreshInterval){
    pin = _pin;
    activeLow = _activeLow;
    expander = _expander;

    if (expander == nullptr) {
        Serial.printf("❌ No I/O expander for input node %s\n", name);
        return;
    }
    if (!expander->setupInput(pin)) {
        Serial.printf("❌ Unable to configure pin %u of %s at 0x%02X as input for node %s\n", pin, expander->chipName(), expander->address(), name);
    }

    if (refreshInterval != 0){
        Serial.printf("PF8574BooleanInputNode::RefreshInterval for node %s is %u\r\n", getName(), refreshInterval);
    }
}

void PF8574BooleanInputNode::attachInterruptPin(int8_t gpio){
    if (gpio < 0 || gpio == interruptPin) {
        return;
    }
    if (interruptPin >= 0) {
        detachInterrupt(interruptPin);
    }
    interruptPin = gpio;
    pinMode(gpio, INPUT_PULLUP);  // sortie INT des expandeurs : drain ouvert
    attachInterrupt(gpio, PF8574BooleanInputNode::interruptHandler, FALLING);
    Serial.printf("I/O expander interrupt attached on GPIO %d\r\n", gpio);
}

void IRAM_ATTR PF8574BooleanInputNode::interruptHandler(){
    //Serial.println("PF8574BooleanInputNode::interruptHandler called");
    interruptFlag = true;
    timeOfInterrupt = millis();
}

bool PF8574BooleanInputNode::isInterrupt(){
    bool result = interruptFlag;
    interruptFlag = false;  // ✅ Clear après lecture
    return result;
}

bool PF8574BooleanInputNode::getNewValue(bool& value){
    char text[256];
    sprintf(text, "PF8574BooleanInputNode::getNewValue() for node: %s, pin: %u", name, pin);
    bool level = false;
    if (expander == nullptr || !expander->readPin(pin, level)) {
        return false;
    }
    value = activeLow ? !level : level;
    return true;
}

ModbusNode::ModbusNode(uint16_t _address, uint16_t _offset)
    : address(_address), offset(_offset), myModbus(MyModbus::getInstance()) {}

ModbusReadCoilNode::ModbusReadCoilNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t _address, uint16_t _offset, bool _inputInverted, uint16_t refreshInterval, uint16_t webRefreshInterval):
    BooleanInputNode(name, parentName, id, hash, _inputInverted, refreshInterval, webRefreshInterval),
    ModbusNode(_address, _offset) {

    Serial.printf("Constructor of ReadCoil, slave address: %u, register: %u\r\n", _address, _offset);
}

bool ModbusReadCoilNode::getNewValue(bool &value){
    char text[256];
    sprintf(text, "ModbusReadCoilNode:: getNewValue for node name: %s, address: %d, register %d", name, address, offset);
    showMessage(text);

    if (!myModbus.requestTransaction(address)){
        return false;
    }  

    myModbus.resetLastEvent();

    // Tenter l'envoi - réessayer si bus occupé (transId=0)
    uint16_t transId = 0;
    for (int retry = 0; retry < 10 && transId == 0; retry++) {
        transId = myModbus.getModbus()->readCoil(address, offset, &value, 1, MyModbus::cbRead);
        if (transId == 0) {
            myModbus.getModbus()->task();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    if (transId == 0) {
        BorneUniverselle::prepareMessage(WARNING, "ReadCoil: bus occupé après retries");
        return false;
    }

    myModbus.waitEndTransaction();

    if (myModbus.getLastEvent() != Modbus::EX_SUCCESS) {
        char _errMsg[64];
        snprintf(_errMsg, sizeof(_errMsg), "Modbus transaction error addr:%d", address);
        PLC_Tools::sendPeriodicMessage(true, WARNING, _errMsg);
        return false;
    }
    return true;
}

ModbusReadHoldingRegister::ModbusReadHoldingRegister(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t _address, uint16_t _offset, uint16_t refreshInterval,  uint16_t webRefreshInterval):
    Uint16InputNode(name, parentName, id, hash, refreshInterval, webRefreshInterval),
    ModbusNode(_address, _offset) {
    //Serial.printf("Constructor of ModbusReadHoldingRegister, slave addresse: %u, offset: %u\r\n", _address, _offset);
}

bool ModbusReadHoldingRegister::getNewValue(uint16_t& value){
    char text[256];
    sprintf(text, "ModbusReadHoldingRegister::getNewValue for node name: %s, address: %d, register %d", name, address, offset);
    showMessage(text);

    if (!myModbus.requestTransaction(address)){
         return false;  // throttle actif
    }

    sprintf(text, "ModbusReadHoldingRegister::getNewValues, sart transaction for hash: %lu, name: %s, address: %u, offset: %u", (long unsigned int)getHash(), getName(), address, offset);
    showMessage(text);
    myModbus.getModbus()->readHreg(address, offset, &value, 1, MyModbus::cbRead);
    myModbus.waitEndTransaction();

    if (myModbus.getLastEvent() != Modbus::EX_SUCCESS) {
        char _errMsg[64];
        snprintf(_errMsg, sizeof(_errMsg), "Modbus transaction error addr:%d", address);
        PLC_Tools::sendPeriodicMessage(true, WARNING, _errMsg);
        return false;
    }
    return true;
}

ModbusReadDoubleHoldingRegisters::ModbusReadDoubleHoldingRegisters(char *name, char *parentName, uint16_t id, uint32_t _hash, uint16_t _address, uint16_t _offset, uint16_t refreshInterval,  uint16_t webRefreshInterval):
    Uint32InputNode(name, parentName, id, _hash, refreshInterval, webRefreshInterval),
    ModbusNode(_address, _offset) {
    // Serial.printf("Constructor of ModbusReadDobbleRegisters, slave addresse: %u, offset: %u\r\n", _address, _offset);
}

bool ModbusReadDoubleHoldingRegisters::getNewValue(uint32_t& uint32Value){
    uint16_t value[2];
    #define TEXT_SIZE   512
    char text[TEXT_SIZE];
    sprintf(text, "ModbusReadDoubleHoldingRegisters::getNewValue for node name: %s, address: %d, register %d", name, address, offset);
    showMessage(text);
   
    if (!myModbus.requestTransaction(address)){
        return false;
    }
    sprintf(text, "%lu:: Start transaction for hash: %lu, name: %s, address: %u, offset: %u\r\n", millis(), (long unsigned int)getHash(), getName(), address, offset);
    showMessage(text);
    myModbus.getModbus()->readHreg(address, offset, value, 2, MyModbus::cbRead);
    myModbus.waitEndTransaction();
    
    if (myModbus.getLastEvent() != Modbus::EX_SUCCESS) {
        char _errMsg[64];
        snprintf(_errMsg, sizeof(_errMsg), "Modbus transaction error addr:%d", address);
        PLC_Tools::sendPeriodicMessage(true, WARNING, _errMsg);
        return false;
    }

    uint32Value = value[1];
    uint32Value = uint32Value << 16;
    uint32Value += value[0];
    return true;
}

ModbusReadDobbleInputRegisters::ModbusReadDobbleInputRegisters(char *name, char *parentName, uint16_t id, uint32_t _hash, uint16_t _address, uint16_t _offset, uint16_t refreshInterval,  uint16_t webRefreshInterval):
    Uint32InputNode(name, parentName, id, _hash, refreshInterval, webRefreshInterval),
    ModbusNode(_address, _offset) {
    // Serial.printf("Constructor of ModbusReadDobbleRegisters, slave addresse: %u, offset: %u\r\n", _address, _offset);
}

bool ModbusReadDobbleInputRegisters::getNewValue(uint32_t& uint32Value){
    uint16_t value[2];
    #define TEXT_SIZE   512
    char text[TEXT_SIZE];
    sprintf(text, "ModbusReadDobbleInputRegisters::getNewValue for node name: %s, address: %d, register %d", name, address, offset);
    showMessage(text);
   
    if (!myModbus.requestTransaction(address)) return false;  // remplace l'ancien appel
    sprintf(text, "%lu:: Start transaction for hash: %lu, name: %s, address: %u, offset: %u\r\n", millis(), (long unsigned int)getHash(), getName(), address, offset);
    showMessage(text);
    myModbus.getModbus()->readIreg(address, offset, value, 2, MyModbus::cbRead);
    myModbus.waitEndTransaction();
    
    if (myModbus.getLastEvent() != Modbus::EX_SUCCESS) {
        char _errMsg[64];
        snprintf(_errMsg, sizeof(_errMsg), "Modbus transaction error addr:%d", address);
        PLC_Tools::sendPeriodicMessage(true, WARNING, _errMsg);
        return false;
    }

    uint32Value = value[1];
    uint32Value = uint32Value << 16;
    uint32Value += value[0];
    return true;
}

ModbusReadInputRegister::ModbusReadInputRegister(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t _address, uint16_t _offset, uint16_t refreshInterval,  uint16_t webRefreshInterval):
    Uint16InputNode(name, parentName, id, hash, refreshInterval, webRefreshInterval),
    ModbusNode(_address, _offset) {
    // Serial.printf("Constructor of ModbusReadInputRegister, slave addresse: %u, offset: %u\r\n", _address, _offset);
}

bool ModbusReadInputRegister::getNewValue(uint16_t& value){
    char text[256];
    sprintf(text, "ModbusReadInputRegister::getNewValue for node name: %s, address: %d, register %d", name, address, offset);
    showMessage(text);

    if (!myModbus.requestTransaction(address)){
        return false;  // remplace l'ancien appel
    }

    sprintf(text, "%lu:: ModbusReadInputRegister::getNewValue, start transaction for hash: %lu, name: %s, address: %u, offset: %u", millis(), (long unsigned int)getHash(), getName(), address, offset);
    showMessage(text);
    myModbus.getModbus()->readIreg(address, offset, &value, 1, myModbus.cbRead);
    myModbus.waitEndTransaction();

    if (myModbus.getLastEvent() != Modbus::EX_SUCCESS) {
        char _errMsg[64];
        snprintf(_errMsg, sizeof(_errMsg), "Modbus transaction error addr:%d", address);
        PLC_Tools::sendPeriodicMessage(true, WARNING, _errMsg);
        return false;
    }

    return true;
}

HardwareBooleanOutputNode::HardwareBooleanOutputNode(char *name, char *parentName,  uint16_t id, uint32_t hash, uint8_t _pin, uint16_t webRefreshInterval, bool _activeLow): BooleanOutputNode(name, parentName, id, hash, webRefreshInterval){
    //Serial.printf("Constructeur de HardwareBoolanOutputNode, hash: %u\r\n", hash);
    pin = _pin;
    activeLow = _activeLow;
    uint64_t bit = (pin < 64) ? (1ULL << pin) : 0;
    if (bit && (hardwareOutputsReady & bit)) {
        return;
    }
    Serial.printf("Will set pin %d as output (active %s)\r\n", pin, activeLow ? "low" : "high");
    gpio_set_level((gpio_num_t)pin, activeLow ? 1 : 0);  // niveau de repos avant le passage en sortie
    pinMode(pin, OUTPUT);
    digitalWrite(pin, activeLow ? HIGH : LOW);
    hardwareOutputsReady |= bit;
}

uint8_t HardwareBooleanOutputNode::getPinNumber() const{
    return pin;
}

bool HardwareBooleanOutputNode::setNewValue(bool newValue){
    char text[256];
    sprintf(text, "HardwareBooleanOutputNode::setNewValue for node: %s, pin: %u, new value: %s", name, pin, newValue ? "true" : "false");
    showMessage(text);
    digitalWrite(pin, (newValue != activeLow) ? HIGH : LOW);
    return true;
}

PF8574BooleanOutputNode::PF8574BooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint8_t  i2cAddr, uint8_t _pin, uint16_t webRefreshInterval)
    : PF8574BooleanOutputNode(name, parentName, id, hash, getLegacyPcf8574(i2cAddr), _pin, true, webRefreshInterval) {
    // Historique : PCF8574 de la KinCony A8S, sorties actives à l'état bas.
}

PF8574BooleanOutputNode::PF8574BooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, IoExpander *_expander, uint8_t _pin, bool _activeLow, uint16_t webRefreshInterval)
    : BooleanOutputNode(name, parentName, id, hash, webRefreshInterval){
    pin = _pin;
    activeLow = _activeLow;
    expander = _expander;
    setupPin();
}

void PF8574BooleanOutputNode::setupPin(){
    if (expander == nullptr) {
        Serial.printf("❌ No I/O expander for output node %s\n", name);
        return;
    }
    if (!expander->setupOutput(pin, activeLow)) {  // repos : niveau haut si la sortie est active à l'état bas
        Serial.printf("❌ Unable to configure pin %u of %s at 0x%02X as output for node %s\n", pin, expander->chipName(), expander->address(), name);
        return;
    }
    Serial.printf("✅ Output node %s created (pin %d on %s at 0x%02X, active %s)\n", name, pin, expander->chipName(), expander->address(), activeLow ? "low" : "high");
}

bool PF8574BooleanOutputNode::setNewValue(bool newValue){
    char text[256];
    sprintf(text, "PF8574BooleanOutputNode::setNewValue for node: %s, pin: %u, new value: %s", name, pin, newValue ? "true" : "false");
    showMessage(text);
    if (expander == nullptr) {
        return false;
    }
    return expander->writePin(pin, newValue != activeLow);
}

PCA9554BooleanOutputNode::PCA9554BooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint8_t i2cAddr, uint8_t _pin, bool _outputInverted, uint16_t webRefreshInterval): BooleanOutputNode(name, parentName, id, hash, webRefreshInterval){
    pin = _pin;
    outputInverted = _outputInverted;
    pca9554Tx = PCA9554Manager::getOrCreate(i2cAddr);

    if (pca9554Tx == nullptr) {
        Serial.printf("❌ Failed to get PCA9554 instance for output node %s at 0x%02X\n", name, i2cAddr);
        return;
    }

    pca9554Tx->registerOutputPin(pin);
    Serial.printf("✅ Output node %s created (pin %d on PCA9554 at 0x%02X), inverted: %s\n", name, pin, i2cAddr, outputInverted ? "true" : "false");
}

bool PCA9554BooleanOutputNode::setNewValue(bool newValue){
    char text[256];
    sprintf(text, "PCA9554BooleanOutputNode::setNewValue for node: %s, pin: %u, new value: %s", name, pin, newValue ? "true" : "false");
    showMessage(text);
    pca9554Tx->write(pin, outputInverted ? !newValue : newValue);
    return true;
}

ModbusWriteCoilNode::ModbusWriteCoilNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t _address, uint16_t _offset, uint16_t webRefreshInterval):
    BooleanOutputNode(name, parentName, id, hash, webRefreshInterval),
    ModbusNode(_address, _offset) {
    // Serial.printf("Constructor of WriteCoil, slave addresse: %u, offset: %u\r\n", _address, _offset);
}

bool ModbusWriteCoilNode::setNewValue(bool newValue){
    char text[256];
    sprintf(text, "ModbusWriteCoilNode::setNewValue for node: %s, address: %d, register %d, new value: %s", name, address, offset, newValue ? "true" : "false");
    showMessage(text);

    if (!myModbus.requestTransaction(address)){
        return false;  
    }

    myModbus.getModbus()->writeCoil(address, offset, newValue, MyModbus::cbWrite);
    myModbus.waitEndTransaction();

    if (myModbus.getLastEvent() != Modbus::EX_SUCCESS) {
        char _errMsg[64];
        snprintf(_errMsg, sizeof(_errMsg), "Modbus transaction error addr:%d", address);
        PLC_Tools::sendPeriodicMessage(true, WARNING, _errMsg);
        return false;
    }

    return true;
}                                                                                                                                                                                      
                            
ModbusWriteMultipleCoilslNode::ModbusWriteMultipleCoilslNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t _address, uint16_t _offset, uint8_t _nbValues, uint16_t webRefreshInterval):
    OutputNode(name, parentName, id, hash, webRefreshInterval),
    ModbusNode(_address, _offset) {
    nbValues = _nbValues;
    //Serial.printf("ModbusWriteMultipleCoilslNode:: constructor nbValues: %d\r\n", nbValues);
    values = new bool[nbValues];
    hideValues = new bool[nbValues];

    for (uint8_t i = 0; i < nbValues; i++){
        values[i] = false;
        hideValues[i] = false;
    }
}

ModbusWriteMultipleCoilslNode::~ModbusWriteMultipleCoilslNode() {
    delete[] values;
    delete[] hideValues;
}

bool ModbusWriteMultipleCoilslNode::specificRefresh(){
    showMessage("specificRefresh for ModbusWriteMultipleCoilslNode");
    if (updateNeeded){
        if (!setNewValues()){
            return false;
        }

        _notifyPending = true;
        lastChange = millis();
        updateNeeded = false;      
    }

    return true;
}

uint8_t ModbusWriteMultipleCoilslNode::getNbValues(){
    return nbValues;
}

bool ModbusWriteMultipleCoilslNode::getValue(uint8_t id){
    return values[id];
}

bool ModbusWriteMultipleCoilslNode::setNewValues(){
    char text[256];
    sprintf(text, "ModbusWriteMultipleCoilslNode::setNewValues for node: %s, address: %d, register %d", name, address, offset);
    showMessage(text);

    for (uint8_t i = 0; i < nbValues; i++){
        values[i] = hideValues[i];
    }

    if (!myModbus.requestTransaction(address)){
        return false;  
    }

    myModbus.getModbus()->writeCoil(address, offset, values, nbValues, myModbus.cbWrite);
    myModbus.waitEndTransaction();

     if (myModbus.getLastEvent() != Modbus::EX_SUCCESS) {
        char _errMsg[64];
        snprintf(_errMsg, sizeof(_errMsg), "Modbus transaction error addr:%d", address);
        PLC_Tools::sendPeriodicMessage(true, WARNING, _errMsg);
        return false;
    }

    return true;
}

bool ModbusWriteMultipleCoilslNode::setValue(bool val, uint8_t id){
    if (id < nbValues){
        if (val != hideValues[id]){
            hideValues[id] = val;
            updateNeeded = true;
            Serial.printf("ModbusWriteMultipleCoilslNode::setValue, for node %s, will set bit %d to %s\r\n", getName(), id, val ? "true": "false");
        }
    } else {
        Serial.printf("ModbusWriteMultipleCoilslNode::setValue: Warning nb values < id !, nbValues: %d\r\n", nbValues);
        return false;
    }
    return true;
}

bool ModbusWriteMultipleCoilslNode::setValues(bool *vals, uint8_t _nbValues){
    if (_nbValues < nbValues){
        for (uint8_t i = 0; i < _nbValues; i++){
            if (vals[i] != hideValues[i]){
                hideValues[i] = vals[i];
                Serial.printf("ModbusWriteMultipleCoilslNode:setValuer,  for node %s, will set bit %d to %s\r\n", getName(), i, vals[i] ? "true": "false");
                updateNeeded = true;
            }
        }
    } else {
        Serial.printf("ModbusWriteMultipleCoilslNode::setValues: Warning nb values < id !, nbValues. %d\r\n", nbValues); 
        return false;
    }
    return true;
}

ModbusReadMultipleInputsRegistersNode::ModbusReadMultipleInputsRegistersNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t _address, uint16_t _offset, uint8_t _nbValues, uint16_t refreshInterval, uint16_t webRefreshInterval, bool *invertedBits):
    InputNode(name, parentName, id, hash, refreshInterval, webRefreshInterval),
    ModbusNode(_address, _offset) {
    nbValues = _nbValues;
    Serial.printf("ModbusReadMultipleInputsRegistersNode:: constructor, nbValues: %d\r\n", nbValues);
    bits = new Bit[nbValues];
    inverted = new bool[nbValues];
    for (uint8_t i = 0; i < nbValues; i++){
        inverted[i] = invertedBits ? invertedBits[i] : false;
    }
}

bool ModbusReadMultipleInputsRegistersNode::specificRefresh(){
    // Utiliser un tableau sur le stack — pas d'allocation dynamique (évite fuite PSRAM)
    // nbValues est uint8_t donc max 255; on prend 64 pour couvrir les modules standard
    bool values[64] = {};
    uint8_t count = (nbValues < 64) ? nbValues : 64;

    for (uint8_t i = 0; i < count; i++){
        values[i] = bits[i].getValue();
    }

    bool succes = getNewValues(values);
    if (!succes){
        updateNeeded = false;
        return false;
    }

    for (uint8_t i = 0; i < count; i++){
        if (values[i] != bits[i].getValue()){
            updateNeeded = true;
            lastChange = millis();
            bits[i].setValue(values[i]);
        }
    }
    return true;
}

 bool ModbusReadMultipleInputsRegistersNode::getNewValues(bool values[]){
    char text[256];
    sprintf(text, "%lu:: ModbusReadMultipleInputsRegistersNode::Bit *ModbusReadMultipleInputsRegistersNode::getNewValues for node name: %s, address: %d, register %d", millis(), name, address, offset);
    showMessage(text);
    
    if (!myModbus.requestTransaction(address)){
        return false;
    }
    

    myModbus.getModbus()->readIsts(address, offset, values, nbValues, myModbus.cbRead);
 
    myModbus.waitEndTransaction();
    if (myModbus.getLastEvent() != Modbus::EX_SUCCESS) {
        char _errMsg[64];
        snprintf(_errMsg, sizeof(_errMsg), "Modbus transaction error addr:%d", address);
        PLC_Tools::sendPeriodicMessage(true, WARNING, _errMsg);
        return false;
    }

    for (uint8_t i = 0; i < nbValues; i++){
        if (inverted[i]) values[i] = !values[i];
        // Serial.printf("%lu:: bit: %d\r\n", millis(), i);
        if (values[i] != bits[i].getValue()){
            updateNeeded = true;
            lastChange = millis();
            // Serial.printf("ModbusReadMultipleInputsRegistersNode %s[%d]: %s\r\n", getName(), i, values[i] ? "1" : "0");
            bits[i].setValue(values[i]); // copy new values
        } 
    }
    return true;
}

ModbusWriteHoldingRegister::ModbusWriteHoldingRegister(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t _address, uint16_t _offset, uint16_t webRefreshInterval):
    Uint16OutputNode(name, parentName, id, hash, webRefreshInterval),
    ModbusNode(_address, _offset) {
    // Serial.printf("Constructor of ModbusWriteHoldingRegister, slave addresse: %u, offset: %u\r\n", _address, _offset);
}

bool ModbusWriteHoldingRegister::setNewValue(uint16_t newValue){
    char text[200]; // Adjust size as needed
    sprintf(text, "ModbusWriteHoldingRegister::setNewValue,  value: %d, address: %d, register: %d", newValue, address, offset); // Adjust fields as needed
    showMessage(text);
    if (!myModbus.requestTransaction(address)){
        return false;
    }

    //Serial.printf("%u:: Start transaction for hash: %u\r\n", millis(), getHash());
    myModbus.getModbus()->writeHreg(address, offset, newValue, myModbus.cbWrite);
    myModbus.waitEndTransaction();
    //Serial.printf("%u:: End transaction for hash: %u\r\n", millis(), getHash());
    if (myModbus.getLastEvent() != Modbus::EX_SUCCESS) {
        char _errMsg[64];
        snprintf(_errMsg, sizeof(_errMsg), "Modbus transaction error addr:%d", address);
        PLC_Tools::sendPeriodicMessage(true, WARNING, _errMsg);
        return false;
    }
    return true;
}

ModbusWriteDoubleHoldingRegister::ModbusWriteDoubleHoldingRegister(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t _address, uint16_t _offset,  uint16_t webRefreshInterval):
    Uint32OutputNode(name, parentName, id, hash, webRefreshInterval),
    ModbusNode(_address, _offset) {
    // Serial.printf("Constructor of ModbusWriteDobbleHoldingRegister, slave addresse: %u, offset: %u\r\n", _address, _offset);
}

bool ModbusWriteDoubleHoldingRegister::setNewValue(uint32_t newValue){
    char text[200]; // Adjust size as needed
    sprintf(text, "%lu:: ModbusWriteHoldingRegister::setNewValue,  value: %lu, address: %d, register: %d", millis(), (long unsigned int)newValue, address, offset); // Adjust fields as needed
    showMessage(text);
    
    if (!myModbus.requestTransaction(address)){
        return false;
    }

    uint16_t values[2];
    values[1] = newValue >> 16;
    values[0] = newValue & 0x0000FFFF;

    sprintf(text, "Value 1: 0x%08X, value 0: 0x%08x\r\n", values[1], values[0]);
    showMessage(text);
    myModbus.getModbus()->writeHreg(address, offset, values, 2, myModbus.cbWrite);
    myModbus.waitEndTransaction();
  
    if (myModbus.getLastEvent() != Modbus::EX_SUCCESS) {
        char _errMsg[64];
        snprintf(_errMsg, sizeof(_errMsg), "Modbus transaction error addr:%d", address);
        PLC_Tools::sendPeriodicMessage(true, WARNING, _errMsg);
        return false;
    }
    return true;  
}

VirtualBooleanInputNode::VirtualBooleanInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, bool _inputInverted, uint16_t webRefreshInterval):BooleanInputNode(name, parentName, id, hash, _inputInverted, 0, webRefreshInterval){

}

void VirtualBooleanInputNode::setValue(bool _value){
    if (hideValue != _value){
        Serial.printf("%lu:: VirtualBooleanInputNode::setValue for node %s, value: %s\r\n",  millis(), getName(), _value ? "true" : "false");
        updateNeeded = true;
        hideValue = _value;
        lastChange = millis();
        if (hideValue){
            lastUp = lastChange;
        } else {
            lastDown = lastChange;
        }
    }
}

bool VirtualBooleanInputNode::specificRefresh(){
    // Node virtuel : la valeur est gérée exclusivement par setValue()
    // Ne jamais écraser hideValue depuis le refresh hardware
    return true;
}

VirtualUint32InputNode::VirtualUint32InputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval):Uint32InputNode(name, parentName, id, hash, 0, webRefreshInterval){

}

void VirtualUint32InputNode::setValue(uint32_t _value){
    if (hideValue != _value){
        updateNeeded = true;
        hideValue = _value;
    }
}

bool VirtualUint32InputNode::specificRefresh(){
    // Node virtuel : la valeur est gérée exclusivement par setValue()
    // Ne jamais écraser hideValue depuis le refresh hardware
    return true;
}

VirtualFloatInputNode::VirtualFloatInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval):FloatInputNode(name, parentName, id, hash, 0, webRefreshInterval){

}

void VirtualFloatInputNode::setValue(float _value){
    if (hideValue != _value){
        hideValue = _value;
        updateNeeded = true;
        //Serial.printf("VirtualFloatInputNode::setValue:: %.2f\r\n", _value);
    }
}

bool VirtualFloatInputNode::specificRefresh(){
    // Node virtuel : la valeur est gérée exclusivement par setValue()
    // Ne jamais écraser hideValue depuis le refresh hardware
    return true;
}

VirtualBooleanOutputNode::VirtualBooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval):BooleanOutputNode(name, parentName, id, hash, webRefreshInterval){
}

VirtualUint32OutputNode::VirtualUint32OutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval):Uint32OutputNode(name, parentName, id, hash, webRefreshInterval){
}

VirtualFloatOutputNode::VirtualFloatOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval):FloatOutputNode(name, parentName, id, hash, webRefreshInterval){
}

VirtualTextInputNode::VirtualTextInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval)
    : TextInputNode(name, parentName, id, hash, 0, webRefreshInterval) {
    
    if (hideValue) {
        hideValue[0] = 0; // Maintenant OK car hideValue pointe vers un buffer alloué
    } else {
        Serial.printf("ERROR: VirtualTextInputNode %s - parent class failed to allocate hideValue\r\n", name);
    }
}

void VirtualTextInputNode::setValue(const char *text) {
    const char* safeName = getName();
    
    if (!text) {
        Serial.printf("ERROR: VirtualTextInputNode setValue called with null for node: %s\r\n", safeName);
        return;
    }
  
    if (!hideValue) {
        Serial.printf("ERROR: VirtualTextInputNode hideValue is null for node: %s\r\n", safeName);
        return;
    }
    
    if (strlen(text) >= MAX_TEXT_SIZE) {
        Serial.printf("ERROR: VirtualTextInputNode value too long for node: %s\r\n", safeName);
        return;
    }
    
    Serial.printf("VirtualTextInputNode::setValue for node %s, value: %s\r\n", safeName, text);
    
    if (strcmp(text, hideValue) != 0) {
        updateNeeded = true;
        strcpy(hideValue, text);
    }
}

VirtualTextOutputNode::VirtualTextOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval):TextOutputNode(name, parentName, id, hash, webRefreshInterval){
}
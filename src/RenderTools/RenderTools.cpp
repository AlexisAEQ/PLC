#include "RenderTools/RenderTools.h"

#define DEBUG_RENDER

Render::Render(Node *_node) {
    //Serial.println("===== DEBUT Constructeur Render =====");
    
    node = _node;

    if (!node) {
        Serial.println("❌ Render: node NULL dans constructeur");
        return;
    } else{
#ifdef DEBUG_RENDER
        Serial.printf("Render::node %s OK\r\n", node->getName());
#endif
    }
    
    //Serial.printf("✓ Node OK: %s, Hash: 0x%08X\n", node->getName(), (unsigned int)node->getHash());

    doc.clear();
    //Serial.println("✓ Doc cleared");
    
    // Nouvelle syntaxe recommandée par ArduinoJson v7
    //Serial.println("Creating statesArray...");
    JsonArray statesArray = doc[NOTIFY_STATES_CHANGED].to<JsonArray>();
    
    if (statesArray.isNull()) {
        Serial.println("❌ statesArray est NULL!");
        return;
    }
    //Serial.println("✓ statesArray créé");
    
    //Serial.println("Adding JsonObject to array...");
    states = statesArray.add<JsonObject>();
    
    if (states.isNull()) {
        Serial.println("❌ states est NULL!");
        return;
    }
    //Serial.println("✓ states créé");
    
    //Serial.printf("Setting hash: 0x%08X\n", (unsigned int)node->getHash());
    states[HASH] = node->getHash();
    //Serial.println("✓ Hash défini");
    
    // Utiliser aussi la nouvelle syntaxe pour descriptor
    //Serial.println("Creating descriptor...");
    descriptor = states[DESCRIPTOR].to<JsonObject>();
    
    if (descriptor.isNull()) {
        Serial.println("❌ descriptor est NULL!");
        return;
    }
    //Serial.println("✓ descriptor créé");
    
    //Serial.println("✅ Render initialisé avec succès!");
    //Serial.println("===== FIN Constructeur Render =====\n");
}

void Render::addProperty(const char *name, uint32_t value) {
    if (!name) {
        Serial.println("❌ addProperty: name est NULL!");
        return;
    }
    
    if (descriptor.isNull()){
        Serial.println("❌ addProperty: descriptor est NULL!");
        return; 
    }

    descriptor[name] = value;
}

void Render::addProperty(const char *name, bool value) {
    if (!name) {
        Serial.println("❌ addProperty: name est NULL!");
        return;
    }
    
    if (descriptor.isNull()){
        Serial.println("❌ addProperty: descriptor est NULL!");
        return; 
    }
    descriptor[name] = value;
}

void Render::addProperty(const char *name, const char *value) {
    if (!name) {
        Serial.println("❌ addProperty: name est NULL!");
        return;
    }
    
    if (descriptor.isNull()){
        Serial.println("❌ addProperty: descriptor est NULL!");
        return; 
    }
    descriptor[name] = value;
}

void Render::addProperties(JsonDocument doc){
    JsonArray items = descriptor["items"].to<JsonArray>();
    for (JsonObject recette : doc["recettes"].as<JsonArray>()) {
        items.add(recette["name"]);
    }
}

bool Render::addNodeProperty(JsonDocument& doc, uint32_t hash, const char* propertyName, bool propertyValue) {
    if (!propertyName) {
        Serial.println("❌ Render::addNodeProperty: propertyName NULL");
        return false;
    }
    
    // Récupérer ou créer l'array states
    JsonArray statesArray;
    if (!doc[NOTIFY_STATES_CHANGED].isNull()) {
        statesArray = doc[NOTIFY_STATES_CHANGED].as<JsonArray>();
    } else {
        statesArray = doc[NOTIFY_STATES_CHANGED].to<JsonArray>();
    }
    
    if (statesArray.isNull()) {
        Serial.println("❌ Render::addNodeProperty: échec création statesArray");
        return false;
    }
    
    // Créer un nouvel objet state
    JsonObject states = statesArray.add<JsonObject>();
    if (states.isNull()) {
        Serial.println("❌ Render::addNodeProperty: échec création states");
        return false;
    }
    
    // Ajouter le hash
    states[HASH] = hash;
    
    // Créer le descriptor
    JsonObject descriptor = states[DESCRIPTOR].to<JsonObject>();
    if (descriptor.isNull()) {
        Serial.println("❌ Render::addNodeProperty: échec création descriptor");
        return false;
    }
    
    // Ajouter la propriété
    descriptor[propertyName] = propertyValue;
    
    // Si c'est la propriété "disable", mettre à jour l'état interne
    if (strcmp(propertyName, "disable") == 0) {
        disabled = propertyValue;
    }
    
    return true;
}

// Version pour uint32_t
bool Render::addNodeProperty(JsonDocument& doc, uint32_t hash, const char* propertyName, uint32_t propertyValue) {
    if (!propertyName) {
        Serial.println("❌ Render::addNodeProperty: propertyName NULL");
        return false;
    }
    
    // Récupérer ou créer l'array states
    JsonArray statesArray;
    if (!doc[NOTIFY_STATES_CHANGED].isNull()) {
        statesArray = doc[NOTIFY_STATES_CHANGED].as<JsonArray>();
    } else {
        statesArray = doc[NOTIFY_STATES_CHANGED].to<JsonArray>();
    }
    
    if (statesArray.isNull()) {
        Serial.println("❌ Render::addNodeProperty: échec création statesArray");
        return false;
    }
    
    // Créer un nouvel objet state
    JsonObject states = statesArray.add<JsonObject>();
    if (states.isNull()) {
        Serial.println("❌ Render::addNodeProperty: échec création states");
        return false;
    }
    
    // Ajouter le hash
    states[HASH] = hash;
    
    // Créer le descriptor
    JsonObject descriptor = states[DESCRIPTOR].to<JsonObject>();
    if (descriptor.isNull()) {
        Serial.println("❌ Render::addNodeProperty: échec création descriptor");
        return false;
    }
    
    // Ajouter la propriété
    descriptor[propertyName] = propertyValue;
    
    return true;
}

// Version pour const char*
bool Render::addNodeProperty(JsonDocument& doc, uint32_t hash, const char* propertyName, const char* propertyValue) {
    if (!propertyName || !propertyValue) {
        Serial.println("❌ Render::addNodeProperty: propertyName ou propertyValue NULL");
        return false;
    }
    
    // Récupérer ou créer l'array states
    JsonArray statesArray;
    if (!doc[NOTIFY_STATES_CHANGED].isNull()) {
        statesArray = doc[NOTIFY_STATES_CHANGED].as<JsonArray>();
    } else {
        statesArray = doc[NOTIFY_STATES_CHANGED].to<JsonArray>();
    }
    
    if (statesArray.isNull()) {
        Serial.println("❌ Render::addNodeProperty: échec création statesArray");
        return false;
    }
    
    // Créer un nouvel objet state
    JsonObject states = statesArray.add<JsonObject>();
    if (states.isNull()) {
        Serial.println("❌ Render::addNodeProperty: échec création states");
        return false;
    }
    
    // Ajouter le hash
    states[HASH] = hash;
    
    // Créer le descriptor
    JsonObject descriptor = states[DESCRIPTOR].to<JsonObject>();
    if (descriptor.isNull()) {
        Serial.println("❌ Render::addNodeProperty: échec création descriptor");
        return false;
    }
    
    // Ajouter la propriété
    descriptor[propertyName] = propertyValue;
    
    return true;
}

bool Render::setDisabled(bool status) {
    disabled = status;
    addProperty("disable", disabled);
    return sendJson();
}

bool Render::sendJson() {
    uint16_t size = measureJson(doc);
    char *chain = (char *)malloc(size + 10);

    serializeJson(doc, chain, size);
    chain[size] = 0; 
    //Serial.println("Document to send to the socket:");
    //serializeJsonPretty(doc, Serial);
    Serial.println();
    bool success = BorneUniverselle::sendTextToClient(chain);
    if (!success){
        Serial.println("Render: unable to send text to client");
    }
    free(chain);
    return success;
}

void BarGraph::setNbDots(uint8_t _nbDots){
    this->nbDots = _nbDots;
    setValue(0);
}

void BarGraph::setValue(uint_least8_t value, bool inProgress) {
    Serial.printf("🔍 BarGraph::setValue: value=%d, inProgress=%d, nbDots=%d\n", 
                  value, inProgress, nbDots);

    if (!node) return;
    
    index = value;
    String display = "";
    for (int i = 0; i < nbDots; i++) {
        if (inProgress) {
            if (i < value - 1)      display += "🟩";  // étape complétée
            else if (i == value - 1) display += "🟨"; // étape en cours
            else                     display += "⬜";  // étape à venir
        } else {
            if (i < value)          display += "🟩";  // étape complétée
            else                    display += "⬜";   // étape à venir
        }
    }
    
    char msg[12];
    snprintf(msg, sizeof(msg), " %d/%d", value, nbDots);
    display += msg;
    
    node->setValue(display.c_str());
    Serial.printf("🔍 BarGraph result: %s\n", display.c_str());
    node->setValue(display.c_str());
}

uint8_t BarGraph::getValue(){
    return index;

}

// ========================================
// CONSTRUCTEUR
// ========================================

EnhancedButton::EnhancedButton(VirtualBooleanInputNode* node)
    : Render(node)
{
    _label[0] = '\0';
}

// ========================================
// ACCÈS TYPÉ AU NODE
// ========================================

VirtualBooleanInputNode* EnhancedButton::getTypedNode() {
    return static_cast<VirtualBooleanInputNode*>(node);
}

// ========================================
// DÉLÉGATION AU NODE
// ========================================

bool EnhancedButton::getValue() {
    return getTypedNode()->getValue();
}

uint32_t EnhancedButton::getLastUp() {
    return getTypedNode()->getLastUp();
}

uint32_t EnhancedButton::getLastDown() {
    return getTypedNode()->getLastDown();
}

bool EnhancedButton::isRisingEdge() {
    return getTypedNode()->isRisingEdge();
}

bool EnhancedButton::isFallingEdge() {
    return getTypedNode()->isFallingEdge();
}

bool EnhancedButton::getIsChanged(){
    return getTypedNode()->getIsChanged();
}

// ========================================
// CONTRÔLE DE L'INTERFACE
// ========================================

void EnhancedButton::setColorStyle(ColorStyle style) {
    bool isNeutral = (style == ColorStyle::COLOR_NEUTRAL);
    setDisabled(isNeutral, false);          // groupé dans un seul sendJson()
    addProperty(BUTTON_COLOR, toStr(style));
    sendJson();
}

void EnhancedButton::setDisabled(bool status, bool send) {
    disabled = status;
    addProperty(BUTTON_DISABLED, disabled);
    addProperty(BUTTON_COLOR, toStr(ColorStyle::COLOR_NEUTRAL));
    if (send) sendJson();
}

void EnhancedButton::setLabel(const char* label) {
    snprintf(_label, sizeof(_label), "%s", label);
    addProperty(BUTTON_LABEL, _label);
    sendJson();
}

const char* EnhancedButton::getLabel() const {
    return _label;
}

// ========================================
// UTILITAIRE PRIVÉ
// ========================================

const char* EnhancedButton::toStr(ColorStyle style) {
    switch (style) {
        case ColorStyle::COLOR_PRIMARY:  return "primary";
        case ColorStyle::COLOR_SUCCESS:  return "success";
        case ColorStyle::COLOR_NEUTRAL:  return "neutral";
        case ColorStyle::COLOR_WARNING:  return "warning";
        case ColorStyle::COLOR_DANGER:   return "danger";
        default:                         return "neutral";
    }
}
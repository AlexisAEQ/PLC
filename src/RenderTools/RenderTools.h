#ifndef RENDER_TOOLS_H
#define  RENDER_TOOLS_H
#define ARDUINOJSON_ENABLE_COMMENTS 1

#include "Arduino.h"
#include "ArduinoJson.h"
#include <list>
#include "Node/Node.h"
#include "BorneUniverselle/borneUniverselle.h"

// Visual Indicator keys
#define MAX                         "max"
#define MIN                         "min"

// RxIndicator (led)
#define BLINK                       "blink"
#define COLOR                       "color"

class Render {
    protected:  // ← Important : protected pour l'héritage
        Node *node;
        JsonDocument doc;
        JsonObject descriptor, states;
        bool disabled = false;
        
    public:
       Render(Node *_node);
       Render(){
        
       };
        
        void addProperty(const char *name, uint32_t value);

        void addProperty(const char *name, bool value);
        
        void addProperty(const char *name, const char *value);

        void addProperties(JsonDocument doc);

        // ========================================
        // MÉTHODES BATCH
        // ========================================
        
        /// @brief Ajoute une propriété pour un node dans le document batch fourni
        /// @param doc Document JSON à enrichir
        /// @param hash Hash du node
        /// @param propertyName Nom de la propriété (ex: "disable", "color", "min", etc.)
        /// @param propertyValue Valeur de la propriété
        /// @return true si ajout réussi, false sinon
        bool addNodeProperty(JsonDocument& doc, uint32_t hash, const char* propertyName, bool propertyValue);
        bool addNodeProperty(JsonDocument& doc, uint32_t hash, const char* propertyName, uint32_t propertyValue);
        bool addNodeProperty(JsonDocument& doc, uint32_t hash, const char* propertyName, const char* propertyValue);

        bool setDisabled(bool status);
        
        bool isDisabled() const {
            return disabled;
        }

        static bool sendBatch(const uint32_t* hashes, uint16_t count, 
                             void (*setupDescriptor)(JsonObject& descriptor, uint16_t index));
        
        /// @brief Surcharge pour passer un contexte utilisateur au callback
        template<typename T>
        static bool sendBatch(const uint32_t* hashes, uint16_t count, 
                             void (*setupDescriptor)(JsonObject& descriptor, uint16_t index, T* context),
                             T* context);
        
        /// @brief Envoie le document JSON au client
        bool sendJson();
};

class VisualIndicator: public Render{
    public:
       VisualIndicator(Node *node) : Render(node){};
        
        /// @brief Définit la valeur maximale
        /// @param maxValue Valeur maximale
        void setMaxValue(uint32_t maxValue){ 
            addProperty(MAX, maxValue);
            sendJson();
        }

        /// @brief Définit la valeur minimale
        /// @param minValue Valeur minimale
        void setMinValue(uint32_t minValue){ 
            addProperty(MIN, minValue);
            sendJson();
        }
    
    private:

};

class DropDown: public Render{
    public:
        DropDown(Node *node) : Render(node){};

        /// @brief Définit les éléments de la liste déroulante
        /// @param doc Document JSON contenant les éléments
        void setItems(JsonDocument doc){
            Serial.println("DropDown::setItems, received document");
            // serializeJsonPretty(doc, Serial);
            addProperties(doc);
            sendJson();
        }
};

class RxIndicator : public Render { 
    public:
        RxIndicator(VirtualBooleanOutputNode *existingNode) 
            : Render(existingNode) {}  // ← Constructeur simple
        
        // Méthodes spécifiques à RxIndicator
        void setBlinking(bool status) {
            addProperty(BLINK, status);  // ← Direct, pas de délégation
            sendJson();
        }
        
        void setColor(const char *color) {
            addProperty(COLOR, color);
            sendJson();
        }
        
        // Accès typé au node
        VirtualBooleanOutputNode* getTypedNode() {
            return static_cast<VirtualBooleanOutputNode*>(node);
        }
        
        // Méthodes déléguées au node
        void setValue(bool value) {
            getTypedNode()->setValue(value);
        }
        
        bool getValue() {
            return getTypedNode()->getValue();
        }
        
        uint32_t getLastUp() {
            return getTypedNode()->getLastUp();
        }
        
        uint32_t getLastDown() {
            return getTypedNode()->getLastDown();
        }
};

class BarGraph {
    public:
        BarGraph(VirtualTextOutputNode *node, uint8_t nbDots){
            this->node = node;
            this->nbDots = nbDots;
        };
        
        void setValue(uint_least8_t value, bool inProgress = true);
        
        uint8_t getValue(); 

        void setNbDots(uint8_t nbDots);


    private:
        VirtualTextOutputNode *node;
        uint8_t nbDots;
        uint8_t index;

};

#define BUTTON_COLOR    "color"     // ← pas "variant"
#define BUTTON_DISABLED "disable"   // ← pas "disabled"
#define BUTTON_LABEL    "name"


enum class ColorStyle {
        COLOR_PRIMARY,
        COLOR_SUCCESS,
        COLOR_NEUTRAL,    // ← désactive le bouton
        COLOR_WARNING,
        COLOR_DANGER
};

class EnhancedButton : public Render {
public:
    EnhancedButton(VirtualBooleanInputNode* node);

    // ========================================
    // DÉLÉGATION AU NODE
    // ========================================

    /// @brief Retourne la valeur du bouton (pressé/relâché)
    bool getValue();

    /// @brief Retourne le timestamp de la dernière montée
    uint32_t getLastUp();

    /// @brief Retourne le timestamp de la dernière descente
    uint32_t getLastDown();

    /// @brief Front montant
    bool isRisingEdge();

    /// @brief Front descendant
    bool isFallingEdge();

    bool getIsChanged();

    // ========================================
    // CONTRÔLE DE L'INTERFACE
    // ========================================

    /// @brief Change la couleur et l'état disabled du bouton
    /// @note COLOR_NEUTRAL désactive automatiquement le bouton
    void setColorStyle(ColorStyle style);

    /// @brief Active ou désactive le bouton
    /// @param status true = désactivé
    /// @param send true = envoie le JSON immédiatement
    void setDisabled(bool status, bool send = true);

    /// @brief Change le label affiché sur le bouton
    void setLabel(const char* label);

    /// @brief Retourne le label actuel
    const char* getLabel() const;

private:
    char _label[64];

    VirtualBooleanInputNode* getTypedNode();

    static const char* toStr(ColorStyle style);
};

#endif
#ifndef NODE_H
#define NODE_H

#define ARDUINOJSON_ENABLE_COMMENTS 1

#include "Arduino.h"
#include <rom/crc.h>
#include <PCF8574.h>
#include "ArduinoJson.h"
#include "esp_debug_helpers.h"
//#include "BorneUniverselle/borneUniverselle.h"

//#include "MyModbus/MyModbus.h"
class MyModbus; // forward declaration
class BorneUniverselle; // forward declaration
class Pca9554Driver; // forward declaration
class IoExpander; // forward declaration

#define NAME_LENGHT                 80

#define CLASS_NODE                              1
#define CLASS_INPUT_NODE                        2
#define CLASS_OUTPUT_NODE                       3
#define CLASS_BOOLEAN_INPUT_NODE                4
#define CLASS_BOOLEAN_OUTPUT_NODE               5
#define CLASS_UINT16_INPUT_NODE                 6
#define CLASS_UINT16_OUTPUT_NODE                7
#define CLASS_UINT32_INPUT_NODE                 8
#define CLASS_UINT32_OUTPUT_NODE                9
#define CLASS_HW_BOOLEAN_INPUT_NODE             10
#define CLASS_HW_BOOLEAN_OUTPUT_NODE            11
#define CLASS_VIRTUAL_BOOLEAN_INPUT_NODE        12
#define CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE       13
#define CLASS_PFC8574_BOOLEAN_INPUT_NODE        14
#define CLASS_PFC8574_BOOLEAN_OUTPUT_NODE       15
#define CLASS_VIRTUAL_UINT32_INPUT_NODE         16
#define CLASS_VIRTUAL_UINT32_OUTPUT_NODE        17

#define FIRST_MODBUS_CLASS                      18

#define CLASS_MODBUS_READ_COIL                  18
#define CLASS_MODBUS_WRITE_COIL_NODE            19
#define CLASS_MODBUS_READHOLDINGREGISTER        20
#define CLASS_MODBUS_WRITEHOLDINGREGISTER       21
#define CLASS_MODBUS_READINPUTREGISTER          22
#define CLASS_MODBUS_READ_DOUBLE_INPUTREGISTER  23
#define CLASS_MODBUS_WRITE_DOUBLE_INPUTREGISTER  24
#define CLASS_MODBUS_READ_DOUBLE_HOLDING_REGISTER 25
#define CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER 26
#define CLASS_MODBUS_WRITE_MULTIPLE_COILS       27
#define CLASS_MODBUS_READ_MULTIPLE_INPUTS_STATUS 28

#define LAST_MODBUS_CLASS                       28

#define CLASS_FLOAT_INPUT_NODE                  29
#define CLASS_FLOAT_OUTPUT_NODE                 30
#define CLASS_VIRTUAL_FLOAT_INPUT_NODE          31
#define CLASS_VIRTUAL_FLOAT_OUTPUT_NODE         32
#define CLASS_TEXT_INPUT_NODE                   33
#define CLASS_VIRTUAL_TEXT_INPUT_NODE           34
#define CLASS_TEXT_OUTPUT_NODE                  35
#define CLASS_VIRTUAL_TEXT_OUTPUT_NODE          36
#define CLASS_PCA9554_BOOLEAN_OUTPUT_NODE       37

#define DEFAULT_MODBUS_REFRESH_INTERVAL         500
#define DEFAULT_MODBUS_WEB_REFRESH_INTERVAL     500

#define DEFAULT_MODE 0
#define INPUT_MODE  1
#define OUTPUT_MODE 2

#define PFC_INIT_ERROR              "PFC8574 initialisation error !!!"
#define PCA9554_INIT_ERROR          "PCA9554 initialisation error !!!"
#define HASH_NOT_FOUND              "Hash not found !!!!"

class VirutalVisu{
    public:
        void setColor(char * color){
            ;
        }

};

class Node{
    public:
        Node(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshIntervl);
        virtual ~Node() {
            // Nettoyer les ressources de Node si nécessaire
            //Serial.printf("Destroying node: %s\n", name);
        }
        virtual int classType() const { return CLASS_NODE; }
        virtual uint8_t getModbusAddress() const { return 0; }
        char *getName();
        bool setName(const char *name, const char *parentName);
        bool refresh();
        uint32_t getLastRefresh(); 

        uint32_t getHash();
        uint8_t getMode();
        uint32_t getLastWebUpdate();
        void setLastWebUpdate(uint32_t lastUpdate);
        uint32_t getWebUpdateInterval();
        void setWebUpdateInterval(uint16_t interval);
        void clearIsChanged();
        void forceIsChanged();
        bool getIsChanged();
        void setChangeEvent();
        void setShowMessages(bool showMessage); 
        bool getShowMessages(){
            return isShowMessage;
        }
        void showMessage(const char *text);
        void setMode(char * text); 

         // Add callback type definition and setter
        typedef std::function<JsonDocument&()> DescriptorCallback;
        void registerDescriptorCallback(DescriptorCallback callback) {
              descriptorCallback = callback;
         }
   
        DescriptorCallback descriptorCallback = nullptr;
        bool isNodeDisabled(){
            return tempDisabled;
        }

    protected:
        virtual bool specificRefresh() = 0;
        char name[NAME_LENGHT];
        bool updateNeeded = false;  // la valeur cachée est différente de la valeur actuelle.
        uint32_t lastChange;
        uint32_t hash = 0;
        uint32_t lastWebUpdate, webUpdateInterval = DEFAULT_MODBUS_WEB_REFRESH_INTERVAL;
  
        uint32_t getLastChange();
        
        void setMode(uint8_t mode);

    private: 
        uint8_t mode; 
        bool isShowMessage = false;
        uint32_t lastRefresh = 0;

        // Pour la désactivation temporaire du noeud apres plusieurs erreurs consécutives
        uint8_t consecutiveErrors = 0;
        bool tempDisabled = false;
        uint32_t disabledTime = 0;        // Timestamp de la désactivation
        uint32_t disableDuration = 5000;  // Durée initiale de désactivation (5 secondes)

//#define DEBUG_THIERRY      
#ifdef DEBUG_THIERRY
        uint32_t maxDisableDuration = 600000;
#else
        uint32_t maxDisableDuration = 60000;
#endif
       
        void setLastRefresh(uint32_t lastUpdate);
};

class InputNode: public Node{
    // external world change the value of the node..
    public:
        InputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t refreshInterval, uint16_t webRefreshInterval);
        virtual ~InputNode() = default; 
        virtual int classType() const{ return CLASS_INPUT_NODE; }
        void setRefreshInterval(uint16_t interval);
        uint32_t getRefreshInterval();

    protected:   
        uint32_t refreshInterval = 0;    
};

class OutputNode: public Node{
    // the node can change the external world..
    public:
        OutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        virtual ~OutputNode() = default; 
        virtual int classType() const { return CLASS_OUTPUT_NODE; }
        // Vrai uniquement quand l'écriture hardware vient de réussir.
        // Utilisé par notifyWebClient() pour ne notifier que les valeurs confirmées.
        bool getNotifyPending() const { return _notifyPending; }
        void clearNotifyPending()     { _notifyPending = false; }

    protected:
        bool _notifyPending = false;     
};

class BooleanInputNode: public InputNode{
     public:
        BooleanInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, bool inverted, uint16_t refreshInterval, uint16_t webRefreshInterval);
        virtual ~BooleanInputNode() = default; 
        virtual int classType() const { return CLASS_BOOLEAN_INPUT_NODE; }  
        bool getValue();
        uint32_t getLastUp();
        uint32_t getLastDown();
        bool specificRefresh();
        bool isRisingEdge();
        bool isFallingEdge();
        bool isUpFrom(uint32_t time);
        bool isDownFrom(uint32_t time);  

    protected:
        virtual bool getNewValue(bool& value) = 0;
        bool hideValue = false;
        bool inputInverted = false;
        uint32_t lastUp = 0, lastDown = 0;
};

class BooleanOutputNode: public OutputNode{
    public: 
        BooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t has,  uint16_t webRefreshInterval);
        virtual ~BooleanOutputNode() = default; 
        virtual int classType() const{ return CLASS_BOOLEAN_OUTPUT_NODE; }
        void setValue(bool newValue, bool force = false);
        bool getValue();
        bool *getValuePtr() { return &hideValue; }

        uint32_t getLastUp();
        uint32_t getLastDown();
        bool specificRefresh();

    protected:
        virtual bool setNewValue(bool newValue) = 0;
        bool hideValue = false;
        uint32_t lastUp, lastDown;
};

class Uint16InputNode: public InputNode{
    public:
        Uint16InputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t refreshInterval, uint16_t webRefreshInterval);
        virtual ~Uint16InputNode() = default; 
        virtual int classType() const { return CLASS_UINT16_INPUT_NODE; }
        uint16_t getValue();
        bool specificRefresh();   

    protected:
        uint16_t hideValue = 0;
        virtual bool getNewValue(uint16_t& val) = 0;
};

class Uint32InputNode: public InputNode{
    public:
        Uint32InputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t refreshInterval, uint16_t webRefreshInterval);
        virtual ~Uint32InputNode() = default;
        virtual int classType() const { return CLASS_UINT32_INPUT_NODE; }
        uint32_t getValue();
        int32_t getValueAsInt32() { return static_cast<int32_t>(hideValue); }
        bool specificRefresh();

    protected:
        uint32_t hideValue = 0;
        virtual bool getNewValue(uint32_t& val) = 0;
};

class FloatInputNode: public InputNode{
     public:
        FloatInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t refreshInterval, uint16_t webRefreshInterval);
        virtual ~FloatInputNode() = default;
        virtual int classType() const { return CLASS_FLOAT_INPUT_NODE; }
        float getValue();
        bool specificRefresh();

    protected:
        float hideValue = 0;
        virtual bool getNewValue(float& val) = 0;
};

class Uint16OutputNode: public OutputNode{
    public: 
        Uint16OutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        virtual ~Uint16OutputNode() = default;
        virtual int classType() const { return CLASS_UINT16_OUTPUT_NODE; }
        void setValue(uint16_t newValue, bool force = false);  // ← force ici
        uint16_t getValue();
        bool specificRefresh();
    
    protected:
        uint16_t hideValue = 0; 
        virtual bool setNewValue(uint16_t newValue) = 0;  // ← pas de force ici
};

class Uint32OutputNode: public OutputNode{
    public: 
        Uint32OutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        virtual ~Uint32OutputNode() = default;
        virtual int classType() const { return CLASS_UINT32_OUTPUT_NODE; }
        void setValue(uint32_t newValue);
        void setValueFromInt32(int32_t newValue) { setValue(static_cast<uint32_t>(newValue)); }
        uint32_t getValue();
        int32_t getValueAsInt32() { return static_cast<int32_t>(hideValue); }

        bool specificRefresh();
    
    protected:
        uint32_t hideValue = 0;
        virtual bool setNewValue(uint32_t newValue) = 0;
};

class FloatOutputNode: public OutputNode{
    public: 
        FloatOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        virtual ~FloatOutputNode() = default;
        virtual int classType() const { return CLASS_FLOAT_OUTPUT_NODE; }
        void setValue(float newValue);
        float getValue();
        bool specificRefresh();
    
    protected:
        float hideValue = 0;
        virtual bool setNewValue(float newValue) = 0;
};

class TextInputNode: public InputNode{
    #define MAX_TEXT_SIZE 512
    public:
        TextInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t refreshInterval, uint16_t webRefreshInterval);
        virtual ~TextInputNode();
        virtual int classType() const { return CLASS_TEXT_INPUT_NODE; }
        char * getValue();
        bool specificRefresh();

    protected:
        // ⚠️ SEUL CHANGEMENT: char hideValue[MAX_TEXT_SIZE]; -> char* hideValue;
        char* hideValue, *tempBuffer; 

        virtual bool getNewValue(char *val) = 0;
};

class TextOutputNode: public OutputNode{
     public: 
        TextOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        virtual ~TextOutputNode();
        virtual int classType() const { return CLASS_TEXT_OUTPUT_NODE; }
        void setValue(const char * newValue);
        char * getValue();
        bool specificRefresh();
    
    protected:
        // ⚠️ SEUL CHANGEMENT: char hideValue[MAX_TEXT_SIZE]; -> char* hideValue;
        char* hideValue;
        virtual bool setNewValue(char * newValue) = 0;
};

class HardwareBooleanInputNode: public BooleanInputNode{
    public:
        // activeLow : l'entrée est vraie au niveau bas (fichier matériel de la carte).
        // pullMode : INPUT_PULLUP (défaut historique), INPUT_PULLDOWN ou INPUT.
        HardwareBooleanInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint8_t _pin, bool inputInverted, uint16_t refreshInterval, uint16_t webRefreshInterval,
                                 bool activeLow = false, uint8_t pullMode = INPUT_PULLUP);
        virtual int classType() const { return CLASS_HW_BOOLEAN_INPUT_NODE; }
        
    private:
        uint8_t pin;
        bool activeLow;
        bool getNewValue(bool& value);
};

/**
 * Entrée booléenne sur expandeur d'E/S I2C (PCF8574, PCF8575, TCA9554, AW9523...).
 * Le nom de classe est historique (KinCony A8S) : la classe sert à tous les expandeurs.
 */
class PF8574BooleanInputNode: public BooleanInputNode{
    public:
        // Historique : PCF8574 à l'adresse i2cAddr, entrées actives à l'état bas.
        PF8574BooleanInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint8_t  i2cAddr, uint8_t pin, bool inputInverted, uint16_t refreshInterval, uint16_t webRefreshInterval);
        // Expandeur quelconque (section "EXP_rx-bool" du fichier matériel).
        PF8574BooleanInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, IoExpander *expander, uint8_t pin, bool activeLow, bool inputInverted, uint16_t refreshInterval, uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_PFC8574_BOOLEAN_INPUT_NODE; }
        // Branche la ligne d'interruption des expandeurs (front descendant) ; une seule fois, gpio < 0 : aucune.
        static void attachInterruptPin(int8_t gpio);
        static void interruptHandler();
        static bool isInterrupt();
        static void clearInterruptFlag(){
            interruptFlag = false;
        }
        static long getTimeOfInterupt(){
            return timeOfInterrupt;
        }

    private:
        uint8_t pin;
        bool activeLow = true;
        bool getNewValue(bool& value);
        IoExpander *expander = nullptr;
        static long timeOfInterrupt;
        static bool interruptFlag;
        static int8_t interruptPin;
};

class ModbusNode {
protected:
    ModbusNode(uint16_t address, uint16_t offset);

    uint16_t address;
    uint16_t offset;
    MyModbus& myModbus;

public:
    uint8_t getModbusAddress() const { return (uint8_t)address; }
};

class ModbusReadCoilNode: public BooleanInputNode, public ModbusNode {
    public:
        ModbusReadCoilNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t address, uint16_t _id, bool _inputInverted, uint16_t refreshInterval,  uint16_t webRefreshInterval);
        bool getNewValue(bool &value);
        virtual int classType() const { return CLASS_MODBUS_READ_COIL; }
        uint8_t getModbusAddress() const override { return ModbusNode::getModbusAddress(); }
};

class ModbusReadMultipleInputsRegistersNode: public InputNode, public ModbusNode  {
    // Function 01
    public:
        ModbusReadMultipleInputsRegistersNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t address,  uint16_t offset, uint8_t nbValues, uint16_t refreshInterval,  uint16_t webRefreshInterval, bool *invertedBits = nullptr);
        virtual int classType() const { return CLASS_MODBUS_READ_MULTIPLE_INPUTS_STATUS; }
        uint8_t getModbusAddress() const override { return ModbusNode::getModbusAddress(); }
        bool specificRefresh();

        uint8_t getNbValues(){
            return nbValues;
        }

        class Bit {
            public:
               
                bool getValue(){
                    return bitValue;
                }

                bool getIsChanged(){
                    //Serial.printf("%lu:: getIsChanged: value: %s, hideValue: %s\r\n", millis(), bitValue ? "true": "false", bitHideValue ? "true": "false");
                    return bitValue != bitHideValue;
                }

                const bool* getValuePtr() const {
                     return &bitValue;
                }

                void setValue(bool _value){
                    bitHideValue = bitValue;
                    if (_value != bitValue){ 
                        bitValue = _value;
                        lastChange = millis();
                        //Serial.printf("%lu:: setValue: value: %s, hideValue: %s\r\n", millis(), bitValue ? "true": "false", bitHideValue ? "true": "false");

                        if (bitValue){
                            lastUp = millis();
                        } else {
                            lastDown = millis();
                        }
                    }
                     //Serial.printf("%lu:: setValue2: value: %s, hideValue: %s\r\n", millis(), bitValue ? "true": "false", bitHideValue ? "true": "false");
                }

                uint32_t getLastChange(){
                    return lastChange;
                }

                bool isRisingEdge(){
                     return bitValue && getIsChanged();
                }

                bool isFallingEdge(){
                     return !bitValue && getIsChanged();
                }

                bool isUpFrom(uint32_t time){
                    return bitValue && (millis() - lastChange > time);
                }

                bool isDownFrom(uint32_t time){
                    return !bitValue && (millis() - lastChange > time);
                }
            
            private:
                bool bitValue = false;
                bool bitHideValue = false;
                uint32_t lastUp = 0;
                uint32_t lastDown = 0;
                uint32_t lastChange = 0;
        };
        
        Bit *bits;

        Bit *getBit(uint id){
            if (!bits || id >= nbValues) {
                Serial.printf("Error: Invalid bits access in getBit(). bits=%p, id=%d, nbValues=%d\n", bits, id, nbValues);
                return nullptr;
            }
            return &bits[id];
        }
        
    private:
        uint8_t  nbValues;
        bool *inverted;
        bool getNewValues(bool values[]);
};

class ModbusReadHoldingRegister: public Uint16InputNode, public ModbusNode {
    // Function 03
    public:
        ModbusReadHoldingRegister(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t address, uint16_t _id, uint16_t refreshInterval,  uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_MODBUS_READHOLDINGREGISTER; }
        uint8_t getModbusAddress() const override { return ModbusNode::getModbusAddress(); }
        private:
            bool getNewValue(uint16_t &value);
};

class ModbusReadDobbleInputRegisters: public Uint32InputNode, public ModbusNode {
    public:
        ModbusReadDobbleInputRegisters(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t address, uint16_t _id, uint16_t refreshInterval,  uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_MODBUS_READ_DOUBLE_INPUTREGISTER; }
        uint8_t getModbusAddress() const override { return ModbusNode::getModbusAddress(); }
        private:
            bool getNewValue(uint32_t& val);
};

class ModbusReadDoubleHoldingRegisters: public Uint32InputNode, public ModbusNode{
    public:
        ModbusReadDoubleHoldingRegisters(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t address, uint16_t _id, uint16_t refreshInterval, uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_MODBUS_READ_DOUBLE_HOLDING_REGISTER; }
        uint8_t getModbusAddress() const override { return ModbusNode::getModbusAddress(); }
        private:
            bool getNewValue(uint32_t &value);
};

class ModbusWriteHoldingRegister: public Uint16OutputNode, public ModbusNode {
    // Function 06
    public:
        ModbusWriteHoldingRegister(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t address, uint16_t _id, uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_MODBUS_WRITEHOLDINGREGISTER; }
        uint8_t getModbusAddress() const override { return ModbusNode::getModbusAddress(); }
    private:
        bool setNewValue(uint16_t newValue) override;  // ← setNewValue, pas setValue
};

class ModbusWriteDoubleHoldingRegister: public Uint32OutputNode, public ModbusNode {
    // Function 06
    public:
        ModbusWriteDoubleHoldingRegister(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t address, uint16_t _id, uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_MODBUS_WRITE_DOUBLE_HOLDING_REGISTER; }
        uint8_t getModbusAddress() const override { return ModbusNode::getModbusAddress(); }
        bool setNewValue(uint32_t newValue);
    
        //    bool setNewValue(uint32_t newValue);
};

class ModbusReadInputRegister: public Uint16InputNode, public ModbusNode {
    // Function 04
    public:
        ModbusReadInputRegister(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t address, uint16_t _id, uint16_t refreshInterval,  uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_MODBUS_READINPUTREGISTER; }
        uint8_t getModbusAddress() const override { return ModbusNode::getModbusAddress(); }
        private:
            bool getNewValue(uint16_t &value);
};


class HardwareBooleanOutputNode: public BooleanOutputNode{
    public:
        // activeLow : la sortie est active au niveau bas ; la broche est mise au repos à sa première création.
        HardwareBooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint8_t _pin, uint16_t webRefreshInterval, bool activeLow = false);
        virtual int classType() const { return CLASS_HW_BOOLEAN_OUTPUT_NODE; }
        uint8_t getPinNumber() const;
        bool isActiveLow() const { return activeLow; }
    private:
        bool setNewValue(bool newValue);

        uint16_t address;  // Adresse de l'esclave Modbus
        uint16_t offset;   // Offset du registre
        uint8_t pin;
        bool activeLow;
};

/**
 * Sortie booléenne sur expandeur d'E/S I2C (PCF8574, PCF8575, TCA9554, AW9523...).
 * Le nom de classe est historique (KinCony A8S) : la classe sert à tous les expandeurs.
 */
class PF8574BooleanOutputNode: public BooleanOutputNode{
    public:
        // Historique : PCF8574 à l'adresse i2cAddr, sorties actives à l'état bas.
        PF8574BooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint8_t  i2cAddr, uint8_t pin, uint16_t webRefreshInterval);
        // Expandeur quelconque (section "EXP_tx-bool" du fichier matériel).
        PF8574BooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, IoExpander *expander, uint8_t pin, bool activeLow, uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_PFC8574_BOOLEAN_OUTPUT_NODE; }
    
    private:
        uint8_t pin;
        bool activeLow = true;
        bool setNewValue(bool newValue);
        void setupPin();
        IoExpander *expander = nullptr;
};

class PCA9554BooleanOutputNode: public BooleanOutputNode{
    public:
        PCA9554BooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint8_t i2cAddr, uint8_t pin, bool outputInverted, uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_PCA9554_BOOLEAN_OUTPUT_NODE; }

    private:
        uint8_t pin;
        bool outputInverted;
        bool setNewValue(bool newValue);
        Pca9554Driver *pca9554Tx = nullptr;
};

class ModbusWriteCoilNode: public BooleanOutputNode, public ModbusNode {
    // Function 05
    public:
        ModbusWriteCoilNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t address, uint16_t _id, uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_MODBUS_WRITE_COIL_NODE; }
        uint8_t getModbusAddress() const override { return ModbusNode::getModbusAddress(); }
        private:
            bool setNewValue(bool newValue);
            uint8_t pin;
};

class ModbusWriteMultipleCoilslNode: public OutputNode, public ModbusNode {
    public:
        ModbusWriteMultipleCoilslNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t address, uint16_t _register, uint8_t nbValues, uint16_t webRefreshInterval);
        virtual ~ModbusWriteMultipleCoilslNode();

        virtual int classType() const { return CLASS_MODBUS_WRITE_MULTIPLE_COILS; }
        uint8_t getModbusAddress() const override { return ModbusNode::getModbusAddress(); }

        bool specificRefresh();
        uint8_t getNbValues();
        bool getValue(uint8_t id);
        bool setValue(bool value, uint8_t id);
        bool setValues(bool *vals, uint8_t nbValues);
        
        private:
            bool setNewValues();   
            bool *values, *hideValues;
            uint8_t nbValues;
};

class VirtualBooleanInputNode: public BooleanInputNode, public VirutalVisu{
    public:
        VirtualBooleanInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, bool _inputInverted, uint16_t webRefreshInterval);
        virtual int classType() const {return CLASS_VIRTUAL_BOOLEAN_INPUT_NODE; }
        void setValue(bool _value);
        bool specificRefresh() override;

    private:
        bool getNewValue(bool& value){
            
            return true;
        };
};

class VirtualUint32InputNode: public Uint32InputNode{
    public:
        VirtualUint32InputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        virtual int classType() const {return CLASS_VIRTUAL_UINT32_INPUT_NODE; }
        void setValue(uint32_t _value);
        bool specificRefresh() override;

    private:
        bool getNewValue(uint32_t& value){
            return true;
        }; 
};

class VirtualFloatInputNode: public FloatInputNode{
    public:
        VirtualFloatInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        virtual int classType() const {return CLASS_VIRTUAL_FLOAT_INPUT_NODE; }
        void setValue(float _value);
        bool specificRefresh() override;

    private:
        bool getNewValue(float& val){
            return true;
        };  
};

class VirtualBooleanOutputNode: public BooleanOutputNode{
    public:
        VirtualBooleanOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        virtual int classType() const { return CLASS_VIRTUAL_BOOLEAN_OUTPUT_NODE ; }

        bool specificRefresh() override {
            // Pas de hardware à synchroniser, mais on doit clear le flag
            if (updateNeeded){
                lastChange = millis();
                updateNeeded = false;
                // Serial.printf("%lu:: VirtualBooleanOutputNode::node: %s, new value: %s\r\n", millis(), name, hideValue ? "true" : "false");
                _notifyPending = true;
            }
            
            return true;
        }

    private:    
        bool setNewValue(bool value){
            return true;
        };
};

class VirtualUint32OutputNode: public Uint32OutputNode{
    public:
        VirtualUint32OutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        virtual int classType() const {return CLASS_VIRTUAL_UINT32_OUTPUT_NODE; }

        bool specificRefresh() override {
            // Pas de hardware à synchroniser, mais on doit clear le flag
            if (updateNeeded){
                lastChange = millis();
                updateNeeded = false;
                Serial.printf("%lu:: VirtualUint32OutputNode::node: %s, new value: %lu\r\n", millis(), name, (unsigned long)hideValue);
                _notifyPending = true;
            }
            return true;
        }

    private:
        bool setNewValue(uint32_t value){
            return true;
        };
};

class VirtualFloatOutputNode: public FloatOutputNode{
    public:
        VirtualFloatOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        virtual int classType() const {return CLASS_VIRTUAL_FLOAT_OUTPUT_NODE; }

        bool specificRefresh() override {
            // Pas de hardware à synchroniser, mais on doit clear le flag
            if (updateNeeded){
                lastChange = millis();
                updateNeeded = false;
                Serial.printf("%lu:: VirtualFloatOutputNode::node: %s, new value: %.2f\r\n", millis(), name, hideValue);
                 _notifyPending = true;
            }
            return true;
        }

    private:
        bool setNewValue(float value){
            return true;
        };
};

class VirtualTextInputNode: public TextInputNode{
    public:
        VirtualTextInputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        ~VirtualTextInputNode() = default;
        virtual int classType() const {return CLASS_VIRTUAL_TEXT_INPUT_NODE; }
        void setValue(const char *text);

    private:
        bool getNewValue(char *value){
            return true;
        }
};

class VirtualTextOutputNode: public TextOutputNode{
     public:
        VirtualTextOutputNode(char *name, char *parentName, uint16_t id, uint32_t hash, uint16_t webRefreshInterval);
        ~VirtualTextOutputNode() = default;
        virtual int classType() const {return CLASS_VIRTUAL_TEXT_OUTPUT_NODE; }

        bool specificRefresh() override {
            // Pas de hardware à synchroniser, mais on doit clear le flag
            if (updateNeeded){
                lastChange = millis();
                updateNeeded = false;
                Serial.printf("%lu:: VirtualTextOutputNode::node: %s, new value: %s\r\n", millis(), name, hideValue);
                _notifyPending = true;
            }
            return true;
        }
        
    private:
        bool setNewValue(char * value) {
            // ✅ FIX: Ne rien faire - setValue() a déjà copié la valeur
            return true;
        }
};
        
#endif
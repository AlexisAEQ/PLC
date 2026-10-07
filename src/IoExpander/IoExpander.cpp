#include "IoExpander/IoExpander.h"
#include <Wire.h>
#include <PCF8574.h>
#include <map>

// =============================================================================
// Accès registres I2C communs
// =============================================================================
namespace {

bool i2cProbe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) {
        return false;
    }
    value = Wire.read();
    return true;
}

// -----------------------------------------------------------------------------
// PCF8574 : 8 voies quasi-bidirectionnelles. On conserve la bibliothèque robtillaart
// utilisée jusqu'ici (comportement identique pour les cartes existantes).
// -----------------------------------------------------------------------------
class Pcf8574Expander : public IoExpander {
public:
    explicit Pcf8574Expander(uint8_t addr) : IoExpander(addr), _pcf(addr) {}

    bool begin() override { return _pcf.begin(); }  // toutes les voies à 1 (entrées / sorties inactives)

    bool configurePin(uint8_t pin, bool output) override {
        // Quasi-bidirectionnel : une entrée doit rester à 1.
        if (!output && pin < 8) _pcf.write(pin, HIGH);
        return pin < 8;
    }

    bool readPin(uint8_t pin, bool& level) override {
        if (pin >= 8) return false;
        level = _pcf.read(pin) != 0;
        return _pcf.lastError() == PCF8574_OK;
    }

    bool writePin(uint8_t pin, bool level) override {
        if (pin >= 8) return false;
        _pcf.write(pin, level ? HIGH : LOW);
        return _pcf.lastError() == PCF8574_OK;
    }

    uint8_t pinCount() const override { return 8; }
    const char* chipName() const override { return "PCF8574"; }

private:
    PCF8574 _pcf;
};

// -----------------------------------------------------------------------------
// PCF8575 : 16 voies quasi-bidirectionnelles (P00-P07 = voies 0-7, P10-P17 = voies 8-15).
// Pas de registre : on écrit/lit 2 octets. Les entrées doivent rester à 1.
// -----------------------------------------------------------------------------
class Pcf8575Expander : public IoExpander {
public:
    explicit Pcf8575Expander(uint8_t addr) : IoExpander(addr) {}

    bool begin() override {
        if (!i2cProbe(_addr)) return false;
        return writeAll();
    }

    bool configurePin(uint8_t pin, bool output) override {
        if (pin >= 16) return false;
        if (!output) {
            _shadow |= (1u << pin);
            return writeAll();
        }
        return true;
    }

    bool readPin(uint8_t pin, bool& level) override {
        if (pin >= 16) return false;
        if (Wire.requestFrom(_addr, (uint8_t)2) != 2) return false;
        uint16_t value = Wire.read();
        value |= (uint16_t)Wire.read() << 8;
        level = (value >> pin) & 1;
        return true;
    }

    bool writePin(uint8_t pin, bool level) override {
        if (pin >= 16) return false;
        if (level) _shadow |= (1u << pin);
        else _shadow &= ~(1u << pin);
        return writeAll();
    }

    uint8_t pinCount() const override { return 16; }
    const char* chipName() const override { return "PCF8575"; }

private:
    uint16_t _shadow = 0xFFFF;  // au démarrage : tout à 1 (entrées et sorties actives à l'état bas inactives)

    bool writeAll() {
        Wire.beginTransmission(_addr);
        Wire.write((uint8_t)(_shadow & 0xFF));
        Wire.write((uint8_t)(_shadow >> 8));
        return Wire.endTransmission() == 0;
    }
};

// -----------------------------------------------------------------------------
// TCA9554 / PCA9554 / TCA9534 / PCA9534 : 8 voies à registres.
// 0x00 entrées, 0x01 sorties, 0x02 inversion de polarité, 0x03 configuration (1 = entrée).
// -----------------------------------------------------------------------------
class Tca9554Expander : public IoExpander {
public:
    Tca9554Expander(uint8_t addr, const char* name) : IoExpander(addr), _name(name) {}

    bool begin() override {
        if (!i2cProbe(_addr)) return false;
        // Sorties au repos (0) avant tout passage en sortie, polarité normale, tout en entrée.
        return i2cWriteReg(_addr, REG_OUTPUT, _output) && i2cWriteReg(_addr, REG_POLARITY, 0x00) &&
               i2cWriteReg(_addr, REG_CONFIG, _config);
    }

    bool configurePin(uint8_t pin, bool output) override {
        if (pin >= 8) return false;
        if (output) _config &= ~(1u << pin);
        else _config |= (1u << pin);
        return i2cWriteReg(_addr, REG_CONFIG, _config);
    }

    bool readPin(uint8_t pin, bool& level) override {
        if (pin >= 8) return false;
        uint8_t value = 0;
        if (!i2cReadReg(_addr, REG_INPUT, value)) return false;
        level = (value >> pin) & 1;
        return true;
    }

    bool writePin(uint8_t pin, bool level) override {
        if (pin >= 8) return false;
        if (level) _output |= (1u << pin);
        else _output &= ~(1u << pin);
        return i2cWriteReg(_addr, REG_OUTPUT, _output);
    }

    uint8_t pinCount() const override { return 8; }
    const char* chipName() const override { return _name; }

private:
    static const uint8_t REG_INPUT = 0x00;
    static const uint8_t REG_OUTPUT = 0x01;
    static const uint8_t REG_POLARITY = 0x02;
    static const uint8_t REG_CONFIG = 0x03;
    const char* _name;
    uint8_t _output = 0x00;
    uint8_t _config = 0xFF;
};

// -----------------------------------------------------------------------------
// AW9523B : 16 voies (port 0 = voies 0-7, port 1 = voies 8-15).
// 0x00/0x01 entrées, 0x02/0x03 sorties, 0x04/0x05 configuration (0 = sortie, 1 = entrée),
// 0x06/0x07 masque d'interruption (1 = désactivée), 0x10 identifiant (0x23),
// 0x11 GCR (bit 4 : port 0 en push-pull), 0x12/0x13 mode LED (1 = GPIO), 0x7F reset logiciel.
// Registres repris de la bibliothèque M5StamPLC (utils/aw9523).
// -----------------------------------------------------------------------------
class Aw9523Expander : public IoExpander {
public:
    explicit Aw9523Expander(uint8_t addr) : IoExpander(addr) {}

    bool begin() override {
        uint8_t id = 0;
        if (!i2cReadReg(_addr, REG_CHIP_ID, id) || id != 0x23) return false;
        if (!i2cWriteReg(_addr, REG_SOFT_RESET, 0x00)) return false;
        delay(1);
        uint8_t gcr = 0;
        i2cReadReg(_addr, REG_GCR, gcr);
        return i2cWriteReg(_addr, REG_GCR, gcr | 0x10) &&            // port 0 en push-pull
               i2cWriteReg(_addr, REG_LED_MODE, 0xFF) && i2cWriteReg(_addr, REG_LED_MODE + 1, 0xFF) &&
               i2cWriteReg(_addr, REG_INT_ENABLE, _intMask[0]) && i2cWriteReg(_addr, REG_INT_ENABLE + 1, _intMask[1]) &&
               i2cWriteReg(_addr, REG_OUTPUT, 0x00) && i2cWriteReg(_addr, REG_OUTPUT + 1, 0x00) &&
               i2cWriteReg(_addr, REG_CONFIG, _config[0]) && i2cWriteReg(_addr, REG_CONFIG + 1, _config[1]);
    }

    bool configurePin(uint8_t pin, bool output) override {
        if (pin >= 16) return false;
        uint8_t port = pin / 8, bit = 1u << (pin % 8);
        if (output) {
            _config[port] &= ~bit;
            _intMask[port] |= bit;
        } else {
            _config[port] |= bit;
            _intMask[port] &= ~bit;  // interruption sur changement d'état des entrées (ligne INT)
        }
        return i2cWriteReg(_addr, REG_CONFIG + port, _config[port]) &&
               i2cWriteReg(_addr, REG_INT_ENABLE + port, _intMask[port]);
    }

    bool readPin(uint8_t pin, bool& level) override {
        if (pin >= 16) return false;
        uint8_t value = 0;
        if (!i2cReadReg(_addr, REG_INPUT + pin / 8, value)) return false;
        level = (value >> (pin % 8)) & 1;
        return true;
    }

    bool writePin(uint8_t pin, bool level) override {
        if (pin >= 16) return false;
        uint8_t port = pin / 8, bit = 1u << (pin % 8);
        if (level) _output[port] |= bit;
        else _output[port] &= ~bit;
        return i2cWriteReg(_addr, REG_OUTPUT + port, _output[port]);
    }

    uint8_t pinCount() const override { return 16; }
    const char* chipName() const override { return "AW9523"; }

private:
    static const uint8_t REG_INPUT = 0x00;
    static const uint8_t REG_OUTPUT = 0x02;
    static const uint8_t REG_CONFIG = 0x04;
    static const uint8_t REG_INT_ENABLE = 0x06;
    static const uint8_t REG_CHIP_ID = 0x10;
    static const uint8_t REG_GCR = 0x11;
    static const uint8_t REG_LED_MODE = 0x12;
    static const uint8_t REG_SOFT_RESET = 0x7F;
    uint8_t _output[2] = {0x00, 0x00};
    uint8_t _config[2] = {0xFF, 0xFF};
    uint8_t _intMask[2] = {0xFF, 0xFF};  // 1 = interruption désactivée
};

IoExpander* createExpander(const char* chip, uint8_t addr) {
    if (!strcasecmp(chip, "PCF8574")) return new Pcf8574Expander(addr);
    if (!strcasecmp(chip, "PCF8575")) return new Pcf8575Expander(addr);
    if (!strcasecmp(chip, "TCA9554")) return new Tca9554Expander(addr, "TCA9554");
    if (!strcasecmp(chip, "PCA9554")) return new Tca9554Expander(addr, "PCA9554");
    if (!strcasecmp(chip, "TCA9534")) return new Tca9554Expander(addr, "TCA9534");
    if (!strcasecmp(chip, "PCA9534")) return new Tca9554Expander(addr, "PCA9534");
    if (!strcasecmp(chip, "AW9523")) return new Aw9523Expander(addr);
    return nullptr;
}

std::map<uint16_t, IoExpander*>& instances() {
    static std::map<uint16_t, IoExpander*> map;
    return map;
}

uint16_t chipKey(const char* chip, uint8_t addr) {
    // Une adresse I2C ne porte qu'un seul chip : la clé est l'adresse, le nom sert au contrôle.
    (void)chip;
    return addr;
}

}  // namespace

bool IoExpander::setupOutput(uint8_t pin, bool idleLevel) {
    if (pin >= pinCount()) return false;
    uint16_t bit = 1u << pin;
    if (_outputsReady & bit) return true;
    if (!writePin(pin, idleLevel) || !configurePin(pin, true)) return false;
    _outputsReady |= bit;
    _inputsReady &= ~bit;
    return true;
}

bool IoExpander::setupInput(uint8_t pin) {
    if (pin >= pinCount()) return false;
    uint16_t bit = 1u << pin;
    if (_inputsReady & bit) return true;
    if (!configurePin(pin, false)) return false;
    _inputsReady |= bit;
    _outputsReady &= ~bit;
    return true;
}

bool IoExpanderManager::isKnownChip(const char* chip) {
    IoExpander* probe = chip ? createExpander(chip, 0) : nullptr;
    bool known = probe != nullptr;
    delete probe;
    return known;
}

IoExpander* IoExpanderManager::getOrCreate(const char* chip, uint8_t i2cAddr) {
    if (!chip) return nullptr;
    auto& map = instances();
    auto it = map.find(chipKey(chip, i2cAddr));
    if (it != map.end()) {
        if (strcasecmp(it->second->chipName(), chip) != 0) {
            Serial.printf("❌ Adresse I2C 0x%02X déjà utilisée par un %s (demandé : %s)\n", i2cAddr, it->second->chipName(), chip);
            return nullptr;
        }
        return it->second;
    }

    IoExpander* expander = createExpander(chip, i2cAddr);
    if (!expander) {
        Serial.printf("❌ Expandeur inconnu : %s\n", chip);
        return nullptr;
    }
    Serial.printf("🔧 Initialisation %s à 0x%02X...\n", expander->chipName(), i2cAddr);
    if (!expander->begin()) {
        Serial.printf("❌ %s absent ou en défaut à 0x%02X\n", expander->chipName(), i2cAddr);
        delete expander;
        return nullptr;
    }
    Serial.printf("✅ %s initialisé à 0x%02X\n", expander->chipName(), i2cAddr);
    map[chipKey(chip, i2cAddr)] = expander;
    return expander;
}

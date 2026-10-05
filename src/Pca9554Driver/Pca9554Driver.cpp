#include "Pca9554Driver/Pca9554Driver.h"
#include <Wire.h>

Pca9554Driver::Pca9554Driver(uint8_t i2cAddr)
    : _i2cAddr(i2cAddr)
{
}

bool Pca9554Driver::begin() {
    Wire.beginTransmission(_i2cAddr);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    // Sorties à 0 avant de configurer quoi que ce soit en sortie (évite un état transitoire)
    return writeRegister(REG_OUTPUT, _outputShadow);
}

void Pca9554Driver::registerOutputPin(uint8_t pin) {
    _configShadow &= ~(1 << pin); // 0 = sortie sur ce registre
    writeRegister(REG_CONFIG, _configShadow);
}

bool Pca9554Driver::write(uint8_t pin, bool value) {
    if (value) {
        _outputShadow |= (1 << pin);
    } else {
        _outputShadow &= ~(1 << pin);
    }
    return writeRegister(REG_OUTPUT, _outputShadow);
}

bool Pca9554Driver::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(_i2cAddr);
    Wire.write(reg);
    Wire.write(value);
    return (Wire.endTransmission() == 0);
}

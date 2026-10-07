#include "Rx8130Driver/Rx8130Driver.h"
#include <Wire.h>

Rx8130Driver::Rx8130Driver()
    : _consecutiveErrors(0)
    , _deviceAvailable(true)
{
}

bool Rx8130Driver::detect() {
    Wire.beginTransmission(RX8130_ADDR);
    if (Wire.endTransmission() != 0) {
        return false;
    }
    // Charge de la batterie de sauvegarde (comme la bibliothèque M5StamPLC).
    uint8_t ctrl1 = 0;
    if (readRegister(REG_CTRL1, ctrl1)) {
        writeRegister(REG_CTRL1, ctrl1 | 0x30);
    }
    return true;
}

bool Rx8130Driver::readRegister(uint8_t reg, uint8_t& value) const {
    Wire.beginTransmission(RX8130_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) {
        return false;
    }
    Wire.requestFrom(RX8130_ADDR, (uint8_t)1);
    if (!Wire.available()) {
        return false;
    }
    value = Wire.read();
    return true;
}

bool Rx8130Driver::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(RX8130_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return (Wire.endTransmission() == 0);
}

void Rx8130Driver::handleReadFailure() const {
    _consecutiveErrors++;
    if (_consecutiveErrors >= MAX_CONSECUTIVE_RTC_ERRORS) {
        _deviceAvailable = false;
        Serial.println("⚠️ RX8130 RTC marqué comme indisponible après 3 échecs");
    }
}

void Rx8130Driver::handleReadSuccess() const {
    if (_consecutiveErrors > 0 || !_deviceAvailable) {
        Serial.println("✅ RX8130 RTC rétabli");
    }
    _consecutiveErrors = 0;
    _deviceAvailable = true;
}

bool Rx8130Driver::readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                                 uint8_t& hour, uint8_t& minute, uint8_t& second) {
    if (!_deviceAvailable) {
        return false;
    }
    Wire.beginTransmission(RX8130_ADDR);
    Wire.write(REG_SECONDS);
    if (Wire.endTransmission() != 0) {
        handleReadFailure();
        return false;
    }
    Wire.requestFrom(RX8130_ADDR, (uint8_t)7);
    if (Wire.available() < 7) {
        handleReadFailure();
        return false;
    }
    uint8_t regs[7];
    for (int i = 0; i < 7; i++) {
        regs[i] = Wire.read();
    }
    handleReadSuccess();

    // [0]=secondes [1]=minutes [2]=heures [3]=jour de semaine (un bit par jour)
    // [4]=jour du mois [5]=mois [6]=année
    second = bcd2bin(regs[0] & 0x7F);
    minute = bcd2bin(regs[1] & 0x7F);
    hour   = bcd2bin(regs[2] & 0x3F);
    day    = bcd2bin(regs[4] & 0x3F);
    month  = bcd2bin(regs[5] & 0x1F);
    year   = 2000 + bcd2bin(regs[6]);
    return true;
}

bool Rx8130Driver::writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                                  uint8_t hour, uint8_t minute, uint8_t second) {
    // Horloge arrêtée pendant l'écriture (bit STOP), comme le préconise le fabricant.
    uint8_t ctrl0 = 0;
    readRegister(REG_CTRL0, ctrl0);
    writeRegister(REG_CTRL0, ctrl0 | BIT_STOP);

    Wire.beginTransmission(RX8130_ADDR);
    Wire.write(REG_SECONDS);
    Wire.write(bin2bcd(second));
    Wire.write(bin2bcd(minute));
    Wire.write(bin2bcd(hour));
    Wire.write((uint8_t)(1 << dayOfWeek(year, month, day)));
    Wire.write(bin2bcd(day));
    Wire.write(bin2bcd(month));
    Wire.write(bin2bcd(year - 2000));
    bool ok = (Wire.endTransmission() == 0);

    writeRegister(REG_CTRL0, ctrl0 & ~BIT_STOP);
    if (ok) {
        clearLostPowerFlag();  // horloge réécrite = intègre
    }
    return ok;
}

uint8_t Rx8130Driver::dayOfWeek(uint16_t year, uint8_t month, uint8_t day) const {
    // Formule de Zeller, ramenée à 0=Dimanche..6=Samedi
    uint16_t y = year;
    uint8_t m = month;
    if (m < 3) {
        m += 12;
        y--;
    }
    uint8_t c = y / 100;
    y = y % 100;
    int dow = (day + (13 * (m + 1)) / 5 + y + y / 4 + c / 4 - 2 * c) % 7;
    return (uint8_t)((dow + 7 + 6) % 7);
}

bool Rx8130Driver::hasLostPower() const {
    uint8_t flags = 0;
    if (!readRegister(REG_FLAG, flags)) {
        return false;
    }
    return (flags & BIT_VLF) != 0;
}

void Rx8130Driver::clearLostPowerFlag() {
    uint8_t flags = 0;
    if (!readRegister(REG_FLAG, flags)) {
        return;
    }
    writeRegister(REG_FLAG, flags & ~BIT_VLF);
    Serial.println("[RTC] Flag perte alimentation effacé (RX8130)");
}

#include "Ds3231Driver/Ds3231Driver.h"
#include <Wire.h>

Ds3231Driver::Ds3231Driver()
    : _consecutiveErrors(0)
    , _deviceAvailable(true)
{
}

bool Ds3231Driver::detect() {
    Wire.beginTransmission(DS3231_ADDR);
    return (Wire.endTransmission() == 0);
}

bool Ds3231Driver::readRawRegisters(uint8_t* buffer) const {
    if (!buffer) {
        return false;
    }

    if (!_deviceAvailable) {
        return false;
    }

    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(REG_SECONDS);
    if (Wire.endTransmission() != 0) {
        handleReadFailure();
        return false;
    }

    Wire.requestFrom(DS3231_ADDR, (uint8_t)7);
    if (Wire.available() < 7) {
        handleReadFailure();
        return false;
    }

    for (int i = 0; i < 7; i++) {
        buffer[i] = Wire.read();
    }

    handleReadSuccess();
    return true;
}

void Ds3231Driver::handleReadFailure() const {
    _consecutiveErrors++;
    if (_consecutiveErrors >= MAX_CONSECUTIVE_RTC_ERRORS) {
        _deviceAvailable = false;
        Serial.println("⚠️ DS3231 RTC marqué comme indisponible après 3 échecs");
    }
}

void Ds3231Driver::handleReadSuccess() const {
    if (_consecutiveErrors > 0 || !_deviceAvailable) {
        Serial.println("✅ DS3231 RTC rétabli");
    }
    _consecutiveErrors = 0;
    _deviceAvailable = true;
}

bool Ds3231Driver::writeRawRegisters(const uint8_t* buffer) {
    if (!buffer) return false;

    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(REG_SECONDS);
    for (int i = 0; i < 7; i++) {
        Wire.write(buffer[i]);
    }

    return (Wire.endTransmission() == 0);
}

bool Ds3231Driver::readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                                 uint8_t& hour, uint8_t& minute, uint8_t& second) {
    uint8_t regs[7];
    if (!readRawRegisters(regs)) {
        return false;
    }

    second = bcd2bin(regs[0] & 0x7F);
    minute = bcd2bin(regs[1]);
    hour   = bcd2bin(regs[2]);
    day    = bcd2bin(regs[4]);
    month  = bcd2bin(regs[5] & 0x7F);
    year   = 2000 + bcd2bin(regs[6]);

    return true;
}

bool Ds3231Driver::writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                                  uint8_t hour, uint8_t minute, uint8_t second) {
    uint8_t regs[7];
    regs[0] = bin2bcd(second);
    regs[1] = bin2bcd(minute);
    regs[2] = bin2bcd(hour);
    regs[3] = dayOfWeek(year, month, day);
    regs[4] = bin2bcd(day);
    regs[5] = bin2bcd(month);
    regs[6] = bin2bcd(year - 2000);

    return writeRawRegisters(regs);
}

uint8_t Ds3231Driver::dayOfWeek(uint16_t year, uint8_t month, uint8_t day) const {
    // Formule de Zeller (retourne 1-7, 1=Dimanche)
    uint16_t y = year;
    uint8_t m = month;

    if (m < 3) {
        m += 12;
        y--;
    }

    uint8_t c = y / 100;
    y = y % 100;

    uint8_t dow = (day + (13 * (m + 1)) / 5 + y + y/4 + c/4 - 2*c) % 7;
    return (dow + 7) % 7 + 1;
}

bool Ds3231Driver::hasLostPower() const {
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(REG_STATUS);
    Wire.endTransmission();

    Wire.requestFrom(DS3231_ADDR, (uint8_t)1);
    if (Wire.available()) {
        uint8_t status = Wire.read();
        return (status & 0x80) != 0; // Bit OSF
    }

    return false;
}

void Ds3231Driver::clearLostPowerFlag() {
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(REG_STATUS);
    Wire.write(0x00); // Clear tous les flags
    Wire.endTransmission();

    Serial.println("[RTC] Flag perte alimentation effacé");
}

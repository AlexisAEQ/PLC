#include "Pcf8563Driver/Pcf8563Driver.h"
#include <Wire.h>

Pcf8563Driver::Pcf8563Driver()
    : _consecutiveErrors(0)
    , _deviceAvailable(true)
{
}

bool Pcf8563Driver::detect() {
    Wire.beginTransmission(PCF8563_ADDR);
    if (Wire.endTransmission() != 0) {
        return false;
    }
    // Horloge en marche (bit STOP du registre de contrôle 1 à 0), mode normal.
    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(REG_CONTROL1);
    Wire.write((uint8_t)0x00);
    return (Wire.endTransmission() == 0);
}

bool Pcf8563Driver::readRawRegisters(uint8_t* buffer) const {
    if (!buffer || !_deviceAvailable) {
        return false;
    }

    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(REG_SECONDS);
    if (Wire.endTransmission() != 0) {
        handleReadFailure();
        return false;
    }

    Wire.requestFrom(PCF8563_ADDR, (uint8_t)7);
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

void Pcf8563Driver::handleReadFailure() const {
    _consecutiveErrors++;
    if (_consecutiveErrors >= MAX_CONSECUTIVE_RTC_ERRORS) {
        _deviceAvailable = false;
        Serial.println("⚠️ PCF8563 RTC marqué comme indisponible après 3 échecs");
    }
}

void Pcf8563Driver::handleReadSuccess() const {
    if (_consecutiveErrors > 0 || !_deviceAvailable) {
        Serial.println("✅ PCF8563 RTC rétabli");
    }
    _consecutiveErrors = 0;
    _deviceAvailable = true;
}

bool Pcf8563Driver::writeRawRegisters(const uint8_t* buffer) {
    if (!buffer) return false;

    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(REG_SECONDS);
    for (int i = 0; i < 7; i++) {
        Wire.write(buffer[i]);
    }
    return (Wire.endTransmission() == 0);
}

bool Pcf8563Driver::readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                                  uint8_t& hour, uint8_t& minute, uint8_t& second) {
    uint8_t regs[7];
    if (!readRawRegisters(regs)) {
        return false;
    }

    // [0]=secondes(bit7=VL) [1]=minutes [2]=heures [3]=jour du mois
    // [4]=jour de semaine [5]=mois(bit7=siècle) [6]=année
    second = bcd2bin(regs[0] & 0x7F);
    minute = bcd2bin(regs[1] & 0x7F);
    hour   = bcd2bin(regs[2] & 0x3F);
    day    = bcd2bin(regs[3] & 0x3F);
    month  = bcd2bin(regs[5] & 0x1F);
    year   = 2000 + bcd2bin(regs[6]);

    return true;
}

bool Pcf8563Driver::writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                                   uint8_t hour, uint8_t minute, uint8_t second) {
    uint8_t regs[7];
    regs[0] = bin2bcd(second); // bit VL remis à 0 : horloge réécrite = intègre
    regs[1] = bin2bcd(minute);
    regs[2] = bin2bcd(hour);
    regs[3] = bin2bcd(day);
    regs[4] = dayOfWeek(year, month, day);
    regs[5] = bin2bcd(month);  // bit siècle à 0 : années 2000-2099
    regs[6] = bin2bcd(year - 2000);

    return writeRawRegisters(regs);
}

uint8_t Pcf8563Driver::dayOfWeek(uint16_t year, uint8_t month, uint8_t day) const {
    // Formule de Zeller, ramenée à 0=Dimanche..6=Samedi (registre Weekdays du PCF8563)
    uint16_t y = year;
    uint8_t m = month;

    if (m < 3) {
        m += 12;
        y--;
    }

    uint8_t c = y / 100;
    y = y % 100;

    int dow = (day + (13 * (m + 1)) / 5 + y + y / 4 + c / 4 - 2 * c) % 7;
    return (uint8_t)((dow + 7 + 6) % 7);  // Zeller : 0=Samedi -> 0=Dimanche
}

bool Pcf8563Driver::hasLostPower() const {
    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(REG_SECONDS);
    Wire.endTransmission();

    Wire.requestFrom(PCF8563_ADDR, (uint8_t)1);
    if (Wire.available()) {
        uint8_t seconds = Wire.read();
        return (seconds & 0x80) != 0; // Bit VL (Voltage Low) : intégrité de l'horloge non garantie
    }
    return false;
}

void Pcf8563Driver::clearLostPowerFlag() {
    // Le bit VL vit dans le registre Secondes : on préserve les secondes en cours.
    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(REG_SECONDS);
    Wire.endTransmission();

    Wire.requestFrom(PCF8563_ADDR, (uint8_t)1);
    if (!Wire.available()) {
        return;
    }
    uint8_t seconds = Wire.read();

    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(REG_SECONDS);
    Wire.write(seconds & 0x7F);
    Wire.endTransmission();

    Serial.println("[RTC] Flag perte alimentation effacé (PCF8563)");
}

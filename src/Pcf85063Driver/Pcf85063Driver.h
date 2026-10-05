#ifndef PCF85063_DRIVER_H
#define PCF85063_DRIVER_H

#include <Arduino.h>
#include "IRtcDriver/IRtcDriver.h"

/**
 * @brief Driver bas niveau pour le chip RTC PCF85063ATL (I2C)
 *
 * Mapping registres différent du DS3231 :
 * - Adresse I2C 0x51 (vs 0x68)
 * - Bloc horodatage à partir du registre 0x04 : secondes, minutes, heures,
 *   jour du mois, jour de semaine, mois, année
 * - Pas de registre "status" séparé : le flag de perte d'alimentation (OS,
 *   Oscillator Stop) est le bit 7 du registre Secondes lui-même
 */
class Pcf85063Driver : public IRtcDriver {
public:
    Pcf85063Driver();

    bool detect() override;

    bool readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                       uint8_t& hour, uint8_t& minute, uint8_t& second) override;

    bool writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                        uint8_t hour, uint8_t minute, uint8_t second) override;

    bool hasLostPower() const override;
    void clearLostPowerFlag() override;

    const char* chipName() const override { return "PCF85063ATL"; }

private:
    static const uint8_t PCF85063_ADDR = 0x51;
    static const uint8_t REG_SECONDS = 0x04; // + minutes,heures,jour,jour-semaine,mois,année
    static const uint8_t MAX_CONSECUTIVE_RTC_ERRORS = 3;

    bool readRawRegisters(uint8_t* buffer) const;
    bool writeRawRegisters(const uint8_t* buffer);
    void handleReadFailure() const;
    void handleReadSuccess() const;
    uint8_t dayOfWeek(uint16_t year, uint8_t month, uint8_t day) const;

    inline uint8_t bcd2bin(uint8_t val) const { return val - 6 * (val >> 4); }
    inline uint8_t bin2bcd(uint8_t val) const { return val + 6 * (val / 10); }

    mutable uint8_t _consecutiveErrors;
    mutable bool _deviceAvailable;
};

#endif // PCF85063_DRIVER_H

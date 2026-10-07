#ifndef PCF8563_DRIVER_H
#define PCF8563_DRIVER_H

#include <Arduino.h>
#include "IRtcDriver/IRtcDriver.h"

/**
 * @brief Driver bas niveau pour le chip RTC PCF8563 (I2C, adresse 0x51)
 *
 * Même adresse que le PCF85063 mais registres différents : le choix se fait par le
 * fichier matériel ("RTC": "PCF8563"), jamais par détection automatique.
 * - Bloc horodatage à partir du registre 0x02 : secondes (bit 7 = VL, tension basse),
 *   minutes, heures, jour du mois, jour de semaine, mois (bit 7 = siècle), année
 */
class Pcf8563Driver : public IRtcDriver {
public:
    Pcf8563Driver();

    bool detect() override;

    bool readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                       uint8_t& hour, uint8_t& minute, uint8_t& second) override;

    bool writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                        uint8_t hour, uint8_t minute, uint8_t second) override;

    bool hasLostPower() const override;
    void clearLostPowerFlag() override;

    const char* chipName() const override { return "PCF8563"; }

private:
    static const uint8_t PCF8563_ADDR = 0x51;
    static const uint8_t REG_CONTROL1 = 0x00;
    static const uint8_t REG_SECONDS = 0x02; // + minutes, heures, jour, jour-semaine, mois, année
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

#endif // PCF8563_DRIVER_H

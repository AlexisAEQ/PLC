#ifndef DS3231_DRIVER_H
#define DS3231_DRIVER_H

#include <Arduino.h>
#include "IRtcDriver/IRtcDriver.h"

/**
 * @brief Driver bas niveau pour le chip RTC DS3231 (I2C)
 *
 * Reprend telle quelle la logique existante de DS3231_RTC.cpp
 * (registres, BCD, gestion des erreurs consécutives).
 */
class Ds3231Driver : public IRtcDriver {
public:
    Ds3231Driver();

    bool detect() override;

    bool readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                       uint8_t& hour, uint8_t& minute, uint8_t& second) override;

    bool writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                        uint8_t hour, uint8_t minute, uint8_t second) override;

    bool hasLostPower() const override;
    void clearLostPowerFlag() override;

    const char* chipName() const override { return "DS3231"; }

private:
    static const uint8_t DS3231_ADDR = 0x68;
    static const uint8_t REG_SECONDS = 0x00;
    static const uint8_t REG_STATUS = 0x0F;
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

#endif // DS3231_DRIVER_H

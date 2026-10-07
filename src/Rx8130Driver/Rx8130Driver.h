#ifndef RX8130_DRIVER_H
#define RX8130_DRIVER_H

#include <Arduino.h>
#include "IRtcDriver/IRtcDriver.h"

/**
 * @brief Driver bas niveau pour le chip RTC Epson RX8130CE (I2C, adresse 0x32)
 *
 * Utilisé sur la M5Stack StamPLC.
 * - Bloc horodatage à partir du registre 0x10 : secondes, minutes, heures,
 *   jour de semaine (un bit par jour), jour du mois, mois, année
 * - Registre Flag 0x1D, bit VLF : perte d'alimentation / oscillateur arrêté
 * - Registre Control0 0x1E, bit STOP : à mettre pendant l'écriture de l'heure
 * - Registre Control1 0x1F : charge de la batterie de sauvegarde (bits 4-5)
 */
class Rx8130Driver : public IRtcDriver {
public:
    Rx8130Driver();

    bool detect() override;

    bool readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                       uint8_t& hour, uint8_t& minute, uint8_t& second) override;

    bool writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                        uint8_t hour, uint8_t minute, uint8_t second) override;

    bool hasLostPower() const override;
    void clearLostPowerFlag() override;

    const char* chipName() const override { return "RX8130"; }

private:
    static const uint8_t RX8130_ADDR = 0x32;
    static const uint8_t REG_SECONDS = 0x10;
    static const uint8_t REG_FLAG    = 0x1D;
    static const uint8_t REG_CTRL0   = 0x1E;
    static const uint8_t REG_CTRL1   = 0x1F;
    static const uint8_t BIT_VLF     = 0x02;
    static const uint8_t BIT_STOP    = 0x40;
    static const uint8_t MAX_CONSECUTIVE_RTC_ERRORS = 3;

    bool readRegister(uint8_t reg, uint8_t& value) const;
    bool writeRegister(uint8_t reg, uint8_t value);
    void handleReadFailure() const;
    void handleReadSuccess() const;
    uint8_t dayOfWeek(uint16_t year, uint8_t month, uint8_t day) const;

    inline uint8_t bcd2bin(uint8_t val) const { return val - 6 * (val >> 4); }
    inline uint8_t bin2bcd(uint8_t val) const { return val + 6 * (val / 10); }

    mutable uint8_t _consecutiveErrors;
    mutable bool _deviceAvailable;
};

#endif // RX8130_DRIVER_H

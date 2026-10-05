#ifndef IRTC_DRIVER_H
#define IRTC_DRIVER_H

#include <Arduino.h>

/**
 * @brief Interface abstraite pour un driver de chip RTC (pattern Strategy/Bridge)
 *
 * Permet à RTC_Service de fonctionner avec différents chips (DS3231, PCF85063ATL,
 * futur NTP...) sans connaître les détails d'implémentation (registres, BCD, I2C).
 */
class IRtcDriver {
public:
    virtual ~IRtcDriver() {}

    /**
     * @brief Détecte la présence du chip sur le bus I2C
     * @return true si le chip répond
     */
    virtual bool detect() = 0;

    /**
     * @brief Lit la date/heure courante du chip
     * @return true si la lecture a réussi
     */
    virtual bool readDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                               uint8_t& hour, uint8_t& minute, uint8_t& second) = 0;

    /**
     * @brief Écrit la date/heure dans le chip
     * @return true si l'écriture a réussi
     */
    virtual bool writeDateTime(uint16_t year, uint8_t month, uint8_t day,
                                uint8_t hour, uint8_t minute, uint8_t second) = 0;

    /**
     * @brief Indique si l'horloge a perdu l'alimentation (batterie déchargée / premier démarrage)
     */
    virtual bool hasLostPower() const = 0;

    /**
     * @brief Efface le flag de perte d'alimentation
     */
    virtual void clearLostPowerFlag() = 0;

    /**
     * @brief Nom du chip pour affichage/diagnostic (ex: "DS3231", "PCF85063ATL")
     */
    virtual const char* chipName() const = 0;
};

#endif // IRTC_DRIVER_H

#ifndef PCA9554_DRIVER_H
#define PCA9554_DRIVER_H

#include <Arduino.h>

/**
 * @brief Driver bas niveau pour l'expander I2C PCA9554 (sorties uniquement)
 *
 * Contrairement au PCF8574 (port quasi-bidirectionnel, écriture 1 octet sans
 * registre), le PCA9554 a de vrais registres adressés :
 * - 0x00 Input Port, 0x01 Output Port, 0x02 Polarity Inversion, 0x03 Configuration
 * - Au power-on, le registre Configuration vaut 0xFF : tous les pins sont en
 *   entrée. Un pin doit être explicitement configuré en sortie avant d'écrire
 *   dessus (registerOutputPin()), sinon l'écriture est silencieusement sans effet.
 */
class Pca9554Driver {
public:
    explicit Pca9554Driver(uint8_t i2cAddr);

    bool begin();

    /**
     * @brief Configure un pin en sortie (registre Configuration)
     */
    void registerOutputPin(uint8_t pin);

    /**
     * @brief Écrit une valeur sur un pin déjà configuré en sortie
     */
    bool write(uint8_t pin, bool value);

private:
    static const uint8_t REG_INPUT     = 0x00;
    static const uint8_t REG_OUTPUT    = 0x01;
    static const uint8_t REG_POLARITY  = 0x02;
    static const uint8_t REG_CONFIG    = 0x03;

    uint8_t _i2cAddr;
    uint8_t _outputShadow = 0x00;
    uint8_t _configShadow = 0xFF; // tout en entrée par défaut (reset du chip)

    bool writeRegister(uint8_t reg, uint8_t value);
};

#endif // PCA9554_DRIVER_H

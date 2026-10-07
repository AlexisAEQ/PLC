#ifndef IO_EXPANDER_H
#define IO_EXPANDER_H

#include <Arduino.h>

/**
 * @brief Expandeur d'E/S I2C générique (une instance par chip et par adresse)
 *
 * Permet de décrire les entrées et sorties d'une carte dans son fichier matériel
 * (data/hardware/<carte>.json) sans code spécifique : section "EXP_rx-bool" / "EXP_tx-bool",
 * chaque voie donnant { "id", "chip", "addr", "pin", "activeLow" }.
 *
 * Chips pris en charge :
 * - "PCF8574" (8 voies, quasi-bidirectionnel ; bibliothèque robtillaart, comme avant)
 * - "PCF8575" (16 voies, quasi-bidirectionnel ; KinCony KC868-A8v3)
 * - "TCA9554" / "PCA9554" / "TCA9534" / "PCA9534" (8 voies à registres ; Waveshare ESP32-S3 8DI-8DO)
 * - "AW9523" (16 voies ; M5Stack StamPLC)
 *
 * Les niveaux manipulés ici sont électriques (true = niveau haut) : l'inversion
 * logique (voie active à l'état bas) est appliquée par les nœuds (activeLow).
 */
class IoExpander {
public:
    virtual ~IoExpander() = default;

    // Détecte et initialise le chip (toutes les voies en entrée / sorties au repos).
    virtual bool begin() = 0;

    // Déclare une voie en sortie (output = true) ou en entrée.
    virtual bool configurePin(uint8_t pin, bool output) = 0;

    // Lit le niveau électrique d'une voie.
    virtual bool readPin(uint8_t pin, bool& level) = 0;

    // Écrit le niveau électrique d'une voie configurée en sortie.
    virtual bool writePin(uint8_t pin, bool level) = 0;

    virtual uint8_t pinCount() const = 0;
    virtual const char* chipName() const = 0;
    uint8_t address() const { return _addr; }

    // Prépare une voie de sortie : le niveau de repos est écrit avant le passage en sortie.
    // Sans effet si la voie est déjà prête : les nœuds sont recréés lors de la vérification
    // d'une nouvelle configuration et ne doivent pas toucher aux sorties en service.
    bool setupOutput(uint8_t pin, bool idleLevel);

    // Prépare une voie d'entrée (une seule fois).
    bool setupInput(uint8_t pin);

protected:
    explicit IoExpander(uint8_t addr) : _addr(addr) {}
    uint8_t _addr;

private:
    uint16_t _outputsReady = 0;
    uint16_t _inputsReady = 0;
};

/**
 * @brief Registre des expandeurs : une seule instance par (chip, adresse), partagée par les nœuds
 */
class IoExpanderManager {
public:
    // Retourne l'expandeur (créé et initialisé au premier appel) ; nullptr si chip inconnu ou absent.
    static IoExpander* getOrCreate(const char* chip, uint8_t i2cAddr);

    // Nom de chip reconnu ?
    static bool isKnownChip(const char* chip);
};

#endif // IO_EXPANDER_H

#ifndef RTC_SERVICE_H
#define RTC_SERVICE_H

#include <Arduino.h>
#include <Wire.h>

class IRtcDriver;

/**
 * @brief Service d'horodatage temps réel pour le PLC (Singleton)
 * 
 * Fournit des timestamps précis pour :
 * - Logs de diagnostic
 * - Événements système
 * - Compteurs de production
 * 
 * Sources possibles (par priorité) :
 * 1. DS3231 RTC (si présent sur la carte)
 * 2. NTP (si WiFi connecté) - futur
 * 3. millis() (fallback système)
 * 
 * Utilisation (singleton) :
 *   RTC_Service* rtc = RTC_Service::getInstance();
 *   char ts[24];
 *   rtc->getTimestamp(ts, sizeof(ts));
 * 
 * Ou directement :
 *   RTC_Service::getInstance()->getTimestamp(ts, sizeof(ts));
 */
class RTC_Service {
public:
    /**
     * @brief Obtient l'instance unique du service (singleton)
     * @return Pointeur vers l'instance unique
     */
    static RTC_Service* getInstance();
    
    /**
     * @brief Initialise le service d'horodatage
     * @param enableDS3231 Tenter d'utiliser le chip DS3231
     * @param sda Pin SDA I2C (défaut: 4 pour Kincony)
     * @param scl Pin SCL I2C (défaut: 5 pour Kincony)
     * @return true si au moins une source d'heure disponible
     * 
     * NOTE: Doit être appelé une fois au démarrage par BorneUniverselle
     */
    bool begin(bool enableDS3231 = true, uint8_t sda = 4, uint8_t scl = 5);
    
    /**
     * @brief Obtient un timestamp formaté pour logs
     * @param buffer Buffer de sortie (min 20 caractères)
     * @param size Taille du buffer
     * @return true si timestamp valide (RTC disponible)
     * 
     * Format avec RTC: "2024-12-21 14:32:45"
     * Format fallback: "+0012345678ms"
     */
    bool getTimestamp(char* buffer, size_t size);
    
    /**
     * @brief Obtient le timestamp Unix
     * @return Secondes depuis 1970-01-01 (0 si non disponible)
     */
    uint32_t getUnixTime();
    
    /**
     * @brief Définit l'heure manuellement
     * @param year Année (2000-2099)
     * @param month Mois (1-12)
     * @param day Jour (1-31)
     * @param hour Heure (0-23)
     * @param minute Minute (0-59)
     * @param second Seconde (0-59)
     * @return true si succès
     */
    bool setTime(uint16_t year, uint8_t month, uint8_t day,
                 uint8_t hour, uint8_t minute, uint8_t second);
    
    /**
     * @brief Définit l'heure via timestamp Unix
     * @param unixTime Secondes depuis 1970-01-01
     * @return true si succès
     */
    bool setUnixTime(uint32_t unixTime);
    
    /**
     * @brief Indique si une horloge matérielle est disponible
     */
    bool hasHardwareClock() const { return _ds3231Available; }
    
    /**
     * @brief Indique si l'horloge a perdu l'alimentation
     * @return true si batterie déchargée ou première utilisation
     */
    bool hasLostPower() const;
    
    /**
     * @brief Efface le flag de perte d'alimentation
     */
    void clearLostPowerFlag();
    
    /**
     * @brief Affiche l'état du service sur Serial
     */
    void printStatus();
    
    // Empêche la copie (pattern singleton)
    RTC_Service(const RTC_Service&) = delete;
    RTC_Service& operator=(const RTC_Service&) = delete;

   bool hasValidTime() const {
        return _ds3231Available && _isValid;  // ← SIMPLE LECTURE DE VARIABLE
    }

    bool hasReasonableTime() const;
    bool setTimeFromISO8601(const String& iso8601);
    
private:
    // Constructeur privé (pattern singleton)
    RTC_Service();
    ~RTC_Service();
    
    // Instance unique (singleton)
    static RTC_Service* _instance;

    // Driver bas niveau du chip RTC détecté (Ds3231Driver, Pcf85063Driver, ...)
    IRtcDriver* _driver;

    bool _ds3231Available;
    uint32_t _bootMillis;
    bool _initialized;  // Pour éviter double initialisation

    // Calculs temporels
    uint32_t toUnixTime(uint16_t y, uint8_t m, uint8_t d,
                        uint8_t h, uint8_t min, uint8_t s);
    void fromUnixTime(uint32_t unixTime, uint16_t& y, uint8_t& m, uint8_t& d,
                      uint8_t& h, uint8_t& min, uint8_t& s);

    struct ParsedDateTime {
        uint16_t year;
        uint8_t month, day, hour, minute, second;
        bool valid;
    };

    ParsedDateTime parseISO8601(const String& iso8601);
    bool _isValid;
};

#endif // RTC_SERVICE_H
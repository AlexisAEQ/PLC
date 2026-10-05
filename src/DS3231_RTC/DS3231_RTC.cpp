#include "DS3231_RTC/DS3231_RTC.h"
#include "DS3231_RTC.h"
#include "IRtcDriver/IRtcDriver.h"
#include "Ds3231Driver/Ds3231Driver.h"
#include "Pcf85063Driver/Pcf85063Driver.h"

// Initialisation du pointeur statique (singleton)
RTC_Service* RTC_Service::_instance = nullptr;

RTC_Service::RTC_Service()
    : _driver(nullptr)
    , _ds3231Available(false)
    , _bootMillis(0)
    , _initialized(false)
    , _isValid(false)
{
}

RTC_Service::~RTC_Service() {
    // Destructeur privé - ne sera jamais appelé en pratique
    // (le singleton vit toute la durée du programme)
    delete _driver;
}

RTC_Service* RTC_Service::getInstance() {
    if (_instance == nullptr) {
        _instance = new RTC_Service();
    }
    return _instance;
}

bool RTC_Service::begin(bool enableDS3231, uint8_t sda, uint8_t scl) {
    // Évite la double initialisation
    if (_initialized) {
        Serial.println("[RTC] Service déjà initialisé");
        return _ds3231Available || true;
    }
    
    _bootMillis = millis();
    _initialized = true;
    
    Serial.println("[RTC] Initialisation service d'horodatage...");
    
    if (enableDS3231) {
        delay(10);

        // Sonde successivement les chips connus : DS3231 (0x68) puis PCF85063ATL (0x51)
        _driver = new Ds3231Driver();
        if (!_driver->detect()) {
            delete _driver;
            _driver = new Pcf85063Driver();
            if (!_driver->detect()) {
                delete _driver;
                _driver = nullptr;
            }
        }

        if (_driver) {
            _ds3231Available = true;
            Serial.printf("[RTC] ✓ %s détecté et activé\n", _driver->chipName());

            // ========================================
            // ÉVALUER LA VALIDITÉ UNE SEULE FOIS
            // ========================================
            bool lostPower = hasLostPower();
            bool reasonable = hasReasonableTime();

            _isValid = !lostPower && reasonable;  // ← STOCKER LE RÉSULTAT

            if (lostPower) {
                Serial.println("[RTC] ⚠️ Horloge invalide (batterie déchargée)");
                clearLostPowerFlag();
            }

            if (!reasonable) {
                Serial.println("[RTC] ⚠️ Horloge invalide (année < 2026)");
            }

            if (_isValid) {
                Serial.println("[RTC] ✓ Horloge valide");
            } else {
                Serial.println("[RTC] ✗ Horloge invalide - utilisation fallback millis()");
            }

            printStatus();
            return true;
        }

        Serial.println("[RTC] Aucune horloge matérielle détectée (DS3231/PCF85063ATL)");
    }

    // Fallback sur millis()
    Serial.println("[RTC] Mode fallback millis() activé");
    _isValid = false;  // ← PAS DE RTC = PAS VALIDE
    return true;
}

bool RTC_Service::getTimestamp(char* buffer, size_t size) {
    if (!buffer || size < 20) {
        return false;
    }
    
     if (_ds3231Available && hasValidTime()) {
        uint16_t yr; uint8_t mon, day, hr, min, sec;
        if (_driver->readDateTime(yr, mon, day, hr, min, sec)) {
            snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d",
                    yr, mon, day, hr, min, sec);
            return true;
        }
    }

    // Fallback sur millis()
    uint32_t elapsed = millis() - _bootMillis;
    snprintf(buffer, size, "+%010lums", elapsed);
    return false;
}

bool RTC_Service::setTime(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second) {
    if (!_ds3231Available) {
        Serial.println("[RTC] Erreur: DS3231 non disponible");
        return false;
    }
    
    // Validation
    if (year < 2000 || year > 2099) {
        Serial.println("[RTC] Erreur: année invalide (2000-2099)");
        return false;
    }
    if (month < 1 || month > 12) {
        Serial.println("[RTC] Erreur: mois invalide (1-12)");
        return false;
    }
    if (day < 1 || day > 31) {
        Serial.println("[RTC] Erreur: jour invalide (1-31)");
        return false;
    }
    if (hour > 23 || minute > 59 || second > 59) {
        Serial.println("[RTC] Erreur: heure invalide");
        return false;
    }
    
    if (_driver->writeDateTime(year, month, day, hour, minute, second)) {
        Serial.printf("[RTC] ✓ Heure réglée: %04d-%02d-%02d %02d:%02d:%02d\n",
                     year, month, day, hour, minute, second);
        return true;
    }
    
    Serial.println("[RTC] Erreur: échec écriture DS3231");
    return false;
}

bool RTC_Service::setUnixTime(uint32_t unixTime) {
    if (!_ds3231Available) return false;
    
    if (unixTime < 946684800) { // 2000-01-01
        Serial.println("[RTC] Erreur: timestamp trop ancien");
        return false;
    }
    
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    fromUnixTime(unixTime, year, month, day, hour, minute, second);
    
    return setTime(year, month, day, hour, minute, second);
}

uint32_t RTC_Service::getUnixTime() {
    if (!_ds3231Available) return 0;

    uint16_t yr; uint8_t mon, day, hr, min, sec;
    if (!_driver->readDateTime(yr, mon, day, hr, min, sec)) return 0;

    return toUnixTime(yr, mon, day, hr, min, sec);
}

uint32_t RTC_Service::toUnixTime(uint16_t year, uint8_t month, uint8_t day,
                                 uint8_t hour, uint8_t minute, uint8_t second) {
    // Calcul du nombre de jours depuis 1970
    uint32_t days = 0;
    
    // Années complètes depuis 1970
    for (uint16_t y = 1970; y < year; y++) {
        bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        days += leap ? 366 : 365;
    }
    
    // Mois de l'année courante
    const uint8_t daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (uint8_t m = 1; m < month; m++) {
        days += daysInMonth[m - 1];
    }
    
    // Ajustement année bissextile
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (month > 2 && leap) {
        days++;
    }
    
    // Jours du mois
    days += day - 1;
    
    return days * 86400UL + hour * 3600UL + minute * 60UL + second;
}

void RTC_Service::fromUnixTime(uint32_t unixTime, uint16_t& year, uint8_t& month, uint8_t& day,
                                uint8_t& hour, uint8_t& minute, uint8_t& second) {
    // Extraction heure/minute/seconde
    second = unixTime % 60;
    unixTime /= 60;
    minute = unixTime % 60;
    unixTime /= 60;
    hour = unixTime % 24;
    uint32_t days = unixTime / 24;
    
    // Calcul année
    year = 1970;
    while (true) {
        bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        uint16_t daysInYear = leap ? 366 : 365;
        
        if (days >= daysInYear) {
            days -= daysInYear;
            year++;
        } else {
            break;
        }
    }
    
    // Calcul mois et jour
    const uint8_t daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    
    month = 1;
    for (month = 1; month <= 12; month++) {
        uint8_t dim = daysInMonth[month - 1];
        if (month == 2 && leap) dim = 29;
        
        if (days >= dim) {
            days -= dim;
        } else {
            break;
        }
    }
    
    day = days + 1;
}

bool RTC_Service::hasLostPower() const {
    if (!_ds3231Available) return false;
    return _driver->hasLostPower();
}

void RTC_Service::clearLostPowerFlag() {
    if (!_ds3231Available) return;
    _driver->clearLostPowerFlag();
}

void RTC_Service::printStatus() {
    Serial.println("[RTC] === Status Service Horodatage ===");
    if (_ds3231Available) {
        Serial.printf("[RTC] Source: %s (hardware)\n", _driver->chipName());
    } else {
        Serial.println("[RTC] Source: millis() (fallback)");
    }

    char ts[24];
    bool hasRTC = getTimestamp(ts, sizeof(ts));
    Serial.printf("[RTC] Heure: %s\n", ts);
    
    if (_ds3231Available) {

        if (hasValidTime()) {
            Serial.println("[RTC] Heure valide");
        } else {
            Serial.println("[RTC] ⚠️ Heure invalide");
            return;
        }

        Serial.printf("[RTC] Perte alim: %s\n", 
                      hasLostPower() ? "OUI (batterie faible)" : "Non");
        
        if (hasRTC) {
            uint32_t unix = getUnixTime();
            Serial.printf("[RTC] Unix time: %lu\n", unix);
        }
    }
    
    Serial.println("[RTC] ================================");
}

bool RTC_Service::hasReasonableTime() const {
    if (!_ds3231Available) return false;

    uint16_t year; uint8_t month, day, hour, minute, second;
    if (!_driver->readDateTime(year, month, day, hour, minute, second)) {
        return false;
    }

    Serial.printf("[RTC] Année actuelle: %d\n", year);
    
    // On considère qu'une année < 2020 est invalide
    // (ajuste cette valeur selon tes besoins)
    return (year >= 2026);
}

RTC_Service::ParsedDateTime RTC_Service::parseISO8601(const String& iso8601) {
    ParsedDateTime result = {0, 0, 0, 0, 0, 0, false};
    
    if (iso8601.length() < 19) {
        return result;
    }
    
    // Parser YYYY-MM-DD
    int year = iso8601.substring(0, 4).toInt();
    int month = iso8601.substring(5, 7).toInt();
    int day = iso8601.substring(8, 10).toInt();
    
    // Parser HH:MM:SS
    int hour = iso8601.substring(11, 13).toInt();
    int minute = iso8601.substring(14, 16).toInt();
    int second = iso8601.substring(17, 19).toInt();
    
    // Validation
    if (year < 2000 || year > 2100 ||
        month < 1 || month > 12 ||
        day < 1 || day > 31 ||
        hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        return result;
    }
    
    result.year = year;
    result.month = month;
    result.day = day;
    result.hour = hour;
    result.minute = minute;
    result.second = second;
    result.valid = true;
    
    return result;
}

bool RTC_Service::setTimeFromISO8601(const String& iso8601) {
    if (!_initialized) {
        Serial.println("RTC_Service: RTC not initialized");
        return false;
    }
    
    ParsedDateTime dt = parseISO8601(iso8601);
    
    if (!dt.valid) {
        Serial.printf("RTC_Service: Invalid ISO8601 format: %s\n", iso8601.c_str());
        return false;
    }
    
    bool success = setTime(dt.year, dt.month, dt.day, 
                          dt.hour, dt.minute, dt.second);
    
    if (success) {
        Serial.printf("RTC_Service: Time updated to %04d-%02d-%02d %02d:%02d:%02d\n",
                      dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    } else {
        Serial.println("RTC_Service: Failed to set RTC time");
    }
    
    return success;
}
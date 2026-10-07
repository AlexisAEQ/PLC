#pragma once

#include <Arduino.h>
#include "ArduinoJson.h"

/**
 * @brief Ethernet filaire des cartes qui en ont (W5500 en SPI, LAN8720 en RMII)
 *
 * - Les broches viennent du fichier matériel de la carte (bloc "ETH") :
 *     W5500   : { "phy": "W5500", "cs": 16, "irq": 12, "rst": 39, "sck": 15, "miso": 14, "mosi": 13 }
 *     LAN8720 : { "phy": "LAN8720", "addr": 0, "mdc": 23, "mdio": 18, "power": -1, "clk": "GPIO17_OUT" }
 * - Les réglages du projet viennent de config.json (élément { "Ethernet": { ... } } de "config") :
 *     { "enabled": true, "dhcp": true }  ou  { "enabled": true, "dhcp": false,
 *       "ip": "192.168.1.20", "gateway": "192.168.1.1", "mask": "255.255.255.0", "dns": "192.168.1.1" }
 *
 * L'Ethernet fonctionne en plus du WiFi (station ou point d'accès) : le serveur web, le mDNS
 * et le Modbus TCP passent par l'interface qui joint le réseau visé. Éviter de mettre le
 * WiFi station et l'Ethernet sur le même sous-réseau.
 *
 * Nécessite Arduino-ESP32 3.x (pioarduino) ; avec une version 2.x, begin() signale que
 * l'Ethernet n'est pas disponible.
 */
class EthernetManager {
public:
    static EthernetManager& getInstance();

    // Bloc "ETH" du fichier matériel de la carte ; false si le bloc est incomplet.
    bool configureBoard(JsonVariantConst eth);
    // Bloc "Ethernet" de config.json.
    bool configureNetwork(JsonVariantConst settings);

    bool boardHasEthernet() const { return boardOk; }
    bool isEnabled() const { return enabled; }

    // Démarre l'interface si la carte a de l'Ethernet et que le projet l'active.
    bool begin(const char* hostname);

    bool isStarted() const { return started; }
    bool hasIP() const;
    IPAddress localIP() const;

    // Vrai une fois après chaque obtention d'adresse (à traiter dans loop()).
    bool takeGotIP();

private:
    EthernetManager() = default;

    enum class Phy : uint8_t { NONE, W5500, LAN8720 };
    Phy phy = Phy::NONE;
    int addr = -1;
    int cs = -1, irq = -1, rst = -1, sck = -1, miso = -1, mosi = -1;   // W5500
    int mdc = -1, mdio = -1, power = -1;                                // LAN8720
    char clk[16] = "GPIO0_IN";

    bool boardOk = false;
    bool enabled = false;
    bool dhcp = true;
    IPAddress ip, gateway, mask, dns;

    bool started = false;
    volatile bool gotIP = false;
    char hostname[33] = "";

    static void onEvent(int event);
};

#include "EthernetManager/EthernetManager.h"

#if ESP_ARDUINO_VERSION_MAJOR >= 3
#include <ETH.h>
#include <Network.h>
#define ETHERNET_SUPPORTED 1
#else
#define ETHERNET_SUPPORTED 0
#endif

EthernetManager& EthernetManager::getInstance() {
    static EthernetManager instance;
    return instance;
}

bool EthernetManager::configureBoard(JsonVariantConst eth) {
    boardOk = false;
    phy = Phy::NONE;
    if (eth.isNull()) {
        return false;
    }
    const char* type = eth["phy"] | "";
    if (!strcasecmp(type, "W5500")) {
        phy = Phy::W5500;
        addr = eth["addr"] | 1;
        cs = eth["cs"] | -1;
        irq = eth["irq"] | -1;
        rst = eth["rst"] | -1;
        sck = eth["sck"] | -1;
        miso = eth["miso"] | -1;
        mosi = eth["mosi"] | -1;
        boardOk = cs >= 0 && sck >= 0 && miso >= 0 && mosi >= 0;
    } else if (!strcasecmp(type, "LAN8720")) {
        phy = Phy::LAN8720;
        addr = eth["addr"] | 0;
        mdc = eth["mdc"] | 23;
        mdio = eth["mdio"] | 18;
        power = eth["power"] | -1;
        strlcpy(clk, eth["clk"] | "GPIO0_IN", sizeof(clk));
        boardOk = true;
    }
    if (!boardOk) {
        Serial.printf("Ethernet : bloc ETH du fichier materiel incomplet ou PHY inconnu (%s)\r\n", type);
    }
    return boardOk;
}

bool EthernetManager::configureNetwork(JsonVariantConst settings) {
    if (settings.isNull()) {
        enabled = false;
        return true;
    }
    enabled = settings["enabled"] | true;
    dhcp = settings["dhcp"] | true;
    if (!dhcp) {
        const char* ipText = settings["ip"] | "";
        const char* gwText = settings["gateway"] | "";
        const char* maskText = settings["mask"] | "255.255.255.0";
        const char* dnsText = settings["dns"] | "";
        if (!ip.fromString(ipText) || !mask.fromString(maskText)) {
            Serial.println("Ethernet : adresse IP ou masque invalide, DHCP utilise");
            dhcp = true;
        } else {
            if (!gateway.fromString(gwText)) gateway = IPAddress(0, 0, 0, 0);
            if (!dns.fromString(dnsText)) dns = gateway;
        }
    }
    return true;
}

void EthernetManager::onEvent(int event) {
#if ETHERNET_SUPPORTED
    EthernetManager& m = getInstance();
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            if (m.hostname[0]) ETH.setHostname(m.hostname);
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("Ethernet : cable connecte");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            m.gotIP = true;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("Ethernet : cable deconnecte");
            break;
        default:
            break;
    }
#else
    (void)event;
#endif
}

bool EthernetManager::begin(const char* name) {
    if (started) return true;
    if (!enabled) return false;
    if (!boardOk) {
        Serial.println("Ethernet active dans le projet mais la carte n'en declare pas (bloc ETH du fichier materiel)");
        return false;
    }
    strlcpy(hostname, name ? name : "", sizeof(hostname));
#if ETHERNET_SUPPORTED
    Network.onEvent([](arduino_event_id_t event, arduino_event_info_t) { EthernetManager::onEvent(event); });
    bool ok = false;
    if (phy == Phy::W5500) {
        ok = ETH.begin(ETH_PHY_W5500, addr, cs, irq, rst, SPI2_HOST, sck, miso, mosi);
    } else if (phy == Phy::LAN8720) {
#if CONFIG_ETH_USE_ESP32_EMAC && CONFIG_IDF_TARGET_ESP32
        eth_clock_mode_t mode = ETH_CLOCK_GPIO0_IN;
        if (!strcasecmp(clk, "GPIO0_OUT")) mode = ETH_CLOCK_GPIO0_OUT;
        else if (!strcasecmp(clk, "GPIO16_OUT")) mode = ETH_CLOCK_GPIO16_OUT;
        else if (!strcasecmp(clk, "GPIO17_OUT")) mode = ETH_CLOCK_GPIO17_OUT;
        ok = ETH.begin(ETH_PHY_LAN8720, addr, mdc, mdio, power, mode);
#else
        Serial.println("Ethernet : LAN8720 (RMII) disponible seulement sur ESP32");
#endif
    }
    if (!ok) {
        Serial.println("Ethernet : demarrage impossible");
        return false;
    }
    if (!dhcp) {
        ETH.config(ip, gateway, mask, dns);
    }
    started = true;
    Serial.printf("Ethernet demarre (%s, %s)\r\n", phy == Phy::W5500 ? "W5500" : "LAN8720", dhcp ? "DHCP" : ip.toString().c_str());
    return true;
#else
    Serial.println("Ethernet : necessite Arduino-ESP32 3.x (pioarduino)");
    return false;
#endif
}

bool EthernetManager::hasIP() const {
#if ETHERNET_SUPPORTED
    return started && ETH.hasIP();
#else
    return false;
#endif
}

IPAddress EthernetManager::localIP() const {
#if ETHERNET_SUPPORTED
    return started ? ETH.localIP() : IPAddress(0, 0, 0, 0);
#else
    return IPAddress(0, 0, 0, 0);
#endif
}

bool EthernetManager::takeGotIP() {
    if (!gotIP) return false;
    gotIP = false;
    return true;
}

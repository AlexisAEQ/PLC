# Cartes automates prises en charge

Le firmware (BorneUniverselle) ne contient plus de code propre à une carte : tout ce qui
la décrit (E/S, bus I2C, horloge, RS485, buzzer) est dans son fichier matériel
`data/hardware/<carte>.json`. PLC Studio propose ces cartes dans son catalogue et indique
l'environnement PlatformIO à compiler.

| Carte | Fichier matériel | Environnement PlatformIO | E/S | Horloge |
|---|---|---|---|---|
| KinCony KC868-A8S (ESP32) | `Kincony_KC868_A8S` | `esp32dev` | 8 entrées, 8 relais (PCF8574), GPIO2 (buzzer) | DS3231 |
| KinCony KC868-A16 (ESP32) | `Kincony_KC868_A16` | `quatre_mb_huge` | 16 entrées, 16 sorties MOSFET (PCF8574), GPIO 32/33/14 | aucune |
| KinCony KC868-A16v3 (ESP32-S3) | `Kincony_KC868_A16v3` | `esp32s3_n16r8` | 16 entrées, 16 sorties MOSFET (PCF8574), GPIO 38-41/47/48 | DS3231 (détection auto) |
| KinCony KC868-A8v3 (ESP32-S3) | `Kincony_KC868_A8v3` | `esp32s3_n16r8` | 8 entrées, 8 relais (PCF8575), GPIO 13/14/40/48 | DS3231 (détection auto) |
| Waveshare ESP32-S3-POE-ETH-8DI-8DO | `Waveshare_ESP32S3_POE_8DI8DO` | `esp32s3_n16r8` | 8 entrées (GPIO 4-11), 8 sorties (TCA9554), GPIO 40/1/47/48, buzzer GPIO46 | PCF85063 |
| Waveshare ESP32-S3-RS485-WLED | `Waveshare_ESP32S3_RS485_WLED` | `esp32s3_n16r8` | bouton BOOT (GPIO0), sorties 5 V GPIO2/GPIO16, GPIO3/GPIO4 | aucune |
| M5Stack StamPLC (ESP32-S3) | `M5Stack_StamPLC` | `m5stack_stamplc` | 8 entrées, 4 relais (AW9523), GPIO Grove 4/5/1/2, buzzer GPIO44 | RX8130 |
| Homemaster MiniPLC (ESP32) | `Homemaster_MiniPLC` | `quatre_mb_huge` | 4 entrées 24 V, 4 boutons, 6 relais, 2 LED (PCF8574), GPIO 5/4, buzzer GPIO2 | PCF8563 |

Réseau : WiFi (client et/ou point d'accès) sur toutes les cartes, plus l'Ethernet filaire
sur les cartes qui en ont (bloc `ETH` du fichier matériel, voir plus bas) : Waveshare
ESP32-S3-POE-ETH-8DI-8DO, KC868-A16v3 et KC868-A8v3 (W5500), KC868-A16 et Homemaster
MiniPLC (LAN8720). L'Ethernet s'active à l'étape Projet de PLC Studio (DHCP ou adresse
fixe) et fonctionne en même temps que le WiFi ; il demande Arduino-ESP32 3.x (environnements
`esp32s3_n16r8` et `quatre_mb_huge`). Voir `docs/modbus-tcp.md`.

Remarques par carte :

- **Waveshare ESP32-S3-POE-ETH-8DI-8DO** : sorties actives à l'état bas sur le TCA9554 (état
  de repos écrit avant le passage en sortie) ; RS485 avec validation d'émission sur GPIO21,
  gérée par l'UART en mode semi-duplex. Les GPIO libres ne sont pas sur bornier (à
  repiquer) ; 47/48 seulement si le lecteur de carte TF n'est pas utilisé.
- **Waveshare ESP32-S3-RS485-WLED** : pas de relais ni d'entrées isolées, à compléter par
  des modules Modbus (Waveshare 8DIO…). GPIO2 et GPIO16 sont des sorties 5 V
  unidirectionnelles (prévues pour les bandeaux LED) : idéales pour STEP/DIR d'un driver
  pas-à-pas. RS485 à sens automatique (TX GPIO17, RX GPIO18). Le codec audio n'est pas
  utilisé. Brochage relevé dans la documentation Waveshare : à confirmer sur le schéma.
- **M5Stack StamPLC** : 8 Mo de flash sans PSRAM (environnement dédié) ; RS485 TX GPIO0,
  RX GPIO39, sens GPIO46 ; console sur l'USB natif.
- **KinCony KC868-A16 / Homemaster MiniPLC** : ESP32 4 Mo sans PSRAM ; GPIO 34-39 sont des
  entrées seules sans tirage (entrées du MiniPLC déclarées `"pull": "none"`).

## Format du fichier matériel d'une carte

```jsonc
{
    "hardware": "Waveshare_ESP32S3_POE_8DI8DO",        // = nom du fichier

    // Bloc "board" : buzzer remis au repos à chaque connexion WiFi (null : aucun).
    // Sans bloc "board", un fichier dont le nom contient « A8S » garde le buzzer GPIO2.
    "board": { "mcu": "ESP32-S3", "buzzer": { "pin": 46, "activeLow": false } },

    // Bus I2C. "RTC" : true (détection DS3231 → PCF85063 → RX8130), false/"none" (pas
    // d'horloge : l'heure part de la mise sous tension) ou le chip : "DS3231", "PCF85063",
    // "PCF8563", "RX8130". "int_pin" : ligne d'interruption des expandeurs (front
    // descendant ; -1 = aucune ; défaut 14 pour les sections PF8574 historiques).
    "I2C": { "SDA": 42, "SCL": 41, "RTC": "PCF85063" },

    // RS485 : "de" = validation d'émission (UART en mode RS485 semi-duplex) ; absent si le
    // transceiver gère le sens tout seul. "config" : SERIAL_8N1, 8N2, 8E1, 8E2, 8O1, 8O2.
    "RS485": { "rx": 18, "tx": 17, "de": 21, "speed": 115200, "config": "SERIAL_8E1" },
    "Modbus": { "modbusRTU": true, "timeout": 101 },

    // Ethernet (facultatif). W5500 en SPI :
    "ETH": { "phy": "W5500", "cs": 16, "irq": 12, "rst": 39, "sck": 15, "miso": 14, "mosi": 13 },
    // ou LAN8720 en RMII (ESP32 seulement) : "clk" = GPIO0_IN, GPIO0_OUT, GPIO16_OUT, GPIO17_OUT
    // "ETH": { "phy": "LAN8720", "addr": 0, "mdc": 23, "mdio": 18, "power": -1, "clk": "GPIO17_OUT" },

    // GPIO de l'ESP32 : "activeLow" (vrai au niveau bas), "pull" : "up" (défaut), "down", "none".
    "rx-bool": [ { "id": 1, "pin": 4, "activeLow": true, "pull": "up" } ],
    "tx-bool": [ { "id": 1, "pin": 40 } ],

    // Expandeurs I2C, voie par voie : "chip" PCF8574, PCF8575, TCA9554, PCA9554, TCA9534,
    // PCA9534 ou AW9523 ; "addr" en décimal ; "pin" de 0 à 7 (0 à 15 pour PCF8575 / AW9523).
    "EXP_rx-bool": [ { "id": 1, "chip": "PCF8574", "addr": 34, "pin": 0, "activeLow": true } ],
    "EXP_tx-bool": [ { "id": 1, "chip": "TCA9554", "addr": 32, "pin": 0, "activeLow": true } ]
}
```

Les sections historiques de la KC868-A8S (`PF8574_rx-bool` / `PF8574_tx-bool` avec
`I2C.rx_addr` / `I2C.tx_addr`, `PCA9554_tx-bool`) restent acceptées telles quelles.

Une sortie (GPIO ou expandeur) est mise à son état de repos une seule fois, à sa première
création : la vérification d'une nouvelle configuration, qui recrée les nœuds, ne touche
pas aux sorties en service.

PLC Studio génère une variante du fichier (ex. `Kincony_KC868_A8S_noRTC`,
`Waveshare_ESP32S3_POE_8DI8DO_19200_8N1`) quand l'horloge est décochée ou que la vitesse ou
le format RS485 est changé dans les options de l'automate.

## Ajouter une carte

1. Écrire `data/hardware/<Carte>.json` avec les clés ci-dessus.
2. Ajouter l'entrée de la carte dans `tools/plc-studio/shared/catalog.js` (rôle
   `controller`, `pioEnv`, groupes de voies dont `type` = nom de la section du fichier
   matériel et `ids` = ses identifiants ; `names` pour nommer les GPIO, `gpio: true` pour
   les sorties directes utilisables en STEP/DIR, `ethernet: true` si le fichier a un bloc
   `ETH`).
3. Si la carte demande un autre microcontrôleur ou une autre taille de flash, ajouter un
   environnement dans `platformio.ini`.
4. `npm test` dans `tools/plc-studio` vérifie que chaque identifiant généré existe dans le
   fichier matériel.

## Axes

Plusieurs axes de types différents peuvent être pilotés par un même projet : SureServo
(Modbus), Lichuan A6 / A5 (Modbus, `docs/axes/lichuan.md`) et moteurs pas-à-pas STEP/DIR
(`docs/axes/stepper.md`). Chacun est piloté par un `AxisController`
(`src/AxisController`) dans la classe générée par PLC Studio.

# Modbus TCP et Ethernet

Chaque équipement Modbus d'un projet (module d'E/S, servo, robot…) passe soit par le bus
RS485 de l'automate (**Modbus RTU**, comportement historique), soit par le réseau
(**Modbus TCP**, en WiFi ou en Ethernet). Les deux se mélangent librement dans un même
projet. L'automate est toujours **client** (maître) : c'est lui qui interroge les
équipements.

## Dans PLC Studio

Étape **Équipements**, sur chaque équipement Modbus :

| Réglage | RTU (RS485) | TCP (réseau) |
|---|---|---|
| Liaison Modbus | « RS485 de l'automate » | « Réseau WiFi / Ethernet » |
| Adresse | adresse d'esclave 1 à 247, unique sur le bus | n° d'unité 0 à 255 (souvent 1) |
| Adresse IP, port | — | IP de l'équipement, port 502 par défaut |

Une **passerelle Modbus TCP → RTU** (convertisseur RS485/Ethernet) se déclare en TCP avec
l'IP de la passerelle ; le n° d'unité est alors l'adresse de l'esclave sur le RS485 derrière
la passerelle. Plusieurs esclaves derrière la même passerelle = même IP, unités différentes.

L'**équipement Modbus générique** (robot, autre automate, passerelle…) offre 16 bobines
écrites (Q1-Q16, FC05), 16 bobines lues (I1-I16, FC01), 16 registres écrits (W1-W16, FC06),
16 registres lus (R1-R16, FC03) et 16 registres d'entrée (IR1-IR16, FC04), à partir des
adresses de départ réglées dans ses options. Avec « Adresses de la documentation comptées à
partir de 1 », l'outil retire 1 (et accepte la notation 40001 / 30001). PLC Studio génère
son fichier matériel `data/hardware/Modbus_<Nom>.json`. Les registres sont des entiers non
signés 16 bits (0 à 65535) ; les flottants 32 bits ne sont pas pris en charge.

Étape **Projet**, carte « Réseau filaire (Ethernet) » : activation, DHCP ou adresse fixe.
Seulement pour les automates qui ont un port Ethernet (voir `docs/cartes.md`).

Exemple de poignée de main avec un robot (grafcet) :

1. Étape « Programme robot » : `Programme := NumProg`, `N Depart_robot` ;
   réceptivité d'entrée : `FM(Depart) ET Robot_pret ET Etat_robot = 0`.
2. Réceptivité `Robot_fini` → étape « Fin » (Depart_robot retombe).
3. Réceptivité `NON Robot_fini` → retour à l'attente.

En arrêt d'urgence, les sorties prennent leur valeur de repli : « Forcée à 0 » ou
« Maintenue » pour un registre.

## Format config.json

Une section Modbus TCP porte un bloc `tcp` ; `address` est le n° d'unité :

```json
{
  "name": "Robot Registres ecrits",
  "hardware": "Modbus_Robot",
  "type": "ModbusWriteHoldingRegister",
  "address": 1,
  "tcp": { "ip": "192.168.10.50", "port": 502 },
  "children": [ { "id": 1, "name": "Programme", "hash": 123456789 } ]
}
```

Ethernet (élément de `config`, entre `Wifi` et `parameters`) :

```json
{ "Ethernet": { "enabled": true, "dhcp": false, "ip": "192.168.10.20",
                "gateway": "192.168.10.1", "mask": "255.255.255.0", "dns": "192.168.10.1" } }
```

Un changement des sections (adresse, IP, port, nœuds) ou de l'Ethernet redémarre l'automate
à l'enregistrement de config.json : ces réglages ne sont lus qu'au démarrage.

## Fonctionnement dans le firmware

- `MyModbus` enregistre chaque équipement TCP (IP, port, unité) et lui attribue une
  adresse interne ≥ 1000 ; les nœuds Modbus ne voient pas la différence avec le RTU.
- Une seule connexion par adresse IP (bibliothèque modbus-esp8266) : au plus 8 adresses IP
  et 16 équipements TCP ; deux ports différents sur la même IP ne sont pas acceptés.
- Connexion à la première requête ; si l'équipement ne répond pas, une nouvelle tentative
  au plus toutes les 3 s (chaque tentative peut bloquer la boucle jusqu'à 500 ms :
  `MODBUSIP_CONNECT_TIMEOUT` dans `platformio.ini`). Délai de réponse : 500 ms.
- Un équipement TCP injoignable est signalé comme un esclave RTU
  (« Peripherique Modbus TCP 192.168.10.50:502 unite 1 injoignable… ») ; il ne compte pas
  dans la détection « bus RS485 mort ».
- Ethernet : `EthernetManager` (W5500 en SPI, LAN8720 en RMII sur ESP32), démarré à côté du
  WiFi ; le serveur web et le mDNS répondent aussi sur l'Ethernet. Nécessite Arduino-ESP32
  3.x (pioarduino) : environnements `esp32s3_n16r8` et `quatre_mb_huge`.

## Précautions

- **Modbus TCP n'est pas une fonction de sécurité.** L'arrêt d'urgence du robot et de la
  machine reste câblé (chaîne de sécurité), indépendamment du réseau.
- Ne mettez pas le WiFi client et l'Ethernet sur le même sous-réseau (routage ambigu).
- Prévoyez des adresses IP fixes (ou des baux DHCP réservés) pour les équipements.
- Le transport TCP et l'Ethernet ont été vérifiés par analyse et compilation de syntaxe
  sur PC seulement : à valider sur l'automate (connexion, reconnexion après coupure,
  débit des rafraîchissements).

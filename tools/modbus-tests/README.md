# Tests Modbus TCP depuis un PC

Deux scripts pour savoir, avant de mettre en cause l'automate, si un équipement répond
vraiment en Modbus TCP. Écrits lors du diagnostic du banc AUBO i10
(`docs/robots/diagnostic-aubo-i10.md`).

| Script | Rôle | Dépendance |
|---|---|---|
| `test_modbus_tcp.py` | Le PC joue l'automate : connexion au port, lectures FC03 sur les adresses du projet, n° d'unité 1 / 0 / 255, balayage FC01 à FC04 (`--scan`). | `pip install pymodbus` |
| `faux_esclave_modbus.py` | Le PC joue l'équipement : chaque registre renvoie son adresse, chaque requête reçue est affichée. Valide l'automate (firmware, Ethernet, câble) sans le robot. | aucune (Python standard) |

## Ordre conseillé

1. Port ouvert ? Sous Windows : `Test-NetConnection 192.168.10.50 -Port 502` (et `-Port 8899`
   pour comparer avec le SDK AUBO). `TcpTestSucceeded : False` sur 502 : aucun serveur Modbus TCP,
   l'automate ne pourra pas communiquer avec cet équipement.
2. Si le port est ouvert : `python test_modbus_tcp.py 192.168.10.50`, puis `--scan` pour relever
   les adresses qui répondent.
3. Pour valider l'automate seul : débrancher l'équipement, donner son IP au PC (ou mettre l'IP du
   PC dans `"tcp": {"ip": ...}` de config.json) et lancer `python faux_esclave_modbus.py`
   (autoriser Python dans le pare-feu Windows). L'écran de l'automate doit afficher les adresses
   lues comme valeurs (310, 311…).

Lecture des résultats de `test_modbus_tcp.py` :

| Résultat | Signification |
|---|---|
| connexion TCP impossible | rien n'écoute sur ce port (serveur absent ou désactivé, mauvaise IP, mauvais réseau) |
| exception 2 | le serveur existe, l'adresse n'existe pas : relever la vraie table |
| exception 1 | code fonction non géré |
| pas de réponse | connexion acceptée mais aucune réponse : essayer un autre n° d'unité |
| valeurs à 0 | les registres existent mais personne ne les écrit côté équipement |

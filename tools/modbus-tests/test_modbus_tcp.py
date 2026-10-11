#!/usr/bin/env python3
"""Vérifie depuis un PC qu'un équipement répond en Modbus TCP, avant d'y brancher l'automate.

    pip install -U "pymodbus>=3.8"      (Python 3.9 ou plus récent)
    python test_modbus_tcp.py 192.168.10.50                  # mêmes lectures que BancAubo
    python test_modbus_tcp.py 192.168.10.50 --port 502 --unites 1,0,255 --adresses 310,320,300
    python test_modbus_tcp.py 192.168.10.50 --scan            # balayage FC01 à FC04, adresses 0 à 599

Lecture du résultat :
  - « connexion TCP impossible » : rien n'écoute sur ce port (pas de serveur Modbus TCP,
    serveur désactivé, mauvaise IP ou mauvais réseau). L'automate ne pourra pas communiquer,
    quel que soit son programme.
  - « exception 2 » : le serveur existe mais l'adresse n'existe pas (table d'adresses à relever).
  - « exception 1 » : le code fonction n'est pas géré.
  - « pas de réponse » : connexion acceptée mais aucune réponse (n° d'unité, serveur muet).
  - des valeurs : le serveur répond ; des 0 partout peuvent vouloir dire que personne n'écrit
    ces registres côté équipement.
"""
import argparse
import sys

import pymodbus
from pymodbus.client import ModbusTcpClient


def _version(texte):
    morceaux = []
    for part in texte.split(".")[:2]:
        chiffres = "".join(c for c in part if c.isdigit())
        morceaux.append(int(chiffres or 0))
    return tuple(morceaux)


# Le n° d'unité s'appelle « slave » avant pymodbus 3.10 et « device_id » ensuite. Les versions
# 3.0 à 3.6 acceptent un mot-clé inconnu sans erreur (et l'ignorent) : on choisit donc d'après la
# version, sans essai.
MOT_UNITE = "device_id" if _version(pymodbus.__version__) >= (3, 10) else "slave"


def lire(fonction, adresse, nombre, unite):
    """Une lecture Modbus ; renvoie la réponse, ou l'exception (délai dépassé, connexion coupée...)."""
    try:
        return fonction(adresse, count=nombre, **{MOT_UNITE: unite})
    except Exception as erreur:
        return erreur


def decrire(reponse, nombre=1, bits=False):
    if isinstance(reponse, Exception):
        return f"pas de réponse ({type(reponse).__name__})"
    if reponse.isError():
        code = getattr(reponse, "exception_code", None)
        return f"exception {code}" if code is not None else f"erreur ({reponse})"
    if bits:
        return f"états {[int(b) for b in reponse.bits[:nombre]]}"
    return f"valeurs {reponse.registers}"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("ip")
    p.add_argument("--port", type=int, default=502)
    p.add_argument("--unites", default="1,0,255", help="n° d'unité à essayer, séparés par des virgules")
    p.add_argument("--adresses", default="310,320,300", help="registres de maintien (FC03) à lire")
    p.add_argument("--scan", action="store_true", help="balayer FC01 à FC04 sur les adresses 0 à 599")
    p.add_argument("--timeout", type=float, default=1.0)
    args = p.parse_args()
    unites = [int(u) for u in args.unites.split(",") if u.strip()]
    adresses = [int(a) for a in args.adresses.split(",") if a.strip()]

    print(f"pymodbus {pymodbus.__version__}")
    client = ModbusTcpClient(args.ip, port=args.port, timeout=args.timeout, retries=1)
    if not client.connect():
        print(f"connexion TCP impossible vers {args.ip}:{args.port} : aucun serveur Modbus TCP ne répond.")
        print("Vérifier l'IP, le câble et le réseau, et que le serveur Modbus est activé sur l'équipement.")
        return 1
    print(f"connexion TCP établie vers {args.ip}:{args.port}")

    for unite in unites:
        for adresse in adresses:
            print(f"FC03 unité {unite} adresse {adresse} : {decrire(lire(client.read_holding_registers, adresse, 1, unite))}")

    if args.scan:
        fonctions = (
            ("FC01", client.read_coils),
            ("FC02", client.read_discrete_inputs),
            ("FC03", client.read_holding_registers),
            ("FC04", client.read_input_registers),
        )
        for unite in unites:
            for nom, fonction in fonctions:
                bits = nom in ("FC01", "FC02")
                for adresse in range(0, 600, 10):
                    reponse = lire(fonction, adresse, 10, unite)
                    if isinstance(reponse, Exception):
                        continue  # pas de réponse : inutile d'insister sur ce bloc
                    if not reponse.isError():
                        print(f"{nom} unité {unite} adresses {adresse}-{adresse + 9} : {decrire(reponse, 10, bits)}")
                        continue
                    # Bloc refusé (exception) : il peut contenir des adresses valides isolées.
                    for a in range(adresse, adresse + 10):
                        r = lire(fonction, a, 1, unite)
                        if not isinstance(r, Exception) and not r.isError():
                            print(f"{nom} unité {unite} adresse {a} : {decrire(r, 1, bits)}")
    client.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

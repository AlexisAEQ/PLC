#!/usr/bin/env python3
"""Vérifie depuis un PC qu'un équipement répond en Modbus TCP, avant d'y brancher l'automate.

    pip install pymodbus
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

from pymodbus.client import ModbusTcpClient


def lire(fonction, adresse, nombre, unite):
    """Une lecture Modbus, compatible avec pymodbus avant et après la 3.10 (slave / device_id)."""
    try:
        try:
            return fonction(adresse, count=nombre, device_id=unite)
        except TypeError:
            return fonction(adresse, count=nombre, slave=unite)
    except Exception as erreur:  # délai dépassé, connexion coupée...
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

    client = ModbusTcpClient(args.ip, port=args.port, timeout=args.timeout, retries=0)
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
                for adresse in range(0, 600, 10):
                    reponse = lire(fonction, adresse, 10, unite)
                    if not isinstance(reponse, Exception) and not reponse.isError():
                        texte = decrire(reponse, 10, bits=nom in ("FC01", "FC02"))
                        print(f"{nom} unité {unite} adresses {adresse}-{adresse + 9} : {texte}")
    client.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

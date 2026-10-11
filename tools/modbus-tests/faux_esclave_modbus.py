#!/usr/bin/env python3
"""Faux équipement Modbus TCP, pour valider l'automate sans le robot (Python standard, sans module).

    python faux_esclave_modbus.py              # port 502 (sous Windows : autoriser Python dans le pare-feu)
    python faux_esclave_modbus.py --port 1502

Donner au PC l'IP de l'équipement remplacé (ex. 192.168.10.50/24) ou mettre l'IP du PC dans
"tcp": {"ip": ...} de config.json. Chaque registre (FC03 / FC04) renvoie son adresse comme
valeur, les bobines et entrées TOR (FC01 / FC02) valent 1 sur les adresses paires ; les écritures
(FC05, FC06, FC15, FC16) sont acceptées et affichées. Chaque requête reçue est affichée : si
l'automate lit bien les valeurs attendues, son firmware et son réseau sont hors de cause.
"""
import argparse
import socket
import struct
import threading
import time


def horodatage():
    return time.strftime("%H:%M:%S")


def reponse(pdu, registres_ecrits):
    fc = pdu[0]
    if fc in (1, 2) and len(pdu) >= 5:
        debut, nombre = struct.unpack(">HH", pdu[1:5])
        bits = [((debut + i) % 2 == 0) for i in range(nombre)]
        octets = bytearray((nombre + 7) // 8)
        for i, b in enumerate(bits):
            if b:
                octets[i // 8] |= 1 << (i % 8)
        return bytes([fc, len(octets)]) + bytes(octets), f"adresse {debut} nb {nombre}"
    if fc in (3, 4) and len(pdu) >= 5:
        debut, nombre = struct.unpack(">HH", pdu[1:5])
        valeurs = [registres_ecrits.get(debut + i, (debut + i) & 0xFFFF) for i in range(nombre)]
        return bytes([fc, 2 * nombre]) + b"".join(struct.pack(">H", v) for v in valeurs), f"adresse {debut} nb {nombre}"
    if fc in (5, 6) and len(pdu) >= 5:
        adresse, valeur = struct.unpack(">HH", pdu[1:5])
        if fc == 6:
            registres_ecrits[adresse] = valeur
        return pdu[:5], f"écriture adresse {adresse} valeur {valeur}"
    if fc in (15, 16) and len(pdu) >= 6:
        debut, nombre = struct.unpack(">HH", pdu[1:5])
        if fc == 16:
            for i in range(nombre):
                registres_ecrits[debut + i] = struct.unpack(">H", pdu[6 + 2 * i:8 + 2 * i])[0]
        return pdu[:5], f"écriture adresses {debut}-{debut + nombre - 1}"
    return bytes([fc | 0x80, 0x01]), "code fonction non géré"


def client(sock, adresse, registres_ecrits):
    print(horodatage(), "connexion de", adresse, flush=True)
    tampon = b""
    with sock:
        while True:
            donnees = sock.recv(1024)
            if not donnees:
                print(horodatage(), "déconnexion de", adresse, flush=True)
                return
            tampon += donnees
            while len(tampon) >= 7:
                transaction, _, longueur, unite = struct.unpack(">HHHB", tampon[:7])
                if len(tampon) < 6 + longueur:
                    break
                pdu, tampon = tampon[7:6 + longueur], tampon[6 + longueur:]
                rep, texte = reponse(pdu, registres_ecrits)
                print(horodatage(), f"FC{pdu[0]:02d} unité {unite} {texte}", flush=True)
                sock.sendall(struct.pack(">HHHB", transaction, 0, len(rep) + 1, unite) + rep)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", type=int, default=502)
    p.add_argument("--ip", default="0.0.0.0", help="interface d'écoute (toutes par défaut)")
    args = p.parse_args()
    registres_ecrits = {}
    serveur = socket.socket()
    serveur.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    serveur.bind((args.ip, args.port))
    serveur.listen(5)
    print(f"Faux équipement Modbus TCP en écoute sur {args.ip}:{args.port}", flush=True)
    while True:
        sock, adresse = serveur.accept()
        threading.Thread(target=client, args=(sock, adresse, registres_ecrits), daemon=True).start()


if __name__ == "__main__":
    main()

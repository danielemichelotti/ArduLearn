"""Trova le schede PLC Blocchi nella rete locale.

Invia un messaggio UDP broadcast sulla porta 4210 da ogni scheda di rete del PC
(Wi-Fi, Ethernet, cavo diretto...): ogni scheda PLC risponde con nome,
indirizzo IP e programma caricato.

    python tools/trova_plc.py
"""
import json
import select
import socket
import time

PORT = 4210
WAIT = 2.0


def local_ipv4():
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    return [ip for ip in ips if not ip.startswith("127.")]


def broadcast_for(ip):
    # 169.254.x.y (indirizzo automatico, cavo diretto) e' una rete /16; per le altre si ipotizza /24
    if ip.startswith("169.254."):
        return "169.254.255.255"
    return ip.rsplit(".", 1)[0] + ".255"


def main():
    socks = []
    for ip in local_ipv4():
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            s.bind((ip, 0))                       # esce da questa scheda di rete
            for dest in (broadcast_for(ip), "255.255.255.255"):
                try:
                    s.sendto(b"PLC?", (dest, PORT))
                except OSError:
                    pass
            socks.append(s)
        except OSError:
            pass

    found = {}
    end = time.time() + WAIT
    while socks and time.time() < end:
        ready, _, _ = select.select(socks, [], [], 0.2)
        for s in ready:
            try:
                data, (ip, _) = s.recvfrom(512)
                found[ip] = json.loads(data.decode("utf-8", "replace"))
            except (OSError, ValueError):
                pass

    if not found:
        print("Nessuna scheda trovata. Controlla che PC e schede siano nella stessa rete")
        print("e che il firewall non blocchi le risposte UDP sulla porta 4210.")
        return
    print(f"{'Nome':<18}{'Indirizzo':<30}{'Stato':<7}Programma")
    for ip, d in sorted(found.items()):
        print(f"{d.get('host', '?'):<18}{'http://' + ip + '/':<30}{'RUN' if d.get('run') else 'STOP':<7}{d.get('prog', '')}")
        print(f"{'':<18}http://{d.get('host', '?')}.local/")


if __name__ == "__main__":
    main()

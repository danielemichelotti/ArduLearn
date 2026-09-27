"""ArduLearn - applicazione per il PC.

Schede della finestra:
  1. "Trova le schede": cerca le schede ArduLearn nella rete locale
     (broadcast UDP sulla porta 4210), le apre nel browser e prepara la
     pagina web sulla microSD del Mega (POST /api/file).
  2. "Carica il firmware": carica il firmware via USB su Arduino Mega 2560
     (avrdude) o UNO R4 WiFi: prima il modulo Wi-Fi ESP32-S3 (firmware ArduLearnBridge,
     con esptool, se manca o e' vecchio), poi il PLC RA4M1 (bossac).
  3. "Monitor seriale": monitor a 115200 baud con i pulsanti dei comandi.
  4. "Guida": istruzioni per l'uso.

La rete Wi-Fi della UNO R4 WiFi si imposta dalla pagina web della scheda
(Impostazioni -> Rete Wi-Fi), non da questo programma.

Si avvia con:  python ardulearn_app.py
Il file eseguibile si crea con build.bat (PyInstaller).
"""
import codecs
import http.client
import json
import locale
import os
import queue
import re
import select
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from datetime import datetime

import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk
from tkinter.scrolledtext import ScrolledText

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pyserial mancante: le schede USB mostreranno un avviso
    serial = None
    list_ports = None


TITOLO = "ArduLearn"
FIRMA = "ideato e realizzato da Daniele Michelotti"
VERDE = "#1a7f37"
ROSSO = "#c62828"
ARANCIO = "#b26a00"

# ---------------------------------------------------------------------------
# Percorsi delle risorse
# ---------------------------------------------------------------------------

def cartella_risorse():
    """Cartella con le risorse incluse (bin/, firmware/, sd/).

    Nell'eseguibile PyInstaller è la cartella temporanea _MEIPASS,
    altrimenti la cartella di questo file.
    """
    return getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))


def cartella_programma():
    """Cartella in cui si trova l'eseguibile (o lo script)."""
    if getattr(sys, "frozen", False):
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


def trova_risorsa(*parti):
    """Percorso di un file di risorse (es. "firmware", "x.hex").

    Se accanto all'eseguibile c'è la stessa cartella con il file, si usa
    quella (così firmware e pagina si aggiornano senza ricreare l'exe);
    altrimenti si usa la copia inclusa. Restituisce None se manca.
    """
    for base in (cartella_programma(), cartella_risorse()):
        percorso = os.path.join(base, *parti)
        if os.path.isfile(percorso):
            return percorso
    return None


def trova_firmware(nome_file):
    return trova_risorsa("firmware", nome_file)


def trova_file_sd(nome_file):
    return trova_risorsa("sd", "PLC", nome_file)


def e_esterno(percorso):
    """True se il file viene dalla cartella accanto all'exe (non da quella inclusa)."""
    return bool(getattr(sys, "frozen", False)) and not percorso.startswith(cartella_risorse())


def leggi_versioni():
    """Legge firmware/versions.json (prima quello esterno, poi quello incluso)."""
    percorso = trova_firmware("versions.json")
    if percorso:
        try:
            with open(percorso, encoding="utf-8") as f:
                dati = json.load(f)
            if isinstance(dati, dict):
                return dati
        except (OSError, ValueError):
            pass
    return {}


def versione_pagina_inclusa():
    """Versione della pagina per la microSD inclusa (numero), 0 se manca."""
    percorso = trova_file_sd("INDEX.VER")
    if not percorso:
        return 0
    try:
        with open(percorso, encoding="ascii", errors="replace") as f:
            return int(f.read().strip() or 0)
    except (OSError, ValueError):
        return 0


# ---------------------------------------------------------------------------
# Ricerca delle schede in rete (UDP broadcast) e dialogo HTTP
# ---------------------------------------------------------------------------

PORTA_UDP = 4210
ATTESA_RISPOSTE = 1.5  # secondi

# Le schede sono nella rete locale: niente proxy di sistema (nelle scuole spesso
# è configurato e le richieste a 192.168.x.x / 169.254.x.x fallirebbero).
_HTTP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def ip_locali():
    """Indirizzi IPv4 di tutte le schede di rete del PC (escluso 127.x)."""
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    try:
        ips.update(socket.gethostbyname_ex(socket.gethostname())[2])
    except OSError:
        pass
    return sorted(ip for ip in ips if not ip.startswith("127."))


def broadcast_per(ip):
    """Indirizzo di broadcast da usare per una scheda di rete.

    169.254.x.y (indirizzo automatico, cavo diretto) è una rete /16;
    per le altre si ipotizza una rete /24.
    """
    if ip.startswith("169.254."):
        return "169.254.255.255"
    return ip.rsplit(".", 1)[0] + ".255"


def cerca_schede(attesa=ATTESA_RISPOSTE):
    """Invia "PLC?" da ogni scheda di rete e raccoglie le risposte.

    Restituisce un dizionario {ip: dati_json}.
    """
    socks = []
    # None = socket non legato a una scheda di rete (riserva se l'elenco degli
    # indirizzi del PC non è disponibile)
    for ip in ip_locali() or [None]:
        s = None
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            s.bind((ip or "", 0))  # la richiesta esce da questa scheda di rete
            destinazioni = (broadcast_per(ip), "255.255.255.255") if ip else ("255.255.255.255",)
            for dest in destinazioni:
                try:
                    s.sendto(b"PLC?", (dest, PORTA_UDP))
                except OSError:
                    pass
            socks.append(s)
        except OSError:
            if s is not None:
                s.close()

    trovate = {}
    fine = time.time() + attesa
    try:
        while socks and time.time() < fine:
            pronti, _, _ = select.select(socks, [], [], 0.2)
            for s in pronti:
                try:
                    dati, (ip, _) = s.recvfrom(4096)
                    info = json.loads(dati.decode("utf-8", "replace"))
                    if isinstance(info, dict):
                        # si usa l'indirizzo da cui arriva la risposta: quello
                        # dichiarato può essere 0.0.0.0 (DHCP in corso)
                        trovate[ip] = info
                except (OSError, ValueError):
                    pass
    finally:
        for s in socks:
            s.close()
    return trovate


def leggi_info(ip, timeout=2.0):
    """GET http://<ip>/api/info (sola lettura). Restituisce il dizionario JSON."""
    with _HTTP.open(f"http://{ip}/api/info", timeout=timeout) as r:
        dati = json.loads(r.read().decode("utf-8", "replace"))
    if not isinstance(dati, dict):
        raise ValueError("risposta non valida")
    return dati


def _chiave_ip(ip):
    """Chiave per ordinare gli indirizzi IP in ordine numerico."""
    try:
        return tuple(int(x) for x in ip.split("."))
    except ValueError:
        return (999,)


def versione_pagina(info):
    """Versione della pagina sulla microSD ("pv" di /api/info), 0 se manca o non valida."""
    try:
        return int(info.get("pv") or 0)
    except (TypeError, ValueError):
        return 0


# valori di "board" in /api/info ("r4" = firmware vecchio delle UNO R4)
NOMI_BOARD = {"mega": "Mega", "r4wifi": "UNO R4 WiFi", "r4": "UNO R4"}


def pagina_nel_firmware(info):
    """True per le vecchie UNO R4 WiFi con la pagina nel firmware del RA4M1.
    Con il firmware ArduLearnBridge ("fs" in /api/info) la pagina sta nella memoria del
    modulo Wi-Fi e si aggiorna come quella della microSD del Mega (POST /api/file)."""
    return isinstance(info, dict) and info.get("board") == "r4wifi" and not info.get("fs")


def stato_pagina(info, pv_inclusa):
    """Stato della pagina web sulla microSD di una scheda.

    Restituisce (testo, livello) con livello "ok", "avviso", "errore" o "".
    """
    if info is None:
        return "...", ""
    if isinstance(info, Exception):
        return "non leggibile", ""
    if pagina_nel_firmware(info):
        return "nel firmware", ""     # niente microSD: la pagina è nel firmware
    r4 = info.get("board") == "r4"    # firmware vecchio delle UNO R4 (pagina sulla SD)
    if "pv" not in info:
        return "firmware vecchio", ""
    pv = versione_pagina(info)
    if r4 and not info.get("sd"):
        return "microSD assente!", "errore"
    if pv == 0:
        if r4:
            return "assente: preparala!", "errore"
        return "assente (usa quella del firmware)", ""
    if pv_inclusa and pv < pv_inclusa:
        return "da aggiornare", "avviso"
    return "aggiornata", "ok"


def segnale_percento(rssi):
    """Intensità del segnale Wi-Fi in percentuale (-100 dBm = 0 %, -50 dBm = 100 %)."""
    try:
        rssi = int(rssi)
    except (TypeError, ValueError):
        return None
    if rssi >= 0:          # 0 = valore non disponibile
        return None
    return max(0, min(100, 2 * (rssi + 100)))


def descrizione_rete(info, udp=None):
    """Testo della colonna "Rete": "Wi-Fi <ssid> (segnale X%)" oppure "Ethernet".

    info = dizionario di /api/info (o None / eccezione), udp = risposta UDP.
    """
    wifi = None
    board = None
    for sorgente in (info, udp):
        if isinstance(sorgente, dict):
            if wifi is None and isinstance(sorgente.get("wifi"), dict):
                wifi = sorgente["wifi"]
            board = board or sorgente.get("board")
    if wifi is not None:
        modo = wifi.get("mode")
        if modo == "ap":
            ap = wifi.get("ap") or "ArduLearn"
            return f"Wi-Fi: rete propria {ap}"
        ssid = wifi.get("ssid") or "?"
        pct = segnale_percento(wifi.get("rssi"))
        return f"Wi-Fi {ssid}" + (f" (segnale {pct}%)" if pct is not None else "")
    if board == "r4wifi":
        return "Wi-Fi"
    if board in ("mega", "r4"):
        return "Ethernet"
    return ""


class _CorpoConAvanzamento:
    """Corpo della richiesta HTTP che segnala quanti byte sono stati inviati."""

    def __init__(self, dati, avanzamento):
        self.dati = dati
        self.pos = 0
        self.avanzamento = avanzamento

    def read(self, n=-1):
        if n is None or n < 0:
            n = len(self.dati) - self.pos
        n = min(n, 4096)  # blocchi piccoli: l'avanzamento segue la scrittura sulla SD
        blocco = self.dati[self.pos:self.pos + n]
        self.pos += len(blocco)
        if self.avanzamento:
            self.avanzamento(self.pos, len(self.dati))
        return blocco


def invia_file(ip, nome, dati, pin, timeout=60, avanzamento=None):
    """POST http://<ip>/api/file?name=<nome> con header X-Pin.

    Restituisce (ok, messaggio_in_italiano).
    """
    url = f"http://{ip}/api/file?name={urllib.parse.quote(nome)}"
    richiesta = urllib.request.Request(
        url, data=_CorpoConAvanzamento(dati, avanzamento), method="POST",
        headers={"X-Pin": str(pin), "Content-Type": "application/octet-stream",
                 "Content-Length": str(len(dati))})
    try:
        with _HTTP.open(richiesta, timeout=timeout) as r:
            corpo = r.read()
            stato = r.status
    except urllib.error.HTTPError as exc:
        stato = exc.code
        try:
            corpo = exc.read()
        except OSError:
            corpo = b""
    except (urllib.error.URLError, OSError, http.client.HTTPException) as exc:
        motivo = getattr(exc, "reason", exc)
        return False, f"Scheda non raggiungibile ({motivo})."

    try:
        risposta = json.loads(corpo.decode("utf-8", "replace") or "{}")
    except ValueError:
        risposta = {}
    if not isinstance(risposta, dict):
        risposta = {}
    if stato == 200 and risposta.get("ok"):
        return True, "ok"
    if stato == 403:
        return False, "PIN docente errato."
    if stato == 404:
        return False, ("La scheda non riconosce il comando: aggiorna prima il "
                       "firmware della scheda.")
    errore = risposta.get("err") or f"errore HTTP {stato}"
    return False, str(errore)


def prepara_microsd(ip, pin, avanzamento=None, timeout=60):
    """Invia INDEX.GZ e INDEX.VER alla scheda. Restituisce (ok, messaggio)."""
    gz = trova_file_sd("INDEX.GZ")
    ver = trova_file_sd("INDEX.VER")
    if not gz or not ver:
        return False, "File della pagina (sd\\PLC\\INDEX.GZ e INDEX.VER) non trovati."
    with open(gz, "rb") as f:
        dati_gz = f.read()
    with open(ver, "rb") as f:
        dati_ver = f.read().strip()
    ok, msg = invia_file(ip, "INDEX.GZ", dati_gz, pin, timeout, avanzamento)
    if not ok:
        return False, msg
    ok, msg = invia_file(ip, "INDEX.VER", dati_ver, pin, timeout)
    if not ok:
        return False, msg
    return True, (f"Pagina copiata sulla scheda ({len(dati_gz) // 1024} KB, "
                  f"versione {dati_ver.decode('ascii', 'replace')}).")


# ---------------------------------------------------------------------------
# Riconoscimento delle schede Arduino sulle porte seriali
# ---------------------------------------------------------------------------

MEGA = "Arduino Mega 2560"
R4_WIFI = "Arduino UNO R4 WiFi"
SCHEDE = (MEGA, R4_WIFI)

MSG_UNO_CLASSICO = ("Arduino Uno classico: non supportato (memoria insufficiente). "
                    "Usa un Arduino Mega 2560 con shield Ethernet, oppure un UNO R4 WiFi.")
MSG_R4_MINIMA = ("UNO R4 Minima: non supportata (per ora). Usa un Arduino Mega 2560 "
                 "con shield Ethernet, oppure un UNO R4 WiFi.")

# prossimi passi dopo il caricamento del firmware sulla UNO R4 WiFi
PASSI_R4_WIFI = ("Se la scheda non conosce ancora una rete Wi-Fi crea la propria rete "
                 "ArduLearn-xxxx (password ardulearn): collega il PC o il telefono a quella rete "
                 "(la pagina si apre da sola, oppure http://192.168.4.1/) e in Impostazioni → "
                 "Rete Wi-Fi inserisci la rete della scuola. L'indirizzo della scheda scorre "
                 "sulla matrice LED.")

VIDPID_MEGA = {(0x2341, 0x0010), (0x2341, 0x0042), (0x2A03, 0x0010),
               (0x2A03, 0x0042), (0x2341, 0x0210), (0x2341, 0x0242)}
VIDPID_UNO_CLASSICO = {(0x2341, 0x0043), (0x2341, 0x0001), (0x2A03, 0x0043),
                       (0x2341, 0x0243)}
# dal file boards.txt di renesas_uno 1.6.0
VIDPID_R4_MINIMA = {(0x2341, 0x0069), (0x2341, 0x0369)}   # anche il bootloader DFU
VIDPID_R4_WIFI = {(0x2341, 0x1002), (0x2341, 0x006D)}
# ESP32-S3 in modalita' download (ROM): UNO R4 WiFi da ripristinare (anche dopo un aggiornamento interrotto)
VIDPID_ESP_DOWNLOAD = {(0x303A, 0x1001)}
VIDPID_COMPATIBILI = {(0x1A86, 0x7523): "CH340", (0x10C4, 0xEA60): "CP210x",
                      (0x0403, 0x6001): "FTDI"}


def classifica_porta(vid, pid):
    """Restituisce (descrizione, scheda, non_supportata) per una coppia VID:PID.

    scheda è una delle SCHEDE oppure None (tipo sconosciuto o non supportato).
    non_supportata è False oppure il messaggio da mostrare (UNO classico, R4 Minima).
    """
    if vid is None or pid is None:
        return "dispositivo non USB / sconosciuto", None, False
    chiave = (vid, pid)
    if chiave in VIDPID_MEGA:
        return MEGA, MEGA, False
    if chiave in VIDPID_R4_WIFI:
        return R4_WIFI, R4_WIFI, False
    if chiave in VIDPID_ESP_DOWNLOAD:
        return "UNO R4 WiFi con il modulo Wi-Fi da ripristinare", R4_WIFI, False
    if chiave in VIDPID_R4_MINIMA:
        return "UNO R4 Minima: non supportata (per ora)", None, MSG_R4_MINIMA
    if chiave in VIDPID_UNO_CLASSICO:
        return "Arduino Uno classico: non supportato", None, MSG_UNO_CLASSICO
    if chiave in VIDPID_COMPATIBILI:
        return f"scheda compatibile {VIDPID_COMPATIBILI[chiave]} (tipo da scegliere)", None, False
    return "dispositivo sconosciuto", None, False


def elenca_porte():
    """Elenco delle porte seriali, senza aprirle.

    Ogni elemento: dict con porta, descrizione, scheda, non_supportata, vidpid, nome.
    """
    if list_ports is None:
        return []
    porte = []
    for p in list_ports.comports():
        descr, scheda, no = classifica_porta(p.vid, p.pid)
        vidpid = f"{p.vid:04X}:{p.pid:04X}" if p.vid is not None and p.pid is not None else "-"
        porte.append({"porta": p.device, "descrizione": descr, "scheda": scheda,
                      "non_supportata": no, "vidpid": vidpid, "nome": p.description or ""})

    def ordine(x):
        num = "".join(c for c in x["porta"] if c.isdigit())
        return int(num) if num else 0
    porte.sort(key=ordine)
    return porte


def porta_presente(porta):
    return any(p["porta"] == porta for p in elenca_porte())


# ---------------------------------------------------------------------------
# Caricamento del firmware (avrdude / bossac)
# ---------------------------------------------------------------------------

FIRMWARE = {MEGA: "PlcBlocchi_mega.hex", R4_WIFI: "PlcBlocchi_r4wifi.bin"}
# UNO R4 WiFi: firmware ArduLearnBridge del modulo Wi-Fi (ESP32-S3), indirizzi di partitions.csv.
# "completo" = firmware Arduino originale o modulo da ripristinare: si scrive tutto, anche la
# pagina (LittleFS); aggiornamento = solo l'app (restano slot, bozza e rete Wi-Fi salvata).
FILE_ESP_COMPLETO = (("ArduLearnBridge_bootloader.bin", 0x0), ("ArduLearnBridge_partitions.bin", 0x8000),
                     ("ArduLearnBridge_boot_app0.bin", 0xE000), ("ArduLearnBridge.bin", 0x10000),
                     ("ArduLearnBridge_littlefs.bin", 0x310000))
FILE_ESP_APP = (("ArduLearnBridge_boot_app0.bin", 0xE000), ("ArduLearnBridge.bin", 0x10000))
CHIAVE_VERSIONE = {MEGA: "mega", R4_WIFI: "r4wifi"}
PROGRAMMA = {MEGA: "avrdude.exe", R4_WIFI: "bossac.exe"}


def cartella_bin():
    return os.path.join(cartella_risorse(), "bin")


# I file si passano senza percorso: lo strumento viene avviato nella cartella
# del firmware (il percorso può contenere spazi, "+" o ":" che confondono avrdude).

def comando_avrdude(porta, file_fw):
    """Arduino Mega 2560: avrdude con il bootloader "wiring"."""
    b = cartella_bin()
    return [os.path.join(b, "avrdude.exe"), "-C", os.path.join(b, "avrdude.conf"),
            "-v", "-p", "atmega2560", "-c", "wiring", "-P", porta, "-b", "115200", "-D",
            "-U", f"flash:w:{os.path.basename(file_fw)}:i"]


def comando_bossac(porta, file_fw):
    """UNO R4 WiFi: bossac attraverso il ponte USB dell'ESP32 (ricetta renesas_uno)."""
    return [os.path.join(cartella_bin(), "bossac.exe"), f"--port={porta}",
            "-U", "-e", "-w", os.path.basename(file_fw), "-R"]


PROCESSI_ATTIVI = set()   # strumenti in esecuzione (da chiudere se si esce)


class Decodifica:
    """Decodifica a blocchi dell'output di un programma o della porta seriale.

    UTF-8, anche con i caratteri spezzati tra due blocchi. Con riserva=True, se
    l'output non è UTF-8 (messaggi di Windows in italiano, codifica ANSI) si
    passa alla codifica di sistema per il resto dell'output; altrimenti i byte
    non validi diventano "?" nel riquadro.
    """

    def __init__(self, riserva=False):
        self.riserva = riserva
        self.dec = codecs.getincrementaldecoder("utf-8")("strict" if riserva else "replace")

    def __call__(self, blocco):
        if self.riserva:
            pendenti = self.dec.getstate()[0]
            try:
                return self.dec.decode(blocco)
            except UnicodeDecodeError:
                dati = pendenti + blocco
                try:
                    dati.decode("utf-8")
                    inizio = len(dati)
                except UnicodeDecodeError as exc:
                    inizio = exc.start   # la parte prima dell'errore è UTF-8 valido
                self.riserva = False
                self.dec = codecs.getincrementaldecoder(
                    locale.getpreferredencoding(False) or "cp1252")("replace")
                return dati[:inizio].decode("utf-8") + self.dec.decode(dati[inizio:])
        return self.dec.decode(blocco)

    def fine(self):
        """Byte rimasti in sospeso alla fine dell'output."""
        if self.riserva:  # UTF-8 incompleto alla fine: era la codifica di sistema
            return self.dec.getstate()[0].decode(
                locale.getpreferredencoding(False) or "cp1252", "replace")
        return self.dec.decode(b"", True)


def esegui_strumento(comando, cartella, scrivi, timeout=None):
    """Avvia un programma, inoltra l'output a scrivi(testo), restituisce il codice di uscita."""
    scrivi("> " + " ".join(f'"{a}"' if " " in a else a for a in
                           [os.path.basename(comando[0])] + comando[1:]) + "\n")
    try:
        proc = subprocess.Popen(
            comando, cwd=cartella, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except OSError as exc:
        scrivi(f"Impossibile avviare {os.path.basename(comando[0])}: {exc}\n")
        return -1
    PROCESSI_ATTIVI.add(proc)
    timer = None
    scaduto = []
    if timeout:
        def scadenza():
            scaduto.append(True)
            proc.kill()
        timer = threading.Timer(timeout, scadenza)
        timer.daemon = True
        timer.start()
    decodifica = Decodifica(riserva=True)
    try:
        # si legge a blocchi: le barre di avanzamento non vanno a capo
        while True:
            blocco = proc.stdout.read1(256) if hasattr(proc.stdout, "read1") else proc.stdout.read(1)
            if not blocco:
                break
            scrivi(decodifica(blocco).replace("\r", ""))
        scrivi(decodifica.fine())
        rc = proc.wait()
        if scaduto:
            scrivi(f"\n{os.path.basename(comando[0])} fermato: nessuna risposta dopo "
                   f"{timeout} secondi (timeout).\n")
        return rc
    finally:
        if timer:
            timer.cancel()
        proc.stdout.close()
        PROCESSI_ATTIVI.discard(proc)


def tocco_1200(porta, scrivi):
    """Apre la porta a 1200 baud e la chiude: la scheda entra nel bootloader."""
    scrivi(f"Tocco a 1200 baud sulla porta {porta}...\n")
    if serial is None:
        raise RuntimeError("libreria pyserial non disponibile")
    s = serial.Serial()
    s.port = porta
    s.baudrate = 1200
    s.open()
    try:
        s.dtr = False
    finally:
        s.close()


def attendi_porta(porta, secondi, presente=porta_presente):
    """Aspetta che la porta compaia (fino a 'secondi'). True se c'è."""
    fine = time.time() + secondi
    while time.time() < fine:
        if presente(porta):
            return True
        time.sleep(0.25)
    return presente(porta)


def versione_tupla(testo):
    """"0.3.1" -> (0, 3, 1)."""
    try:
        return tuple(int(x) for x in str(testo).split(".")[:3])
    except ValueError:
        return (0, 0, 0)


def leggi_modulo_wifi():
    """Firmware del modulo Wi-Fi della UNO R4 WiFi, letto via HID (come fa l'IDE Arduino).

    Restituisce (versione, ardulearn): (0, 3, 1), True per ArduLearnBridge (che aggiunge il
    byte 'A'); (0, 6, 0), False per il firmware Arduino originale; None se non si legge.
    """
    try:
        import hid
        d = hid.device()
        d.open(0x2341, 0x1002)
        try:
            r = list(d.get_feature_report(0, 65))
        finally:
            d.close()
    except Exception:
        return None
    if len(r) >= 4 and r[0] == 0:
        r = r[1:]                      # primo byte: numero del report
    if len(r) < 3:
        return None
    return (r[0], r[1], r[2]), len(r) >= 4 and r[3] == 0x41


def porta_modulo_in_download():
    """Porta dell'ESP32-S3 in modalita' download (303A:1001), None se non c'e'."""
    if list_ports is None:
        return None
    for p in list_ports.comports():
        if (p.vid, p.pid) in VIDPID_ESP_DOWNLOAD:
            return p.device
    return None


def porta_r4():
    """Porta della UNO R4 WiFi (2341:1002) dopo il riavvio del modulo, None se non c'e'."""
    if list_ports is None:
        return None
    for p in list_ports.comports():
        if (p.vid, p.pid) in VIDPID_R4_WIFI:
            return p.device
    return None


def modulo_in_download(scrivi, attendi_s=15):
    """Comando HID 0xAA (come l'aggiornamento del firmware dell'IDE): l'ESP32 si riavvia in
    modalita' download. Restituisce la porta seriale della ROM dell'ESP32, None se non compare."""
    porta = porta_modulo_in_download()
    if porta:
        return porta
    scrivi("Modulo Wi-Fi: riavvio in modalita' aggiornamento...\n")
    fine = time.time() + attendi_s
    inviato = False
    while time.time() < fine:
        if not inviato:
            try:
                import hid
                d = hid.device()
                d.open(0x2341, 0x1002)
                b = [0] * 65
                b[1] = 0xAA
                d.send_feature_report(b)
                d.close()
                inviato = True
            except Exception:
                time.sleep(0.1)        # un firmware che si riavvia di continuo risponde a tratti
                continue
        porta = porta_modulo_in_download()
        if porta:
            return porta
        time.sleep(0.25)
    return porta_modulo_in_download()


def comando_esptool(porta, file_indirizzi):
    return ([os.path.join(cartella_bin(), "esptool.exe"), "--chip", "esp32s3", "--port", porta,
             "--baud", "921600", "--before", "no-reset", "--after", "hard-reset", "write-flash", "-z",
             "--flash-mode", "keep", "--flash-size", "keep"] +
            [x for nome, indirizzo in file_indirizzi for x in (hex(indirizzo), nome)])


def installa_modulo_wifi(scrivi, esegui=esegui_strumento, forza=False):
    """Prima installazione o aggiornamento del firmware ArduLearnBridge sul modulo Wi-Fi.

    Restituisce la porta della scheda dopo il riavvio (None se qualcosa non va)."""
    incluso = versione_tupla(leggi_versioni().get("bridge", "0"))
    rom = porta_modulo_in_download()
    letto = None if rom else leggi_modulo_wifi()
    if not rom and letto is None:
        scrivi("Modulo Wi-Fi: versione non leggibile (manca la libreria hid o la scheda non "
               "risponde). Scollega e ricollega la scheda e riprova.\n")
        return None
    if letto:
        v, nostro = letto
        nome = "ArduLearnBridge" if nostro else "firmware Arduino originale"
        scrivi(f"Modulo Wi-Fi: {nome} {'.'.join(map(str, v))}\n")
        if nostro and v >= incluso and not forza:
            scrivi("Modulo Wi-Fi gia' aggiornato.\n")
            return porta_r4()
    completo = rom is not None or not letto[1]
    file_esp = FILE_ESP_COMPLETO if completo else FILE_ESP_APP
    mancanti = [nome for nome, _ in file_esp if not trova_firmware(nome)]
    if mancanti:
        scrivi("Mancano i file del modulo Wi-Fi: " + ", ".join(mancanti) + "\n")
        return None
    scrivi("Modulo Wi-Fi: " + ("installazione completa di ArduLearnBridge (anche la pagina)"
                               if completo else "aggiornamento di ArduLearnBridge (slot e rete restano)")
           + f" {'.'.join(map(str, incluso))}\n")
    rom = rom or modulo_in_download(scrivi)
    if not rom:
        scrivi("Il modulo Wi-Fi non e' entrato in modalita' aggiornamento.\n")
        return None
    cartella = os.path.dirname(trova_firmware(file_esp[0][0]))
    for tentativo in range(3):         # la porta a volte e' occupata per un attimo
        if esegui(comando_esptool(rom, file_esp), cartella, scrivi, timeout=240) == 0:
            break
        time.sleep(2)
    else:
        return None
    scrivi("Modulo Wi-Fi aggiornato: attendo che la scheda riparta...\n")
    fine = time.time() + 20
    while time.time() < fine and not porta_r4():
        time.sleep(0.5)
    time.sleep(4)                      # il modulo riavvia anche il PLC: si aspetta che sia pronto
    return porta_r4()


def esegui_caricamento(scheda, porta, file_fw, scrivi, esegui=esegui_strumento,
                       tocco=tocco_1200, attesa=time.sleep, attendi=attendi_porta,
                       modulo=installa_modulo_wifi):
    """Sequenza completa di caricamento. Restituisce True se riuscito.

    Le funzioni esegui/tocco/attesa/attendi/modulo si possono sostituire nei test.
    """
    cartella = os.path.dirname(file_fw)
    if scheda == MEGA:
        return esegui(comando_avrdude(porta, file_fw), cartella, scrivi, timeout=180) == 0

    if scheda == R4_WIFI:
        # 1) modulo Wi-Fi (ESP32-S3): pagina, rete e collegamento; 2) PLC (RA4M1) con bossac
        nuova = modulo(scrivi, esegui)
        if not nuova:
            return False
        porta = nuova
        scrivi(f"\nPLC (RA4M1) sulla porta {porta}:\n")
        tocco(porta, scrivi)
        attesa(1.5)
        if not attendi(porta, 5):
            scrivi(f"La porta {porta} non è ricomparsa dopo il tocco a 1200 baud.\n")
        return esegui(comando_bossac(porta, file_fw), cartella, scrivi, timeout=180) == 0

    raise ValueError(f"scheda non supportata: {scheda}")


def suggerimento_errore(testo, scheda=MEGA):
    """Spiegazione in italiano degli errori più comuni."""
    t = testo.lower()
    if "can't open device" in t or "access is denied" in t or "accesso negato" in t \
            or "could not open port" in t or "permissionerror" in t:
        return ("La porta è occupata o non esiste. Chiudi il monitor seriale "
                "dell'IDE Arduino e gli altri programmi che usano la porta, poi riprova.")
    if scheda == R4_WIFI:
        return ("La scheda non risponde. Controlla la porta e il cavo USB; se non "
                "basta premi due volte velocemente il tasto RESET e riprova.")
    if "not in sync" in t or "not responding" in t or "timeout" in t:
        return ("La scheda non risponde. Controlla di aver scelto la porta e il "
                "tipo di scheda giusti e il cavo USB, poi riprova.")
    if "expected signature" in t or "device signature" in t:
        return "Il tipo di scheda scelto non corrisponde alla scheda collegata."
    return "Controlla il cavo USB, la porta e il tipo di scheda, poi riprova."


# ---------------------------------------------------------------------------
# Comandi del monitor seriale
# ---------------------------------------------------------------------------

def valida_ipv4(testo):
    parti = testo.strip().split(".")
    if len(parti) != 4:
        return False
    for p in parti:
        if not p.isdigit() or len(p) > 3 or int(p) > 255:
            return False
    return True


def valida_mac(testo):
    return re.fullmatch(r"[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}", testo.strip()) is not None


def comando_ip(ip, maschera, gateway):
    """Comando "ip <indirizzo> <maschera> <gateway>" (ValueError se non valido)."""
    for nome, valore in (("indirizzo", ip), ("maschera", maschera), ("gateway", gateway)):
        if not valida_ipv4(valore):
            raise ValueError(f"{nome} non valido: \"{valore}\" (esempio 192.168.1.50)")
    return f"ip {ip.strip()} {maschera.strip()} {gateway.strip()}"


def comando_mac(mac):
    if not valida_mac(mac):
        raise ValueError(f"MAC non valido: \"{mac}\" (esempio 00:08:DC:12:34:56)")
    return f"mac {mac.strip().upper()}"


COMANDI_SEMPLICI = (
    ("help", "elenco dei comandi"),
    ("info", "nome, IP, MAC, SD, programma, RAM"),
    ("sd", "diagnosi della microSD"),
    ("run", "avvia il programma"),
    ("stop", "ferma il programma"),
    ("io", "ingressi/uscite e valori dei blocchi (una volta)"),
    ("io on", "stato I/O ogni secondo"),
    ("io off", "ferma lo stato I/O periodico"),
    ("ip dhcp", "torna al DHCP (riavvio della rete)"),
    ("pin reset", "PIN docente di nuovo 1234"),
)

COMANDI_WIFI = (   # solo UNO R4 WiFi
    ("wifi", "stato della rete Wi-Fi"),
    ("wifi clear", "dimentica la rete e torna alla rete ArduLearn"),
)


# ---------------------------------------------------------------------------
# Interfaccia grafica: elementi comuni
# ---------------------------------------------------------------------------

class Suggerimento:
    """Piccolo riquadro di aiuto che compare passando con il mouse."""

    def __init__(self, widget, testo):
        self.widget, self.testo, self.finestra = widget, testo, None
        widget.bind("<Enter>", self._mostra, add="+")
        widget.bind("<Leave>", self._nascondi, add="+")

    def _mostra(self, _e=None):
        if self.finestra:
            return
        x = self.widget.winfo_rootx() + 10
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self.finestra = tw = tk.Toplevel(self.widget)
        tw.wm_overrideredirect(True)
        tw.wm_geometry(f"+{x}+{y}")
        tk.Label(tw, text=self.testo, background="#ffffe0", relief="solid", borderwidth=1,
                 font=("Segoe UI", 9), justify="left", wraplength=320).pack(ipadx=4, ipady=2)

    def _nascondi(self, _e=None):
        if self.finestra:
            self.finestra.destroy()
            self.finestra = None


class ScegliPorta(ttk.Frame):
    """Combobox delle porte USB con aggiornamento automatico (condivisa dalle schede)."""

    INTERVALLO = 2000  # millisecondi

    def __init__(self, master, al_cambio=None):
        super().__init__(master)
        self.porte = []
        self.pausa = False
        self.al_cambio = al_cambio
        self.columnconfigure(0, weight=1)
        self.var = tk.StringVar()
        self.combo = ttk.Combobox(self, textvariable=self.var, state="readonly")
        self.combo.grid(row=0, column=0, sticky="ew")
        self.combo.bind("<<ComboboxSelected>>", lambda e: self._cambiata(False))
        self.pulsante = ttk.Button(self, text="Aggiorna", command=self.aggiorna)
        self.pulsante.grid(row=0, column=1, padx=(6, 0))
        self.errore = ""
        self.after(50, self.aggiorna)
        self.after(self.INTERVALLO, self._periodico)

    def _periodico(self):
        # solo se la scheda della finestra è visibile: l'elenco delle porte
        # blocca l'interfaccia per qualche decimo di secondo
        if not self.pausa and self.winfo_ismapped():
            self.aggiorna()
        self.after(self.INTERVALLO, self._periodico)

    def abilita(self, si):
        stato = ["!disabled"] if si else ["disabled"]
        self.combo.state(stato)
        self.pulsante.state(stato)

    def attuale(self):
        testo = self.var.get()
        for p in self.porte:
            if testo.startswith(p["porta"] + " "):
                return p
        return None

    def seleziona(self, porta):
        for i, p in enumerate(self.porte):
            if p["porta"] == porta:
                self.combo.current(i)
                self._cambiata(False)
                return True
        return False

    def aggiorna(self):
        if list_ports is None:
            self.errore = "Libreria pyserial non disponibile."
            self._cambiata(True)
            return
        try:
            porte = elenca_porte()
            self.errore = ""
        except Exception as exc:
            self.errore = f"Errore nell'elenco delle porte: {exc}"
            self._cambiata(True)
            return
        nomi_vecchi = [p["porta"] for p in self.porte]
        scelta = self.attuale()
        self.porte = porte
        voci = [f'{p["porta"]} - {p["descrizione"]}' for p in porte]
        self.combo["values"] = voci
        if [p["porta"] for p in porte] == nomi_vecchi and scelta:
            for p, v in zip(porte, voci):
                if p["porta"] == scelta["porta"]:
                    self.var.set(v)
            return
        vecchia = scelta["porta"] if scelta else None
        indice = next((i for i, p in enumerate(porte) if p["porta"] == vecchia), None)
        if indice is None:
            # preseleziona la prima scheda supportata, poi una non supportata (per l'avviso)
            indice = next((i for i, p in enumerate(porte) if p["scheda"]),
                          next((i for i, p in enumerate(porte) if p["non_supportata"]),
                               0 if porte else None))
        if indice is None:
            self.var.set("")
        else:
            self.combo.current(indice)
        self._cambiata(True)

    def _cambiata(self, automatica):
        if self.al_cambio:
            self.al_cambio(self.attuale(), automatica)


class PannelloScorrevole(ttk.Frame):
    """Riquadro con barra di scorrimento verticale che compare solo se serve.

    Il contenuto va creato dentro self.interno.
    """

    def __init__(self, master):
        super().__init__(master)
        sfondo = ttk.Style(self).lookup("TFrame", "background") or None
        self.canvas = tk.Canvas(self, highlightthickness=0, borderwidth=0, background=sfondo)
        self.barra = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=self.barra.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        self.interno = ttk.Frame(self.canvas)
        self.canvas.create_window(0, 0, window=self.interno, anchor="nw")
        self.interno.bind("<Configure>", self._adatta)
        self.canvas.bind("<Configure>", self._adatta)
        # rotellina del mouse solo mentre il puntatore è sul riquadro
        self.bind("<Enter>", lambda e: self.bind_all("<MouseWheel>", self._rotellina))
        self.bind("<Leave>", lambda e: self.unbind_all("<MouseWheel>"))

    def _adatta(self, _e=None):
        larg = self.interno.winfo_reqwidth()
        alt = self.interno.winfo_reqheight()
        self.canvas.configure(width=larg, scrollregion=(0, 0, larg, alt))
        serve = alt > self.canvas.winfo_height() > 1
        if serve and not self.barra.winfo_ismapped():
            self.barra.pack(side="right", fill="y", before=self.canvas)
        elif not serve and self.barra.winfo_ismapped():
            self.barra.pack_forget()
            self.canvas.yview_moveto(0)

    def _rotellina(self, e):
        if self.barra.winfo_ismapped():
            self.canvas.yview_scroll(int(-e.delta / 120) or (-1 if e.delta > 0 else 1), "units")


class TestoLog(ScrolledText):
    """Area di testo in sola lettura con aggiunta in fondo."""

    def __init__(self, master, **kw):
        kw.setdefault("font", ("Consolas", 9))
        kw.setdefault("wrap", "word")
        super().__init__(master, state="disabled", relief="flat", borderwidth=1, **kw)

    def aggiungi(self, testo, scorri=True):
        self.configure(state="normal")
        self.insert("end", testo)
        # limite: si tengono al massimo circa 5000 righe
        righe = int(self.index("end-1c").split(".")[0])
        if righe > 5000:
            self.delete("1.0", f"{righe - 5000}.0")
        if scorri:
            self.see("end")
        self.configure(state="disabled")

    def pulisci(self):
        self.configure(state="normal")
        self.delete("1.0", "end")
        self.configure(state="disabled")

    def testo(self):
        return self.get("1.0", "end-1c")


# ---------------------------------------------------------------------------
# Scheda "Trova le schede"
# ---------------------------------------------------------------------------

class SchedaTrova(ttk.Frame):
    """Ricerca in rete con aggiornamento automatico e preparazione della microSD."""

    INTERVALLO = 3.0        # secondi tra una ricerca e l'altra
    VALIDITA_INFO = 20.0    # secondi prima di rileggere /api/info
    TOLLERANZA = 10.0       # secondi senza risposta prima di togliere una scheda

    def __init__(self, master):
        super().__init__(master, padding=12)
        self.risultati = queue.Queue()
        self.subito = threading.Event()
        self.dati = {}
        self.info = {}          # ip -> (istante, dict o eccezione)
        self.info_lock = threading.Lock()
        self.da_rileggere = set()
        self.info_viste = {}
        self.viste = {}         # ip -> (istante dell'ultima risposta, dati UDP)
        self.ip_in_corso = None  # scheda a cui si sta copiando la pagina
        self.pv_inclusa = versione_pagina_inclusa()
        self.in_corso = False

        ttk.Label(self, text="Schede ArduLearn trovate nella rete",
                  style="Titolo.TLabel").pack(anchor="w")

        colonne = ("host", "ip", "tipo", "rete", "stato", "prog", "pagina")
        riquadro = ttk.Frame(self)
        riquadro.pack(fill="both", expand=True, pady=(8, 6))
        self.albero = ttk.Treeview(riquadro, columns=colonne, show="headings",
                                   selectmode="browse", height=8)
        for col, testo, larg in (("host", "Nome", 100), ("ip", "Indirizzo IP", 100),
                                 ("tipo", "Scheda", 85), ("rete", "Rete", 150),
                                 ("stato", "Stato", 50), ("prog", "Programma", 130),
                                 ("pagina", "Pagina", 150)):
            self.albero.heading(col, text=testo)
            self.albero.column(col, width=larg, anchor="w")
        self.albero.tag_configure("errore", foreground=ROSSO)
        self.albero.tag_configure("avviso", foreground=ARANCIO)
        barra = ttk.Scrollbar(riquadro, orient="vertical", command=self.albero.yview)
        self.albero.configure(yscrollcommand=barra.set)
        self.albero.pack(side="left", fill="both", expand=True)
        barra.pack(side="right", fill="y")
        self.albero.bind("<Double-1>", lambda e: self.apri_ip())
        self.albero.bind("<<TreeviewSelect>>", lambda e: self._aggiorna_avviso())

        pulsanti = ttk.Frame(self)
        pulsanti.pack(fill="x")
        ttk.Button(pulsanti, text="Apri nel browser", style="Accento.TButton",
                   command=self.apri_ip).pack(side="left")
        ttk.Button(pulsanti, text="Apri con il nome",
                   command=self.apri_nome).pack(side="left", padx=6)
        self.pulsante_sd = ttk.Button(pulsanti, text="Prepara la microSD / Aggiorna la pagina",
                                      command=self.prepara_sd)
        self.pulsante_sd.pack(side="left")
        Suggerimento(self.pulsante_sd, "Copia la pagina web sulla microSD dell'Arduino Mega o "
                                       "nella memoria del modulo Wi-Fi della UNO R4 WiFi "
                                       "selezionata (serve il PIN docente).")
        ttk.Button(pulsanti, text="Cerca di nuovo",
                   command=self.cerca_di_nuovo).pack(side="right")

        riga = ttk.Frame(self)
        riga.pack(fill="x", pady=(6, 0))
        self.barra = ttk.Progressbar(riga, mode="determinate", maximum=100)
        self.esito_sd = ttk.Label(riga, text="", style="Nota.TLabel")
        self.esito_sd.pack(side="left")

        self.avviso = ttk.Label(self, text="", foreground=ROSSO, wraplength=780, justify="left")
        self.avviso.pack(anchor="w", pady=(4, 0))
        self.stato = ttk.Label(self, text="Ricerca in corso...", style="Nota.TLabel")
        self.stato.pack(anchor="w", pady=(4, 0))
        v = f"versione {self.pv_inclusa}" if self.pv_inclusa else "non disponibile"
        self.nota_pagina = ttk.Label(self, style="Nota.TLabel",
                                     text=f"Pagina per la microSD inclusa nel programma: {v}")
        self.nota_pagina.pack(anchor="w")
        self.aiuto = ttk.Label(self, style="Nota.TLabel", justify="left", wraplength=780, text=(
            "Nessuna scheda trovata. Controlla che:\n"
            "  - il PC e le schede siano collegati alla stessa rete;\n"
            "  - il firewall di Windows permetta il traffico UDP sulla porta 4210;\n"
            "  - Mega con il cavo collegato direttamente al PC: l'indirizzo è del tipo "
            "169.254.x.x, dopo l'accensione aspetta circa 10 secondi;\n"
            "  - UNO R4 WiFi: il PC sia collegato alla rete ArduLearn-xxxx della scheda "
            "oppure alla stessa rete Wi-Fi impostata nella pagina; alcune reti Wi-Fi "
            "scolastiche isolano i dispositivi (\"client isolation\") e bloccano la ricerca."))

        threading.Thread(target=self._ciclo_ricerca, daemon=True).start()
        self.after(200, self._controlla_risultati)

    # --- thread in background -------------------------------------------
    def _ciclo_ricerca(self):
        while True:
            try:
                trovate = cerca_schede()
                self._leggi_info_schede(trovate)
            except Exception as exc:  # non deve mai fermare il thread
                trovate = exc
            with self.info_lock:
                info = dict(self.info)
            self.risultati.put((trovate, info))
            self.subito.wait(self.INTERVALLO)
            self.subito.clear()

    def _leggi_info_schede(self, trovate):
        """Legge /api/info delle schede nuove o con dati vecchi, in parallelo."""
        ora = time.time()
        with self.info_lock:
            # niente /api/info alla scheda che sta ricevendo la pagina
            da_leggere = [ip for ip in trovate if ip != self.ip_in_corso and (
                          ip in self.da_rileggere or ip not in self.info
                          or ora - self.info[ip][0] > self.VALIDITA_INFO)]
            self.da_rileggere.difference_update(da_leggere)

        def leggi(ip):
            try:
                risultato = leggi_info(ip)
            except Exception as exc:
                risultato = exc
            with self.info_lock:
                self.info[ip] = (time.time(), risultato)

        fili = [threading.Thread(target=leggi, args=(ip,), daemon=True) for ip in da_leggere]
        for f in fili:
            f.start()
        for f in fili:
            f.join(3.0)

    # --- thread dell'interfaccia ----------------------------------------
    def _controlla_risultati(self):
        try:
            while True:
                self._mostra(*self.risultati.get_nowait())
        except queue.Empty:
            pass
        self.after(200, self._controlla_risultati)

    def _info_di(self, ip):
        voce = self.info_viste.get(ip)
        return voce[1] if voce else None

    def _mostra(self, risposte, info):
        if isinstance(risposte, Exception):
            self.stato.config(text=f"Errore nella ricerca: {risposte}")
            return
        self.info_viste = info
        # una risposta UDP persa (o la scheda occupata a scrivere sulla SD) non
        # toglie subito la scheda dall'elenco: si tiene per TOLLERANZA secondi
        ora_s = time.time()
        for ip, d in risposte.items():
            self.viste[ip] = (ora_s, d)
        for ip in list(self.viste):
            if ora_s - self.viste[ip][0] > self.TOLLERANZA and ip != self.ip_in_corso:
                del self.viste[ip]
        trovate = {ip: v[1] for ip, v in self.viste.items()}
        self.dati = trovate
        # aggiornamento sul posto: la selezione e lo scorrimento restano
        for iid in self.albero.get_children():
            if iid not in trovate:
                self.albero.delete(iid)
        for pos, (ip, d) in enumerate(sorted(trovate.items(), key=lambda x: _chiave_ip(x[0]))):
            i = self._info_di(ip)
            tipo = ""
            if isinstance(i, dict):
                tipo = NOMI_BOARD.get(i.get("board"), i.get("board") or "")
            pagina, livello = stato_pagina(i, self.pv_inclusa)
            valori = (d.get("host", "?"), ip, tipo, descrizione_rete(i, d),
                      "RUN" if d.get("run") else "STOP", d.get("prog", ""), pagina)
            tag = (livello,) if livello else ()
            if self.albero.exists(ip):
                self.albero.item(ip, values=valori, tags=tag)
                self.albero.move(ip, "", pos)
            else:
                self.albero.insert("", pos, iid=ip, tags=tag, values=valori)
        if not self.albero.selection() and trovate:
            self.albero.selection_set(self.albero.get_children()[0])
        ora = datetime.now().strftime("%H:%M:%S")
        n = len(trovate)
        testo = "1 scheda trovata" if n == 1 else f"{n} schede trovate"
        self.stato.config(text=f"{testo} (ultimo aggiornamento {ora})")
        if trovate:
            self.aiuto.pack_forget()
        else:
            self.aiuto.pack(anchor="w", pady=(6, 0))
        self._aggiorna_avviso()

    def _manca_pagina_r4(self, ip):
        i = self._info_di(ip)
        return isinstance(i, dict) and i.get("board") == "r4" and "pv" in i \
            and not versione_pagina(i)

    def _aggiorna_avviso(self):
        sel = self.albero.selection()
        testo = ""
        # "Prepara la microSD" solo per le schede con la microSD (non la UNO R4 WiFi)
        senza_sd = bool(sel) and pagina_nel_firmware(self._info_di(sel[0]))
        if not self.in_corso:
            self.pulsante_sd.state(["disabled"] if senza_sd else ["!disabled"])
        if sel:
            ip = sel[0]
            i = self._info_di(ip)
            if self._manca_pagina_r4(ip):
                testo = ("Questa UNO R4 non ha ancora la pagina web sulla microSD: premi "
                         "\"Prepara la microSD\" prima di aprire il browser.")
                if isinstance(i, dict) and not i.get("sd"):
                    testo = ("Questa UNO R4 non vede la microSD: inseriscila (formattata FAT32) "
                             "nello shield, riavvia la scheda e poi premi \"Prepara la microSD\".")
        self.avviso.config(text=testo)

    def cerca_di_nuovo(self):
        self.stato.config(text="Ricerca in corso...")
        with self.info_lock:
            self.da_rileggere.update(self.dati.keys())
        self.subito.set()

    def _selezionata(self):
        sel = self.albero.selection()
        if not sel:
            messagebox.showinfo(TITOLO, "Seleziona prima una scheda dall'elenco.")
            return None
        return sel[0]

    def _controlla_pagina(self, ip):
        """Prima di aprire il browser: se manca la pagina su una R4 propone di prepararla."""
        if self._manca_pagina_r4(ip):
            if messagebox.askyesno(TITOLO, "Su questa UNO R4 la pagina web non è ancora "
                                           "sulla microSD, quindi il browser non la mostrerà.\n\n"
                                           "Vuoi prepararla adesso?"):
                self.prepara_sd()
                return False
        return True

    def apri_ip(self):
        ip = self._selezionata()
        if ip and self._controlla_pagina(ip):
            webbrowser.open(f"http://{ip}/")

    def apri_nome(self):
        ip = self._selezionata()
        if not ip:
            return
        host = self.dati.get(ip, {}).get("host")
        if not host:
            messagebox.showinfo(TITOLO, "Questa scheda non ha comunicato il suo nome.")
        elif self._controlla_pagina(ip):
            webbrowser.open(f"http://{host}.local/")

    # --- preparazione della microSD -------------------------------------
    def prepara_sd(self):
        if self.in_corso:
            return
        ip = self._selezionata()
        if not ip:
            return
        if pagina_nel_firmware(self._info_di(ip)):
            messagebox.showinfo(TITOLO, "La UNO R4 WiFi non ha la microSD: la sua pagina web è "
                                        "dentro il firmware. Per aggiornarla carica il firmware "
                                        "nuovo (scheda \"Carica il firmware\").")
            return
        if not trova_file_sd("INDEX.GZ") or not trova_file_sd("INDEX.VER"):
            messagebox.showerror(TITOLO, "I file della pagina (sd\\PLC\\INDEX.GZ e INDEX.VER) "
                                         "non sono inclusi nel programma.")
            return
        i = self._info_di(ip)
        if isinstance(i, dict) and "sd" in i and not i.get("sd"):
            if not messagebox.askyesno(TITOLO, "La scheda dice di non avere la microSD.\n"
                                               "Inseriscila (FAT32) e riavvia la scheda.\n\n"
                                               "Provare lo stesso?"):
                return
        nome = self.dati.get(ip, {}).get("host", ip)
        iniziale = "1234" if isinstance(i, dict) and i.get("pinDefault") else ""
        pin = simpledialog.askstring(
            TITOLO, f"Scheda {nome} ({ip})\n\nPIN docente (predefinito 1234):",
            show="*", initialvalue=iniziale, parent=self)
        if pin is None:
            return
        pin = pin.strip()
        if not pin:
            messagebox.showwarning(TITOLO, "Scrivi il PIN docente.")
            return
        if not (pin.isascii() and pin.isprintable()):
            messagebox.showwarning(TITOLO, "Il PIN può contenere solo cifre e lettere senza accenti.")
            return

        self.in_corso = True
        self.ip_in_corso = ip
        self.pulsante_sd.state(["disabled"])
        self.barra["value"] = 0
        self.barra.pack(side="left", fill="x", expand=True, padx=(0, 10), before=self.esito_sd)
        self.esito_sd.config(text="Copia della pagina sulla microSD...", foreground="")
        coda = queue.Queue()

        def avanzamento(fatti, totale):
            coda.put(("av", fatti, totale))

        def lavoro():
            try:
                ok, msg = prepara_microsd(ip, pin, avanzamento)
            except Exception as exc:
                ok, msg = False, str(exc)
            coda.put(("fine", ok, msg))

        threading.Thread(target=lavoro, daemon=True).start()
        self.after(100, self._segui_sd, coda, ip)

    def _segui_sd(self, coda, ip):
        fine = None
        try:
            while True:
                m = coda.get_nowait()
                if m[0] == "av":
                    self.barra["value"] = 100 * m[1] / max(m[2], 1)
                    if m[1] >= m[2]:
                        self.esito_sd.config(text="Scrittura sulla microSD in corso...")
                else:
                    fine = m
        except queue.Empty:
            pass
        if fine is None:
            self.after(100, self._segui_sd, coda, ip)
            return
        self.in_corso = False
        self.ip_in_corso = None
        self.pulsante_sd.state(["!disabled"])
        self._aggiorna_avviso()
        self.barra.pack_forget()
        _, ok, msg = fine
        with self.info_lock:
            self.da_rileggere.add(ip)
        self.subito.set()
        if ok:
            self.esito_sd.config(text=msg, foreground=VERDE)
            messagebox.showinfo(TITOLO, msg + "\n\nOra puoi aprire la scheda nel browser.")
        else:
            self.esito_sd.config(text=f"Non riuscito: {msg}", foreground=ROSSO)
            messagebox.showerror(TITOLO, f"Preparazione della microSD non riuscita.\n\n{msg}")


# ---------------------------------------------------------------------------
# Scheda "Carica il firmware"
# ---------------------------------------------------------------------------

class SchedaFirmware(ttk.Frame):
    """Scelta della porta e caricamento con avrdude / bossac."""

    def __init__(self, master):
        super().__init__(master, padding=12)
        self.in_corso = False
        self.uscita = queue.Queue()
        self.scheda_manuale = False
        self.monitor = None           # impostato dall'App
        self.da_riconnettere = None

        ttk.Label(self, text="Carica il firmware sulla scheda Arduino",
                  style="Titolo.TLabel").pack(anchor="w")

        griglia = ttk.Frame(self)
        griglia.pack(fill="x", pady=(8, 4))
        griglia.columnconfigure(1, weight=1)

        ttk.Label(griglia, text="Porta USB:").grid(row=0, column=0, sticky="w", pady=3)
        self.scegli = ScegliPorta(griglia, al_cambio=self._porta_cambiata)
        self.scegli.grid(row=0, column=1, sticky="ew", padx=6)

        ttk.Label(griglia, text="Scheda:").grid(row=1, column=0, sticky="w", pady=3)
        self.var_scheda = tk.StringVar(value=MEGA)
        self.combo_scheda = ttk.Combobox(griglia, textvariable=self.var_scheda,
                                         values=SCHEDE, state="readonly")
        self.combo_scheda.grid(row=1, column=1, sticky="ew", padx=6)
        self.combo_scheda.bind("<<ComboboxSelected>>", lambda e: self._scheda_scelta())

        self.info_porta = ttk.Label(self, style="Nota.TLabel", text="", wraplength=780,
                                    justify="left")
        self.info_porta.pack(anchor="w")
        self.info_versioni = ttk.Label(self, style="Nota.TLabel", justify="left", text="")
        self.info_versioni.pack(anchor="w", pady=(4, 0))

        riga = ttk.Frame(self)
        riga.pack(fill="x", pady=8)
        self.pulsante = ttk.Button(riga, text="Carica il firmware", style="Accento.TButton",
                                   command=self.carica)
        self.pulsante.pack(side="left")
        self.barra = ttk.Progressbar(riga, mode="indeterminate")
        self.barra.pack(side="left", fill="x", expand=True, padx=(10, 0))

        self.esito = ttk.Label(self, text="", style="Esito.TLabel")
        self.esito.pack(anchor="w")
        self.log = TestoLog(self, height=10)
        self.log.pack(fill="both", expand=True, pady=(4, 0))

        self._mostra_versioni()

    # --- porte e schede -------------------------------------------------
    def _porta_cambiata(self, p, automatica):
        if not p:
            self.info_porta.config(
                text=self.scegli.errore or "Nessuna porta USB trovata: collega la scheda con "
                                           "il cavo USB.",
                foreground="")
            return
        if not automatica:
            self.scheda_manuale = False
        if p["scheda"] and not self.scheda_manuale:
            self.var_scheda.set(p["scheda"])
        testo = f'{p["nome"]}  (USB {p["vidpid"]})'
        colore = ""
        if p["non_supportata"]:
            testo = p["non_supportata"]
            colore = ROSSO
        elif not p["scheda"]:
            testo += "  -  scegli tu il tipo di scheda"
        self.info_porta.config(text=testo, foreground=colore)

    def _scheda_scelta(self):
        self.scheda_manuale = True

    def _mostra_versioni(self):
        versioni = leggi_versioni()
        righe = ["Firmware incluso:"]
        for scheda in SCHEDE:
            nome = FIRMWARE[scheda]
            percorso = trova_firmware(nome)
            if not percorso:
                righe.append(f"  {scheda}: non ancora disponibile")
                continue
            v = versioni.get(nome) or versioni.get(scheda) or versioni.get(CHIAVE_VERSIONE[scheda])
            data = datetime.fromtimestamp(os.path.getmtime(percorso)).strftime("%d/%m/%Y %H:%M")
            dettagli = f"versione {v}, " if v else ""
            dettagli += f"file del {data}"
            if e_esterno(percorso):
                dettagli += ", dalla cartella firmware accanto al programma"
            righe.append(f"  {scheda}: {dettagli}")
        if versioni.get("bridge"):
            righe.append(f"  UNO R4 WiFi, modulo Wi-Fi (ArduLearnBridge): versione {versioni['bridge']}")
        self.info_versioni.config(text="\n".join(righe))

    # --- caricamento ----------------------------------------------------
    def carica(self):
        if self.in_corso:
            return
        p = self.scegli.attuale()
        scheda = self.var_scheda.get()
        if p and p["non_supportata"]:
            messagebox.showerror(TITOLO, p["non_supportata"])
            return
        if not p:
            messagebox.showwarning(TITOLO, "Scegli la porta USB a cui è collegata la scheda.")
            return
        if scheda not in FIRMWARE:
            messagebox.showwarning(TITOLO, "Scegli il tipo di scheda.")
            return
        file_fw = trova_firmware(FIRMWARE[scheda])
        if not file_fw:
            messagebox.showinfo(TITOLO, f"Firmware per {scheda} non ancora disponibile.")
            return
        for nome_prog in [PROGRAMMA[scheda]] + (["esptool.exe"] if scheda == R4_WIFI else []):
            if not os.path.isfile(os.path.join(cartella_bin(), nome_prog)):
                messagebox.showerror(TITOLO, f"Programma {nome_prog} non trovato nella cartella bin.")
                return
        if p and p["scheda"] and p["scheda"] != scheda:
            if not messagebox.askyesno(TITOLO, (
                    f'Sulla porta {p["porta"]} sembra collegata una scheda "{p["scheda"]}", '
                    f'ma hai scelto "{scheda}".\nVuoi continuare lo stesso?')):
                return
        if not messagebox.askokcancel(TITOLO, (
                f'Stai per caricare il firmware su {scheda} (porta {p["porta"]}).\n\n'
                "- I programmi salvati nella memoria della scheda restano.\n"
                + ("- UNO R4 WiFi: se serve si aggiorna prima il modulo Wi-Fi (fino a un minuto).\n"
                   if scheda == R4_WIFI else "") +
                "- Chiudi prima ogni altro programma che usa la porta seriale "
                "(per esempio il monitor seriale dell'IDE Arduino).\n\n"
                "Durante il caricamento non scollegare il cavo USB.\nContinuare?")):
            return

        porta = p["porta"]
        # il monitor seriale di questo programma libera la porta da solo
        self.da_riconnettere = None
        if porta and self.monitor and self.monitor.porta_connessa() == porta:
            self.monitor.disconnetti()
            self.da_riconnettere = porta

        self.in_corso = True
        self.pulsante.state(["disabled"])
        self.scegli.abilita(False)
        self.scegli.pausa = True
        self.combo_scheda.state(["disabled"])
        self.barra.start(12)
        self.esito.config(text="Caricamento in corso...", foreground="")
        self.log.pulisci()
        self.log.aggiungi(f"Firmware: {file_fw}\n")
        if self.da_riconnettere:
            self.log.aggiungi(f"Monitor seriale disconnesso da {porta}.\n")
        self.log.aggiungi("\n")
        threading.Thread(target=self._lavora, args=(scheda, porta, file_fw), daemon=True).start()
        self.after(100, self._leggi_uscita, scheda)

    def _lavora(self, scheda, porta, file_fw):
        """Thread in background."""
        scrivi = lambda t: self.uscita.put(("testo", t))  # noqa: E731
        try:
            ok = esegui_caricamento(scheda, porta, file_fw, scrivi)
        except Exception as exc:
            scrivi(f"\nErrore: {exc}\n")
            ok = False
        self.uscita.put(("fine", ok))

    def _leggi_uscita(self, scheda):
        fine = None
        try:
            while True:
                msg = self.uscita.get_nowait()
                if msg[0] == "testo":
                    self.log.aggiungi(msg[1])
                else:
                    fine = msg
        except queue.Empty:
            pass
        if fine is None:
            self.after(100, self._leggi_uscita, scheda)
            return

        self.in_corso = False
        self.barra.stop()
        self.pulsante.state(["!disabled"])
        self.scegli.abilita(True)
        self.scegli.pausa = False
        self.combo_scheda.state(["!disabled"])
        if self.da_riconnettere and self.monitor:
            self.after(2500, self._riconnetti, self.da_riconnettere, 6)
            self.da_riconnettere = None
        if fine[1]:
            self.esito.config(text="Firmware caricato correttamente.", foreground=VERDE)
            testo = "Firmware caricato correttamente.\nLa scheda si riavvia da sola."
            if scheda == R4_WIFI:
                testo += "\n\n" + PASSI_R4_WIFI
                self.log.aggiungi("\nProssimi passi:\n" + PASSI_R4_WIFI + "\n")
            messagebox.showinfo(TITOLO, testo)
        else:
            aiuto = suggerimento_errore(self.log.testo(), scheda)
            self.esito.config(text="Caricamento non riuscito.", foreground=ROSSO)
            self.log.aggiungi(f"\nSuggerimento: {aiuto}\n")
            messagebox.showerror(TITOLO, f"Caricamento non riuscito.\n\n{aiuto}")

    def _riconnetti(self, porta, tentativi):
        """Dopo il caricamento ricollega il monitor (la porta può metterci un po')."""
        if self.in_corso or self.monitor.porta_connessa():
            return  # nel frattempo è partito un altro caricamento o il monitor è già collegato
        if porta_presente(porta):
            self.monitor.connetti(porta)
        elif tentativi > 0:
            self.after(1000, self._riconnetti, porta, tentativi - 1)


# ---------------------------------------------------------------------------
# Scheda "Monitor seriale"
# ---------------------------------------------------------------------------

class SchedaMonitor(ttk.Frame):
    """Monitor seriale a 115200 baud con i pulsanti dei comandi del firmware."""

    BAUD = 115200

    def __init__(self, master):
        super().__init__(master, padding=12)
        self.ser = None
        self.porta = None
        self.ferma = threading.Event()
        self.filo = None
        self.firmware = None          # impostato dall'App
        self.arrivi = queue.Queue()

        alto = ttk.Frame(self)
        alto.pack(fill="x")
        ttk.Label(alto, text="Porta USB:").pack(side="left")
        self.scegli = ScegliPorta(alto, al_cambio=self._porta_cambiata)
        self.scegli.pack(side="left", fill="x", expand=True, padx=6)
        ttk.Label(alto, text=f"{self.BAUD} baud", style="Nota.TLabel").pack(side="left", padx=6)
        self.pulsante_conn = ttk.Button(alto, text="Connetti", style="Accento.TButton",
                                        command=self.connetti_disconnetti, width=13)
        self.pulsante_conn.pack(side="left")
        self.stato = ttk.Label(self, text="Non connesso.", style="Nota.TLabel")
        self.stato.pack(anchor="w", pady=(4, 4))

        corpo = ttk.Frame(self)
        corpo.pack(fill="both", expand=True)
        corpo.columnconfigure(0, weight=1)
        corpo.rowconfigure(0, weight=1)

        sinistra = ttk.Frame(corpo)
        sinistra.grid(row=0, column=0, sticky="nsew")
        self.testo = TestoLog(sinistra, height=12, wrap="char")
        self.testo.pack(fill="both", expand=True)
        riga = ttk.Frame(sinistra)
        riga.pack(fill="x", pady=(6, 0))
        self.var_invio = tk.StringVar()
        self.voce = ttk.Entry(riga, textvariable=self.var_invio, font=("Consolas", 10))
        self.voce.pack(side="left", fill="x", expand=True)
        self.voce.bind("<Return>", lambda e: self.invia_riga())
        ttk.Button(riga, text="Invia", command=self.invia_riga).pack(side="left", padx=(6, 0))
        riga2 = ttk.Frame(sinistra)
        riga2.pack(fill="x", pady=(6, 0))
        self.var_scorri = tk.BooleanVar(value=True)
        ttk.Checkbutton(riga2, text="Scorrimento automatico",
                        variable=self.var_scorri).pack(side="left")
        ttk.Button(riga2, text="Salva log...", command=self.salva).pack(side="right")
        ttk.Button(riga2, text="Pulisci", command=self.testo.pulisci).pack(side="right", padx=6)

        self._pannello_comandi(corpo).grid(row=0, column=1, sticky="ns", padx=(10, 0))
        self.after(100, self._leggi_arrivi)

    # --- pannello dei comandi -------------------------------------------
    def _pannello_comandi(self, master):
        # scorrevole: con la finestra bassa i comandi in fondo restano raggiungibili
        riquadro = PannelloScorrevole(master)
        p = riquadro.interno
        ttk.Label(p, text="Comandi", style="Sottotitolo.TLabel").grid(
            row=0, column=0, columnspan=2, sticky="w", pady=(0, 4))
        r = 1
        for cmd, descr in COMANDI_SEMPLICI:
            if cmd == "pin reset":
                azione = self.pin_reset
            else:
                azione = lambda c=cmd: self.invia(c)  # noqa: E731
            b = ttk.Button(p, text=cmd, width=10, command=azione)
            b.grid(row=r, column=0, sticky="w", pady=1)
            ttk.Label(p, text=descr, style="Nota.TLabel", wraplength=200).grid(
                row=r, column=1, sticky="w", padx=(6, 0))
            Suggerimento(b, f"Invia \"{cmd}\": {descr}")
            r += 1

        ttk.Separator(p).grid(row=r, column=0, columnspan=2, sticky="ew", pady=6)
        r += 1
        ttk.Label(p, text="IP fisso", style="Sottotitolo.TLabel").grid(
            row=r, column=0, columnspan=2, sticky="w")
        r += 1
        self.var_ip = tk.StringVar(value="192.168.1.50")
        self.var_maschera = tk.StringVar(value="255.255.255.0")
        self.var_gateway = tk.StringVar(value="192.168.1.1")
        for etichetta, var in (("Indirizzo", self.var_ip), ("Maschera", self.var_maschera),
                               ("Gateway", self.var_gateway)):
            ttk.Label(p, text=etichetta).grid(row=r, column=0, sticky="w")
            ttk.Entry(p, textvariable=var, width=18).grid(row=r, column=1, sticky="w",
                                                          padx=(6, 0), pady=1)
            r += 1
        ttk.Button(p, text="Imposta IP fisso", command=self.imposta_ip).grid(
            row=r, column=0, columnspan=2, sticky="w", pady=(2, 0))
        r += 1

        ttk.Separator(p).grid(row=r, column=0, columnspan=2, sticky="ew", pady=6)
        r += 1
        ttk.Label(p, text="Indirizzo MAC", style="Sottotitolo.TLabel").grid(
            row=r, column=0, columnspan=2, sticky="w")
        r += 1
        self.var_mac = tk.StringVar(value="00:08:DC:")
        ttk.Entry(p, textvariable=self.var_mac, width=18).grid(row=r, column=1, sticky="w",
                                                               padx=(6, 0))
        ttk.Label(p, text="MAC").grid(row=r, column=0, sticky="w")
        r += 1
        ttk.Button(p, text="Imposta MAC", command=self.imposta_mac).grid(
            row=r, column=0, columnspan=2, sticky="w", pady=(2, 0))
        r += 1

        ttk.Separator(p).grid(row=r, column=0, columnspan=2, sticky="ew", pady=6)
        r += 1
        ttk.Label(p, text="Wi-Fi (UNO R4 WiFi)", style="Sottotitolo.TLabel").grid(
            row=r, column=0, columnspan=2, sticky="w")
        r += 1
        for cmd, descr in COMANDI_WIFI:
            if cmd == "wifi clear":
                azione = self.wifi_clear
            else:
                azione = lambda c=cmd: self.invia(c)  # noqa: E731
            b = ttk.Button(p, text=cmd, width=10, command=azione)
            b.grid(row=r, column=0, sticky="w", pady=1)
            ttk.Label(p, text=descr, style="Nota.TLabel", wraplength=200).grid(
                row=r, column=1, sticky="w", padx=(6, 0))
            Suggerimento(b, f"Invia \"{cmd}\": {descr}")
            r += 1
        ttk.Label(p, text="La rete della scuola si imposta dalla pagina web: "
                          "Impostazioni → Rete Wi-Fi.", style="Nota.TLabel",
                  wraplength=260, justify="left").grid(
            row=r, column=0, columnspan=2, sticky="w", pady=(2, 0))
        return riquadro

    # --- connessione ----------------------------------------------------
    def _porta_cambiata(self, p, automatica):
        if self.ser is None and not p and self.scegli.errore:
            self.stato.config(text=self.scegli.errore)

    def porta_connessa(self):
        return self.porta if self.ser is not None else None

    def connetti_disconnetti(self):
        if self.ser is not None:
            self.disconnetti()
        else:
            p = self.scegli.attuale()
            if not p:
                messagebox.showwarning(TITOLO, "Scegli la porta USB della scheda.")
                return
            self.connetti(p["porta"])

    def connetti(self, porta):
        if self.ser is not None:
            return
        if serial is None:
            messagebox.showerror(TITOLO, "Libreria pyserial non disponibile.")
            return
        if self.firmware and self.firmware.in_corso:
            messagebox.showwarning(TITOLO, "Caricamento del firmware in corso: aspetta che "
                                           "finisca prima di connetterti.")
            return
        try:
            s = serial.Serial(porta, self.BAUD, timeout=0.1, write_timeout=2)
        except Exception as exc:
            self.stato.config(text=f"Impossibile aprire {porta}: {exc}", foreground=ROSSO)
            messagebox.showerror(TITOLO, f"Impossibile aprire la porta {porta}.\n\n"
                                         "Forse è usata da un altro programma (per esempio il "
                                         "monitor seriale dell'IDE Arduino).\n\n"
                                         f"Dettagli: {exc}")
            return
        self.ser, self.porta = s, porta
        self.scegli.seleziona(porta)
        # un evento per ogni connessione: un thread vecchio non riparte mai
        self.ferma = threading.Event()
        self.filo = threading.Thread(target=self._ciclo_lettura, args=(s, self.ferma),
                                     daemon=True)
        self.filo.start()
        self.pulsante_conn.config(text="Disconnetti")
        self.scegli.abilita(False)
        self.stato.config(text=f"Connesso a {porta} ({self.BAUD} baud). Il Mega si riavvia "
                               "all'apertura della porta: è normale.", foreground=VERDE)
        self.testo.aggiungi(f"\n--- connesso a {porta} ---\n", self.var_scorri.get())
        self.voce.focus_set()

    def disconnetti(self):
        """Chiude la porta e aspetta che il thread di lettura finisca."""
        if self.ser is None:
            return
        self.ferma.set()
        if self.filo:
            self.filo.join(2.0)
        try:
            self.ser.close()
        except Exception:
            pass
        porta = self.porta
        self.ser, self.porta, self.filo = None, None, None
        self.pulsante_conn.config(text="Connetti")
        self.scegli.abilita(True)
        self.stato.config(text="Non connesso.", foreground="")
        self.testo.aggiungi(f"\n--- disconnesso da {porta} ---\n", self.var_scorri.get())

    def _ciclo_lettura(self, s, ferma):
        """Thread in background: legge la porta finché non si chiede di fermarsi."""
        decodifica = Decodifica()
        while not ferma.is_set():
            try:
                dati = s.read(s.in_waiting or 1)
            except Exception as exc:
                if not ferma.is_set():  # errore vero, non la chiusura voluta
                    self.arrivi.put(("errore", (s, str(exc))))
                return
            if dati:
                self.arrivi.put(("testo", decodifica(dati).replace("\r", "")))

    def _leggi_arrivi(self):
        pezzi = []
        errore = None
        try:
            while True:
                tipo, val = self.arrivi.get_nowait()
                if tipo == "testo":
                    pezzi.append(val)
                else:
                    errore = val
        except queue.Empty:
            pass
        if pezzi:
            self.testo.aggiungi("".join(pezzi), self.var_scorri.get())
        # solo se l'errore riguarda la connessione attuale (non una già chiusa)
        if errore and self.ser is not None and errore[0] is self.ser:
            self.disconnetti()
            self.stato.config(text=f"Connessione interrotta: {errore[1]}", foreground=ROSSO)
        self.after(100, self._leggi_arrivi)

    # --- invio ----------------------------------------------------------
    def invia(self, comando):
        if self.ser is None:
            messagebox.showinfo(TITOLO, "Prima premi \"Connetti\" per collegarti alla scheda.")
            return False
        try:
            self.ser.write((comando + "\n").encode("utf-8"))
        except Exception as exc:
            messagebox.showerror(TITOLO, f"Invio non riuscito: {exc}")
            return False
        self.testo.aggiungi(f"> {comando}\n", self.var_scorri.get())
        return True

    def invia_riga(self):
        riga = self.var_invio.get()
        if self.invia(riga):
            self.var_invio.set("")

    def pin_reset(self):
        if self.ser is None:
            self.invia("pin reset")  # mostra l'avviso
            return
        if messagebox.askyesno(TITOLO, "Riportare il PIN docente al valore predefinito 1234?"):
            self.invia("pin reset")

    def wifi_clear(self):
        if self.ser is None:
            self.invia("wifi clear")  # mostra l'avviso
            return
        if messagebox.askyesno(TITOLO, "Dimenticare la rete Wi-Fi salvata nella UNO R4 WiFi?\n\n"
                                       "La scheda torna alla sua rete ArduLearn-xxxx (password "
                                       "ardulearn): per usarla collega il PC a quella rete e "
                                       "apri http://192.168.4.1/."):
            self.invia("wifi clear")

    def imposta_ip(self):
        try:
            cmd = comando_ip(self.var_ip.get(), self.var_maschera.get(), self.var_gateway.get())
        except ValueError as exc:
            testo = str(exc)
            messagebox.showwarning(TITOLO, testo[:1].upper() + testo[1:])
            return
        self.invia(cmd)

    def imposta_mac(self):
        try:
            cmd = comando_mac(self.var_mac.get())
        except ValueError as exc:
            messagebox.showwarning(TITOLO, str(exc))
            return
        if self.ser is None:
            self.invia(cmd)
            return
        if messagebox.askyesno(TITOLO, f"Impostare il MAC {cmd[4:]}?\n\nLa scheda si riavvierà. "
                                       "Ogni scheda della rete deve avere un MAC diverso."):
            self.invia(cmd)

    def salva(self):
        percorso = filedialog.asksaveasfilename(
            parent=self, title="Salva il log", defaultextension=".txt",
            initialfile=f"monitor_{datetime.now():%Y%m%d_%H%M%S}.txt",
            filetypes=[("File di testo", "*.txt"), ("Tutti i file", "*.*")])
        if percorso:
            try:
                with open(percorso, "w", encoding="utf-8") as f:
                    f.write(self.testo.testo())
            except OSError as exc:
                messagebox.showerror(TITOLO, f"Salvataggio non riuscito: {exc}")


# ---------------------------------------------------------------------------
# Scheda "Guida"
# ---------------------------------------------------------------------------

GUIDA = [
    ("h1", "ArduLearn - guida rapida"),
    ("p", "Questo programma serve a: caricare il firmware ArduLearn sulle schede Arduino "
          "via USB, trovare le schede nella rete e aprirle nel browser, preparare la pagina "
          "web sulla microSD del Mega, dialogare con la scheda tramite il monitor seriale."),

    ("h2", "Cosa serve"),
    ("p", "Due possibilità, a scelta:"),
    ("li", "A) Arduino Mega 2560 + shield DFRobot \"Ethernet & PoE Shield for Arduino\" "
           "(DFR0850, chip W5500, con slot per microSD), collegata con il cavo di rete."),
    ("link", "Acquista la shield DFR0850 su dfrobot.com", "https://www.dfrobot.com/product-2370.html"),
    ("li", "B) Arduino UNO R4 WiFi da sola: usa il Wi-Fi integrato, NON serve nessuna shield "
           "(la DFR0850 non va montata). Alimentala con il cavo USB-C o con il jack di "
           "alimentazione."),
    ("li", "Arduino UNO R4 Minima: non supportata (per ora)."),
    ("li", "Non supportati: Arduino Uno classico (ATmega328P, troppo poca memoria) e la scheda "
           "tutto-in-uno DFRobot \"W5500 Ethernet with POE IoT Board\" (DFR0342: ATmega32u4 "
           "con 28 KB di flash e 2,5 KB di RAM, troppo pochi per ArduLearn)."),
    ("li", "Solo per il Mega: una microSD formattata FAT32 (consigliata: progetti, slot e "
           "pagina web aggiornata) e un cavo di rete verso lo switch/router della scuola "
           "oppure direttamente al PC; con uno switch o un iniettore PoE la shield alimenta "
           "anche la scheda."),
    ("li", "Facoltativi: display OLED I2C 128x64 (SSD1306 o SH1106) o LCD 16x2/20x4."),

    ("h2", "1. La prima volta: carica il firmware (cavo USB)"),
    ("li", "Collega la scheda con il cavo USB e apri \"Carica il firmware\": la porta e il "
           "tipo di scheda vengono riconosciuti da soli (controlla che siano giusti)."),
    ("li", "Premi \"Carica il firmware\" e aspetta il messaggio finale. Non scollegare il cavo."),
    ("li", "UNO R4 WiFi: la prima volta si installa anche il firmware del modulo Wi-Fi "
           "(ArduLearnBridge, fino a un minuto); le volte successive solo se ne serve uno nuovo. "
           "Programmi, slot, bozza e rete Wi-Fi salvati restano. Il firmware originale del modulo "
           "si rimette con l'IDE Arduino (Strumenti → Aggiornamento firmware)."),
    ("li", "UNO R4 WiFi: se il caricamento non riesce, premi due volte velocemente il tasto "
           "RESET e riprova. Se la scheda compare come \"modulo Wi-Fi da ripristinare\" "
           "basta caricare di nuovo. I driver USB vengono installati con l'IDE Arduino "
           "(piattaforma \"Arduino UNO R4 Boards\")."),
    ("li", "Chiudi gli altri programmi che usano la porta (per esempio il monitor seriale "
           "dell'IDE Arduino). Il monitor di questo programma si scollega da solo."),

    ("h2", "2A. Arduino Mega: cavo di rete e microSD"),
    ("li", "In una rete con router l'indirizzo arriva in automatico (DHCP)."),
    ("li", "Con il cavo collegato direttamente al PC, scheda e PC prendono da soli un indirizzo "
           "169.254.x.x: dopo l'accensione aspetta circa 10 secondi."),
    ("li", "La pagina web è già nel firmware del Mega; se sulla microSD (FAT32, nello shield) "
           "ce n'è una più recente si usa quella. Per copiarla: in \"Trova le schede\" "
           "seleziona la scheda e premi \"Prepara la microSD / Aggiorna la pagina\" (serve il "
           "PIN docente, predefinito 1234). La colonna \"Pagina\" indica: aggiornata, da "
           "aggiornare oppure assente."),

    ("h2", "2B. UNO R4 WiFi: la rete Wi-Fi"),
    ("li", "La UNO R4 WiFi non ha la microSD: pagina web, slot e bozza stanno nella memoria "
           "del modulo Wi-Fi. La pagina si aggiorna da qui (\"Aggiorna la pagina\") o dalla "
           "pagina stessa (Impostazioni → Aggiornamento del firmware), anche da Internet."),
    ("li", "Dopo il caricamento la scheda crea la sua rete Wi-Fi \"ArduLearn-xxxx\" "
           "(password: ardulearn). Il nome esatto scorre sulla matrice LED della scheda."),
    ("li", "Collega il PC (o il telefono) a quella rete e apri http://192.168.4.1/. Mentre è "
           "collegato alla rete della scheda il PC non ha internet: è normale."),
    ("li", "Nella pagina apri Impostazioni → Rete Wi-Fi, scrivi nome e password della rete "
           "della scuola e conferma con il PIN docente (predefinito 1234). La scheda si "
           "collega a quella rete: ricollega anche il PC alla rete della scuola e trova la "
           "scheda con \"Trova le schede\"."),
    ("li", "Se la rete non è raggiungibile (password sbagliata, rete spenta) dopo 3 tentativi "
           "falliti la scheda torna alla sua rete ArduLearn-xxxx: collegati di nuovo a quella "
           "e correggi le impostazioni."),
    ("li", "Le reti con nome utente e password (WPA2-Enterprise, per esempio eduroam) NON sono "
           "supportate: usa una rete con sola password (WPA2-Personal), per esempio quella di "
           "un router o di un access point del laboratorio."),
    ("li", "PC e scheda devono essere nella stessa rete. Alcune reti Wi-Fi scolastiche isolano "
           "i dispositivi tra loro (\"client isolation\"): in quel caso la scheda non si trova "
           "e la pagina non si apre; chiedi al tecnico o usa un router dedicato."),
    ("li", "Dal monitor seriale: \"wifi\" mostra lo stato della rete, \"wifi clear\" dimentica "
           "la rete salvata e fa tornare la scheda alla rete ArduLearn-xxxx."),

    ("h2", "3. Trova le schede e apri il browser"),
    ("li", "La scheda \"Trova le schede\" cerca le schede ogni 3 secondi. Doppio clic (o "
           "\"Apri nel browser\") per aprirne una. La colonna \"Rete\" indica Ethernet oppure "
           "la rete Wi-Fi con l'intensità del segnale."),
    ("li", "\"Apri con il nome\" usa l'indirizzo nome.local (per esempio http://plc-004b.local/)."),
    ("li", "Al primo avvio Windows può chiedere il permesso del firewall: consenti l'accesso "
           "(reti private), altrimenti le schede non si trovano (UDP porta 4210)."),

    ("h2", "4. Monitor seriale e comandi"),
    ("li", "Scegli la porta, premi \"Connetti\" (115200 baud). Scrivi un comando e premi Invio, "
           "oppure usa i pulsanti a destra."),
    ("li", "Quando si apre la porta il Mega si riavvia: è normale."),
    ("li", "Comandi: help, info, sd, run, stop, io, io on/off, ip dhcp, ip <indirizzo> "
           "<maschera> <gateway>, pin reset, mac xx:xx:xx:xx:xx:xx; solo UNO R4 WiFi: wifi, "
           "wifi clear."),

    ("h2", "5. Se qualcosa non va"),
    ("li", "Scheda non trovata in rete: PC e scheda nella stessa rete, firewall che permette "
           "UDP 4210. Mega: cavo collegato, attendi 10 secondi dopo l'accensione. UNO R4 WiFi: "
           "controlla la rete impostata (comando \"wifi\" dal monitor seriale) e l'isolamento "
           "dei dispositivi della rete Wi-Fi."),
    ("li", "UNO R4 WiFi che non si collega alla rete della scuola: collegati alla sua rete "
           "ArduLearn-xxxx e correggi nome e password; oppure \"wifi clear\" dal monitor seriale."),
    ("li", "PIN errato: il PIN docente si riporta a 1234 con il comando \"pin reset\" dal "
           "monitor seriale."),
    ("li", "Errore di caricamento: chiudi i programmi che usano la porta (monitor seriale "
           "dell'IDE Arduino), controlla porta, tipo di scheda e cavo USB; per la UNO R4 WiFi "
           "prova il doppio RESET."),
]


class SchedaGuida(ttk.Frame):
    def __init__(self, master):
        super().__init__(master, padding=12)
        t = ScrolledText(self, wrap="word", relief="flat", borderwidth=0, padx=10, pady=6,
                         font=("Segoe UI", 10), cursor="arrow")
        t.pack(fill="both", expand=True)
        t.tag_configure("h1", font=("Segoe UI", 14, "bold"), spacing3=6)
        t.tag_configure("h2", font=("Segoe UI", 11, "bold"), spacing1=10, spacing3=3,
                        foreground="#1f4e79")
        t.tag_configure("p", spacing3=4)
        t.tag_configure("li", lmargin1=12, lmargin2=26, spacing3=2)
        t.tag_configure("firma", font=("Segoe UI", 8, "italic"), foreground="#888888",
                        spacing1=14)
        t.tag_configure("link", foreground="#0b62d6", underline=True, lmargin1=26, spacing3=4)
        for voce in GUIDA:
            tag, testo = voce[0], voce[1]
            if tag == "link":                 # collegamento cliccabile: apre il browser
                nome = f"link{id(voce)}"
                t.tag_bind(nome, "<Button-1>", lambda _e, u=voce[2]: webbrowser.open(u))
                t.tag_bind(nome, "<Enter>", lambda _e: t.configure(cursor="hand2"))
                t.tag_bind(nome, "<Leave>", lambda _e: t.configure(cursor="arrow"))
                t.insert("end", "→  " + testo + "\n", ("link", nome))
                continue
            t.insert("end", ("•  " if tag == "li" else "") + testo + "\n", tag)
        t.insert("end", FIRMA + "\n", "firma")
        t.configure(state="disabled")


# ---------------------------------------------------------------------------
# Finestra principale
# ---------------------------------------------------------------------------

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(TITOLO)
        self.geometry("900x660")
        self.minsize(720, 560)
        self._stili()
        self._icona()

        testata = ttk.Frame(self)
        testata.pack(fill="x", padx=12, pady=(10, 0))
        if self.logo is not None:
            ttk.Label(testata, image=self.logo).pack(side="left", padx=(0, 8))
        ttk.Label(testata, text=TITOLO, style="Marchio.TLabel").pack(side="left")

        schede = ttk.Notebook(self)
        schede.pack(fill="both", expand=True, padx=10, pady=(8, 0))
        self.trova = SchedaTrova(schede)
        self.firmware = SchedaFirmware(schede)
        self.monitor = SchedaMonitor(schede)
        self.firmware.monitor = self.monitor
        self.monitor.firmware = self.firmware
        schede.add(self.trova, text="  Trova le schede  ")
        schede.add(self.firmware, text="  Carica il firmware  ")
        schede.add(self.monitor, text="  Monitor seriale  ")
        schede.add(SchedaGuida(schede), text="  Guida  ")

        ttk.Label(self, text=FIRMA, style="Firma.TLabel").pack(side="bottom", anchor="e",
                                                               padx=12, pady=4)
        self.protocol("WM_DELETE_WINDOW", self._chiudi)

    def _icona(self):
        """Icona della finestra e della barra delle applicazioni, logo nella testata."""
        self.logo = None
        try:  # icona propria sulla barra delle applicazioni anche avviando lo script
            import ctypes
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("ArduLearn.App")
        except Exception:
            pass
        ico = trova_risorsa("ardulearn.ico")
        if ico:
            try:
                self.iconbitmap(default=ico)
            except tk.TclError:
                pass
        png = trova_risorsa("ardulearn.png")
        if png:
            try:
                self._icona_png = tk.PhotoImage(file=png)
                self.iconphoto(True, self._icona_png)
            except tk.TclError:
                pass
        piccolo = trova_risorsa("ardulearn_32.png")
        if piccolo:
            try:
                self.logo = tk.PhotoImage(file=piccolo)
            except tk.TclError:
                self.logo = None

    def _chiudi(self):
        if self.firmware.in_corso:
            if not messagebox.askyesno(TITOLO, "Caricamento del firmware in corso: se chiudi "
                                               "adesso la scheda potrebbe restare senza "
                                               "firmware (poi basta ricaricarlo).\n\n"
                                               "Chiudere lo stesso?", icon="warning"):
                return
        elif self.trova.in_corso:
            if not messagebox.askyesno(TITOLO, "Copia della pagina sulla microSD in corso.\n\n"
                                               "Chiudere lo stesso?", icon="warning"):
                return
        try:
            self.monitor.disconnetti()
        except Exception:
            pass
        finally:
            # gli strumenti di caricamento non devono restare attivi senza finestra
            for proc in list(PROCESSI_ATTIVI):
                try:
                    proc.kill()
                except OSError:
                    pass
            self.destroy()

    def _stili(self):
        stile = ttk.Style(self)
        if "vista" in stile.theme_names():
            stile.theme_use("vista")
        stile.configure("Titolo.TLabel", font=("Segoe UI", 12, "bold"))
        stile.configure("Marchio.TLabel", font=("Segoe UI", 16, "bold"), foreground="#1A1A1A")
        stile.configure("Sottotitolo.TLabel", font=("Segoe UI", 9, "bold"))
        stile.configure("Nota.TLabel", font=("Segoe UI", 9), foreground="#555555")
        stile.configure("Esito.TLabel", font=("Segoe UI", 10, "bold"))
        stile.configure("Firma.TLabel", font=("Segoe UI", 8), foreground="#888888")
        stile.configure("Accento.TButton", font=("Segoe UI", 9, "bold"))
        stile.configure("Treeview", rowheight=24)


def main():
    App().mainloop()


if __name__ == "__main__":
    main()

"""Prepara la cartella aggiornamenti/ per l'UNO R4 WiFi: la pagina web la legge da GitHub
(raw.githubusercontent.com, il repository dev'essere pubblico), avvisa quando c'e' una versione
nuova e, su conferma del docente, aggiorna la scheda.

    python tools/prepara_aggiornamento.py "Nota per i docenti (facoltativa)"

Compila i firmware (arduino-cli), rigenera la pagina per l'ESP32 (build_web.py --solo-esp32)
e scrive:
    aggiornamenti/manifest.json          versioni e nomi dei file
    aggiornamenti/PlcBlocchi_r4wifi.bin  firmware del PLC (RA4M1)      versione: FW_VERSION in PlcBlocchi/config.h
    aggiornamenti/ArduLearnBridge.bin    firmware del modulo Wi-Fi      versione: BRIDGE_FW_VERSION in ArduLearnBridge/bridge_config.h
    aggiornamenti/INDEX.GZ               pagina web                     versione: minuti dal 2020 (build_web.py)
Poi basta fare commit e push: le schede se ne accorgono (il controllo si ripete ogni 6 ore).
Ricordarsi di aumentare le versioni quando si cambia un firmware.
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "aggiornamenti"
BUILD = ROOT / "build_aggiornamento"
FQBN_R4 = "arduino:renesas_uno:unor4wifi"
FQBN_ESP = ("esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,FlashSize=4M,FlashMode=qio,"
            "PSRAM=disabled,CPUFreq=240,LoopCore=1,EventsCore=1")


def version(path, name):
    m = re.search(r'#define\s+' + name + r'\s+"([^"]+)"', path.read_text(encoding="utf-8"))
    if not m:
        raise SystemExit(f"{name} non trovato in {path}")
    return m.group(1)


def compile_sketch(fqbn, sketch, out):
    print(f"compilo {sketch} ...")
    r = subprocess.run(["arduino-cli", "compile", "-b", fqbn, "--output-dir", str(out), str(ROOT / sketch)],
                       capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(r.stdout + r.stderr)


subprocess.run([sys.executable, str(ROOT / "tools" / "build_web.py"), "--solo-esp32"], check=True)
compile_sketch(FQBN_R4, "PlcBlocchi", BUILD / "r4")
compile_sketch(FQBN_ESP, "ArduLearnBridge", BUILD / "esp")

OUT.mkdir(exist_ok=True)
shutil.copy(BUILD / "r4" / "PlcBlocchi.ino.bin", OUT / "PlcBlocchi_r4wifi.bin")
shutil.copy(BUILD / "esp" / "ArduLearnBridge.ino.bin", OUT / "ArduLearnBridge.bin")
shutil.copy(ROOT / "ArduLearnBridge" / "data" / "PLC" / "INDEX.GZ", OUT / "INDEX.GZ")
manifest = {
    "r4wifi": {"fw": version(ROOT / "PlcBlocchi" / "config.h", "FW_VERSION"), "file": "PlcBlocchi_r4wifi.bin"},
    "bridge": {"ver": version(ROOT / "ArduLearnBridge" / "bridge_config.h", "BRIDGE_FW_VERSION"), "file": "ArduLearnBridge.bin"},
    "page": {"ver": int((ROOT / "ArduLearnBridge" / "data" / "PLC" / "INDEX.VER").read_text()), "file": "INDEX.GZ"},
    "note": sys.argv[1] if len(sys.argv) > 1 else "",
}
(OUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
shutil.rmtree(BUILD, ignore_errors=True)
print(json.dumps(manifest, ensure_ascii=False, indent=1))
print(f"pronto in {OUT.relative_to(ROOT)}: fare commit e push")

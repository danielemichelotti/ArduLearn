"""Prepara la cartella aggiornamenti/ per l'UNO R4 WiFi: la pagina web la legge da GitHub
(raw.githubusercontent.com, il repository dev'essere pubblico), avvisa quando c'e' una versione
nuova e, su conferma del docente, aggiorna la scheda.

    python tools/prepara_aggiornamento.py "Prima novita'" "Seconda novita'" ...

Ogni argomento e' una voce delle note di rilascio: la pagina le mostra prima di aggiornare
(con quelle delle versioni precedenti che la scheda non ha ancora, dal campo "novita").

Compila i firmware (arduino-cli), rigenera la pagina per l'ESP32 (build_web.py --solo-esp32)
e scrive:
    aggiornamenti/manifest.json          versioni, nomi dei file e note di rilascio ("novita")
    aggiornamenti/PlcBlocchi_r4wifi.bin  firmware del PLC (RA4M1)      versione: FW_VERSION in PlcBlocchi/config.h
    aggiornamenti/ArduLearnBridge.bin    firmware del modulo Wi-Fi      versione: BRIDGE_FW_VERSION in ArduLearnBridge/bridge_config.h
    aggiornamenti/INDEX.GZ               pagina web                     versione: minuti dal 2020 (build_web.py)
Poi basta fare commit e push: le schede se ne accorgono (il controllo si ripete ogni 6 ore).
Ricordarsi di aumentare le versioni quando si cambia un firmware.
"""
import datetime
import json
import re
import shutil
import subprocess
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
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
fw = version(ROOT / "PlcBlocchi" / "config.h", "FW_VERSION")
bridge = version(ROOT / "ArduLearnBridge" / "bridge_config.h", "BRIDGE_FW_VERSION")
page = int((ROOT / "ArduLearnBridge" / "data" / "PLC" / "INDEX.VER").read_text())
voci = [v.strip() for v in sys.argv[1:] if v.strip()]
if not voci:
    raise SystemExit("scrivere le novita' come argomenti: la pagina le mostra prima di aggiornare")
# note di rilascio: la nuova versione in testa, poi quelle gia' pubblicate (al massimo 30)
try:
    storia = json.loads((OUT / "manifest.json").read_text(encoding="utf-8")).get("novita", [])
except (OSError, ValueError):
    storia = []
novita = [{"data": datetime.date.today().strftime("%d/%m/%Y"), "fw": fw, "bridge": bridge, "page": page, "voci": voci}]
novita += [e for e in storia if e.get("page") != page][:29]
manifest = {
    "r4wifi": {"fw": fw, "file": "PlcBlocchi_r4wifi.bin"},
    "bridge": {"ver": bridge, "file": "ArduLearnBridge.bin"},
    "page": {"ver": page, "file": "INDEX.GZ"},
    "note": " · ".join(voci),          # per le pagine che non conoscono "novita"
    "novita": novita,
}
(OUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
shutil.rmtree(BUILD, ignore_errors=True)
print(json.dumps(manifest, ensure_ascii=False, indent=1))
print(f"pronto in {OUT.relative_to(ROOT)}: fare commit e push")

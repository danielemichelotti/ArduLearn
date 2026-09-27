"""Prepara i firmware da includere nell'app ArduLearn (cartella firmware/, bin/esptool.exe, sd/).

    python tools/app/prepara_firmware.py
    tools\\app\\build.bat

Compila con arduino-cli:
    PlcBlocchi per Arduino Mega 2560           -> firmware/PlcBlocchi_mega.hex
    PlcBlocchi per UNO R4 WiFi (RA4M1)         -> firmware/PlcBlocchi_r4wifi.bin
    ArduLearnBridge per il modulo Wi-Fi ESP32  -> firmware/ArduLearnBridge*.bin (bootloader,
                                                  partizioni, otadata, app, pagina LittleFS)
Rigenera la pagina per l'ESP32 e per la microSD del Mega (build_web.py --solo-esp32: la pagina
dentro il firmware del Mega non cambia) e scrive firmware/versions.json.
"""
import glob
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent
ROOT = APP.parent.parent
FW = APP / "firmware"
BUILD = ROOT / "build_app"
FQBN_MEGA = "arduino:avr:mega"
FQBN_R4 = "arduino:renesas_uno:unor4wifi"
FQBN_ESP = ("esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,FlashSize=4M,FlashMode=qio,"
            "PSRAM=disabled,CPUFreq=240,LoopCore=1,EventsCore=1")
ARDUINO15 = Path(os.path.expanduser("~/AppData/Local/Arduino15/packages"))


def version(path, name):
    m = re.search(r'#define\s+' + name + r'\s+"([^"]+)"', path.read_text(encoding="utf-8"))
    if not m:
        raise SystemExit(f"{name} non trovato in {path}")
    return m.group(1)


def compile_sketch(fqbn, sketch, out):
    print(f"compilo {sketch} ({fqbn.split(':')[1]}) ...", flush=True)
    r = subprocess.run(["arduino-cli", "compile", "-b", fqbn, "--output-dir", str(out), str(ROOT / sketch)],
                       capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(r.stdout + r.stderr)


def unico(pattern):
    trovati = sorted(glob.glob(str(pattern)))
    if not trovati:
        raise SystemExit(f"non trovato: {pattern}")
    return Path(trovati[-1])


subprocess.run([sys.executable, str(ROOT / "tools" / "build_web.py"), "--solo-esp32"], check=True)
compile_sketch(FQBN_MEGA, "PlcBlocchi", BUILD / "mega")
compile_sketch(FQBN_R4, "PlcBlocchi", BUILD / "r4")
compile_sketch(FQBN_ESP, "ArduLearnBridge", BUILD / "esp")

FW.mkdir(exist_ok=True)
shutil.copy(BUILD / "mega" / "PlcBlocchi.ino.hex", FW / "PlcBlocchi_mega.hex")
shutil.copy(BUILD / "r4" / "PlcBlocchi.ino.bin", FW / "PlcBlocchi_r4wifi.bin")
shutil.copy(BUILD / "esp" / "ArduLearnBridge.ino.bootloader.bin", FW / "ArduLearnBridge_bootloader.bin")
shutil.copy(BUILD / "esp" / "ArduLearnBridge.ino.partitions.bin", FW / "ArduLearnBridge_partitions.bin")
shutil.copy(BUILD / "esp" / "ArduLearnBridge.ino.bin", FW / "ArduLearnBridge.bin")
shutil.copy(unico(ARDUINO15 / "esp32/hardware/esp32/*/tools/partitions/boot_app0.bin"), FW / "ArduLearnBridge_boot_app0.bin")
# ArduLearnBridge_littlefs.bin e' gia' stata scritta da build_web.py

# pagina per la microSD del Mega: la stessa della UNO R4 WiFi
sd = APP / "sd" / "PLC"
sd.mkdir(parents=True, exist_ok=True)
for nome in ("INDEX.GZ", "INDEX.VER"):
    shutil.copy(ROOT / "ArduLearnBridge" / "data" / "PLC" / nome, sd / nome)

# esptool (dal core esp32 di Arduino) per scrivere il modulo Wi-Fi
shutil.copy(unico(ARDUINO15 / "esp32/tools/esptool_py/*/esptool.exe"), APP / "bin" / "esptool.exe")

fw = version(ROOT / "PlcBlocchi" / "config.h", "FW_VERSION")
versioni = {"mega": fw, "r4wifi": fw,
            "bridge": version(ROOT / "ArduLearnBridge" / "bridge_config.h", "BRIDGE_FW_VERSION"),
            "pagina": int((sd / "INDEX.VER").read_text())}
(FW / "versions.json").write_text(json.dumps(versioni, indent=1) + "\n", encoding="utf-8")
shutil.rmtree(BUILD, ignore_errors=True)
print(json.dumps(versioni))
for f in sorted(FW.iterdir()):
    print(f"  {f.name:34} {f.stat().st_size:>9} byte")

"""Comprime web/index.html (gzip) e genera:
- PlcBlocchi/web_page.h: la pagina dentro il firmware del Mega;
- tools/app/sd/PLC/INDEX.GZ e INDEX.VER: la pagina per la microSD del Mega (la usa se piu'
  nuova di quella interna; l'EXE la invia alla scheda via rete);
- ArduLearnBridge/data/PLC/INDEX.GZ e INDEX.VER: la pagina per l'UNO R4 WiFi, nella flash del
  modulo ESP32, e tools/app/firmware/ArduLearnBridge_littlefs.bin, l'immagine di quella flash
  (serve mklittlefs del core esp32) da scrivere all'indirizzo 0x310000.

Da rilanciare ogni volta che si modifica la pagina web:
    python tools/build_web.py
    python tools/build_web.py --solo-esp32     # senza toccare web_page.h e la pagina della microSD
"""
import gzip
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "web" / "index.html"
DST = ROOT / "PlcBlocchi" / "web_page.h"
CHUNK = 16384          # l'AVR non accetta oggetti piu' grandi di 32 KB
# I dati vanno nella sezione .fini7 (dopo il codice, oltre i primi 64 KB di flash)
# e si leggono con puntatori "far": cosi' la pagina puo' crescere senza spingere
# fuori dai 64 KB bassi le stringhe PROGMEM del firmware.
SECTION = '__attribute__((__section__(".fini7")))'


def minify(html: str) -> str:
    """Toglie rientri e righe di solo commento: riduce ma non altera il codice."""
    out = []
    for line in html.splitlines():
        s = line.strip()
        if not s or s.startswith("//"):
            continue
        out.append(s)
    return "\n".join(out)


def build_modules():
    """Legge i moduli PlcBlocchi/mod_*.cpp e genera il registro del firmware e web/modules.js."""
    import json
    import re
    names, defs = [], []
    for f in sorted((ROOT / "PlcBlocchi").glob("mod_*.cpp")):
        src = f.read_text(encoding="utf-8")
        names += re.findall(r"extern const ModuleDef (\w+) PROGMEM", src)
        m = re.search(r"/\*\s*@blocchi\s*(.*?)\*/", src, re.S)
        if m:
            for d in json.loads(m.group(1)):
                d["file"] = f.name
                defs.append(d)
    types = [d["t"] for d in defs]
    if len(types) != len(set(types)):
        raise SystemExit("Due moduli usano lo stesso numero di blocco: " + str(types))
    reg = ["// FILE GENERATO da tools/build_web.py a partire dai file mod_*.cpp: non modificare a mano.",
           '#include "config.h"', "#if HAS_MODULES", '#include "modules.h"', ""]
    reg += [f"extern const ModuleDef {n};" for n in names]
    reg += ["", "const ModuleDef* const MODULES[] PROGMEM = {"]
    reg += [f"  &{n}," for n in names] or ["  nullptr,"]
    reg += ["};", f"const uint8_t NUM_MODULES = {len(names)};", "#endif  // HAS_MODULES", ""]
    (ROOT / "PlcBlocchi" / "modules_registry.cpp").write_text("\n".join(reg), encoding="utf-8")
    (ROOT / "web" / "modules.js").write_text(
        "// FILE GENERATO da tools/build_web.py: blocchi dei moduli (PlcBlocchi/mod_*.cpp)\n"
        "const MODULE_DEFS = " + json.dumps(defs, ensure_ascii=False, indent=1) + ";\n", encoding="utf-8")
    print(f"moduli: {len(names)} ({', '.join(names)})")
    return (ROOT / "web" / "modules.js").read_text(encoding="utf-8")


modules_js = build_modules()
html = SRC.read_text(encoding="utf-8")
# la pagina di sviluppo carica modules.js a parte; nel firmware viene incorporato
html = html.replace('<script src="modules.js"></script>', "<script>\n" + modules_js + "</script>")
def minify_js(html: str) -> str:
    """Minifica gli script con terser (tools/node_modules), se Node.js e' installato.
    Senza, la pagina resta solo "ripulita" da minify(): funziona uguale ma e' piu' grande."""
    import shutil
    import subprocess
    here = Path(__file__).resolve().parent
    if not shutil.which("node") or not (here / "node_modules" / "terser").exists():
        print("terser non disponibile (npm install nella cartella tools): pagina non minificata")
        return html
    r = subprocess.run(["node", str(here / "minify_js.mjs")], input=html.encode("utf-8"),
                       capture_output=True, check=False)
    if r.returncode:
        raise SystemExit("terser: " + r.stderr.decode("utf-8", "replace"))
    return r.stdout.decode("utf-8")


def compress(text: str) -> bytes:
    """gzip il piu' piccolo possibile: zopfli se installato (pip install zopfli), altrimenti gzip -9.
    Il risultato e' un normale gzip: lo leggono tutti i browser."""
    raw = text.encode("utf-8")
    try:
        import zopfli.gzip
        return zopfli.gzip.compress(raw, numiterations=15)
    except ImportError:
        print("zopfli non installato: uso gzip -9 (pagina un po' piu' grande)")
        return gzip.compress(raw, compresslevel=9, mtime=0)


full = compress(minify(minify_js(html)))
data = full                                         # la stessa pagina completa per tutte le schede
# versione della pagina: minuti dal 2020 (per scegliere la piu' recente tra firmware e SD; ETag)
version = int((time.time() - 1577836800) // 60)
solo_esp32 = "--solo-esp32" in sys.argv


def page_arrays(payload: bytes, out: list):
    chunks = [payload[i:i + CHUNK] for i in range(0, len(payload), CHUNK)]
    for n, chunk in enumerate(chunks):
        out.append(f"const uint8_t WEB_PAGE_GZ_{n}[] WEB_PAGE_ATTR = {{")
        for i in range(0, len(chunk), 20):
            out.append("  " + ", ".join(f"0x{b:02x}" for b in chunk[i:i + 20]) + ",")
        out.append("};")
        out.append("")
    out.append(f"const uint32_t WEB_PAGE_GZ_LEN = {len(payload)};")
    out.append("")
    out.append("// Copia n byte della pagina dalla posizione off (mai a cavallo di due blocchi:")
    out.append("// WEB_PAGE_CHUNK e' multiplo della dimensione delle letture)")
    out.append("static inline void webPageRead(uint8_t* dst, uint32_t off, uint16_t n) {")
    out.append("#ifdef __AVR__")
    out.append("  uint32_t p = 0;")
    out.append("  switch (off / WEB_PAGE_CHUNK) {")
    for n in range(len(chunks)):
        out.append(f"    case {n}: p = pgm_get_far_address(WEB_PAGE_GZ_{n}); break;")
    out.append("  }")
    out.append("  memcpy_PF(dst, p + off % WEB_PAGE_CHUNK, n);")
    out.append("#else")
    out.append("  static const uint8_t* const parts[] = { " + ", ".join(f"WEB_PAGE_GZ_{n}" for n in range(len(chunks))) + " };")
    out.append("  memcpy(dst, parts[off / WEB_PAGE_CHUNK] + off % WEB_PAGE_CHUNK, n);")
    out.append("#endif")
    out.append("}")
    return len(chunks)


if not solo_esp32:
    out = [
        "#pragma once",
        "// FILE GENERATO da tools/build_web.py a partire da web/index.html: non modificare a mano.",
        "// Solo Mega: l'UNO R4 WiFi ha la pagina nella flash del modulo ESP32 (ArduLearnBridge/data).",
        "#include <Arduino.h>",
        "#ifdef __AVR__",
        "#define WEB_PAGE_ATTR " + SECTION,
        f"const uint16_t WEB_PAGE_CHUNK = {CHUNK};",
        f"const uint32_t WEB_PAGE_VER = {version}UL;",
        "",
    ]
    nf = page_arrays(full, out)
    out.append("#endif")
    DST.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"{SRC.name}: {len(html.encode())} byte -> {len(full)} byte compressi ({nf} blocchi) -> {DST.relative_to(ROOT)}")

    # pagina per la microSD del Mega (usata se piu' nuova di quella interna)
    SD_DIR = ROOT / "tools" / "app" / "sd" / "PLC"
    SD_DIR.mkdir(parents=True, exist_ok=True)
    (SD_DIR / "INDEX.GZ").write_bytes(data)
    (SD_DIR / "INDEX.VER").write_text(str(version), encoding="ascii")
    print(f"pagina per la microSD: {SD_DIR.relative_to(ROOT)} (versione {version})")

# UNO R4 WiFi: pagina nella flash del modulo ESP32 (firmware ArduLearnBridge)
ESP_DIR = ROOT / "ArduLearnBridge" / "data" / "PLC"
ESP_DIR.mkdir(parents=True, exist_ok=True)
(ESP_DIR / "INDEX.GZ").write_bytes(data)
(ESP_DIR / "INDEX.VER").write_text(str(version), encoding="ascii")
(ESP_DIR.parent / "PB").mkdir(exist_ok=True)      # slot e bozza


def littlefs_image():
    """Immagine della partizione LittleFS (partitions.csv: 0x310000, 0xF0000) con la pagina."""
    import glob
    import os
    import subprocess
    tools = glob.glob(os.path.expanduser("~/AppData/Local/Arduino15/packages/esp32/tools/mklittlefs/*/mklittlefs*")) + \
        glob.glob(os.path.expanduser("~/.arduino15/packages/esp32/tools/mklittlefs/*/mklittlefs"))
    if not tools:
        print("mklittlefs non trovato (core esp32 di Arduino): immagine LittleFS non generata")
        return
    img = ROOT / "tools" / "app" / "firmware" / "ArduLearnBridge_littlefs.bin"
    img.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([sorted(tools)[-1], "-c", str(ESP_DIR.parent), "-b", "4096", "-p", "256", "-s", str(0xF0000), str(img)],
                   check=True, capture_output=True)
    print(f"pagina per l'UNO R4 WiFi: {ESP_DIR.relative_to(ROOT)} e {img.relative_to(ROOT)} (0x310000)")


littlefs_image()

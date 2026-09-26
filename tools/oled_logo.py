"""Converte img/ardulearn_logo.h (bitmap 128x64 per righe, formato Adafruit drawBitmap)
nel formato del driver OLED del firmware (8 fasce da 8 righe x 128 colonne, bit 0 in alto)
e genera PlcBlocchi/splash_logo.h.

    python tools/oled_logo.py
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
src = (ROOT / "img" / "ardulearn_logo.h").read_text(encoding="utf-8")
body = src[src.index("ardulearn_logo[] PROGMEM = {"):]
body = body[:body.index("};")]
rows = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
assert len(rows) == 128 * 64 // 8, len(rows)

def px(x, y):
    return rows[y * 16 + x // 8] >> (7 - x % 8) & 1

pages = []
for p in range(8):
    for x in range(128):
        pages.append(sum(px(x, p * 8 + b) << b for b in range(8)))

out = ["#pragma once",
       "// FILE GENERATO da tools/oled_logo.py a partire da img/ardulearn_logo.h: non modificare a mano.",
       "#include <Arduino.h>",
       "// Schermata di avvio 128x64: 8 fasce da 8 righe x 128 colonne, bit 0 in alto",
       "const uint8_t SPLASH_LOGO[1024] PROGMEM = {"]
for i in range(0, len(pages), 16):
    out.append("  " + ", ".join(f"0x{b:02X}" for b in pages[i:i + 16]) + ",")
out.append("};")
(ROOT / "PlcBlocchi" / "splash_logo.h").write_text("\n".join(out) + "\n", encoding="utf-8")
print("PlcBlocchi/splash_logo.h generato")

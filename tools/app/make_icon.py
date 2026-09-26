"""Crea l'icona di ArduLearn (ardulearn.ico e PNG) dal file SVG del logo.

Serve solo Pillow: i percorsi SVG (comandi M L H V Q A Z, regola even-odd)
vengono letti e disegnati qui, senza librerie SVG.
Uso:  python make_icon.py
I file creati sono già nella cartella: rieseguire solo se cambia il logo.
"""
import math
import os
import re
import xml.etree.ElementTree as ET

from PIL import Image, ImageChops, ImageDraw

QUI = os.path.dirname(os.path.abspath(__file__))
SVG = os.path.join(QUI, "..", "..", "img", "ardulearn-icon-dark.svg")
SFONDO = (0x1A, 0x1A, 0x1A, 255)
GLIFO = (0xF5, 0xF5, 0xF5, 255)
MISURE_ICO = (16, 24, 32, 48, 64, 128, 256)


def _arco(p0, rx, ry, rot, grande, verso, p1, passi=24):
    """Punti di un arco ellittico SVG (conversione estremi -> centro)."""
    x1, y1 = p0
    x2, y2 = p1
    if rx == 0 or ry == 0:
        return [p1]
    phi = math.radians(rot)
    c, s = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = c * dx + s * dy, -s * dx + c * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
    den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
    k = math.sqrt(max(0.0, num / den)) * (-1 if grande == verso else 1)
    cxp, cyp = k * rx * y1p / ry, -k * ry * x1p / rx
    cx = c * cxp - s * cyp + (x1 + x2) / 2
    cy = s * cxp + c * cyp + (y1 + y2) / 2
    ang = lambda ux, uy: math.atan2(uy, ux)  # noqa: E731
    t1 = ang((x1p - cxp) / rx, (y1p - cyp) / ry)
    t2 = ang((-x1p - cxp) / rx, (-y1p - cyp) / ry)
    dt = t2 - t1
    if verso and dt < 0:
        dt += 2 * math.pi
    elif not verso and dt > 0:
        dt -= 2 * math.pi
    punti = []
    for i in range(1, passi + 1):
        t = t1 + dt * i / passi
        px, py = rx * math.cos(t), ry * math.sin(t)
        punti.append((c * px - s * py + cx, s * px + c * py + cy))
    return punti


def sottopercorsi(d):
    """Divide l'attributo d in poligoni (liste di punti)."""
    token = re.findall(r"[MLHVQAZmlhvqaz]|-?\d*\.?\d+(?:e-?\d+)?", d)
    i, cmd = 0, None
    poligoni, attuale, pos = [], [], (0.0, 0.0)

    def num():
        nonlocal i
        i += 1
        return float(token[i - 1])

    while i < len(token):
        if token[i].isalpha():
            cmd = token[i]
            i += 1
        if cmd in "Zz":
            if attuale:
                poligoni.append(attuale)
                pos = attuale[0]
            attuale = []
            continue
        if cmd == "M":
            if attuale:
                poligoni.append(attuale)
            pos = (num(), num())
            attuale = [pos]
            cmd = "L"
        elif cmd == "L":
            pos = (num(), num())
            attuale.append(pos)
        elif cmd == "H":
            pos = (num(), pos[1])
            attuale.append(pos)
        elif cmd == "V":
            pos = (pos[0], num())
            attuale.append(pos)
        elif cmd == "Q":
            c1, p1 = (num(), num()), (num(), num())
            for k in range(1, 13):
                t = k / 12
                attuale.append(((1 - t) ** 2 * pos[0] + 2 * (1 - t) * t * c1[0] + t * t * p1[0],
                                (1 - t) ** 2 * pos[1] + 2 * (1 - t) * t * c1[1] + t * t * p1[1]))
            pos = p1
        elif cmd == "A":
            rx, ry, rot, g, v = num(), num(), num(), num(), num()
            p1 = (num(), num())
            attuale.extend(_arco(pos, rx, ry, rot, int(g), int(v), p1))
            pos = p1
        else:
            raise ValueError(f"comando SVG non gestito: {cmd}")
    if attuale:
        poligoni.append(attuale)
    return poligoni


def leggi_percorsi(svg):
    ns = {"s": "http://www.w3.org/2000/svg"}
    radice = ET.parse(svg).getroot()
    return [p.get("d") for p in radice.iterfind(".//s:path", ns)]


def maschera_glifo(percorsi, lato, scala, dx, dy):
    """Maschera (L) del logo: ogni percorso in even-odd, i percorsi uniti."""
    totale = Image.new("L", (lato, lato), 0)
    for d in percorsi:
        m = Image.new("L", (lato, lato), 0)
        for poli in sottopercorsi(d):
            sub = Image.new("L", (lato, lato), 0)
            ImageDraw.Draw(sub).polygon([(x * scala + dx, y * scala + dy) for x, y in poli], fill=255)
            m = ImageChops.logical_xor(m.convert("1"), sub.convert("1")).convert("L")
        totale = ImageChops.lighter(totale, m)
    return totale


def disegna(percorsi, misura, sfondo=True, ss=8):
    lato = misura * ss
    # riquadro del logo nel sistema 64x64: x 6..62, y 3..56
    bx0, by0, bx1, by1 = 6.0, 3.0, 62.0, 56.0
    margine = 0.13 if sfondo else 0.02
    scala = lato * (1 - 2 * margine) / max(bx1 - bx0, by1 - by0)
    dx = lato / 2 - (bx0 + bx1) / 2 * scala
    dy = lato / 2 - (by0 + by1) / 2 * scala
    img = Image.new("RGBA", (lato, lato), (0, 0, 0, 0))
    if sfondo:
        ImageDraw.Draw(img).rounded_rectangle((0, 0, lato - 1, lato - 1),
                                              radius=int(lato * 0.22), fill=SFONDO)
        colore = GLIFO
    else:
        colore = SFONDO
    glifo = Image.new("RGBA", (lato, lato), colore)
    img.paste(glifo, (0, 0), maschera_glifo(percorsi, lato, scala, dx, dy))
    return img.resize((misura, misura), Image.LANCZOS)


def main():
    percorsi = leggi_percorsi(SVG)
    immagini = [disegna(percorsi, m) for m in MISURE_ICO]
    immagini[-1].save(os.path.join(QUI, "ardulearn.ico"), format="ICO",
                      sizes=[(m, m) for m in MISURE_ICO], append_images=immagini[:-1])
    immagini[-1].save(os.path.join(QUI, "ardulearn.png"))
    disegna(percorsi, 32).save(os.path.join(QUI, "ardulearn_32.png"))
    print("creati ardulearn.ico, ardulearn.png, ardulearn_32.png")


if __name__ == "__main__":
    main()

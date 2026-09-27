"""Manuale del docente: unisce p1..p5.html in manuale.html e lo stampa in PDF (docs/ArduLearn_manuale_docente.pdf).

    python docs/manuale/crea_manuale.py

Servono Google Chrome e Node.js. Le schermate sono in shots/ (WebP), le figure annotate in figdefs.js
(coordinate in pixel delle schermate originali: dimensioni in data.js). Il sito usa gli stessi file.
"""
import os, subprocess, sys, time, shutil, tempfile

QUI = os.path.dirname(os.path.abspath(__file__))
parti = [open(os.path.join(QUI, f"p{i}.html"), encoding="utf-8").read() for i in range(1, 6)]
open(os.path.join(QUI, "manuale.html"), "w", encoding="utf-8").write("".join(parti))
chrome = next((c for c in (r"C:\Program Files\Google\Chrome\Application\chrome.exe",
                           r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
                           shutil.which("google-chrome") or "", shutil.which("chromium") or "") if c and os.path.exists(c)), None)
if not chrome:
    sys.exit("Chrome o Edge non trovato")
profilo = tempfile.mkdtemp()
proc = subprocess.Popen([chrome, "--headless=new", "--remote-debugging-port=9333", f"--user-data-dir={profilo}", "about:blank"])
try:
    time.sleep(3)
    subprocess.run(["node", os.path.join(QUI, "cdp.mjs"), os.path.join(QUI, "print.mjs")], check=True)
finally:
    proc.kill()
    shutil.rmtree(profilo, ignore_errors=True)

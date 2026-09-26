# ArduLearn

PLC didattico per la scuola: si programma dal browser in **FBD** (blocchi collegati con i fili),
**LADDER** (stile TIA Portal) o **GRAFCET/SFC** con segmenti LADDER, e gira su Arduino.
Stati in tempo reale, simulatore nella pagina, PIN docente, più studenti collegati insieme,
display OLED/LCD facoltativi, moduli per sensori e attuatori, blocchi Script ST e Script C.

*Ideato e realizzato da Daniele Michelotti.*

## Schede

| Scheda | Stato | Rete | Memoria programmi |
|---|---|---|---|
| **Arduino Mega 2560** + shield DFRobot [Ethernet & PoE DFR0850](https://www.dfrobot.com/product-2370.html) (W5500) | completo | Ethernet (DHCP o indirizzo automatico 169.254.x.y), PoE | microSD della shield (slot 1–99, bozza condivisa) + copia in EEPROM |
| **Arduino UNO R4 WiFi** (senza shield) | in sviluppo | Wi-Fi integrato, rete propria "ArduLearn-xxxx" per la prima configurazione | memoria interna (programma attivo + 2 slot) |

Non supportate: Arduino Uno classico e DFR0342 (memoria insufficiente), UNO R4 Minima (per ora).

## Struttura

```
PlcBlocchi/        firmware (un solo sorgente, si compila per Mega e UNO R4 WiFi)
  config.h         scheda, pin, limiti, funzioni presenti (HAS_*)
  engine.*         interprete dei blocchi (ciclo PLC)
  script.*         macchina virtuale per Script ST e Script C
  web.*            server HTTP e API (/api/...)
  storage.*        EEPROM, microSD, slot
  net.h, wifi.cpp  rete Ethernet o Wi-Fi
  displays.*, textoled.*, charlcd.*, ledmatrix.*   display e matrice LED
  mod_*.cpp        moduli (buzzer, DHT, DS18B20, HC-SR04, encoder, servo, stepper, WS2812, RTC)
  web_page.h       pagina web compressa (GENERATO da tools/build_web.py)
web/index.html     editor (pagina unica); web/modules.js e' generato dai moduli
tools/build_web.py genera web_page.h, modules.js, modules_registry.cpp e la pagina per la microSD
tools/app/         app Windows ArduLearn.exe (trova le schede, carica il firmware, monitor seriale)
img/               logo (pagina, OLED, icona dell'app)
```

## Compilare

Requisiti: [Arduino CLI](https://arduino.github.io/arduino-cli/), core `arduino:avr` e `arduino:renesas_uno`,
librerie Ethernet, SD, ArduinoMDNS, Adafruit NeoPixel, Servo; Python 3; facoltativi Node.js (per minificare la pagina)
e `pip install zopfli` (compressione migliore).

```bash
cd tools && npm install          # una volta: minificatore della pagina (terser)
python tools/build_web.py        # dopo ogni modifica a web/index.html o ai moduli
arduino-cli compile -b arduino:avr:mega PlcBlocchi
```

Prova della pagina senza scheda: `python -m http.server 8765 --directory web` e apri
`http://localhost:8765/index.html?demo` (simulatore; `?demo&r4` simula l'UNO R4 WiFi).

L'app per Windows si crea con `tools/app/build.bat` (PyInstaller); i firmware da includere vanno in
`tools/app/firmware/` (vedi `tools/app/README.md`).

## Licenza

GPL-3.0: vedi [LICENSE](LICENSE).

# ArduLearn - applicazione per il PC

Un solo file, `dist\ArduLearn.exe`, con quattro schede:

- **Trova le schede**: cerca le schede ArduLearn nella rete (messaggio UDP
  `PLC?` sulla porta 4210 da ogni scheda di rete del PC) e le apre nel browser
  con l'indirizzo IP (`http://<ip>/`) o con il nome (`http://<nome>.local/`).
  L'elenco si aggiorna da solo ogni 3 secondi; per ogni scheda legge
  `GET /api/info` (in background) e mostra il tipo (`board`: Mega / UNO R4 WiFi),
  la rete (*Ethernet*, oppure `Wi-Fi <ssid> (segnale X%)` dal campo
  `"wifi":{"mode","ssid","rssi"}`) e lo stato della pagina:
  - Mega: pagina sulla microSD *aggiornata*, *da aggiornare*, *assente*.
    Il pulsante **Prepara la microSD / Aggiorna la pagina** chiede il PIN
    docente e invia `sd\PLC\INDEX.GZ` e `INDEX.VER` con
    `POST /api/file?name=...` (header `X-Pin`, timeout 60 s).
  - UNO R4 WiFi (`"board":"r4wifi"`): *nel firmware* (non ha la microSD; il
    pulsante della microSD è disattivato).
- **Carica il firmware**: riconosce la scheda collegata via USB e carica il firmware:
  - Arduino Mega 2560 - avrdude (`-c wiring`, 115200)
  - Arduino UNO R4 WiFi - tocco a 1200 baud, poi bossac
    `--port=COMx -U -e -w ... -R` (attraverso il ponte USB dell'ESP32).
    Alla fine mostra i passi successivi (rete ArduLearn-xxxx, pagina
    `http://192.168.4.1/`, Impostazioni → Rete Wi-Fi).
  - Non supportate (caricamento bloccato con un messaggio):
    UNO R4 Minima (2341:0069, 2341:0369) "per ora", Arduino Uno classico
    (2341:0043, 2341:0001, 2A03:0043, 2341:0243, memoria insufficiente).

  Se il monitor seriale del programma usa la stessa porta, si scollega da solo
  prima del caricamento e si ricollega dopo.
- **Monitor seriale**: 115200 baud, invio con Invio (termina con `\n`),
  pulsanti per i comandi del firmware (`help`, `info`, `sd`, `run`, `stop`,
  `io`, `io on/off`, `ip dhcp`, `ip <ind> <maschera> <gateway>`, `pin reset`,
  `mac xx:xx:xx:xx:xx:xx`; solo UNO R4 WiFi: `wifi` stato della rete,
  `wifi clear` dimentica la rete, con conferma), salvataggio del log.
  Aprire la porta riavvia il Mega.
- **Guida**: istruzioni per gli insegnanti.

## Schede supportate

- **Arduino Mega 2560 + shield DFRobot DFR0850** (Ethernet & PoE, W5500,
  microSD): <https://www.dfrobot.com/product-2370.html>. Firmware
  `PlcBlocchi_mega.hex`.
- **Arduino UNO R4 WiFi da sola**: Wi-Fi integrato, nessuna shield (la DFR0850
  non serve), alimentazione da USB-C o jack. Niente microSD: la pagina web è nel
  firmware `PlcBlocchi_r4wifi.bin`.
- Non supportate: UNO R4 Minima (per ora), Arduino Uno classico, DFRobot DFR0342.

## Wi-Fi della UNO R4 WiFi

Il programma per il PC **non** imposta la rete Wi-Fi: si fa dalla pagina web.

1. Dopo il caricamento del firmware la scheda crea la sua rete
   `ArduLearn-xxxx` (password `ardulearn`); il nome esatto scorre sulla
   matrice LED.
2. Collega il PC o il telefono a quella rete e apri `http://192.168.4.1/`
   (mentre è collegato alla rete della scheda il PC non ha internet).
3. In **Impostazioni → Rete Wi-Fi** inserisci nome e password della rete
   della scuola (serve il PIN docente, predefinito 1234).
4. Se la rete non è raggiungibile, dopo 3 tentativi falliti la scheda torna
   alla rete `ArduLearn-xxxx`. Dal monitor seriale `wifi clear` dimentica la
   rete salvata.

Limiti: le reti con nome utente e password (WPA2-Enterprise, es. eduroam) non
sono supportate; PC e scheda devono stare nella stessa rete e alcune reti
Wi-Fi scolastiche isolano i dispositivi ("client isolation"), bloccando la
ricerca e la pagina.

## Aggiornare firmware e pagina senza ricreare l'exe

Accanto a `ArduLearn.exe` si possono mettere:

- una cartella `firmware` con `PlcBlocchi_mega.hex` e/o
  `PlcBlocchi_r4wifi.bin` e, se si vuole, `versions.json`:
  ```json
  { "mega": "1.2", "r4wifi": "1.2" }
  ```
- una cartella `sd\PLC` con `INDEX.GZ` e `INDEX.VER` (pagina per la microSD
  del Mega).

I file nelle cartelle accanto all'exe hanno la precedenza su quelli inclusi.

## Ricreare l'exe

Serve Python 3 con pyserial e PyInstaller:

```
python -m pip install --user pyserial pyinstaller
build.bat
```

Contenuto della cartella:

- `ardulearn_app.py` - sorgente dell'applicazione
- `bin\` - avrdude 6.3.0 (`avrdude.exe`, `avrdude.conf`, `libusb0.dll`),
  bossac 1.9.1-arduino (`bossac.exe`), presi da
  `%LOCALAPPDATA%\Arduino15\packages\arduino\tools\`. `dfu-util.exe` (usato
  solo per la UNO R4 Minima) non serve più.
- `firmware\` - file del firmware inclusi nell'exe
  (`PlcBlocchi_r4minima.bin` non serve più e si può cancellare)
- `sd\PLC\` - pagina web per la microSD del Mega (creata da `tools\build_web.py`)
- `ardulearn.ico` (16-256 px), `ardulearn.png`, `ardulearn_32.png` - icona
  dell'exe e della finestra, logo nella testata; creati da `make_icon.py`
  partendo da `img\ardulearn-icon-dark.svg` (glifo #F5F5F5 su quadrato
  arrotondato #1A1A1A). Serve solo Pillow e solo se cambia il logo.
- `build.bat` - crea `dist\ArduLearn.exe`

## Driver

Mega e UNO R4 WiFi usano driver seriali standard; per la UNO R4 WiFi i driver
vengono installati con l'IDE Arduino (piattaforma "Arduino UNO R4 Boards").

## Se non trova le schede

- PC e schede devono essere nella stessa rete (UNO R4 WiFi: la rete
  `ArduLearn-xxxx` della scheda oppure la rete impostata nella pagina).
- Il firewall di Windows deve permettere il traffico UDP sulla porta 4210
  (al primo avvio Windows può chiedere il permesso: consenti le reti private).
- Mega con il cavo collegato direttamente al PC: l'indirizzo è del tipo
  169.254.x.x, dopo l'accensione aspetta circa 10 secondi.
- Reti Wi-Fi con isolamento dei dispositivi ("client isolation"): la ricerca
  non funziona; serve una rete senza isolamento (es. router del laboratorio).

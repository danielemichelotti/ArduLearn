// =====================================================================
//  ArduLearn - PLC a blocchi per la didattica
//  Arduino Mega 2560 o UNO R4 + DFRobot Ethernet & PoE Shield (DFR0850, W5500)
//  (lo stesso sorgente: l'EXE di caricamento sceglie il firmware per la scheda)
//
//  Il programma si disegna nel browser collegando blocchi con dei fili,
//  viene salvato sulla scheda ed eseguito qui come in un PLC:
//  lettura ingressi -> calcolo dei blocchi in ordine -> scrittura uscite.
//
//  Per trovare la scheda in rete:
//    - http://<nome>.local   (mDNS, nome impostabile dalla pagina)
//    - script tools/trova_plc.py (risposta UDP broadcast sulla porta 4210)
//    - monitor seriale a 115200 baud (comando "info")
//    - display OLED/LCD, se collegati
// =====================================================================
#include <SPI.h>
#include <Wire.h>
#include "config.h"
#include "net.h"
#if HAS_MDNS
#include <ArduinoMDNS.h>
#endif
#include "ledmatrix.h"
#include "storage.h"
#include "engine.h"
#include "displays.h"
#include "web.h"

uint8_t g_netState = NET_NO_LINK;
char    g_hostname[16];

#if HAS_MDNS
static NetUDP mdnsUdp;
static MDNS        mdns(mdnsUdp);
#endif
static bool        mdnsStarted = false;
#if !NET_BRIDGE
static NetUDP discovery;
static const uint16_t DISCOVERY_PORT = 4210;
#endif

static void printInfo();
#if BOARD_R4
static char resetCause[40] = "";
// Causa del riavvio dai registri del RA4M1; poi si azzerano per il prossimo avvio
static void readResetCause() {
  uint8_t r0 = R_SYSTEM->RSTSR0;
  uint16_t r1 = R_SYSTEM->RSTSR1;
  const char* c = "pulsante o linea di reset (RES)";
  if (r0 & 0x01) c = "accensione";
  else if (r0 & 0x0E) c = "tensione bassa (LVD)";
  else if (r1 & 0x01) c = "watchdog indipendente";
  else if (r1 & 0x02) c = "watchdog";
  else if (r1 & 0x04) c = "software";
  else if (r1 & 0xFF00) c = "errore di memoria o del bus";
  snprintf(resetCause, sizeof(resetCause), "%s (%02x %04x)", c, r0, r1);
  R_SYSTEM->PRCR = 0xA502;
  R_SYSTEM->RSTSR0 = 0;
  R_SYSTEM->RSTSR1 = 0;
  R_SYSTEM->PRCR = 0xA500;
}
#endif

#if BOARD_MEGA
int freeRam() {
  extern char __heap_start, *__brkval;
  char top;
  return &top - (__brkval ? __brkval : &__heap_start);
}
#else
extern "C" char* sbrk(int incr);
int freeRam() {
  char top;
  return &top - sbrk(0);
}
#endif

static void reboot() {
#if BOARD_MEGA
  asm volatile("jmp 0");
#else
  NVIC_SystemReset();
#endif
}

// ---------------------------------------------------------------------
//  Rete
// ---------------------------------------------------------------------
void netHostnameChanged() {
  strlcpy(g_hostname, cfg.host, sizeof(g_hostname));
#if HAS_MDNS
  if (mdnsStarted) mdns.setName(g_hostname);
#endif
}

static bool netUp() { return g_netState == NET_OK || g_netState == NET_FALLBACK; }

#if !NET_BRIDGE
static void startServices() {
#if HAS_MDNS
  mdns.begin(netIP(), g_hostname);
  if (!mdnsStarted) mdns.addServiceRecord("ArduLearn._http", 80, MDNSServiceTCP);
#endif
  mdnsStarted = true;
  discovery.begin(DISCOVERY_PORT);
  printInfo();
}

// Senza DHCP (es. cavo diretto tra PC e scheda) la scheda si assegna un indirizzo
// "link-local" 169.254.x.y, come fanno Windows e le stampanti: il PC la raggiunge
// senza configurare niente. Il DHCP viene ritentato quando si ricollega il cavo
// (un tentativo blocca il ciclo PLC per alcuni secondi).
static void startDhcp() {
  g_netState = NET_DHCP;
  displaysShowStatus();
  displaysTick();
  if (cfg.ipStatic == 1) {
    Serial.println(F("IP fisso"));
    IPAddress gw(cfg.gw);
    Ethernet.begin(cfg.mac, IPAddress(cfg.ip), gw, gw, IPAddress(cfg.mask));
    // (sull'UNO R4 la forma a 5 parametri non applica gateway e maschera: si reimpostano)
    Ethernet.setLocalIP(IPAddress(cfg.ip));
    Ethernet.setGatewayIP(gw);
    Ethernet.setSubnetMask(IPAddress(cfg.mask));
    g_netState = NET_OK;
    startServices();
    displaysShowStatus();
    return;
  }
  Serial.println(F("Richiesta DHCP..."));
  if (Ethernet.begin(cfg.mac, 8000, 4000)) {
    g_netState = NET_OK;
  } else {
    g_netState = NET_FALLBACK;
    Ethernet.setLocalIP(IPAddress(169, 254, cfg.mac[4] % 254 + 1, cfg.mac[5] % 254 + 1));
    Ethernet.setSubnetMask(IPAddress(255, 255, 0, 0));
    Ethernet.setGatewayIP(IPAddress(0, 0, 0, 0));
    Serial.println(F("Nessun DHCP: uso un indirizzo automatico 169.254.x.y"));
  }
  startServices();
  displaysShowStatus();
}

// Ripetitori Wi-Fi e bridge che traducono il MAC dei dispositivi via cavo
// (es. TP-Link in modalita' range extender) imparano l'associazione IP <-> MAC
// solo dal traffico che la scheda invia: un piccolo pacchetto periodico verso
// il gateway (con la relativa richiesta ARP) la rende raggiungibile.
static void keepAlive() {
  // Se il gateway non risponde alla richiesta ARP, l'invio blocca il ciclo PLC per ~0,8 s:
  // allora si riprova solo ogni 5 minuti invece che ogni 20 s.
  static uint32_t last = 0, every = 20000UL;
  if (last && millis() - last < every) return;
  last = millis();
  IPAddress gw = Ethernet.gatewayIP();
  if (gw == IPAddress(0, 0, 0, 0)) return;         // nessun gateway (es. IP fisso senza router)
  discovery.beginPacket(gw, 9);   // porta 9 = "discard"
  discovery.write((uint8_t)0);
  every = discovery.endPacket() ? 20000UL : 300000UL;
}

// Inizializza il W5500. Dopo l'accensione il chip ha bisogno di ~250 ms prima di rispondere:
// l'UNO R4 parte molto prima del Mega, quindi si aspetta e, se la shield non risponde,
// netTick() riprova ogni 3 s (es. shield alimentata dopo la scheda).
static bool ethInit() {
  while (millis() < 400) {}
  Ethernet.init(PIN_ETH_CS);
  Ethernet.begin(cfg.mac, IPAddress(0, 0, 0, 0));   // inizializza il W5500
  // un destinatario che non risponde non deve fermare il PLC per troppo tempo
  Ethernet.setRetransmissionTimeout(200);            // ms
  Ethernet.setRetransmissionCount(3);
  if (Ethernet.hardwareStatus() == EthernetNoHardware) return false;
  g_netState = NET_NO_LINK;
  return true;
}

// Servizi di rete comuni: mDNS e risposta alla ricerca UDP dell'app ArduLearn ("PLC?")
static void netServices() {
#if HAS_MDNS
  mdns.run();
#endif
  int len = discovery.parsePacket();
  if (len <= 0) return;
  char q[8] = {0};
  discovery.read(q, sizeof(q) - 1);
  if (strncmp_P(q, PSTR("PLC?"), 4)) return;
  Serial.print(F("Ricerca UDP da "));
  Serial.println(discovery.remoteIP());
  discovery.beginPacket(discovery.remoteIP(), discovery.remotePort());
  discovery.print(F("{\"host\":\""));
  discovery.print(g_hostname);
  discovery.print(F("\",\"ip\":\""));
  discovery.print(netIP());
  discovery.print(F("\",\"prog\":\""));
  for (const char* p = Engine::name; *p; p++) if (*p != '"' && *p != '\\') discovery.write(*p);
  discovery.print(F("\",\"run\":"));
  discovery.print(Engine::running);
  discovery.print('}');
  discovery.endPacket();
}
#endif

#if NET_BRIDGE
// UNO R4 WiFi: mDNS, ricerca UDP e Wi-Fi li gestisce l'ESP32
static void netTick() { bridgeTick(); }
#else
static void netTick() {
  static uint32_t lastCheck = 0;
  if (g_netState == NET_NO_HW) {
    if (millis() - lastCheck < 3000) return;
    lastCheck = millis();
    if (ethInit()) { Serial.println(F("Shield Ethernet trovata")); displaysShowStatus(); }
    return;
  }

  if (netUp()) {
    if (g_netState == NET_OK) keepAlive();
    netServices();
  }

  if (millis() - lastCheck < 500) return;
  lastCheck = millis();

  if (Ethernet.linkStatus() != LinkON) {
    if (g_netState != NET_NO_LINK) {
      g_netState = NET_NO_LINK;
      Serial.println(F("Cavo di rete scollegato"));
      displaysShowStatus();
    }
    return;
  }
  if (g_netState == NET_NO_LINK) {
    startDhcp();
  } else if (g_netState == NET_OK && cfg.ipStatic != 1) {
    uint8_t r = Ethernet.maintain();
    if (r == 2 || r == 4) {                  // rinnovo/rebind riuscito: l'IP potrebbe essere cambiato
      mdns.begin(netIP(), g_hostname);
      printInfo();
    }
  }
}
#endif

// ---------------------------------------------------------------------
//  Monitor seriale: "info", "pin reset"
// ---------------------------------------------------------------------
static void printInfo() {
  Serial.println(F("--------------------------------"));
  Serial.print(F("ArduLearn " FW_VERSION " (" BOARD_NAME ")  nome: "));
  Serial.print(g_hostname);
  Serial.println(F(".local"));
#if NET_BRIDGE
  Serial.print(F("Rete: "));
  switch (g_netState) {
    case NET_NO_HW:    Serial.println(F("il modulo Wi-Fi non risponde (serve il firmware ArduLearnBridge)")); break;
    case NET_NO_LINK:  Serial.println(F("modulo Wi-Fi in avvio")); break;
    case NET_DHCP:     Serial.println(F("collegamento al Wi-Fi in corso")); break;
    case NET_FALLBACK: Serial.print(F("rete della scheda ")); Serial.print(bridgeApName()); Serial.println(F(" (password ardulearn)")); break;
    default:           Serial.println(F("Wi-Fi collegato")); break;
  }
  Serial.print(F("IP: "));
  if (netUp()) Serial.println(netIP());
  else Serial.println(F("(nessuno)"));
  bridgeSerial("wifi");                            // dettagli (e riga "@wifi" per l'app) dall'ESP32
#else
  Serial.print(F("Rete: "));
  switch (g_netState) {
    case NET_NO_HW:    Serial.println(F("shield Ethernet non trovata (controlla alimentazione e connettore ICSP)")); break;
    case NET_NO_LINK:  Serial.println(F("cavo scollegato (nessun collegamento sulla porta)")); break;
    case NET_DHCP:     Serial.println(F("attendo il DHCP")); break;
    default:           Serial.println(F("collegata")); break;
  }
  Serial.print(F("IP: "));
  if (netUp()) Serial.print(netIP());
  else Serial.print(F("(nessuno)"));
  Serial.println(cfg.ipStatic == 1 ? F(" (fisso)") : g_netState == NET_FALLBACK ? F(" (automatico, senza DHCP)") : F(" (DHCP)"));
#endif
  Serial.print(F("MAC: "));
  for (uint8_t i = 0; i < 6; i++) {
    if (i) Serial.print(':');
    if (cfg.mac[i] < 16) Serial.print('0');
    Serial.print(cfg.mac[i], HEX);
  }
  Serial.println();
#if HAS_SD
  Serial.print(F("SD: "));
  Serial.println(g_sdOk ? F("presente") : F("assente (programma in EEPROM)"));
#endif
  Serial.print(F("Programma: "));
  if (Engine::loaded) {
    Serial.print(Engine::name);
    Serial.print(F(" ("));
    Serial.print(Engine::nBlocks);
    Serial.println(Engine::running ? F(" blocchi, RUN)") : F(" blocchi, STOP)"));
  } else {
    Serial.println(Engine::error[0] ? Engine::error : "nessuno");
  }
  Serial.print(F("RAM libera: "));
  Serial.println(freeRam());
#if BOARD_R4
  Serial.print(F("Ultimo riavvio: "));
  Serial.println(resetCause);
#endif
  Serial.println(F("Scrivi help per l'elenco dei comandi"));
  Serial.println(F("--------------------------------"));
}

static void printHelp() {
  Serial.println(F("Comandi:\n"
    "  info               stato della scheda\n"
#if HAS_SD
    "  sd                 diagnosi della microSD\n"
#endif
    "  run / stop         avvia / ferma il programma\n"
    "  io                 ingressi, uscite e valori dei blocchi (io on / io off: ogni secondo)\n"
#if NET_BRIDGE
    "  wifi               stato del Wi-Fi\n"
    "  wifi clear         dimentica la rete e torna alla rete ArduLearn della scheda\n"
    "  wifi ssid <nome> / wifi pass <password> / wifi connect   imposta la rete\n"
#else
    "  ip dhcp            indirizzo dal DHCP\n"
    "  ip A M G           IP fisso, es. ip 192.168.1.50 255.255.255.0 192.168.1.1\n"
#endif
    "  pin reset          PIN docente = 1234\n"
#if !NET_BRIDGE
    "  mac xx:xx:xx:xx:xx:xx  cambia il MAC (riavvio)"
#endif
    ));
}

static void pinLabel(uint8_t p) {
  if (p >= FIRST_APIN) { Serial.print('A'); Serial.print(p - FIRST_APIN); }
  else { Serial.print('D'); Serial.print(p); }
}

// Stato di ingressi/uscite del progetto e valori dei blocchi
static void printIo() {
  static const char* const MODES[] = { "", "IN", "IN_PULLUP", "OUT", "ANALOG", "MODULO" };
  Serial.print(F("[io] "));
  Serial.print(Engine::running ? F("RUN") : Engine::loaded ? F("STOP") : F("nessun programma"));
  Serial.print(F("  ciclo ")); Serial.print(Engine::scanUs); Serial.println(F(" us"));
  bool any = false;
  for (uint8_t p = 0; p < NUM_PINS; p++) {
    uint8_t m = Engine::pinModes[p];
    if (m == PM_NONE || m > PM_MODULE || Engine::isReserved(p)) continue;
    any = true;
    Serial.print(F("  ")); pinLabel(p);
    Serial.print(' '); Serial.print(MODES[m]); Serial.print(F(" = "));
    Serial.println(m == PM_ANALOG ? analogRead(p) : digitalRead(p));
  }
  if (!any) Serial.println(F("  nessun pin usato"));
  for (uint8_t i = 0; i < Engine::nBlocks; i++) {
    uint8_t outs = Engine::typeOutputs(Engine::blocks[i].type);
    if (!outs) continue;
    Serial.print(F("  blocco ")); Serial.print(i + 1);
    Serial.print(F(" (tipo ")); Serial.print(Engine::blocks[i].type); Serial.print(F("): "));
    Serial.print(Engine::vals[i][0]);
    if (outs > 1) { Serial.print(F(", ")); Serial.print(Engine::vals[i][1]); }
    Serial.println();
  }
}

static bool parseIp(const char*& s, uint8_t* out) {
  unsigned int a[4];
  int used = 0;
  while (*s == ' ') s++;
  if (sscanf_P(s, PSTR("%u.%u.%u.%u%n"), &a[0], &a[1], &a[2], &a[3], &used) != 4) return false;
  for (uint8_t i = 0; i < 4; i++) { if (a[i] > 255) return false; out[i] = a[i]; }
  s += used;
  return true;
}

#if !NET_BRIDGE
static void restartNetwork() {
  if (g_netState != NET_NO_HW && Ethernet.linkStatus() == LinkON) startDhcp();
  else Serial.println(F("La rete ripartira' quando il cavo e' collegato"));
}
#endif

static void serialTick() {
  static bool ioLive = false;
  static uint32_t ioLast = 0;
  if (ioLive && millis() - ioLast >= 1000) { ioLast = millis(); printIo(); }
  static char line[100];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < sizeof(line) - 1) line[n++] = c; continue; }
    line[n] = 0;
    n = 0;
#if BOARD_R4
    // l'ESP32 riconfigura la seriale quando il PC apre la porta: il disturbo arriva come caratteri a caso
    bool junk = false;
    for (const char* p = line; *p; p++) if ((uint8_t)*p < 0x20 || (uint8_t)*p >= 0x7F) junk = true;
    if (junk) continue;
#endif
    if (!strcmp_P(line, PSTR("info"))) printInfo();
    else if (!strcmp_P(line, PSTR("help")) || !strcmp_P(line, PSTR("?"))) printHelp();
    else if (!strcmp_P(line, PSTR("run")) || !strcmp_P(line, PSTR("stop"))) {
      cfg.run = line[0] == 'r';
      cfgSave();
      Engine::setRunning(cfg.run);
      displaysShowStatus();
      if (!Engine::loaded) Serial.println(F("Nessun programma caricato"));
      else Serial.println(Engine::running ? F("Programma avviato (RUN)") : F("Programma fermato (STOP)"));
    }
    else if (!strcmp_P(line, PSTR("io"))) printIo();
    else if (!strcmp_P(line, PSTR("io on"))) { ioLive = true; ioLast = 0; }
    else if (!strcmp_P(line, PSTR("io off"))) { ioLive = false; Serial.println(F("[io] fermo")); }
#if NET_BRIDGE
    else if (bridgeSerial(line)) {}
#else
    else if (!strcmp_P(line, PSTR("ip dhcp"))) {
      cfg.ipStatic = 0;
      cfgSave();
      Serial.println(F("DHCP attivato"));
      restartNetwork();
    }
    else if (!strncmp_P(line, PSTR("ip "), 3)) {
      const char* p = line + 3;
      uint8_t ip[4], mask[4], gw[4];
      if (parseIp(p, ip) && parseIp(p, mask) && parseIp(p, gw) && ip[0] && ip[0] < 224) {
        memcpy(cfg.ip, ip, 4); memcpy(cfg.mask, mask, 4); memcpy(cfg.gw, gw, 4);
        cfg.ipStatic = 1;
        cfgSave();
        Serial.println(F("IP fisso salvato"));
        restartNetwork();
      } else {
        Serial.println(F("Formato: ip 192.168.1.50 255.255.255.0 192.168.1.1  (oppure: ip dhcp)"));
      }
    }
#endif
    else if (!strcmp_P(line, PSTR("pin reset"))) {
      strcpy_P(cfg.pin, PSTR("1234"));
      cfgSave();
      Serial.println(F("PIN docente reimpostato a 1234"));
#if HAS_SD
    } else if (!strncmp_P(line, PSTR("sd"), 2) && (line[2] == 0 || line[2] == ' ')) {
      uint8_t cs = line[2] ? atoi(line + 3) : PIN_SD_CS;
      if (cs == PIN_ETH_CS || cs >= NUM_PINS || (Engine::isReserved(cs) && cs != PIN_SD_CS)) Serial.println(F("pin non ammesso"));
      else { pinMode(cs, OUTPUT); digitalWrite(cs, HIGH); storageDiag(Serial, cs); }
#endif
#if !NET_BRIDGE
    } else if (!strncmp_P(line, PSTR("mac "), 4)) {
      // "mac 00:08:DC:12:34:56": cambia il MAC (serve un riavvio)
      unsigned int m[6];
      if (sscanf_P(line + 4, PSTR("%x:%x:%x:%x:%x:%x"), &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6 && !(m[0] & 1) &&
          m[0] < 256 && m[1] < 256 && m[2] < 256 && m[3] < 256 && m[4] < 256 && m[5] < 256) {
        for (uint8_t i = 0; i < 6; i++) cfg.mac[i] = m[i];
        cfgSave();
        Serial.println(F("MAC salvato: riavvio"));
        Serial.flush();
        reboot();
      } else {
        Serial.println(F("Formato: mac 00:08:DC:12:34:56"));
      }
#endif
    } else if (line[0]) {
      Serial.print(F("Comando sconosciuto: ")); Serial.println(line);
      printHelp();
    }
  }
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
#if BOARD_R4
  readResetCause();
  // la seriale USB dell'R4 e' nativa: si aspetta un attimo che il PC la apra (senza bloccare)
  for (uint32_t t = millis(); !Serial && millis() - t < 1500; ) {}
#endif

#if HAS_LED_MATRIX
  ledBegin();
#endif
#if !NET_BRIDGE
  // tutti i dispositivi SPI deselezionati prima di iniziare
  pinMode(SS, OUTPUT);
  pinMode(PIN_SD_CS, OUTPUT);  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_ETH_CS, OUTPUT); digitalWrite(PIN_ETH_CS, HIGH);
#endif

  Wire.begin();
  Wire.setWireTimeout(5000, true);   // un display scollegato non deve bloccare la scheda

  cfgLoad();
  strlcpy(g_hostname, cfg.host, sizeof(g_hostname));

#if NET_BRIDGE
  bridgeBegin();
#else
  if (!ethInit()) {
    g_netState = NET_NO_HW;
    Serial.println(F("ERRORE: shield Ethernet non trovata (riprovo ogni 3 s)"));
  }
#endif

#if HAS_SD
  // alcune microSD non rispondono subito dopo l'accensione: si riprova prima di caricare il programma
  for (uint8_t i = 0; i < 5 && !g_sdOk; i++) {
    storageBegin();
    if (!g_sdOk) delay(200);
  }
#endif
  displaysBegin();
  displaysSplash();

  ImgSource* src = openActive();
  Engine::load(*src);
  delete src;

  webBegin();
  printInfo();
}

void loop() {
  Engine::scan();
  netTick();
  webTick();
  displaysTick();
  serialTick();
  storageTick();
#if HAS_LED_MATRIX
  ledTick();
#endif
}

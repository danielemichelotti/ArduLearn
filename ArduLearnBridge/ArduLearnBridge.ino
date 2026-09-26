// =====================================================================
//  ArduLearn Bridge - firmware del modulo ESP32-S3 dell'Arduino UNO R4 WiFi
//
//  VERSIONE DI PROVA (fase 0): verifica che il core esp32 3.x vada bene.
//   - ponte USB <-> Serial del RA4M1, tocco a 1200 baud (bootloader) e 2400 (ROM Renesas),
//     HID CMSIS-DAP con il comando 0xAA (ESP32 in modalita' download): come il firmware Arduino;
//   - rete di prova "ArduLearn-test-xxxx" (password ardulearn) con una pagina su 192.168.4.1;
//   - comandi di prova sul collegamento col RA4M1 (UART1): PING, FS n, BAUD b, ECHO n, WIFI.
// =====================================================================
#include "bridge_config.h"
#include "USB.h"
#include "USBCDC.h"
#include "DAP.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

#define SERIAL_USER          USBSerial     // la porta seriale che vede il PC
#define SERIAL_USER_INTERNAL Serial        // UART0 verso il Serial del RA4M1
#define SERIAL_LINK          Serial1       // UART1 verso il Serial2 del RA4M1

USBCDC USBSerial(0);
static volatile uint32_t userBaud = 0;
static WebServer web(80);
static char apName[24];
static bool fsOk = false;

static void usbEventCallback(void* arg, esp_event_base_t base, int32_t id, void* data);

// diagnostica della versione di prova: messaggi "[esp] ..." sulla USB, mescolati a quelli del RA4M1
static volatile uint32_t linkRx = 0, netLoops = 0;
static void dbg(const char* fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
  va_end(ap);
  if (n > (int)sizeof(buf) - 3) n = sizeof(buf) - 3;
  buf[n++] = '\r'; buf[n++] = '\n';
  USBSerial.write((const uint8_t*)"[esp] ", 6);
  USBSerial.write((const uint8_t*)buf, n);
}

// ---------------------------------------------------------------------
//  Comandi di prova sul collegamento col RA4M1
// ---------------------------------------------------------------------
static void linkPrintf(const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  SERIAL_LINK.print(buf);
  SERIAL_LINK.print('\n');
}

// n cicli di scrittura + rilettura di file da 4 KB (8 nomi a rotazione)
static void fsTest(long n) {
  if (!fsOk) { linkPrintf("FS err LittleFS non montato"); return; }
  static uint8_t buf[4096], rd[4096];
  uint32_t t = millis();
  for (long i = 0; i < n; i++) {
    char path[16];
    snprintf(path, sizeof(path), "/t%ld.bin", i % 8);
    for (size_t k = 0; k < sizeof(buf); k++) buf[k] = (uint8_t)(k * 7 + i);
    File f = LittleFS.open(path, "w");
    if (!f || f.write(buf, sizeof(buf)) != sizeof(buf)) { linkPrintf("FS err scrittura %ld", i); return; }
    f.close();
    f = LittleFS.open(path, "r");
    if (!f || f.read(rd, sizeof(rd)) != sizeof(rd) || memcmp(buf, rd, sizeof(buf))) { linkPrintf("FS err lettura %ld", i); return; }
    f.close();
  }
  linkPrintf("FS ok n=%ld ms=%lu usati=%u/%u", n, millis() - t, (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
}

// rimanda indietro n byte cosi' come arrivano (misura di velocita' dal RA4M1)
static void echo(long n) {
  linkPrintf("GO");
  SERIAL_LINK.flush();
  uint8_t buf[256];
  uint32_t last = millis();
  while (n > 0 && millis() - last < 3000) {
    int a = SERIAL_LINK.available();
    if (a <= 0) { delay(0); continue; }
    int r = SERIAL_LINK.readBytes(buf, min((long)a, min((long)sizeof(buf), n)));
    SERIAL_LINK.write(buf, r);
    n -= r;
    last = millis();
  }
}

static void linkCommand(char* line) {
  if (!strcmp(line, "PING")) {
    linkPrintf("PONG ArduLearnBridge %s heap=%u fs=%u/%u", BRIDGE_FW_VERSION, (unsigned)ESP.getFreeHeap(),
               fsOk ? (unsigned)LittleFS.usedBytes() : 0, fsOk ? (unsigned)LittleFS.totalBytes() : 0);
  } else if (!strncmp(line, "FS ", 3)) {
    fsTest(atol(line + 3));
  } else if (!strncmp(line, "BAUD ", 5)) {
    long b = atol(line + 5);
    linkPrintf("OK %ld", b);
    SERIAL_LINK.flush();
    delay(20);
    SERIAL_LINK.updateBaudRate(b);
  } else if (!strncmp(line, "ECHO ", 5)) {
    echo(atol(line + 5));
  } else if (!strcmp(line, "WIFI")) {
    linkPrintf("AP %s %s clienti=%d", apName, WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum());
  } else if (line[0]) {
    linkPrintf("ERR %s", line);
  }
}

static void linkTick() {
  static char line[64];
  static uint8_t n = 0;
  while (SERIAL_LINK.available()) {
    char c = SERIAL_LINK.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < sizeof(line) - 1) line[n++] = c; continue; }
    line[n] = 0;
    n = 0;
    dbg("link: %s", line);
    linkCommand(line);
  }
}

// ---------------------------------------------------------------------
//  Rete e pagina di prova
// ---------------------------------------------------------------------
static void handleRoot() {
  String s = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
               "<title>ArduLearn Bridge</title><body style='font:16px sans-serif;margin:30px'>"
               "<h2>ArduLearn Bridge (prova)</h2><p>Firmware ESP32 ");
  s += BRIDGE_FW_VERSION;
  s += F("</p><p>Memoria libera: ");
  s += ESP.getFreeHeap();
  s += F(" byte</p><p>LittleFS: ");
  s += fsOk ? String(LittleFS.usedBytes()) + " / " + String(LittleFS.totalBytes()) + " byte" : String("non montato");
  s += F("</p>");
  web.send(200, "text/html; charset=utf-8", s);
}

// Rete, pagina e collegamento col RA4M1: su un task a parte (core 0), il ponte USB resta nel loop
static void netTask(void*) {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apName, sizeof(apName), "ArduLearn-test-%02x%02x", mac[4], mac[5]);
  dbg("netTask avviato, fs=%d", fsOk);
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(apName, "ardulearn");
  dbg("softAP %s: %d %s", apName, ok, WiFi.softAPIP().toString().c_str());
  web.on("/", handleRoot);
  web.begin();
  uint32_t last = millis();
  for (;;) {
    web.handleClient();
    linkTick();
    netLoops++;
    if (millis() - last > 5000) { last = millis(); dbg("vivo: giri=%lu byte dal RA4M1=%lu heap=%u", netLoops, linkRx, ESP.getFreeHeap()); }
    delay(1);
  }
}

// ---------------------------------------------------------------------
void setup() {
  pinMode(GPIO_BOOT, OUTPUT);
  pinMode(GPIO_RST, OUTPUT);
  digitalWrite(GPIO_BOOT, HIGH);
  digitalWrite(GPIO_RST, HIGH);

  // USB come il firmware Arduino: l'IDE riconosce la scheda come UNO R4 WiFi
  USB.VID(0x2341);
  USB.PID(0x1002);
  USB.manufacturerName("Arduino");
  USB.productName("UNO WiFi R4 CMSIS-DAP");
  USB.firmwareVersion(0x0100 | BRIDGE_FW_MINOR);
  DAP.begin();
  SERIAL_USER.onEvent(usbEventCallback);
  SERIAL_USER.enableReboot(false);
  SERIAL_USER.setRxBufferSize(2048);
  SERIAL_USER.begin(115200);
  SERIAL_USER_INTERNAL.setRxBufferSize(8192);
  SERIAL_USER_INTERNAL.setTxBufferSize(8192);
  SERIAL_USER_INTERNAL.begin(115200, SERIAL_8N1, PIN_USER_RX, PIN_USER_TX);
  SERIAL_LINK.setRxBufferSize(8192);
  SERIAL_LINK.setTxBufferSize(8192);
  SERIAL_LINK.begin(115200, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX);
  USB.begin();

  // Mentre l'ESP32 e' in modalita' download (esptool) BOOT resta basso: se il RA4M1 si resetta
  // in quel momento entra nella modalita' di avvio della ROM Renesas e ci resta. Qui BOOT e' alto:
  // un solo impulso di reset (a piu' di 500 ms dall'accensione, per non sembrare un doppio
  // tocco che apre il bootloader) fa ripartire il programma del RA4M1.
  while (millis() < 1000) delay(10);
  digitalWrite(GPIO_RST, LOW);
  delay(10);
  digitalWrite(GPIO_RST, HIGH);
  delay(1500);                   // prova: il tempo di aprire il monitor
  dbg("avvio ArduLearnBridge %s", BRIDGE_FW_VERSION);
  fsOk = LittleFS.begin(true);
  dbg("LittleFS: %d", fsOk);   // la prima volta formatta la partizione
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
}

// Ponte USB <-> Serial del RA4M1 (come nel firmware Arduino)
static uint8_t buf[2048];
void loop() {
  userBaud = SERIAL_USER.baudRate();
  int n = 0;
  if (SERIAL_USER.available()) {
    n = min((unsigned int)SERIAL_USER.available(), sizeof(buf));
    SERIAL_USER.readBytes(buf, n);
  }
  if (n > 0) SERIAL_USER_INTERNAL.write(buf, n);
  n = 0;
  if (SERIAL_USER_INTERNAL.available()) {
    n = min((unsigned int)SERIAL_USER_INTERNAL.available(), sizeof(buf));
    SERIAL_USER_INTERNAL.readBytes(buf, n);
  }
  if (n > 0) SERIAL_USER.write(buf, n);
  yield();
}

// Il PC cambia il baud della porta: 1200 = RA4M1 nel bootloader (caricamento sketch),
// 2400 = RA4M1 nella modalita' di avvio della ROM; altrimenti si segue il baud del PC
static void usbEventCallback(void* arg, esp_event_base_t base, int32_t id, void* data) {
  if (base != ARDUINO_USB_CDC_EVENTS || id != ARDUINO_USB_CDC_LINE_CODING_EVENT) return;
  arduino_usb_cdc_event_data_t* d = (arduino_usb_cdc_event_data_t*)data;
  uint32_t baud = d->line_coding.bit_rate;
  if (baud == 1200) {
    digitalWrite(GPIO_BOOT, HIGH);
    digitalWrite(GPIO_RST, LOW);  delay(100);
    digitalWrite(GPIO_RST, HIGH); delay(100);
    digitalWrite(GPIO_RST, LOW);  delay(100);
    digitalWrite(GPIO_RST, HIGH);
  } else if (baud == 2400) {
    digitalWrite(GPIO_BOOT, LOW);
    digitalWrite(GPIO_RST, HIGH); delay(100);
    digitalWrite(GPIO_RST, LOW);  delay(100);
    digitalWrite(GPIO_RST, HIGH);
  } else {
    SERIAL_USER_INTERNAL.updateBaudRate(baud);
  }
  while (SERIAL_USER_INTERNAL.available()) SERIAL_USER_INTERNAL.read();
}

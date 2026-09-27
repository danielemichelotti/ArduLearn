// =====================================================================
//  ArduLearn Bridge - firmware del modulo ESP32-S3 dell'Arduino UNO R4 WiFi
//
//  Sostituisce il firmware "USB bridge" di Arduino (github.com/arduino/uno-r4-wifi-usb-bridge)
//  tenendone la parte USB, cosi' IDE, arduino-cli e l'app ArduLearn funzionano come prima:
//   - ponte USB <-> Serial del RA4M1, tocco a 1200 baud (bootloader) e 2400 (ROM Renesas);
//   - HID CMSIS-DAP con il comando 0xAA (ESP32 in modalita' download: ripristino dall'IDE).
//  Al posto dei comandi AT (WiFiS3) c'e' ArduLearn:
//   - Wi-Fi della scuola o rete propria "ArduLearn-xxxx" con portale (wifimgr.cpp);
//   - server web con la pagina nella flash dell'ESP32 (web.cpp);
//   - collegamento veloce col RA4M1, che resta il PLC (link.cpp);
//   - nome <host>.local (mDNS) e risposta alla ricerca UDP "PLC?" dell'app (porta 4210).
//  Compilare con: esp32:esp32:esp32s3 USBMode=default CDCOnBoot=default FlashSize=4M
//  (partitions.csv di questa cartella).
// =====================================================================
#include "bridge_config.h"
#include "USB.h"
#include "USBCDC.h"
#include "DAP.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include "link.h"
#include "wifimgr.h"
#include "web.h"
#include "ota.h"

#define SERIAL_USER          USBSerial     // la porta seriale che vede il PC
#define SERIAL_USER_INTERNAL Serial        // UART0 verso il Serial del RA4M1

USBCDC USBSerial(0);
static void usbEventCallback(void* arg, esp_event_base_t base, int32_t id, void* data);
volatile uint32_t quietUntil = 0;   // caricamento del RA4M1 in corso: niente messaggi sulla USB

// ---------------------------------------------------------------------
//  Servizi di rete: mDNS e ricerca UDP
// ---------------------------------------------------------------------
static WiFiUDP discovery;
static const uint16_t DISCOVERY_PORT = 4210;

static void netServices() {
  static uint8_t lastState = 0xFF;
  static char mdnsHost[32] = "";
  static uint32_t mdnsTry = 0;
  uint8_t st = wifiNetState();
  bool up = st == NET_OK || st == NET_FALLBACK;
  if (st != lastState) {                        // rete cambiata: si riaprono i servizi
    lastState = st;
    discovery.stop();
    if (up) discovery.begin(DISCOVERY_PORT);
    mdnsHost[0] = 0;
  }
  const char* h = webHost();
  if (up && h[0] && strcmp(h, mdnsHost) && (!mdnsTry || millis() - mdnsTry > 10000)) {   // nome .local
    mdnsTry = millis();
    MDNS.end();
    if (MDNS.begin(h)) {
      MDNS.setInstanceName("ArduLearn");
      MDNS.addService("http", "tcp", 80);
      strlcpy(mdnsHost, h, sizeof(mdnsHost));
    }
  }
  if (!up) return;
  int len = discovery.parsePacket();
  if (len <= 0) return;
  char q[8] = { 0 };
  discovery.read(q, sizeof(q) - 1);
  if (strncmp(q, "PLC?", 4)) return;
  String o;
  webDiscoveryJson(o);
  discovery.beginPacket(discovery.remoteIP(), discovery.remotePort());
  discovery.print(o);
  discovery.endPacket();
}

// Rete, collegamento col RA4M1, copia dello stato: un task sul core 0 (il ponte USB resta nel loop)
static volatile const char* svcWhere = "avvio";
extern uint32_t resetByWatchdog, webServerRestarts;
static char bootWhy[40] = "";

// motivo dell'ultimo riavvio dell'ESP32 (nel registro all'avvio e nella diagnostica)
static void readBootReason() {
  static const char* const R[] = { "?", "accensione", "pin EN", "software", "crash", "watchdog interrupt",
                                   "watchdog task", "watchdog", "sonno", "tensione bassa", "SDIO", "USB", "JTAG" };
  int r = esp_reset_reason();
  snprintf(bootWhy, sizeof(bootWhy), "%s%s", r >= 0 && r < 13 ? R[r] : "?",
           resetByWatchdog == 0xA11E ? " (controllo del server web)" : "");
  resetByWatchdog = 0;
}
static volatile uint32_t svcLoops = 0;

static void svcTask(void*) {
  svcWhere = "wifiBegin";
  linkLog("avvio: ultimo riavvio per %s", bootWhy);
  wifiBegin();
  svcWhere = "webBegin";
  webBegin();                           // dopo il Wi-Fi: lo stack di rete (lwIP) dev'essere avviato
  for (;;) {
    svcWhere = "wifiTick";
    wifiTick();
    svcWhere = "linkSetNet";
    linkSetNet(wifiNetState(), wifiIP().toString().c_str(), wifiApName());
    svcWhere = "linkService";
    linkService();
    svcWhere = "webPoll";
    webPoll();
    svcWhere = "netServices";
    netServices();
    otaService();
    svcWhere = "pausa";
    svcLoops++;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// Diagnostica ("wifi diag"): stato dei task e del collegamento sulla USB ogni 5 s, da un task
// sull'altro core (funziona anche se il task "svc" o il server web restano bloccati)
bool diagOn = false;
static void diagTask(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    if (!diagOn || (int32_t)(quietUntil - millis()) > 0) continue;
    char b[340];
    int n = snprintf(b, sizeof(b), "[diag] svc=%s giri=%lu | web=%s | link=%s da %lu ms, up=%d baud=%lu saluti %lu/%lu persi %lu rx %lu | heap=%u\r\n",
                     (const char*)svcWhere, (unsigned long)svcLoops, (const char*)webWhere, (const char*)linkWhere,
                     (unsigned long)(millis() - linkWhereAt), linkUp(), (unsigned long)linkBaud(),
                     (unsigned long)linkHelloOk, (unsigned long)linkHelloTries, (unsigned long)linkLost,
                     (unsigned long)linkRxBytes, (unsigned)ESP.getFreeHeap());
    n += snprintf(b + n - 2, sizeof(b) - n + 2, " (blocco max %u, minimo %u) avvio: %s, server riavviato %lu\r\n",
                  (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap(), bootWhy, (unsigned long)webServerRestarts) - 2;
    USBSerial.write((const uint8_t*)b, n);
  }
}

// Messaggi della ROM, di ESP-IDF e printf: sulla USB, mai sulla UART0, che e' la seriale del RA4M1
// (li riceverebbe come comandi e rovinerebbero i caricamenti con bossac). Come nel firmware Arduino.
static void usbLog(const char* p, size_t n) {
  if ((int32_t)(quietUntil - millis()) <= 0) USBSerial.write((const uint8_t*)p, n);
}

static ssize_t stdoutWrite(void*, const char* buf, ssize_t size) { usbLog(buf, size); return size; }

static int logVprintf(const char* fmt, va_list ap) {
  char b[200];
  int n = vsnprintf(b, sizeof(b), fmt, ap);
  if (n > 0) usbLog(b, n < (int)sizeof(b) ? n : sizeof(b) - 1);
  return n;
}

static void romPutc(char c) {
  static char buf[128];
  static size_t n = 0;
  buf[n++] = c;
  if (c == '\n' || n == sizeof(buf)) { usbLog(buf, n); n = 0; }
}

// ---------------------------------------------------------------------
void setup() {
  readBootReason();
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
  SERIAL_USER_INTERNAL.setDebugOutput(false);
  stdout = funopen(nullptr, nullptr, &stdoutWrite, nullptr, nullptr);
  static char linebuf[256];
  setvbuf(stdout, linebuf, _IOLBF, sizeof(linebuf));
  esp_log_set_vprintf(logVprintf);
  esp_log_level_set("*", ESP_LOG_WARN);
  ets_install_putc1(romPutc);
  linkBegin();
  USB.begin();

  // Mentre l'ESP32 e' in modalita' download (esptool) BOOT resta basso: se il RA4M1 si resetta
  // in quel momento entra nella modalita' di avvio della ROM Renesas e ci resta. Qui BOOT e' alto:
  // un solo impulso di reset (a piu' di 500 ms dall'accensione, per non sembrare un doppio
  // tocco che apre il bootloader) fa ripartire il programma del RA4M1.
  while (millis() < 1000) delay(10);
  digitalWrite(GPIO_RST, LOW);
  delay(10);
  digitalWrite(GPIO_RST, HIGH);

  LittleFS.begin(true);                 // la prima volta formatta la partizione (qualche secondo)
  xTaskCreatePinnedToCore(svcTask, "svc", 8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(diagTask, "diag", 4096, nullptr, 2, nullptr, 1);

}

// Ponte USB <-> Serial del RA4M1 (come nel firmware Arduino)
static uint8_t buf[2048];
void loop() {
  if (bridgeHold) { delay(1); return; }   // aggiornamento del RA4M1: la sua seriale la usa BOSSA
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
    quietUntil = millis() + 30000;
    linkPause(15000);                   // il RA4M1 si sta caricando: non lo si cerca per un po'
    digitalWrite(GPIO_BOOT, HIGH);
    digitalWrite(GPIO_RST, LOW);  delay(100);
    digitalWrite(GPIO_RST, HIGH); delay(100);
    digitalWrite(GPIO_RST, LOW);  delay(100);
    digitalWrite(GPIO_RST, HIGH);
  } else if (baud == 2400) {
    quietUntil = millis() + 120000;
    linkPause(60000);
    digitalWrite(GPIO_BOOT, LOW);
    digitalWrite(GPIO_RST, HIGH); delay(100);
    digitalWrite(GPIO_RST, LOW);  delay(100);
    digitalWrite(GPIO_RST, HIGH);
  } else {
    SERIAL_USER_INTERNAL.updateBaudRate(baud);
  }
  while (SERIAL_USER_INTERNAL.available()) SERIAL_USER_INTERNAL.read();
}

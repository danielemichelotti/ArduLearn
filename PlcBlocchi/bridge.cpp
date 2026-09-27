#include "net.h"
#if NET_BRIDGE
#include "link_proto.h"
#include "storage.h"
#include "displays.h"

// =====================================================================
//  UNO R4 WiFi: collegamento col modulo ESP32-S3 (firmware ArduLearnBridge) su Serial2
//  (vedi link_proto.h). L'ESP32 comanda: saluto e velocita', stato della rete ogni
//  secondo, richieste HTTP del browser per il PLC. Qui si ricevono i pacchetti senza
//  mai bloccare il ciclo PLC; il testo della richiesta va in un buffer da cui il server
//  web (web.cpp) legge come da un client di rete, e la risposta torna a pacchetti.
// =====================================================================
#define LINK Serial2
const char WIFI_AP_PASS[] = "ardulearn";

static uint32_t baud = LINK_SLOW_BAUD, lastFrame = 0, lastNet = 0;
static uint32_t nOk = 0, nBad = 0, nSlow = 0;   // pacchetti buoni, rovinati, ritorni a 115200 (comando "link")
static uint8_t  fr[LINK_MAX], fst = 0, ftype = 0;
static uint16_t flen = 0, fpos = 0, fcrc = 0;
// richiesta in corso: buffer circolare con il testo ricevuto
static const uint16_t RX_LEN = 512;
static uint8_t  rx[RX_LEN];
static uint16_t rxHead = 0, rxCount = 0;
static bool     reqActive = false, reqAborted = false, ackOwed = false;
// stato della rete (dall'ESP32)
static IPAddress ip(0, 0, 0, 0);
static char apName[24] = "ArduLearn";

IPAddress netIP() { return ip; }
const char* bridgeApName() { return apName; }

static void sendFrame(uint8_t type, const void* data, uint16_t n) {
  uint8_t h[4] = { LINK_SOF, type, (uint8_t)n, (uint8_t)(n >> 8) };
  uint16_t crc = linkCrc(h + 1, 3);
  crc = linkCrc((const uint8_t*)data, n, crc);
  uint8_t t[2] = { (uint8_t)crc, (uint8_t)(crc >> 8) };
  LINK.write(h, 4);
  if (n) LINK.write((const uint8_t*)data, n);
  LINK.write(t, 2);
}

static void setBaud(uint32_t b) {
  if (b == baud) return;
  LINK.flush();
  LINK.end();
  LINK.begin(b);
  baud = b;
}

static uint16_t rxFree() { return RX_LEN - rxCount; }

static void maybeAck() {
  if (ackOwed && rxFree() >= LINK_MAX) { ackOwed = false; sendFrame(LINK_ACK, nullptr, 0); }
}

static void rxPush(const uint8_t* p, uint16_t n) {
  for (uint16_t i = 0; i < n && rxCount < RX_LEN; i++) rx[(rxHead + rxCount++) % RX_LEN] = p[i];
  ackOwed = true;           // si conferma quando c'e' posto per un altro pacchetto intero
  maybeAck();
}

// valore esadecimale di 2 cifre
static int hex2(const char* s) {
  char b[3] = { s[0], s[1], 0 };
  if (!isxdigit(b[0]) || !isxdigit(b[1])) return -1;
  return strtol(b, nullptr, 16);
}

// L'ESP32 comunica il suo MAC: la scheda usa quello (nome ardulearn-xxxx come la sua rete)
static void applyMac(const char* m) {
  uint8_t mac[6];
  for (uint8_t i = 0; i < 6; i++) { int v = hex2(m + 2 * i); if (v < 0) return; mac[i] = v; }
  if (!memcmp(mac, cfg.mac, 6)) return;
  char oldDefault[16];
  snprintf(oldDefault, sizeof(oldDefault), "ardulearn-%02x%02x", cfg.mac[4], cfg.mac[5]);
  bool defaultName = !strcmp(cfg.host, oldDefault);
  memcpy(cfg.mac, mac, 6);
  if (defaultName) snprintf(cfg.host, sizeof(cfg.host), "ardulearn-%02x%02x", mac[4], mac[5]);
  cfgSave();
  netHostnameChanged();
}

static const char* field(const char* s, const char* key) {
  const char* p = strstr(s, key);
  return p ? p + strlen(key) : nullptr;
}

static void onFrame(uint8_t type, uint8_t* d, uint16_t n) {
  switch (type) {
    case LINK_HELLO: {
      d[n < LINK_MAX ? n : LINK_MAX - 1] = 0;
      reqActive = false;                                  // l'ESP32 e' ripartito: si ricomincia
      rxCount = 0;
      const char* m = field((char*)d, "m=");
      if (m) applyMac(m);
      static const char r[] = "fw=" FW_VERSION " b=" BOARD_NAME;
      sendFrame(LINK_HELLO_R, r, sizeof(r) - 1);
      const char* b = field((char*)d, "b=");
      if (b) setBaud(strtoul(b, nullptr, 10));
      break;
    }
    case LINK_PING:
      sendFrame(LINK_PONG, nullptr, 0);
      break;
    case LINK_NET: {
      d[n < LINK_MAX ? n : LINK_MAX - 1] = 0;
      const char* s = field((char*)d, "s=");
      const char* i = field((char*)d, "ip=");
      const char* a = field((char*)d, "ap=");
      uint8_t st = s ? atoi(s) : g_netState;
      IPAddress nip = ip;
      if (i) { char b[16]; uint8_t k = 0; while (i[k] && i[k] != ' ' && k < 15) { b[k] = i[k]; k++; } b[k] = 0; nip.fromString(b); }
      if (a) { uint8_t k = 0; while (a[k] && a[k] != ' ' && k < sizeof(apName) - 1) { apName[k] = a[k]; k++; } apName[k] = 0; }
      bool changed = st != g_netState || nip != ip;
      g_netState = st;
      ip = nip;
      lastNet = millis();
      sendFrame(LINK_NET_R, nullptr, 0);
      if (changed) displaysShowStatus();
      break;
    }
    case LINK_START:
      rxHead = rxCount = 0;
      reqActive = true;
      reqAborted = false;
      rxPush(d, n);
      break;
    case LINK_MORE:
      if (reqActive) rxPush(d, n);
      else sendFrame(LINK_ACK, nullptr, 0);               // richiesta gia' chiusa: si scarta
      break;
    case LINK_ABORT:
      reqAborted = true;
      break;
    case LINK_TEXT:
      Serial.write(d, n);
      break;
  }
}

// Riceve i pacchetti arrivati (senza attendere)
static void poll() {
  while (LINK.available()) {
    uint8_t c = LINK.read();
    switch (fst) {
      case 0: if (c == LINK_SOF) fst = 1; break;
      case 1: ftype = c; fst = 2; break;
      case 2: flen = c; fst = 3; break;
      case 3:
        flen |= c << 8;
        if (flen > LINK_MAX) { fst = 0; break; }
        fpos = 0;
        fst = flen ? 4 : 5;
        break;
      case 4: fr[fpos++] = c; if (fpos == flen) fst = 5; break;
      case 5: fcrc = c; fst = 6; break;
      case 6: {
        fcrc |= c << 8;
        fst = 0;
        uint8_t h[3] = { ftype, (uint8_t)flen, (uint8_t)(flen >> 8) };
        if (linkCrc(fr, flen, linkCrc(h, 3)) != fcrc) { nBad++; break; }   // rovinato: si scarta
        nOk++;
        lastFrame = millis();
        onFrame(ftype, fr, flen);
        break;
      }
    }
  }
  // nessun pacchetto da un po' (ESP32 riavviato o in aggiornamento): si torna alla velocita' iniziale
  if (baud != LINK_SLOW_BAUD && !reqActive && millis() - lastFrame > LINK_IDLE_MS) { nSlow++; setBaud(LINK_SLOW_BAUD); }
}

void bridgeBegin() {
  LINK.begin(LINK_SLOW_BAUD);
  g_netState = NET_NO_LINK;
}

void bridgeTick() {
  poll();
  // il modulo Wi-Fi non si fa sentire: sulla matrice LED compare la X
  if (millis() - lastNet > 10000UL && millis() > 15000UL && g_netState != NET_NO_HW) {
    g_netState = NET_NO_HW;
    ip = IPAddress(0, 0, 0, 0);
    displaysShowStatus();
  }
}

bool bridgeSerial(const char* line) {
  if (!strcmp(line, "link")) {
    Serial.print(F("Collegamento col modulo Wi-Fi: ")); Serial.print(baud);
    Serial.print(F(" baud, pacchetti buoni ")); Serial.print(nOk);
    Serial.print(F(", rovinati ")); Serial.print(nBad);
    Serial.print(F(", ritorni a 115200 ")); Serial.println(nSlow);
    return true;
  }
  if (strncmp(line, "wifi", 4) || (line[4] && line[4] != ' ')) return false;
  if (g_netState == NET_NO_HW) { Serial.println(F("@err modulo Wi-Fi non collegato")); return true; }
  sendFrame(LINK_CMD, line, strlen(line));               // la risposta arriva come testo (LINK_TEXT)
  return true;
}

// ---------------------------------------------------------------------
//  Client e server "di rete" per web.cpp
// ---------------------------------------------------------------------
LinkClient LinkServer::available() {
  poll();
  return LinkClient(reqActive);
}

LinkClient::operator bool() const { return valid && reqActive; }

int LinkClient::available() {
  poll();
  return rxCount;
}

int LinkClient::read() {
  poll();
  if (!rxCount) return -1;
  uint8_t b = rx[rxHead];
  rxHead = (rxHead + 1) % RX_LEN;
  rxCount--;
  maybeAck();
  return b;
}

int LinkClient::read(uint8_t* buf, size_t n) {
  poll();
  size_t k = 0;
  while (k < n && rxCount) {
    buf[k++] = rx[rxHead];
    rxHead = (rxHead + 1) % RX_LEN;
    rxCount--;
  }
  maybeAck();
  return k;
}

bool LinkClient::connected() {
  poll();
  return reqActive && !reqAborted;
}

size_t LinkClient::write(const uint8_t* buf, size_t n) {
  if (!reqActive || reqAborted) return 0;
  for (size_t off = 0; off < n; off += LINK_MAX) {
    uint16_t k = n - off < LINK_MAX ? n - off : LINK_MAX;
    sendFrame(LINK_RESP, buf + off, k);
  }
  return n;
}

void LinkClient::stop() {
  if (reqActive && !reqAborted) sendFrame(LINK_END, nullptr, 0);
  reqActive = false;
  rxCount = 0;
  ackOwed = false;
}
#endif

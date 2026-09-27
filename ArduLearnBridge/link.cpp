#include "link.h"
#include "bridge_config.h"
#include "wifimgr.h"

// =====================================================================
//  Collegamento col RA4M1 sulla UART1 (vedi link_proto.h)
//  Un solo "dialogo" alla volta (mutex): richieste del browser, lettura periodica
//  dello stato, saluto iniziale. I comandi che il RA4M1 manda da solo (C) arrivano
//  quando si e' in ascolto e vengono eseguiti subito.
// =====================================================================
#define LINK Serial1

static SemaphoreHandle_t mtx;
static volatile bool up = false;

static uint32_t pausedUntil = 0, lastHello = 0, lastBeat = 0;
static uint8_t  beatFails = 0;
static char raFw[16] = "";
static uint8_t netState = 0;
static char netIp[16] = "0.0.0.0", netAp[24] = "";
static bool netDirty = true;

volatile const char* linkWhere = "libero";
volatile uint32_t linkWhereAt = 0;
volatile uint32_t linkHelloTries = 0, linkHelloOk = 0, linkRxBytes = 0, linkLost = 0;
static inline void where(const char* w) { linkWhere = w; linkWhereAt = millis(); }
bool linkLogOn = false;         // "wifi log": richieste del browser sul monitor seriale
static char logBuf[1024];
static size_t logLen = 0;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

void linkLog(const char* fmt, ...) {
  if (!linkLogOn) return;
  char line[160];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof(line) - 3) n = sizeof(line) - 3;
  line[n++] = '\r'; line[n++] = '\n';
  portENTER_CRITICAL(&logMux);
  if (logLen + 6 + n < sizeof(logBuf)) {
    memcpy(logBuf + logLen, "[esp] ", 6);
    memcpy(logBuf + logLen + 6, line, n);
    logLen += 6 + n;
  }
  portEXIT_CRITICAL(&logMux);
}

bool linkUp() { return up; }
const char* linkRaVersion() { return raFw; }
uint32_t linkBaud() { return LINK_BAUD; }
void linkPause(uint32_t ms) { pausedUntil = millis() + ms; up = false; }


// Il RA4M1 non risponde piu' (riavviato, o tornato a 115200): si rifa' il saluto al prossimo giro
static void lost() {
  linkLost++;
  up = false;
  lastHello = 0;
  while (LINK.available()) LINK.read();
}

static void sendFrame(uint8_t type, const void* data, size_t n) {
  uint8_t h[4] = { LINK_SOF, type, (uint8_t)n, (uint8_t)(n >> 8) };
  uint16_t crc = linkCrc(h + 1, 3);
  crc = linkCrc((const uint8_t*)data, n, crc);
  uint8_t t[2] = { (uint8_t)crc, (uint8_t)(crc >> 8) };
  LINK.write(h, 4);
  if (n) LINK.write((const uint8_t*)data, n);
  LINK.write(t, 2);
}

static void handleCommand(const uint8_t* p, uint16_t n);

// Legge un pacchetto (tipo, dati) entro timeoutMs. -1 = niente. I comandi C si eseguono qui.
static int readFrame(uint8_t* buf, uint16_t& len, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  uint8_t st = 0, type = 0;
  uint16_t n = 0, pos = 0, crc = 0;
  for (;;) {
    int c = LINK.read();
    if (c >= 0) linkRxBytes++;
    if (c < 0) {
      if (millis() - t0 >= timeoutMs) return -1;
      vTaskDelay(1);
      continue;
    }
    switch (st) {
      case 0: if (c == LINK_SOF) st = 1; break;
      case 1: type = c; st = 2; break;
      case 2: n = c; st = 3; break;
      case 3:
        n |= c << 8;
        if (n > LINK_MAX) { st = 0; break; }
        pos = 0;
        st = n ? 4 : 5;
        break;
      case 4: buf[pos++] = c; if (pos == n) st = 5; break;
      case 5: crc = c; st = 6; break;
      case 6: {
        crc |= c << 8;
        uint8_t h[3] = { type, (uint8_t)n, (uint8_t)(n >> 8) };
        st = 0;
        if (linkCrc(buf, n, linkCrc(h, 3)) != crc) break;       // rovinato: si scarta
        if (type == LINK_CMD) { handleCommand(buf, n); t0 = millis(); break; }
        len = n;
        return type;
      }
    }
  }
}

static bool expect(uint8_t want, uint8_t* buf, uint16_t& len, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  for (;;) {
    uint32_t el = millis() - t0;
    if (el >= timeoutMs) return false;
    int t = readFrame(buf, len, timeoutMs - el);
    if (t < 0) return false;
    if (t == want) return true;             // altri pacchetti (resti di un dialogo interrotto): si ignorano
  }
}

// Comando dal monitor seriale del RA4M1: si esegue e si rimanda il testo da stampare
static void handleCommand(const uint8_t* p, uint16_t n) {
  char line[LINK_MAX + 1];
  memcpy(line, p, n);
  line[n] = 0;
  String out;
  wifiSerialCommand(line, out);
  // a pezzi, con una pausa: il RA4M1 ha un buffer di ricezione di 512 byte
  for (size_t off = 0; off < out.length(); off += LINK_MAX) {
    size_t k = min((size_t)LINK_MAX, out.length() - off);
    sendFrame(LINK_TEXT, out.c_str() + off, k);
    if (off + k < out.length()) vTaskDelay(pdMS_TO_TICKS(40));
  }
}

// ---------------------------------------------------------------------
// Saluto: H/h (MAC e versioni), poi una prova con P/p
// ---------------------------------------------------------------------
static bool hello() {
  uint8_t mac[6], buf[LINK_MAX + 1];
  uint16_t len;
  wifiMac(mac);
  char msg[64];
  snprintf(msg, sizeof(msg), "m=%02x%02x%02x%02x%02x%02x v=%s",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], BRIDGE_FW_VERSION);
  while (LINK.available()) LINK.read();
  linkHelloTries++;
  sendFrame(LINK_HELLO, msg, strlen(msg));
  if (!expect(LINK_HELLO_R, buf, len, 300)) return false;
  buf[len] = 0;
  const char* fw = strstr((char*)buf, "fw=");
  if (fw) { strlcpy(raFw, fw + 3, sizeof(raFw)); char* sp = strchr(raFw, ' '); if (sp) *sp = 0; }
  for (uint8_t i = 0; i < 3; i++) {
    sendFrame(LINK_PING, nullptr, 0);
    if (expect(LINK_PONG, buf, len, 100)) { netDirty = true; linkHelloOk++; return true; }
  }
  return false;
}

static bool sendNet() {
  uint8_t buf[LINK_MAX + 1];
  uint16_t len;
  char msg[80];
  snprintf(msg, sizeof(msg), "s=%u ip=%s ap=%s", netState, netIp, netAp);
  sendFrame(LINK_NET, msg, strlen(msg));
  return expect(LINK_NET_R, buf, len, 300);
}

void linkSetNet(uint8_t state, const char* ip, const char* ap) {
  if (state != netState || strcmp(ip, netIp) || strcmp(ap, netAp)) {
    netState = state;
    strlcpy(netIp, ip, sizeof(netIp));
    strlcpy(netAp, ap, sizeof(netAp));
    netDirty = true;
  }
}

void linkBegin() {
  mtx = xSemaphoreCreateMutex();
  LINK.setRxBufferSize(8192);
  LINK.setTxBufferSize(4096);
  LINK.begin(LINK_BAUD, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX);
}

void linkService() {
  uint32_t now = millis();
  if ((int32_t)(pausedUntil - now) > 0) return;
  if (xSemaphoreTake(mtx, pdMS_TO_TICKS(50)) != pdTRUE) return;
  where("servizio");
  if (!up) {
    if (now - lastHello >= 1000) {
      lastHello = now;
      up = hello();
      beatFails = 0;
      lastBeat = millis();
    }
  } else if (netDirty || now - lastBeat >= 1000) {
    // battito (e stato della rete): tiene il RA4M1 alla velocita' alta
    lastBeat = now;
    if (sendNet()) { netDirty = false; beatFails = 0; }
    else if (++beatFails >= 3) lost();
  } else if (LINK.available()) {
    uint8_t buf[LINK_MAX + 1];
    uint16_t len;
    readFrame(buf, len, 0);                   // eventuali comandi C del RA4M1
  } else if (logLen) {                        // registro diagnostico, un pezzo alla volta
    char out[200];
    size_t n = 0;
    portENTER_CRITICAL(&logMux);
    n = min(logLen, sizeof(out));
    memcpy(out, logBuf, n);
    memmove(logBuf, logBuf + n, logLen - n);
    logLen -= n;
    portEXIT_CRITICAL(&logMux);
    sendFrame(LINK_TEXT, out, n);
  }
  where("libero");
  xSemaphoreGive(mtx);
}

// ---------------------------------------------------------------------
//  Richiesta HTTP inoltrata
// ---------------------------------------------------------------------
int linkHttp(const LinkReq& r, uint32_t timeoutMs) {
  if (!up) return -1;
  if (xSemaphoreTake(mtx, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return -2;
  where("http: invio richiesta");
  int result = -2;
  uint8_t buf[LINK_MAX + 1];
  uint16_t len;
  // testo della richiesta: intestazioni e corpo, a pacchetti di LINK_MAX con conferma
  char head[700];
  int hl = snprintf(head, sizeof(head), "%s %s HTTP/1.1\r\nX-Pin: %s\r\nContent-Length: %ld\r\n\r\n",
                    r.method, r.uri, r.pin ? r.pin : "", (long)r.bodyLen);
  if (hl >= (int)sizeof(head)) hl = sizeof(head) - 1;
  int32_t bodyLeft = r.bodyLen;
  size_t headOff = 0;
  bool first = true, ok = true;
  int early = -1;                 // il RA4M1 ha risposto prima di leggere tutto il corpo (es. un errore)
  while (ok && early < 0 && (headOff < (size_t)hl || bodyLeft > 0)) {
    uint8_t pkt[LINK_MAX];
    size_t n = 0;
    while (headOff < (size_t)hl && n < LINK_MAX) pkt[n++] = head[headOff++];
    while (bodyLeft > 0 && n < LINK_MAX) {
      int k = r.readBody ? r.readBody(r.ctx, pkt + n, min((int32_t)(LINK_MAX - n), bodyLeft)) : -1;
      if (k <= 0) { ok = false; break; }
      n += k;
      bodyLeft -= k;
    }
    if (!ok) break;
    sendFrame(first ? LINK_START : LINK_MORE, pkt, n);
    first = false;
    where("http: attesa conferma");
    for (;;) {
      // la conferma arriva subito (al massimo qualche centinaio di ms mentre il RA4M1 scrive la memoria)
      int t = readFrame(buf, len, 4000);
      if (t < 0) { ok = false; result = -2; lost(); break; }
      if (t == LINK_ACK) break;
      if (t == LINK_RESP || t == LINK_END) { early = t; break; }   // il resto del corpo non serve
    }
  }
  if (!ok) {
    sendFrame(LINK_ABORT, nullptr, 0);
    where("libero");
    xSemaphoreGive(mtx);
    return result;
  }
  where("http: attesa risposta");
  // risposta: intestazioni HTTP (fino alla riga vuota), poi il corpo
  char rh[512];
  size_t rhl = 0;
  bool inBody = false, want = true;
  int status = 0;
  for (;;) {
    int t = early >= 0 ? early : readFrame(buf, len, timeoutMs);
    early = -1;
    if (t < 0) { sendFrame(LINK_ABORT, nullptr, 0); result = -2; lost(); break; }
    if (t == LINK_END) { result = status ? status : -2; break; }
    if (t != LINK_RESP) continue;
    size_t off = 0;
    if (!inBody) {
      while (off < len && rhl < sizeof(rh) - 1) {
        rh[rhl++] = buf[off++];
        if (rhl >= 4 && !memcmp(rh + rhl - 4, "\r\n\r\n", 4)) { inBody = true; break; }
      }
      if (!inBody && rhl >= sizeof(rh) - 1) inBody = true;   // intestazioni troppo lunghe: si tronca
      if (inBody) {
        rh[rhl] = 0;
        status = atoi(rh + 9);                 // "HTTP/1.1 200 OK"
        char type[48] = "application/octet-stream";
        int32_t clen = -1;
        for (char* l = strtok(rh, "\r\n"); l; l = strtok(nullptr, "\r\n")) {
          if (!strncasecmp(l, "Content-Type:", 13)) { l += 13; while (*l == ' ') l++; strlcpy(type, l, sizeof(type)); }
          else if (!strncasecmp(l, "Content-Length:", 15)) clen = atol(l + 15);
        }
        if (r.onHead) r.onHead(r.ctx, status, type, clen);
      }
    }
    if (inBody && off < len && want && r.onData) {
      where("http: invio al browser");
      want = r.onData(r.ctx, buf + off, len - off);
      where("http: attesa risposta");
    }
  }
  where("libero");
  xSemaphoreGive(mtx);
  return result;
}

// ---- richiesta interna con la risposta in un buffer ----
struct GetCtx { char* out; size_t max, n; int status; };
static void getHead(void* c, int status, const char*, int32_t) { ((GetCtx*)c)->status = status; }
static bool getData(void* c, const uint8_t* b, size_t n) {
  GetCtx* g = (GetCtx*)c;
  size_t k = min(n, g->max - 1 - g->n);
  memcpy(g->out + g->n, b, k);
  g->n += k;
  return true;
}

int linkGet(const char* uri, char* out, size_t outLen, uint32_t timeoutMs) {
  GetCtx g = { out, outLen, 0, 0 };
  LinkReq r = { "GET", uri, "", 0, nullptr, getHead, getData, &g };
  int s = linkHttp(r, timeoutMs);
  out[g.n] = 0;
  return s;
}

#include "web.h"
#include "engine.h"
#include "storage.h"
#include "displays.h"
#if HAS_EMBEDDED_PAGE
#include "web_page.h"
#endif
#if HAS_MODULES
#include "modules.h"
#endif
#if HAS_LED_MATRIX
#include "ledmatrix.h"
#endif
#include "net.h"
#include <Wire.h>

static NetServer server(80);

// bozza condivisa (vedi postDraft)
static const char DRAFT_PATH[] = "/PB/DRAFT.PB";
static uint16_t draftRev = 0;

#if HAS_MODULES
// forniti da mod_rtc.cpp se presente (simboli deboli: senza modulo valgono nullptr)
bool rtcRead(uint8_t* t) __attribute__((weak));
bool rtcSet(uint8_t yy, uint8_t mo, uint8_t dd, uint8_t hh, uint8_t mi, uint8_t ss, uint8_t dow) __attribute__((weak));
#endif

// Pagina web sulla microSD: /PLC/INDEX.GZ con la sua versione in /PLC/INDEX.VER
// (la invia l'EXE). Sull'Uno e' l'unica; il Mega la usa se e' piu' nuova di quella interna.
static const char PAGE_PATH[] = "/PLC/INDEX.GZ";
static const char PAGE_VER_PATH[] = "/PLC/INDEX.VER";

// ---------------------------------------------------------------------
//  Scrittura bufferizzata: NetClient invia un pacchetto per ogni
//  write(), quindi si accumula e si spedisce a blocchi.
// ---------------------------------------------------------------------
class Out : public Print {
public:
  explicit Out(NetClient& c) : cl(c) {}
  ~Out() { send(); }
  size_t write(uint8_t b) override {
    buf[n++] = b;
    if (n == sizeof(buf)) send();
    return 1;
  }
  size_t write(const uint8_t* p, size_t len) override {
    for (size_t i = 0; i < len; i++) write(p[i]);
    return len;
  }
  void send() { if (n) { if (cl.write(buf, n) != n) dead = true; n = 0; } }
  bool alive() const { return !dead; }   // false se il browser ha chiuso la connessione
private:
  bool dead = false;
  NetClient& cl;
  uint8_t buf[128];
  uint8_t n = 0;
};

struct Req {
  char method[8];
  char path[24];
  char query[96];
  int32_t contentLength;
  uint32_t etag;          // If-None-Match (versione della pagina gia' nel browser)
  bool authed;
};

// ---------------------------------------------------------------------
//  Utilita'
// ---------------------------------------------------------------------
static int readLine(NetClient& c, char* buf, uint8_t len, uint32_t deadline) {
  uint8_t n = 0;
  while ((int32_t)(deadline - millis()) > 0) {
    int ch = c.read();
    if (ch < 0) { if (!c.connected()) break; continue; }
    if (ch == '\n') { buf[n] = 0; return n; }
    if (ch != '\r' && n < len - 1) buf[n++] = ch;
  }
  return -1;
}

static bool getParam(const char* q, const char* key, char* out, uint8_t outLen) {
  uint8_t kl = strlen(key);
  while (q && *q) {
    if (!strncmp(q, key, kl) && q[kl] == '=') {
      q += kl + 1;
      uint8_t n = 0;
      while (*q && *q != '&' && n < outLen - 1) {
        char c = *q++;
        if (c == '+') c = ' ';
        else if (c == '%' && isxdigit(q[0]) && isxdigit(q[1])) {
          char h[3] = { q[0], q[1], 0 };
          c = (char)strtol(h, nullptr, 16);
          q += 2;
        }
        out[n++] = c;
      }
      out[n] = 0;
      return true;
    }
    q = strchr(q, '&');
    if (q) q++;
  }
  return false;
}

static bool getLong(const char* q, const char* key, long& v, int base = 10) {
  char buf[16];
  if (!getParam(q, key, buf, sizeof(buf)) || !buf[0]) return false;
  char* end;
  v = strtol(buf, &end, base);
  return *end == 0;
}

static void header(Print& o, uint16_t code, const __FlashStringHelper* type, int32_t len = -1, bool gzip = false) {
  o.print(F("HTTP/1.1 "));
  o.print(code);
  switch (code) {
    case 200: o.println(F(" OK")); break;
    case 400: o.println(F(" Bad Request")); break;
    case 403: o.println(F(" Forbidden")); break;
    case 404: o.println(F(" Not Found")); break;
    default:  o.println(F(" Error")); break;
  }
  o.print(F("Content-Type: ")); o.println(type);
  if (len >= 0) { o.print(F("Content-Length: ")); o.println(len); }
  if (gzip) o.println(F("Content-Encoding: gzip"));
  o.println(F("Cache-Control: no-store\r\nConnection: close\r\n"));
}

// Pagina: il browser la tiene in cache e chiede solo se e' cambiata (ETag = versione)
static void pageHeader(Print& o, uint16_t code, uint32_t len, uint32_t ver) {
  o.print(F("HTTP/1.1 "));
  o.println(code == 304 ? F("304 Not Modified") : F("200 OK"));
  o.print(F("ETag: \"")); o.print(ver); o.println('"');
  o.println(F("Cache-Control: no-cache"));
  if (code == 200) {
    o.print(F("Content-Type: text/html; charset=utf-8\r\nContent-Encoding: gzip\r\nContent-Length: "));
    o.println(len);
  }
  o.println(F("Connection: close\r\n"));
}

static void jsonStr(Print& o, const char* s) {
  o.write('"');
  for (; *s; s++) {
    uint8_t c = *s;
    if (c == '"' || c == '\\') { o.write('\\'); o.write(c); }
    else if (c < 0x20) o.print(' ');
    else o.write(c);
  }
  o.write('"');
}

static void replyOk(Print& o) {
  header(o, 200, F("application/json"));
  o.print(F("{\"ok\":true}"));
}

static void replyErr(Print& o, uint16_t code, const char* msg) {
  header(o, code, F("application/json"));
  o.print(F("{\"ok\":false,\"err\":"));
  jsonStr(o, msg);
  o.print('}');
}

static void replyErrP(Print& o, uint16_t code, const __FlashStringHelper* msg) {
  char buf[48];
  strncpy_P(buf, (const char*)msg, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  replyErr(o, code, buf);
}

static void printIp(Print& o, IPAddress ip) {
  o.print('"'); o.print(ip); o.print('"');
}

// Riceve il corpo della richiesta e lo scrive nella destinazione
// Il tempo limite vale dall'ultimo dato ricevuto: la pagina web (~118 KB) scritta
// sulla microSD puo' richiedere piu' di qualche secondo, ma non si resta mai appesi.
// Intanto il PLC continua a girare (se era in RUN).
static bool receiveBody(NetClient& c, ImgSink* sink, int32_t len) {
  uint8_t buf[64];                 // piccolo: sul Mega lo stack qui e' gia' profondo (SD + scan)
  uint32_t last = millis();
  bool ok = sink != nullptr;
  while (len > 0 && millis() - last < 5000UL) {
    int avail = c.available();
    if (avail <= 0) { if (!c.connected()) break; Engine::scan(); continue; }
    int n = c.read(buf, min((int32_t)sizeof(buf), min((int32_t)avail, len)));
    if (n <= 0) continue;
    last = millis();
    if (ok && !sink->write(buf, n)) ok = false;
    len -= n;
    Engine::scan();
  }
  return ok && len == 0;
}

static void streamSource(Print& o, ImgSource& src) {
  uint16_t size = src.size();
  header(o, 200, F("application/octet-stream"), size);
  uint8_t buf[64];
  for (uint16_t off = 0; off < size; off += sizeof(buf)) {
    uint16_t n = min((uint16_t)sizeof(buf), (uint16_t)(size - off));
    if (!src.read(off, buf, n)) break;
    o.write(buf, n);
  }
}

// ---------------------------------------------------------------------
//  Endpoint
// ---------------------------------------------------------------------
#if !NET_BRIDGE
// versione della pagina sulla SD (0 = assente o incompleta)
static uint32_t sdPageVersion() {
  if (!sdExists(PAGE_PATH)) return 0;
  FileSource v(PAGE_VER_PATH);
  char buf[12] = {0};
  if (!v.ok() || !v.readAt(0, buf, min((uint32_t)sizeof(buf) - 1, v.length()))) return 0;
  return strtoul(buf, nullptr, 10);
}

static void sendPage(Out& o, Req& r) {
  uint32_t sdVer = sdPageVersion();
  FileSource f(PAGE_PATH);
  if (!f.ok() || !f.length()) sdVer = 0;     // versione senza pagina: si ignora
#if HAS_EMBEDDED_PAGE
  if (sdVer <= WEB_PAGE_VER) {
    if (r.etag == WEB_PAGE_VER) return pageHeader(o, 304, 0, WEB_PAGE_VER);
    pageHeader(o, 200, WEB_PAGE_GZ_LEN, WEB_PAGE_VER);
    uint8_t buf[64];
    for (uint32_t off = 0; off < WEB_PAGE_GZ_LEN && o.alive(); off += sizeof(buf)) {
      uint16_t n = min((uint32_t)sizeof(buf), WEB_PAGE_GZ_LEN - off);
      webPageRead(buf, off, n);
      o.write(buf, n);
    }
    return;
  }
#endif
  if (!sdVer) {
    header(o, 200, F("text/html; charset=utf-8"));
    o.print(F("<!doctype html><meta charset=utf-8><title>ArduLearn</title><body style='font:16px sans-serif;margin:40px'>"
              "<h2>ArduLearn</h2><p>"));
    o.print(g_sdOk ? F("La pagina non &egrave; ancora sulla microSD.") : F("Inserisci la microSD nella shield e riavvia la scheda."));
    o.print(F("</p><p>Apri il programma <b>ArduLearn</b> sul PC e usa <b>Prepara la microSD</b>.</p>"));
    return;
  }
  if (r.etag == sdVer) return pageHeader(o, 304, 0, sdVer);
  uint32_t len = f.length();
  pageHeader(o, 200, len, sdVer);
  o.send();
  // pagina grande (~100 KB): il PLC continua a girare mentre la si invia
  uint8_t buf[64];                 // Out spedisce da solo ogni 128 byte
  for (uint32_t off = 0; off < len; off += sizeof(buf)) {
    uint16_t n = min((uint32_t)sizeof(buf), len - off);
    if (!f.readAt(off, buf, n) || !o.alive()) break;
    o.write(buf, n);
    Engine::scan();
  }
}
#endif

static void sendInfo(Print& o) {
  header(o, 200, F("application/json"));
  o.print(F("{\"fw\":\"" FW_VERSION "\",\"board\":\"" BOARD_NAME "\",\"host\":")); jsonStr(o, g_hostname);
  o.print(F(",\"mac\":\""));
  for (uint8_t i = 0; i < 6; i++) {
    if (i) o.print(':');
    if (cfg.mac[i] < 16) o.print('0');
    o.print(cfg.mac[i], HEX);
  }
  o.print(F("\",\"ip\":")); printIp(o, netIP());
  o.print(F(",\"net\":")); o.print(g_netState);
#if NET_BRIDGE
  // UNO R4 WiFi: "sd", "pv", "dr" e "wifi" li aggiunge l'ESP32 (slot, pagina, bozza e rete sono li')
  o.print(F(",\"prog\":")); jsonStr(o, Engine::name);
#else
  o.print(F(",\"sd\":")); o.print(g_sdOk);
#endif
  o.print(F(",\"rev\":")); o.print(cfg.rev);
#if !NET_BRIDGE
  o.print(F(",\"dr\":")); o.print(draftRev);
#endif
  o.print(F(",\"run\":")); o.print(Engine::running);
  o.print(F(",\"ld\":")); o.print(Engine::loaded);
  o.print(F(",\"err\":")); jsonStr(o, Engine::error);
  o.print(F(",\"maxBlocks\":")); o.print(MAX_BLOCKS);
  o.print(F(",\"maxImage\":")); o.print(maxImage());
  o.print(F(",\"maxText\":")); o.print(MAX_POOL);
  o.print(F(",\"ver\":")); o.print(IMG_VERSION);
#if HAS_MODULES
  o.print(F(",\"mods\":")); printModuleList(o);
#else
  o.print(F(",\"mods\":[]"));
#endif
  o.print(F(",\"np\":")); o.print(NUM_PINS);
  o.print(F(",\"fa\":")); o.print(FIRST_APIN);
  o.print(F(",\"nmb\":")); o.print(NUM_MBITS);
  o.print(F(",\"nmw\":")); o.print(NUM_MWORDS);
  o.print(F(",\"nsv\":")); o.print(SCRIPT_VARS);
#if HAS_LED_MATRIX
  o.print(F(",\"mtx\":1"));                       // matrice LED 12x8: blocchi 44 e 45
#endif
#if !NET_BRIDGE
  o.print(F(",\"pv\":")); o.print(sdPageVersion());
#endif
  o.print(F(",\"pinDefault\":")); o.print(strcmp_P(cfg.pin, PSTR("1234")) == 0);
  o.print(F(",\"oled\":{\"a\":")); o.print(cfg.oledAddr);
  o.print(F(",\"t\":")); o.print(cfg.oledType);
  o.print(F(",\"ok\":")); o.print(oledPresent());
  o.print(F("},\"lcd\":{\"t\":")); o.print(cfg.lcdType);
  o.print(F(",\"a\":")); o.print(cfg.lcdAddr);
  o.print(F(",\"c\":")); o.print(cfg.lcdCols);
  o.print(F(",\"r\":")); o.print(cfg.lcdRows);
  o.print(F(",\"p\":["));
  for (uint8_t i = 0; i < 6; i++) { if (i) o.print(','); o.print(cfg.lcdPins[i]); }
  o.print(F("],\"ok\":")); o.print(lcdPresent());
  o.print(F("},\"res\":["));
  bool first = true;
  for (uint8_t p = 0; p < NUM_PINS; p++) {
    if (!Engine::isReserved(p)) continue;
    if (!first) o.print(',');
    o.print(p);
    first = false;
  }
  o.print(F("]}"));
}

static void sendLive(Print& o) {
  header(o, 200, F("application/json"));
  o.print(F("{\"rev\":")); o.print(cfg.rev);
  o.print(F(",\"run\":")); o.print(Engine::running);
  o.print(F(",\"ld\":")); o.print(Engine::loaded);
  o.print(F(",\"scan\":")); o.print(Engine::scanUs);
  o.print(F(",\"smax\":")); o.print(Engine::scanMaxUs);
  Engine::scanMaxUs = 0;
  o.print(F(",\"up\":")); o.print(millis() / 1000);
  o.print(F(",\"mem\":")); o.print(freeRam());
  o.print(F(",\"oled\":")); o.print(oledPresent());
  o.print(F(",\"lcd\":")); o.print(lcdPresent());
  o.print(F(",\"err\":")); jsonStr(o, Engine::error);
  o.print(F(",\"so\":")); o.print(Engine::scriptOverrun);
#if !NET_BRIDGE
  o.print(F(",\"dr\":")); o.print(draftRev);
#endif
  o.print(F(",\"v\":["));
  for (uint8_t i = 0; i < Engine::nBlocks; i++) {
    if (i) o.print(',');
    o.print(Engine::vals[i][0]); o.print(','); o.print(Engine::vals[i][1]);
  }
  // stato digitale di tutti i pin, 1 bit per pin, in esadecimale
  o.print(F("],\"d\":\""));
  for (uint8_t base = 0; base < NUM_PINS; base += 8) {
    uint8_t byte = 0;
    for (uint8_t b = 0; b < 8 && base + b < NUM_PINS; b++) {
      uint8_t p = base + b;
      if (!Engine::isReserved(p) && digitalRead(p)) byte |= 1 << b;
    }
    if (byte < 16) o.print('0');
    o.print(byte, HEX);
  }
  o.print(F("\",\"mb\":\""));                      // memorie %M, 1 bit ciascuna
  for (uint8_t i = 0; i < NUM_MBITS / 8; i++) {
    if (Engine::mbits[i] < 16) o.print('0');
    o.print(Engine::mbits[i], HEX);
  }
  o.print(F("\",\"mw\":["));
  for (uint8_t i = 0; i < NUM_MWORDS; i++) { if (i) o.print(','); o.print(Engine::mwords[i]); }
  o.print(F("],\"pg\":["));
  o.print(Engine::pageOled); o.print(','); o.print(Engine::pageLcd);
  o.print(F("],\"a\":["));
  for (uint8_t i = 0; i < NUM_PINS - FIRST_APIN; i++) {
    if (i) o.print(',');
    uint8_t p = FIRST_APIN + i;
    if (Engine::pinModes[p] == PM_ANALOG) o.print(analogRead(p));
    else o.print(-1);
  }
#if HAS_LED_MATRIX
  o.print(F("],\"pm\":")); o.print(Engine::pageMtx);
  o.print(F(",\"mx\":\"")); ledHex(o); o.print(F("\"}"));       // matrice LED: per l'anteprima nella pagina
#else
  o.print(F("]}"));
#endif
}

static void reloadProgram() {
  ImgSource* src = openActive();
  Engine::load(*src);
  delete src;
}

static void postProgram(Print& o, NetClient& c, Req& r) {
  if (!maxImage()) return replyErrP(o, 400, F("Serve la microSD"));
  if (r.contentLength < IMG_HDR_LEN || r.contentLength > maxImage())
    return replyErrP(o, 400, F("Dimensione non valida"));

  Engine::setRunning(false);
  ImgSink* sink = openStaging(r.contentLength);
  bool ok = receiveBody(c, sink, r.contentLength);
  delete sink;
  if (!ok) { Engine::setRunning(cfg.run); return replyErrP(o, 400, F("Ricezione non riuscita")); }

  // controlla il file appena scritto prima di renderlo attivo
  char err[48];
  ImgSource* staged = g_sdOk ? (ImgSource*)new FileSource(stagingPath()) : (ImgSource*)new EepromSource();
  ok = Engine::validate(*staged, err, sizeof(err));
  delete staged;
  if (!ok) { Engine::setRunning(cfg.run); return replyErr(o, 400, err); }

  commitStaging();
  reloadProgram();
  displaysShowStatus();
  replyOk(o);
}

// Descrizione di un programma: {"name":..,"n":blocchi,"len":byte} oppure null
static void progJson(Print& o, ImgSource& src) {
  uint8_t h[40];
  uint16_t len = src.size();
  if (len < IMG_HDR_LEN || !src.read(0, h, sizeof(h)) || h[0] != 'P' || h[1] != 'B') { o.print(F("null")); return; }
  char name[23];
  memcpy(name, h + 18, 22);
  name[22] = 0;
  o.print(F("{\"name\":")); jsonStr(o, name);
  o.print(F(",\"n\":")); o.print(h[3]);
  o.print(F(",\"len\":")); o.print(len);
  o.print('}');
}

// Programmi nella memoria della scheda: quello attivo sulla microSD e la copia in EEPROM.
// "act": da dove e' stato caricato il programma in esecuzione (sd / ee / none)
static void sendStored(Print& o) {
  header(o, 200, F("application/json"));
  FileSource sd(activePath());
  EepromSource ee;
  bool onSd = sd.ok() && sd.size() >= IMG_HDR_LEN;
  o.print(F("{\"act\":\""));
  o.print(!Engine::loaded ? F("none") : onSd ? F("sd") : F("ee"));
  o.print(F("\",\"sd\":"));
  if (onSd) progJson(o, sd); else o.print(F("null"));
  o.print(F(",\"ee\":"));
  progJson(o, ee);
  o.print(F(",\"eeMax\":")); o.print(MAX_IMAGE_EE);
  o.print('}');
}

// what=sd: cancella il programma attivo della microSD; what=ee: la copia in EEPROM.
// Poi si ricarica: se resta l'altra copia parte quella, altrimenti la scheda non ha programma.
static void postErase(Print& o, Req& r) {
  char what[4];
  if (!getParam(r.query, "what", what, sizeof(what))) return replyErrP(o, 400, F("Parametro mancante"));
  if (!strcmp_P(what, PSTR("sd"))) {
    if (!g_sdOk) return replyErrP(o, 400, F("Scheda SD assente"));
    Engine::setRunning(false);
    sdRemove(activePath());
  } else if (!strcmp_P(what, PSTR("ee"))) {
    eeErase();
  } else return replyErrP(o, 400, F("Parametro non valido"));
  cfg.rev++;
  cfgSave();
  reloadProgram();
  displaysShowStatus();
  replyOk(o);
}

#if HAS_SD
static void sendSlots(Print& o) {
  header(o, 200, F("application/json"));
  o.print(F("{\"sd\":")); o.print(g_sdOk);
  o.print(F(",\"slots\":["));
  bool first = true;
  char fn[13], path[20];
  uint32_t size;
  SdDir dir("/PB");
  while (dir.next(fn, size)) {
    if (!(fn[0] == 'S' && isdigit(fn[1]) && isdigit(fn[2]) && fn[3] == '.')) continue;   // "S07.PB"
    char name[25] = {0};
    snprintf_P(path, sizeof(path), PSTR("/PB/%s"), fn);
    FileSource f(path);
    if (f.length() >= 40) f.readAt(18, name, 22);
    name[24] = 0;
    if (!first) o.print(',');
    first = false;
    o.print(F("{\"n\":")); o.print((fn[1] - '0') * 10 + (fn[2] - '0'));
    o.print(F(",\"name\":")); jsonStr(o, name);
    o.print(F(",\"len\":")); o.print(size);
    o.print('}');
  }
  o.print(F("]}"));
}

#endif

#if HAS_SD
static bool slotNumber(const char* q, char* path) {
  long n;
  if (!getLong(q, "n", n) || n < 1 || n > 99) return false;
  slotPath(n, path);
  return true;
}

static void getSlot(Print& o, Req& r) {
  char path[16];
  if (!g_sdOk) return replyErrP(o, 404, F("Scheda SD assente"));
  if (!slotNumber(r.query, path)) return replyErrP(o, 400, F("Slot non valido"));
  FileSource src(path);
  if (!src.ok()) return replyErrP(o, 404, F("Slot vuoto"));
  streamSource(o, src);
}

// ---------------------------------------------------------------------
//  Bozza condivisa: il progetto che il docente sta modificando (anche non
//  ancora caricato), salvato su microSD; gli altri browser lo seguono.
//  draftRev cambia a ogni salvataggio (0 = nessuna bozza).
// ---------------------------------------------------------------------

static void postDraft(Print& o, NetClient& c, Req& r) {
  if (!g_sdOk) return replyErrP(o, 400, F("Serve la microSD per condividere la bozza"));
  if (r.contentLength < IMG_HDR_LEN || r.contentLength > MAX_IMAGE_SD) return replyErrP(o, 400, F("Dimensione non valida"));
  ImgSink* sink = openFileWrite(DRAFT_PATH);
  bool ok = receiveBody(c, sink, r.contentLength);
  delete sink;
  if (!ok) return replyErrP(o, 400, F("Scrittura non riuscita"));
  if (++draftRev == 0) draftRev = 1;
  header(o, 200, F("application/json"));
  o.print(F("{\"ok\":true,\"dr\":")); o.print(draftRev); o.print('}');
}

static void getDraft(Print& o) {
  if (!g_sdOk) return replyErrP(o, 404, F("Nessuna bozza"));
  FileSource src(DRAFT_PATH);
  if (!src.ok() || src.size() < IMG_HDR_LEN) return replyErrP(o, 404, F("Nessuna bozza"));
  streamSource(o, src);
}

// Svuota la microSD: cancella i file di ArduLearn (slot, bozza e, con all=1, anche il programma attivo).
// I file si cancellano uno alla volta riaprendo la cartella: non serve memoria per l'elenco.
static void postSdWipe(Print& o, Req& r) {
  if (!g_sdOk) return replyErrP(o, 400, F("Scheda SD assente"));
  long all = 0;
  getLong(r.query, "all", all);
  const char* keep = all ? "" : activePath() + 4;          // nome senza "/PB/"
  uint16_t removed = 0;
  for (;;) {
    char fn[13], path[20];
    uint32_t size;
    bool found = false;
    {
      SdDir dir("/PB");                                      // chiusa prima di cancellare
      while (!found && dir.next(fn, size)) found = strcmp(fn, keep) != 0;
    }
    if (!found) break;
    snprintf_P(path, sizeof(path), PSTR("/PB/%s"), fn);
    if (!sdRemove(path)) break;
    removed++;
  }
  draftRev = 0;
  if (all) { cfg.activeAB = 0; cfgSave(); }
  header(o, 200, F("application/json"));
  o.print(F("{\"ok\":true,\"removed\":")); o.print(removed); o.print('}');
}

// File della pagina web sulla microSD (li invia l'EXE): name = INDEX.GZ o INDEX.VER.
// Mentre arriva la pagina la versione viene tolta: una pagina a meta' non si usa mai.
static void postFile(Print& o, NetClient& c, Req& r) {
  char name[13], path[24];
  if (!g_sdOk) return replyErrP(o, 400, F("Scheda SD assente"));
  if (!getParam(r.query, "name", name, sizeof(name)) || (strcmp_P(name, PSTR("INDEX.GZ")) && strcmp_P(name, PSTR("INDEX.VER"))))
    return replyErrP(o, 400, F("Nome non valido"));
  if (r.contentLength <= 0) return replyErrP(o, 400, F("Dimensione non valida"));
  snprintf_P(path, sizeof(path), PSTR("/PLC/%s"), name);
  sdMkdir("/PLC");                          // SD.open non crea la cartella
  if (name[6] == 'G') sdRemove(PAGE_VER_PATH);
  ImgSink* sink = openFileWrite(path);
  bool ok = receiveBody(c, sink, r.contentLength);
  delete sink;
  if (!ok) return replyErrP(o, 400, F("Scrittura non riuscita"));
  replyOk(o);
}

static void postSlot(Print& o, NetClient& c, Req& r) {
  char path[16], err[48];
  if (!g_sdOk) return replyErrP(o, 400, F("Scheda SD assente"));
  if (!slotNumber(r.query, path)) return replyErrP(o, 400, F("Slot non valido"));
  if (r.contentLength < IMG_HDR_LEN || r.contentLength > maxImage())
    return replyErrP(o, 400, F("Dimensione non valida"));
  ImgSink* sink = openFileWrite(path);
  bool ok = receiveBody(c, sink, r.contentLength);
  delete sink;
  if (ok) {
    FileSource src(path);
    ok = Engine::validate(src, err, sizeof(err));
  } else {
    strcpy_P(err, PSTR("Scrittura non riuscita"));
  }
  if (!ok) { sdRemove(path); return replyErr(o, 400, err); }
  replyOk(o);
}
#endif

static void postConfig(Print& o, Req& r) {
  long v;
  char buf[24];
  bool displays = false;
  if (getLong(r.query, "oled", v) && v >= 0x08 && v <= 0x77) { cfg.oledAddr = v; displays = true; }
  if (getLong(r.query, "oledt", v) && v >= 0 && v <= 1) { cfg.oledType = v; displays = true; }
#if HAS_LCD
  if (getLong(r.query, "lcdt", v) && v >= LCD_NONE && v <= LCD_PARALLEL) { cfg.lcdType = v; displays = true; }
  if (getLong(r.query, "lcda", v) && v >= 0x08 && v <= 0x77) { cfg.lcdAddr = v; displays = true; }
  if (getLong(r.query, "lcdc", v) && (v == 16 || v == 20)) { cfg.lcdCols = v; displays = true; }
  if (getLong(r.query, "lcdr", v) && v >= 1 && v <= 4) { cfg.lcdRows = v; displays = true; }
  if (getParam(r.query, "lcdp", buf, sizeof(buf))) {
    uint8_t pins[6], n = 0;
    for (char* t = strtok(buf, ","); t && n < 6; t = strtok(nullptr, ",")) {
      long p = atol(t);
      bool fixed = false;
      for (uint8_t q : RESERVED_PINS) if (q == p) fixed = true;
      if (p < 2 || p >= NUM_PINS || fixed) return replyErrP(o, 400, F("Pin LCD non valido"));
      pins[n++] = p;
    }
    if (n != 6) return replyErrP(o, 400, F("Servono 6 pin per l'LCD"));
    memcpy(cfg.lcdPins, pins, 6);
    displays = true;
  }
#endif
  if (getParam(r.query, "host", buf, sizeof(buf))) {
    uint8_t n = strlen(buf);
    if (n < 1 || n >= sizeof(cfg.host)) return replyErrP(o, 400, F("Nome non valido"));
    for (uint8_t i = 0; i < n; i++)
      if (!isalnum(buf[i]) && buf[i] != '-') return replyErrP(o, 400, F("Nome: solo lettere, numeri e -"));
    strcpy(cfg.host, buf);
    netHostnameChanged();
  }
  if (getParam(r.query, "newpin", buf, sizeof(buf))) {
    uint8_t n = strlen(buf);
    if (n < 4 || n >= sizeof(cfg.pin)) return replyErrP(o, 400, F("Il PIN deve avere 4-8 caratteri"));
    strcpy(cfg.pin, buf);
  }
  cfgSave();
  if (displays) {
    displaysBegin();
    reloadProgram();       // i pin dell'LCD parallelo potrebbero confliggere col programma
  }
  replyOk(o);
}

static void sendScan(Print& o) {
  header(o, 200, F("application/json"));
  o.print(F("{\"found\":["));
  bool first = true;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      if (!first) o.print(',');
      o.print(a);
      first = false;
    }
  }
  o.print(F("]}"));
}

// ---------------------------------------------------------------------
//  Instradamento
// ---------------------------------------------------------------------
static bool is(const char* path, const char* p) { return strcmp_P(path, p) == 0; }

static void route(Out& o, NetClient& c, Req& r) {
  bool get = !strcmp_P(r.method, PSTR("GET"));
  bool post = !strcmp_P(r.method, PSTR("POST"));
  const char* p = r.path;
  long a, b;

#if !NET_BRIDGE
  if (get && is(p, PSTR("/")))            return sendPage(o, r);
#endif
  if (get && is(p, PSTR("/api/info")))    return sendInfo(o);
  if (get && is(p, PSTR("/api/live")))    return sendLive(o);
#if HAS_SD
  if (get && is(p, PSTR("/api/slots")))   return sendSlots(o);
#endif
  if (get && is(p, PSTR("/api/stored")))  return sendStored(o);
#if HAS_SD
  if (get && is(p, PSTR("/api/draft")))   return getDraft(o);
  if (get && is(p, PSTR("/api/slot")))    return getSlot(o, r);
#endif
  if (get && is(p, PSTR("/api/scan")))    return sendScan(o);
#if HAS_MODULES
  if (get && is(p, PSTR("/api/rtc"))) {
    uint8_t t[7];
    if (!rtcRead || !rtcRead(t)) return replyErrP(o, 404, F("Orologio RTC non trovato"));
    header(o, 200, F("application/json"));
    char buf[64];
    snprintf_P(buf, sizeof(buf), PSTR("{\"ok\":true,\"t\":\"20%02u-%02u-%02u %02u:%02u:%02u\",\"dow\":%u}"), t[6], t[5], t[4], t[2], t[1], t[0], t[3]);
    o.print(buf);
    return;
  }
#endif
  if (get && is(p, PSTR("/api/program"))) {
    char s[4] = "";
    getParam(r.query, "src", s, sizeof(s));            // src=ee: la copia in EEPROM
    ImgSource* src = strcmp_P(s, PSTR("ee")) ? openActive() : new EepromSource();
    if (src->size() == 0) replyErrP(o, 404, F("Nessun programma"));
    else streamSource(o, *src);
    delete src;
    return;
  }
  if (get && is(p, PSTR("/api/auth"))) {
    header(o, 200, F("application/json"));
    o.print(r.authed ? F("{\"ok\":true}") : F("{\"ok\":false}"));
    return;
  }

  // interazione libera per tutti: ingressi virtuali e prova delle uscite
  if (post && is(p, PSTR("/api/vset"))) {
    if (!getLong(r.query, "b", a) || !getLong(r.query, "v", b) || a < 0 || a >= MAX_BLOCKS || !Engine::setVirtual(a, b))
      return replyErrP(o, 400, F("Blocco non valido"));
    return replyOk(o);
  }
  if (post && is(p, PSTR("/api/pinset"))) {
    if (!getLong(r.query, "p", a) || !getLong(r.query, "v", b) || a < 0 || a >= NUM_PINS || !Engine::manualPin(a, b != 0))
      return replyErrP(o, 400, F("Pin non comandabile"));
    return replyOk(o);
  }

  // da qui in poi serve il PIN docente
  if (post && !r.authed) return replyErrP(o, 403, F("PIN docente errato"));
  if (post && is(p, PSTR("/api/program"))) return postProgram(o, c, r);
#if HAS_SD
  if (post && is(p, PSTR("/api/slot")))    return postSlot(o, c, r);
  if (post && is(p, PSTR("/api/draft"))) return postDraft(o, c, r);
  if (post && is(p, PSTR("/api/sdwipe"))) return postSdWipe(o, r);
  if (post && is(p, PSTR("/api/file")))   return postFile(o, c, r);
#endif
  if (post && is(p, PSTR("/api/erase")))  return postErase(o, r);
  if (post && is(p, PSTR("/api/eecopy"))) {
    if (!eeCopyActive()) return replyErrP(o, 400, F("Copia non riuscita: programma assente o piu' grande della EEPROM"));
    return replyOk(o);
  }
#if HAS_SD
  if (post && is(p, PSTR("/api/slotdel"))) {
    char path[16];
    if (!g_sdOk || !slotNumber(r.query, path)) return replyErrP(o, 400, F("Slot non valido"));
    sdRemove(path);
    return replyOk(o);
  }
#endif
  if (post && is(p, PSTR("/api/run"))) {
    if (!getLong(r.query, "s", a)) return replyErrP(o, 400, F("Parametro mancante"));
    cfg.run = a ? 1 : 0;
    cfgSave();
    Engine::setRunning(cfg.run);
    return replyOk(o);
  }
  if (post && is(p, PSTR("/api/config"))) return postConfig(o, r);
#if HAS_MODULES
  if (post && is(p, PSTR("/api/rtc"))) {
    long v[7];
    const char* keys[7] = { "y", "mo", "d", "h", "mi", "s", "dow" };
    for (uint8_t i = 0; i < 7; i++) if (!getLong(r.query, keys[i], v[i])) return replyErrP(o, 400, F("Parametri mancanti"));
    if (!rtcSet || !rtcSet(v[0] % 100, v[1], v[2], v[3], v[4], v[5], v[6])) return replyErrP(o, 404, F("Orologio RTC non trovato"));
    return replyOk(o);
  }
#endif

  replyErrP(o, 404, F("Non trovato"));
}

void webBegin() {
  server.begin();
  if (sdExists(DRAFT_PATH)) draftRev = 1;
}


// Wi-Fi: la rete e' cambiata (rete della scuola o rete della scheda): si riapre il server
void webNetUp() { server.begin(); }

void webTick() {
  NetClient c = server.available();
  if (!c) return;

  Req r;
  memset(&r, 0, sizeof(r));
  char line[128];
  uint32_t deadline = millis() + 3000;

  // "GET /api/live?x=1 HTTP/1.1"
  if (readLine(c, line, sizeof(line), deadline) <= 0) { c.stop(); return; }
  char* sp1 = strchr(line, ' ');
  char* sp2 = sp1 ? strchr(sp1 + 1, ' ') : nullptr;
  if (!sp1 || !sp2) { c.stop(); return; }
  *sp1 = 0; *sp2 = 0;
  strncpy(r.method, line, sizeof(r.method) - 1);
  char* target = sp1 + 1;
  char* q = strchr(target, '?');
  if (q) { *q = 0; strncpy(r.query, q + 1, sizeof(r.query) - 1); }
  strncpy(r.path, target, sizeof(r.path) - 1);

  // intestazioni: servono solo Content-Length e X-Pin
  for (;;) {
    int n = readLine(c, line, sizeof(line), deadline);
    if (n < 0) { c.stop(); return; }
    if (n == 0) break;
    if (!strncasecmp_P(line, PSTR("Content-Length:"), 15)) r.contentLength = atol(line + 15);
    else if (!strncasecmp_P(line, PSTR("X-Pin:"), 6)) {
      char* v = line + 6;
      while (*v == ' ') v++;
      r.authed = strcmp(v, cfg.pin) == 0;
    } else if (!strncasecmp_P(line, PSTR("If-None-Match:"), 14)) {
      const char* v = strchr(line + 14, '"');
      if (v) r.etag = strtoul(v + 1, nullptr, 10);
    }
  }

  {
    Out o(c);
    route(o, c, r);
  }
  // scarta un eventuale corpo non letto, poi chiude
  uint32_t t = millis();
  while (c.available() && millis() - t < 200) c.read();
  c.stop();
}

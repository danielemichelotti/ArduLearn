#include "web.h"
#include "bridge_config.h"
#include "link.h"
#include "wifimgr.h"
#include "ota.h"
#include <Update.h>
#include <Preferences.h>
#include <esp_http_server.h>
#include <lwip/sockets.h>
#include <LittleFS.h>

// =====================================================================
//  Server web dell'UNO R4 WiFi (sull'ESP32)
//  - pagina: /PLC/INDEX.GZ nella flash dell'ESP32 (versione in /PLC/INDEX.VER, ETag);
//  - gestite qui: Wi-Fi, slot 1..99 e bozza condivisa (/PB/... come sulla microSD del
//    Mega), file della pagina, e la copia di /api/live per tutti i browser;
//  - tutto il resto (/api/info, program, run, vset, config, ...) passa al RA4M1.
//  Le API sono le stesse del Mega: la pagina e l'app ArduLearn non cambiano.
// =====================================================================
static httpd_handle_t srv = nullptr;
volatile const char* webWhere = "fermo";
static char webUri[48];

static const char PAGE_PATH[] = "/PLC/INDEX.GZ";
static const char PAGE_VER_PATH[] = "/PLC/INDEX.VER";
static const char DRAFT_PATH[] = "/PB/DRAFT.PB";
static const char STAGE_PATH[] = "/PB/STAGE.PB";   // programma in arrivo, prima di passarlo al RA4M1
static const uint16_t IMG_HDR_LEN = 110;       // come config.h del firmware ArduLearn
static const int32_t MAX_SLOT = 8192, MAX_DRAFT = 16384, MAX_FILE = 900 * 1024;
static uint16_t draftRev = 0;

// copia dello stato del PLC (/api/live) e dati dell'ultima /api/info
static SemaphoreHandle_t cacheMtx;
static char     liveBuf[8192];
static size_t   liveLen = 0;
static uint32_t liveAt = 0, lastLiveReq = 0, lastInfoAt = 0;
static char     host[32] = "", prog[24] = "";
static bool     running = false;
static int      lastRev = -1;
static bool     infoWanted = true;
bool webStress = false;           // "wifi stress": /api/live sempre ogni 200 ms (prova del collegamento)

// ---------------------------------------------------------------------
//  Utilita'
// ---------------------------------------------------------------------
static const char* statusText(int code) {
  switch (code) {
    case 200: return "200 OK";
    case 304: return "304 Not Modified";
    case 400: return "400 Bad Request";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 503: return "503 Service Unavailable";
    case 504: return "504 Gateway Timeout";
  }
  return "500 Internal Server Error";
}

static void jsonStr(String& o, const char* s) {
  o += '"';
  for (; *s; s++) {
    uint8_t c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) o += ' ';
    else o += (char)c;
  }
  o += '"';
}

static esp_err_t sendJson(httpd_req_t* req, int code, const String& body) {
  httpd_resp_set_status(req, statusText(code));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, body.c_str(), body.length());
}

static esp_err_t replyOk(httpd_req_t* req) { return sendJson(req, 200, "{\"ok\":true}"); }

static esp_err_t replyErr(httpd_req_t* req, int code, const char* msg) {
  String o = "{\"ok\":false,\"err\":";
  jsonStr(o, msg);
  o += '}';
  return sendJson(req, code, o);
}

// parametro della query, decodificato (%xx e +)
static bool getParam(httpd_req_t* req, const char* key, char* out, size_t outLen) {
  char q[600];
  char raw[200];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return false;
  if (httpd_query_key_value(q, key, raw, sizeof(raw)) != ESP_OK) return false;
  size_t n = 0;
  for (const char* p = raw; *p && n < outLen - 1; p++) {
    char c = *p;
    if (c == '+') c = ' ';
    else if (c == '%' && isxdigit((uint8_t)p[1]) && isxdigit((uint8_t)p[2])) {
      char h[3] = { p[1], p[2], 0 };
      c = (char)strtol(h, nullptr, 16);
      p += 2;
    }
    out[n++] = c;
  }
  out[n] = 0;
  return true;
}

static bool getLong(httpd_req_t* req, const char* key, long& v) {
  char b[16];
  if (!getParam(req, key, b, sizeof(b)) || !b[0]) return false;
  char* end;
  v = strtol(b, &end, 10);
  return *end == 0;
}

static void getHeader(httpd_req_t* req, const char* name, char* out, size_t len) {
  out[0] = 0;
  if (httpd_req_get_hdr_value_len(req, name) > 0) httpd_req_get_hdr_value_str(req, name, out, len);
}

// Legge dal browser (attende un po' se i dati tardano)
static int recvSome(httpd_req_t* req, uint8_t* buf, size_t max) {
  for (uint8_t tries = 0; tries < 5; tries++) {
    int n = httpd_req_recv(req, (char*)buf, max);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    return n;
  }
  return -1;
}

// ---------------------------------------------------------------------
//  PIN docente: lo controlla il RA4M1 (e' nella sua configurazione).
//  L'ultimo PIN giusto si ricorda per un minuto per non chiederlo a ogni file.
// ---------------------------------------------------------------------
static char okPin[12] = "";
static uint32_t okPinAt = 0;

static void forgetPin() { okPin[0] = 0; }

static int pinStatus = 0;        // esito dell'ultimo controllo col RA4M1 (< 0 = non risponde)
static bool pinOk(httpd_req_t* req) {
  char pin[12];
  getHeader(req, "X-Pin", pin, sizeof(pin));
  pinStatus = 0;
  if (!pin[0]) return false;
  if (okPin[0] && !strcmp(pin, okPin) && millis() - okPinAt < 60000UL) return true;
  char resp[64];
  struct Ctx { char* out; size_t n; } c = { resp, 0 };
  LinkReq r = { "GET", "/api/auth", pin, 0, nullptr, nullptr,
                [](void* x, const uint8_t* b, size_t n) { Ctx* c = (Ctx*)x; size_t k = min(n, 63 - c->n); memcpy(c->out + c->n, b, k); c->n += k; return true; },
                &c };
  int s = linkHttp(r, 3000);
  pinStatus = s;
  resp[c.n] = 0;
  if (s == 200 && strstr(resp, "\"ok\":true")) {
    strlcpy(okPin, pin, sizeof(okPin));
    okPinAt = millis();
    Preferences p;                          // ultimo PIN giusto: serve se il RA4M1 non risponde (vedi postOta)
    p.begin("ardulearn", false);
    if (p.getString("pin", "") != pin) p.putString("pin", pin);
    p.end();
    return true;
  }
  return false;
}

// Con il RA4M1 muto (firmware guasto) il PIN si confronta con l'ultimo verificato
static bool pinOkOffline(httpd_req_t* req) {
  char pin[12];
  getHeader(req, "X-Pin", pin, sizeof(pin));
  Preferences p;
  p.begin("ardulearn", true);
  String saved = p.getString("pin", "1234");
  p.end();
  return pin[0] && saved == pin;
}

// ---------------------------------------------------------------------
//  Pagina
// ---------------------------------------------------------------------
static uint32_t pageVersion() {
  if (!LittleFS.exists(PAGE_PATH)) return 0;
  File v = LittleFS.open(PAGE_VER_PATH, "r");
  if (!v) return 0;
  char b[16] = { 0 };
  v.readBytes(b, sizeof(b) - 1);
  return strtoul(b, nullptr, 10);
}

static esp_err_t streamFile(httpd_req_t* req, File& f) {
  // niente attese di Nagle: ogni pezzo "chunked" sono tre scritture piccole e il PC ritarda gli ACK
  // (senza, la pagina da 124 KB impiegava ~5 s invece di una frazione di secondo)
  int one = 1;
  setsockopt(httpd_req_to_sockfd(req), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  static uint8_t buf[8192];               // un solo task del server: il buffer statico basta
  for (;;) {
    size_t n = f.read(buf, sizeof(buf));
    if (!n) break;
    if (httpd_resp_send_chunk(req, (const char*)buf, n) != ESP_OK) return ESP_FAIL;
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t sendPage(httpd_req_t* req) {
  uint32_t ver = pageVersion();
  File f;
  if (ver) f = LittleFS.open(PAGE_PATH, "r");
  if (!f) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<!doctype html><meta charset=utf-8><title>ArduLearn</title>"
      "<body style='font:16px sans-serif;margin:40px'><h2>ArduLearn</h2>"
      "<p>La pagina non &egrave; ancora nella memoria della scheda.</p>"
      "<p>Apri il programma <b>ArduLearn</b> sul PC e carica il firmware della UNO R4 WiFi.</p>");
  }
  char etag[20], inm[24];
  snprintf(etag, sizeof(etag), "\"%lu\"", (unsigned long)ver);
  getHeader(req, "If-None-Match", inm, sizeof(inm));
  httpd_resp_set_hdr(req, "ETag", etag);
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  if (!strcmp(inm, etag)) {
    httpd_resp_set_status(req, "304 Not Modified");
    return httpd_resp_send(req, nullptr, 0);
  }
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return streamFile(req, f);
}

// Tutto cio' che non e' /api/: la pagina; sulla rete della scheda qualunque altro indirizzo
// (i controlli "c'e' Internet?" di telefoni e PC) porta alla pagina: portale di configurazione
static esp_err_t pageRoute(httpd_req_t* req);
static esp_err_t pageHandler(httpd_req_t* req) {
  const char* u = req->uri;
  linkLog("pagina %s", u);
  strlcpy(webUri, u, sizeof(webUri));
  webWhere = webUri;
  esp_err_t e = pageRoute(req);
  webWhere = "fermo";
  return e;
}

static esp_err_t pageRoute(httpd_req_t* req) {
  const char* u = req->uri;
  if (!strcmp(u, "/") || !strncmp(u, "/?", 2) || !strcmp(u, "/index.html")) return sendPage(req);
  if (wifiIsAP()) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, nullptr, 0);
  }
  return replyErr(req, 404, "Non trovato");
}

// ---------------------------------------------------------------------
//  File nella flash dell'ESP32: slot, bozza, pagina
// ---------------------------------------------------------------------
static void slotPath(int n, char* out) { snprintf(out, 16, "/PB/S%02d.PB", n); }

static bool slotNumber(httpd_req_t* req, char* path) {
  long n;
  if (!getLong(req, "n", n) || n < 1 || n > 99) return false;
  slotPath(n, path);
  return true;
}

// Riceve il corpo della richiesta in un file (prima in un file temporaneo: un file a meta' non resta)
static bool receiveToFile(httpd_req_t* req, const char* path) {
  static uint8_t buf[2048];
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  int32_t left = req->content_len;
  bool ok = true;
  while (left > 0) {
    int n = recvSome(req, buf, min((int32_t)sizeof(buf), left));
    if (n <= 0 || f.write(buf, n) != (size_t)n) { ok = false; break; }
    left -= n;
  }
  f.close();
  if (!ok) { LittleFS.remove(tmp); return false; }
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

// controllo minimo di un programma: intestazione "PB" e lunghezza dichiarata = lunghezza del file
static bool looksLikeProgram(const char* path) {
  File f = LittleFS.open(path, "r");
  uint8_t h[6];
  if (!f || f.read(h, 6) != 6) return false;
  return h[0] == 'P' && h[1] == 'B' && (size_t)(h[4] | (h[5] << 8)) == f.size();
}

static esp_err_t sendFileBin(httpd_req_t* req, const char* path, const char* missing) {
  File f = LittleFS.open(path, "r");
  if (!f || f.size() < IMG_HDR_LEN) return replyErr(req, 404, missing);
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return streamFile(req, f);
}

static esp_err_t sendSlots(httpd_req_t* req) {
  String o = "{\"sd\":1,\"slots\":[";
  bool first = true;
  for (int n = 1; n <= 99; n++) {
    char path[16];
    slotPath(n, path);
    File f = LittleFS.open(path, "r");
    if (!f) continue;
    char name[23] = { 0 };
    if (f.size() >= 40) { f.seek(18); f.read((uint8_t*)name, 22); }
    if (!first) o += ',';
    first = false;
    o += "{\"n\":"; o += n;
    o += ",\"name\":"; jsonStr(o, name);
    o += ",\"len\":"; o += (unsigned)f.size();
    o += '}';
  }
  o += "]}";
  return sendJson(req, 200, o);
}

static esp_err_t postSlot(httpd_req_t* req) {
  char path[16];
  if (!slotNumber(req, path)) return replyErr(req, 400, "Slot non valido");
  if (req->content_len < IMG_HDR_LEN || (int32_t)req->content_len > MAX_SLOT) return replyErr(req, 400, "Dimensione non valida");
  if (!receiveToFile(req, path)) return replyErr(req, 400, "Scrittura non riuscita");
  if (!looksLikeProgram(path)) { LittleFS.remove(path); return replyErr(req, 400, "Programma non valido"); }
  return replyOk(req);
}

static esp_err_t postDraft(httpd_req_t* req) {
  if (req->content_len < IMG_HDR_LEN || (int32_t)req->content_len > MAX_DRAFT) return replyErr(req, 400, "Dimensione non valida");
  if (!receiveToFile(req, DRAFT_PATH)) return replyErr(req, 400, "Scrittura non riuscita");
  if (++draftRev == 0) draftRev = 1;
  return sendJson(req, 200, String("{\"ok\":true,\"dr\":") + draftRev + "}");
}

// Svuota la memoria: slot e bozza (il programma attivo sta nel RA4M1 e non si tocca)
static esp_err_t postWipe(httpd_req_t* req) {
  int removed = 0;
  for (;;) {
    String victim;
    {
      File d = LittleFS.open("/PB");
      if (!d) break;
      File f = d.openNextFile();
      if (!f) break;
      victim = String("/PB/") + f.name();
    }
    if (!LittleFS.remove(victim)) break;
    removed++;
  }
  draftRev = 0;
  return sendJson(req, 200, String("{\"ok\":true,\"removed\":") + removed + "}");
}

// File della pagina (li invia l'app ArduLearn, come per la microSD del Mega)
static esp_err_t postFile(httpd_req_t* req) {
  char name[16], path[24];
  if (!getParam(req, "name", name, sizeof(name)) || (strcmp(name, "INDEX.GZ") && strcmp(name, "INDEX.VER")))
    return replyErr(req, 400, "Nome non valido");
  if (req->content_len <= 0 || (int32_t)req->content_len > MAX_FILE) return replyErr(req, 400, "Dimensione non valida");
  snprintf(path, sizeof(path), "/PLC/%s", name);
  if (name[6] == 'G') LittleFS.remove(PAGE_VER_PATH);    // una pagina a meta' non si usa mai
  if (!receiveToFile(req, path)) return replyErr(req, 400, "Scrittura non riuscita");
  return replyOk(req);
}

static esp_err_t postWifi(httpd_req_t* req) {
  char ssid[40], pass[70];
  String err;
  if (getParam(req, "clear", ssid, sizeof(ssid))) { wifiForget(); wifiApplyLater(); return replyOk(req); }
  if (!getParam(req, "ssid", ssid, sizeof(ssid))) return replyErr(req, 400, "Manca il nome della rete");
  if (!getParam(req, "pass", pass, sizeof(pass))) pass[0] = 0;
  if (!wifiSetCredentials(ssid, pass, err)) return replyErr(req, 400, err.c_str());
  wifiApplyLater();                                // prima si risponde, poi ci si ricollega
  return replyOk(req);
}

// ---------------------------------------------------------------------
//  Richieste inoltrate al RA4M1
// ---------------------------------------------------------------------
struct ProxyCtx {
  httpd_req_t* req;
  File* src;                // se non nullo, il corpo da inoltrare si legge da qui invece che dal browser
  char type[48];
  bool headSent;
  String* capture;          // se non nullo, la risposta si raccoglie qui invece di mandarla
  int status;
};

static int proxyRead(void* c, uint8_t* buf, size_t max) {
  ProxyCtx* p = (ProxyCtx*)c;
  if (p->src) return p->src->read(buf, max);
  return recvSome(p->req, buf, max);
}

static void proxyHead(void* c, int status, const char* type, int32_t) {
  ProxyCtx* p = (ProxyCtx*)c;
  p->status = status;
  strlcpy(p->type, type, sizeof(p->type));
  if (p->capture) return;
  httpd_resp_set_status(p->req, statusText(status));
  httpd_resp_set_type(p->req, p->type);
  httpd_resp_set_hdr(p->req, "Cache-Control", "no-store");
  p->headSent = true;
}

static bool proxyData(void* c, const uint8_t* b, size_t n) {
  ProxyCtx* p = (ProxyCtx*)c;
  if (p->capture) { p->capture->concat((const char*)b, n); return true; }
  return httpd_resp_send_chunk(p->req, (const char*)b, n) == ESP_OK;
}

static esp_err_t linkError(httpd_req_t* req, int s) {
  return replyErr(req, s == -1 ? 503 : 504, s == -1 ? "Il PLC non risponde (collegamento col modulo Wi-Fi)" : "Il PLC non ha risposto in tempo");
}

static esp_err_t pinError(httpd_req_t* req) {
  if (pinStatus < 0) return linkError(req, pinStatus);
  return replyErr(req, 403, "PIN docente errato");
}

static esp_err_t proxy(httpd_req_t* req, String* capture = nullptr) {
  char pin[12];
  getHeader(req, "X-Pin", pin, sizeof(pin));
  ProxyCtx c = { req, nullptr, "", false, capture, 0 };
  LinkReq r = { req->method == HTTP_POST ? "POST" : "GET", req->uri, pin, (int32_t)req->content_len,
                proxyRead, proxyHead, proxyData, &c };
  int s = linkHttp(r, 10000);
  linkLog("  RA4M1: %d, tipo %s", s, c.type);
  if (capture) return s < 0 ? linkError(req, s) : ESP_OK;
  if (s < 0 && !c.headSent) return linkError(req, s);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// Caricamento del programma: prima tutto nella flash dell'ESP32, poi al RA4M1. Se la rete cade
// a meta' il programma in esecuzione non si tocca (il RA4M1 lo scrive direttamente nella sua memoria).
static esp_err_t postProgram(httpd_req_t* req) {
  if (!pinOk(req)) return pinError(req);
  if (req->content_len < IMG_HDR_LEN || (int32_t)req->content_len > MAX_SLOT) return replyErr(req, 400, "Dimensione non valida");
  if (!receiveToFile(req, STAGE_PATH)) return replyErr(req, 400, "Ricezione non riuscita");
  if (!looksLikeProgram(STAGE_PATH)) { LittleFS.remove(STAGE_PATH); return replyErr(req, 400, "Programma non valido"); }
  File f = LittleFS.open(STAGE_PATH, "r");
  if (!f) return replyErr(req, 400, "Ricezione non riuscita");
  char pin[12];
  getHeader(req, "X-Pin", pin, sizeof(pin));
  ProxyCtx c = { req, &f, "", false, nullptr, 0 };
  LinkReq r = { "POST", req->uri, pin, (int32_t)f.size(), proxyRead, proxyHead, proxyData, &c };
  int s = linkHttp(r, 15000);
  f.close();
  LittleFS.remove(STAGE_PATH);
  if (s < 0 && !c.headSent) return linkError(req, s);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// Aggiornamento: target=esp (firmware dell'ESP32) o target=ra (firmware del RA4M1)
static esp_err_t postOta(httpd_req_t* req) {
  char target[8];
  if (!getParam(req, "target", target, sizeof(target))) return replyErr(req, 400, "Manca target (esp o ra)");
  bool esp = !strcmp(target, "esp"), ra = !strcmp(target, "ra");
  if (!esp && !ra) return replyErr(req, 400, "target: esp o ra");
  if (!pinOk(req) && !(pinStatus < 0 && pinOkOffline(req))) return pinError(req);
  if (req->content_len <= 0) return replyErr(req, 400, "Dimensione non valida");
  if (esp) {
    static uint8_t buf[4096];
    int32_t left = req->content_len;
    int n = recvSome(req, buf, min((int32_t)sizeof(buf), left));
    if (n <= 0) return replyErr(req, 400, "Ricezione non riuscita");
    if (buf[0] != 0xE9) return replyErr(req, 400, "Non e' un firmware per il modulo Wi-Fi (ESP32)");   // intestazione delle app ESP32
    if (!Update.begin(req->content_len, U_FLASH)) return replyErr(req, 400, "Firmware troppo grande per la partizione");
    if (Update.write(buf, n) != (size_t)n) { Update.abort(); return replyErr(req, 400, "Scrittura non riuscita"); }
    left -= n;
    while (left > 0) {
      n = recvSome(req, buf, min((int32_t)sizeof(buf), left));
      if (n <= 0 || Update.write(buf, n) != (size_t)n) { Update.abort(); return replyErr(req, 400, "Ricezione non riuscita"); }
      left -= n;
    }
    if (!Update.end(true)) {
      String e = String("Firmware non valido: ") + Update.errorString();
      return replyErr(req, 400, e.c_str());
    }
    otaRestartLater();                        // la scheda riparte fra poco con il firmware nuovo
    return sendJson(req, 200, "{\"ok\":true,\"restart\":true}");
  }
  static const char RA_PATH[] = "/OTA/RA.BIN";
  LittleFS.mkdir("/OTA");
  if ((int32_t)req->content_len > 240 * 1024) return replyErr(req, 400, "File troppo grande per il RA4M1");
  if (!receiveToFile(req, RA_PATH)) return replyErr(req, 400, "Ricezione non riuscita");
  String err;
  bool ok = otaCheckRaImage(RA_PATH, err) && otaFlashRa(RA_PATH, err);
  LittleFS.remove(RA_PATH);
  infoWanted = true;
  if (!ok) return replyErr(req, 400, err.c_str());
  return replyOk(req);
}

// Toglie la "}" finale da una risposta JSON per aggiungere dei campi
static void openJson(String& s) {
  int e = s.lastIndexOf('}');
  if (e >= 0) s.remove(e);
}

static void parseInfo(const String& s) {
  auto field = [&](const char* key, char* out, size_t len) {
    int i = s.indexOf(key);
    if (i < 0) return;
    i += strlen(key);
    int e = s.indexOf('"', i);
    if (e < 0) return;
    String v = s.substring(i, e);
    strlcpy(out, v.c_str(), len);
  };
  field("\"host\":\"", host, sizeof(host));
  field("\"prog\":\"", prog, sizeof(prog));
  running = s.indexOf("\"run\":1") >= 0;
  if (host[0]) wifiSetHostname(host);
  lastInfoAt = millis();
}

// /api/info: quella del RA4M1 piu' Wi-Fi, memoria dell'ESP32, pagina e bozza
static esp_err_t sendInfo(httpd_req_t* req) {
  String s;
  esp_err_t e = proxy(req, &s);
  if (s.length() < 2) return e;
  parseInfo(s);
  openJson(s);
  s += ",\"sd\":1,\"fs\":1,\"pv\":"; s += pageVersion();
  s += ",\"dr\":"; s += draftRev;
  s += ",\"bridge\":\"" BRIDGE_FW_VERSION "\",\"wifi\":"; wifiJson(s);
  s += '}';
  return sendJson(req, 200, s);
}

// /api/live: la copia aggiornata da webPoll(); se e' vecchia si chiede al RA4M1
static bool refreshLive() {
  static char tmp[sizeof(liveBuf)];
  int s = linkGet("/api/live", tmp, sizeof(tmp) - 16, 3000);
  if (s != 200) return false;
  size_t n = strlen(tmp);
  while (n && tmp[n - 1] != '}') n--;               // si aggiunge la revisione della bozza (sta qui)
  if (!n) return false;
  n += snprintf(tmp + n - 1, 16, ",\"dr\":%u}", draftRev) - 1;
  int rev = -1;
  const char* r = strstr(tmp, "\"rev\":");
  if (r) rev = atoi(r + 6);
  if (rev != lastRev) { lastRev = rev; infoWanted = true; }   // programma cambiato: nome e stato nuovi
  xSemaphoreTake(cacheMtx, portMAX_DELAY);
  memcpy(liveBuf, tmp, n);
  liveLen = n;
  liveAt = millis();
  xSemaphoreGive(cacheMtx);
  return true;
}

static esp_err_t sendLive(httpd_req_t* req) {
  lastLiveReq = millis();
  if (!liveAt || millis() - liveAt > 3000) return linkError(req, linkUp() ? -2 : -1);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  static char copy[sizeof(liveBuf)];
  xSemaphoreTake(cacheMtx, portMAX_DELAY);
  size_t n = liveLen;
  memcpy(copy, liveBuf, n);
  xSemaphoreGive(cacheMtx);
  return httpd_resp_send(req, copy, n);
}

// ---------------------------------------------------------------------
//  Instradamento delle API
// ---------------------------------------------------------------------
static bool is(const char* uri, const char* p) {
  size_t n = strlen(p);
  return !strncmp(uri, p, n) && (uri[n] == 0 || uri[n] == '?');
}

static esp_err_t apiRoute(httpd_req_t* req);

static esp_err_t apiHandler(httpd_req_t* req) {
  uint32_t t = millis();
  strlcpy(webUri, req->uri, sizeof(webUri));
  webWhere = webUri;
  if (strncmp(req->uri, "/api/live", 9)) linkLog("%s %s ...", req->method == HTTP_POST ? "POST" : "GET", req->uri);
  esp_err_t e = apiRoute(req);
  if (strncmp(req->uri, "/api/live", 9)) linkLog("  %s fatto in %lu ms (%d)", req->uri, millis() - t, (int)e);
  webWhere = "fermo";
  return e;
}

static esp_err_t apiRoute(httpd_req_t* req) {
  const char* u = req->uri;
  bool get = req->method == HTTP_GET, post = req->method == HTTP_POST;
  if (get) {
    if (is(u, "/api/live"))     return sendLive(req);
    if (is(u, "/api/info"))     return sendInfo(req);
    if (is(u, "/api/wifi"))     { String o; wifiJson(o); return sendJson(req, 200, o); }
    if (is(u, "/api/wifiscan")) { String o; wifiScanJson(o); return sendJson(req, 200, o); }
    if (is(u, "/api/slots"))    return sendSlots(req);
    if (is(u, "/api/slot"))     { char p[16]; if (!slotNumber(req, p)) return replyErr(req, 400, "Slot non valido"); return sendFileBin(req, p, "Slot vuoto"); }
    if (is(u, "/api/draft"))    return sendFileBin(req, DRAFT_PATH, "Nessuna bozza");
    return proxy(req);
  }
  if (!post) return replyErr(req, 400, "Metodo non valido");
  // gestite qui ma con il PIN docente
  bool local = is(u, "/api/wifi") || is(u, "/api/slot") || is(u, "/api/slotdel") || is(u, "/api/draft") ||
               is(u, "/api/sdwipe") || is(u, "/api/file");
  if (local) {
    if (!pinOk(req)) return pinError(req);
    if (is(u, "/api/wifi"))    return postWifi(req);
    if (is(u, "/api/slot"))    return postSlot(req);
    if (is(u, "/api/draft"))   return postDraft(req);
    if (is(u, "/api/sdwipe"))  return postWipe(req);
    if (is(u, "/api/file"))    return postFile(req);
    char p[16];
    if (!slotNumber(req, p)) return replyErr(req, 400, "Slot non valido");
    LittleFS.remove(p);
    return replyOk(req);
  }
  if (is(u, "/api/program")) { infoWanted = true; return postProgram(req); }
  if (is(u, "/api/ota"))     return postOta(req);
  if (is(u, "/api/config")) { forgetPin(); infoWanted = true; }   // PIN o nome forse cambiati
  if (is(u, "/api/run") || is(u, "/api/erase")) infoWanted = true;
  return proxy(req);
}

void webBegin() {
  cacheMtx = xSemaphoreCreateMutex();
  LittleFS.mkdir("/PB");
  LittleFS.mkdir("/PLC");
  if (LittleFS.exists(DRAFT_PATH)) draftRev = 1;

  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.uri_match_fn = httpd_uri_match_wildcard;
  c.max_uri_handlers = 8;
  c.max_open_sockets = 9;          // lwIP ne ha 16: restano DNS, UDP, mDNS
  c.lru_purge_enable = true;       // molti browser: si chiudono le connessioni ferme da piu' tempo
  c.stack_size = 12288;
  c.recv_wait_timeout = 5;
  c.send_wait_timeout = 10;
  c.core_id = 0;
  if (httpd_start(&srv, &c) != ESP_OK) return;
  httpd_uri_t apiGet  = { "/api/*", HTTP_GET,  apiHandler, nullptr };
  httpd_uri_t apiPost = { "/api/*", HTTP_POST, apiHandler, nullptr };
  httpd_uri_t page    = { "/*",     HTTP_GET,  pageHandler, nullptr };
  httpd_register_uri_handler(srv, &apiGet);
  httpd_register_uri_handler(srv, &apiPost);
  httpd_register_uri_handler(srv, &page);
}

// Task "svc": copia di /api/live (spesso se qualcuno guarda, altrimenti ogni 2 s) e info ogni 15 s
void webPoll() {
  if (!linkUp()) { lastRev = -1; infoWanted = true; return; }
  uint32_t now = millis();
  uint32_t every = webStress || now - lastLiveReq < 3000 ? 200 : 2000;
  if (now - liveAt >= every) refreshLive();
  if (infoWanted || now - lastInfoAt > 15000UL) {
    static char buf[4096];
    if (linkGet("/api/info", buf, sizeof(buf), 3000) == 200) { parseInfo(String(buf)); infoWanted = false; }
    else lastInfoAt = now;
  }
}

const char* webHost() { return host; }

void webDiscoveryJson(String& o) {
  o = "{\"host\":";
  jsonStr(o, host);
  o += ",\"ip\":\""; o += wifiIP().toString();
  o += "\",\"prog\":"; jsonStr(o, prog);
  o += ",\"run\":"; o += running ? 1 : 0;
  o += '}';
}

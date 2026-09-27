#include "wifimgr.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_wifi.h>

// =====================================================================
//  Wi-Fi dell'UNO R4 WiFi (sull'ESP32): stessa logica del vecchio wifi.cpp del RA4M1
//  - nessuna rete salvata: rete propria "ArduLearn-xxxx" (password ardulearn), pagina su
//    http://192.168.4.1/. Niente portale ("captive"): Windows apriva MSN e i telefoni la pagina
//    in un mini browser che restava bianco;
//  - rete salvata: ci si collega; dopo 3 tentativi falliti si torna alla rete propria e ogni
//    10 minuti (se nessuno e' collegato alla rete della scheda) si riprova quella salvata.
//  Le credenziali stanno nella memoria nvs dell'ESP32.
// =====================================================================
const char WIFI_AP_PASS[] = "ardulearn";

enum : uint8_t { W_OFF, W_CONN, W_STA, W_AP };
static uint8_t  mode = W_OFF, fails = 0;
static uint32_t t0 = 0, lastCheck = 0, applyAt = 0;
static char apName[24], ssid[33], pass[64], host[32] = "";
static Preferences prefs;

static bool saved() { return ssid[0] != 0; }
const char* wifiApName() { return apName; }
bool wifiIsAP() { return mode == W_AP; }

void wifiMac(uint8_t* mac) { esp_read_mac(mac, ESP_MAC_WIFI_STA); }

uint8_t wifiNetState() {
  switch (mode) {
    case W_CONN: return NET_DHCP;
    case W_STA:  return NET_OK;
    case W_AP:   return NET_FALLBACK;
  }
  return NET_NO_LINK;
}

IPAddress wifiIP() { return mode == W_AP ? WiFi.softAPIP() : WiFi.localIP(); }

void wifiSetHostname(const char* h) {
  if (!h[0] || !strcmp(h, host)) return;
  strlcpy(host, h, sizeof(host));
  WiFi.setHostname(host);                  // vale dal prossimo collegamento
}

static void load() {
  prefs.begin("wifi", true);
  prefs.getString("ssid", ssid, sizeof(ssid));
  prefs.getString("pass", pass, sizeof(pass));
  prefs.end();
}

static void store() {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

static void startAP() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);                      // solo rete propria: la parte "stazione" occupa memoria, si accende per cercare le reti
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName, WIFI_AP_PASS);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);           // col risparmio energetico la rete della scheda risponde lenta e a scatti
  mode = W_AP;
  t0 = millis();
}

static void startConnect() {
  if (!saved()) { startAP(); return; }
  if (mode == W_AP) WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                    // niente risparmio energetico: risposte piu' pronte (la scheda e' alimentata)
  if (host[0]) WiFi.setHostname(host);
  WiFi.begin(ssid, pass[0] ? pass : nullptr);
  mode = W_CONN;
  t0 = millis();
}

void wifiBegin() {
  uint8_t mac[6];
  wifiMac(mac);
  snprintf(apName, sizeof(apName), "ArduLearn-%02x%02x", mac[4], mac[5]);
  WiFi.persistent(false);                  // le credenziali le teniamo noi (nvs "wifi")
  WiFi.setAutoReconnect(false);            // i nuovi tentativi li decide wifiTick()
  load();
  startConnect();
}

void wifiTick() {
  uint32_t now = millis();
  if (applyAt && (int32_t)(now - applyAt) >= 0) { applyAt = 0; fails = 0; startConnect(); return; }
  if (now - lastCheck < 500) return;
  lastCheck = now;
  switch (mode) {
    case W_CONN:
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        mode = W_STA;
        fails = 0;
        esp_wifi_set_ps(WIFI_PS_NONE);       // il core lo riaccende al collegamento: risposte lente e irregolari
      } else if (now - t0 > 20000UL) {                  // 20 s senza collegamento
        if (++fails >= 3) startAP();
        else startConnect();
      }
      break;
    case W_STA:
      if (WiFi.status() != WL_CONNECTED) startConnect();   // rete persa: si riprova
      break;
    case W_AP:
      // ogni 10 minuti si riprova la rete salvata, ma solo se nessuno e' collegato alla scheda
      if (saved() && now - t0 > 600000UL && WiFi.softAPgetStationNum() == 0) { fails = 0; startConnect(); }
      break;
  }
}

bool wifiSetCredentials(const char* s, const char* p, String& err) {
  size_t ls = strlen(s), lp = strlen(p);
  if (ls < 1 || ls > 32) { err = "Nome della rete: da 1 a 32 caratteri"; return false; }
  if (lp && (lp < 8 || lp > 63)) { err = "Password: da 8 a 63 caratteri (vuota per le reti aperte)"; return false; }
  strlcpy(ssid, s, sizeof(ssid));
  strlcpy(pass, p, sizeof(pass));
  store();
  return true;
}

void wifiForget() {
  ssid[0] = pass[0] = 0;
  store();
}

void wifiApplyLater() { applyAt = millis() + 1500; }   // il tempo di rispondere al browser

static void jsonStr(String& o, const char* s) {
  o += '"';
  for (; *s; s++) { uint8_t c = *s; if (c == '"' || c == '\\') o += '\\'; if (c >= 0x20) o += (char)c; }
  o += '"';
}

void wifiJson(String& o) {
  static const char* const M[] = { "off", "conn", "sta", "ap" };
  o += "{\"mode\":\""; o += M[mode];
  o += "\",\"ssid\":"; jsonStr(o, saved() ? ssid : "");
  o += ",\"rssi\":"; o += mode == W_STA ? WiFi.RSSI() : 0;
  o += ",\"ip\":\""; o += wifiIP().toString();
  o += "\",\"ap\":"; jsonStr(o, apName);
  o += ",\"saved\":"; o += saved() ? 1 : 0;
  o += '}';
}

void wifiScanJson(String& o) {
  bool soloAP = WiFi.getMode() == WIFI_AP;   // sulla rete propria la ricerca richiede anche la parte "stazione"
  if (soloAP) WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();
  o += "{\"nets\":[";
  int shown = 0;
  for (int i = 0; i < n && shown < 20; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;                           // reti nascoste
    bool dup = false;                                    // stessa rete da piu' ripetitori: la piu' forte
    for (int j = 0; j < i && !dup; j++) dup = WiFi.SSID(j) == s;
    if (dup) continue;
    if (shown++) o += ',';
    o += "{\"ssid\":"; jsonStr(o, s.c_str());
    o += ",\"rssi\":"; o += WiFi.RSSI(i);
    o += ",\"enc\":"; o += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1;
    o += '}';
  }
  o += "]}";
  WiFi.scanDelete();
  if (soloAP && mode == W_AP) { WiFi.mode(WIFI_AP); esp_wifi_set_ps(WIFI_PS_NONE); }
}

// Comandi dal monitor seriale (li inoltra il RA4M1): stesse risposte del vecchio firmware
// wifi | wifi ssid <nome> | wifi pass <password> | wifi connect | wifi clear | wifi ap
void wifiSerialCommand(const char* line, String& out) {
  static char newSsid[33], newPass[64];     // impostati con "wifi ssid" / "wifi pass", salvati da "wifi connect"
  static bool hasSsid = false, hasPass = false;
  if (strncmp(line, "wifi", 4) || (line[4] && line[4] != ' ')) { out = "@err comando sconosciuto\r\n"; return; }
  const char* a = line[4] ? line + 5 : "";
  // diagnostica (per chi sviluppa): wifi log | wifi diag | wifi stress
  extern bool webStress, diagOn;
  extern bool linkLogOn;
  if (!strcmp(a, "log"))    { linkLogOn = !linkLogOn; out = linkLogOn ? "registro acceso\r\n" : "registro spento\r\n"; return; }
  if (!strcmp(a, "diag"))   { diagOn = !diagOn; out = diagOn ? "diagnostica accesa (sulla USB ogni 5 s)\r\n" : "diagnostica spenta\r\n"; return; }
  if (!strcmp(a, "webstop")) { extern void webTestStop(); webTestStop(); out = "server web fermato (prova: deve ripartire da solo)\r\n"; return; }
  if (!strcmp(a, "stress")) { webStress = !webStress; out = webStress ? "stress acceso\r\n" : "stress spento\r\n"; return; }
  if (!*a) {
    static const char* const M[] = { "spento", "in collegamento", "collegata", "rete della scheda (AP)" };
    out += "Wi-Fi: "; out += M[mode];
    if (saved()) { out += " - rete salvata: "; out += ssid; }
    out += " - indirizzo "; out += wifiIP().toString();
    wifi_ps_type_t ps;
    if (esp_wifi_get_ps(&ps) == ESP_OK) { out += " - risparmio energetico "; out += ps == WIFI_PS_NONE ? "spento" : "acceso"; }
    out += "\r\n";
    if (mode == W_AP) { out += "Rete della scheda: "; out += apName; out += " (password ardulearn)\r\n"; }
    out += "@wifi "; wifiJson(out); out += "\r\n";
  } else if (!strncmp(a, "ssid ", 5)) {
    strlcpy(newSsid, a + 5, sizeof(newSsid));
    hasSsid = true;
    out = "@ok\r\n";
  } else if (!strcmp(a, "pass") || !strncmp(a, "pass ", 5)) {
    strlcpy(newPass, a[4] ? a + 5 : "", sizeof(newPass));
    hasPass = true;
    out = "@ok\r\n";
  } else if (!strcmp(a, "connect")) {
    String err;
    char s[33], p[64];
    strlcpy(s, hasSsid ? newSsid : ssid, sizeof(s));
    strlcpy(p, hasPass ? newPass : pass, sizeof(p));
    if (!wifiSetCredentials(s, p, err)) out = "@err " + err + "\r\n";
    else { out = "@ok\r\n"; hasSsid = hasPass = false; fails = 0; wifiApplyLater(); }
  } else if (!strcmp(a, "clear")) {
    wifiForget();
    out = "@ok rete dimenticata\r\n";
    startAP();
  } else if (!strcmp(a, "ap")) {
    out = "@ok\r\n";
    startAP();
  } else {
    out = "@err comandi: wifi | wifi ssid <nome> | wifi pass <password> | wifi connect | wifi clear | wifi ap\r\n";
  }
}

#include "net.h"
#if NET_WIFI
#include "storage.h"
#include <EEPROM.h>

// =====================================================================
//  Wi-Fi dell'UNO R4 WiFi
//  - nessuna rete salvata: la scheda crea la propria rete "ArduLearn-xxxx" (password
//    ardulearn); dalla pagina (http://192.168.4.1/) si imposta la rete della scuola;
//  - rete salvata: ci si collega; dopo 3 tentativi falliti si torna alla propria rete e ogni
//    10 minuti (se nessuno e' collegato alla rete della scheda) si riprova quella salvata.
//  Il collegamento non blocca il PLC: si avvia e poi si controlla lo stato a ogni giro.
// =====================================================================
const char WIFI_AP_PASS[] = "ardulearn";

// credenziali nella memoria interna, dopo la configurazione (vedi config.h: fino a 511)
static const uint16_t WIFI_EE = 256;
static const uint8_t  WIFI_MAGIC = 0xA7;
struct WifiCfg { uint8_t magic; char ssid[33]; char pass[64]; };
static WifiCfg wc;

enum : uint8_t { W_OFF, W_CONN, W_STA, W_AP };
static uint8_t  mode = W_OFF, fails = 0;
static uint32_t t0 = 0, lastCheck = 0, applyAt = 0;
static char apName[20];
static bool needSvc = false;              // rete appena disponibile: il .ino riavvia i servizi

static bool saved() { return wc.magic == WIFI_MAGIC && wc.ssid[0]; }
const char* wifiApName() { return apName; }

static void load() {
  EEPROM.get(WIFI_EE, wc);
  wc.ssid[sizeof(wc.ssid) - 1] = 0;
  wc.pass[sizeof(wc.pass) - 1] = 0;
  if (wc.magic != WIFI_MAGIC) memset(&wc, 0, sizeof(wc));
}

static void store() { wc.magic = WIFI_MAGIC; EEPROM.put(WIFI_EE, wc); }

static void startAP() {
  WiFi.disconnect();
  WiFi.end();
  WiFi.beginAP(apName, WIFI_AP_PASS);
  mode = W_AP;
  t0 = millis();
  g_netState = NET_FALLBACK;
  needSvc = true;
  Serial.print(F("Wi-Fi: rete della scheda attiva: ")); Serial.print(apName);
  Serial.println(F(" (password ardulearn) - pagina su http://192.168.4.1/"));
}

static void startConnect() {
  if (!saved()) { startAP(); return; }
  if (mode == W_AP) WiFi.end();
  WiFi.setTimeout(50);                     // begin() avvia e torna subito: lo stato si controlla dopo
  WiFi.begin(wc.ssid, wc.pass[0] ? wc.pass : nullptr);
  mode = W_CONN;
  t0 = millis();
  g_netState = NET_DHCP;
  Serial.print(F("Wi-Fi: collegamento a ")); Serial.println(wc.ssid);
}

void wifiBegin() {
  snprintf_P(apName, sizeof(apName), PSTR("ArduLearn-%02x%02x"), cfg.mac[4], cfg.mac[5]);
  load();
  if (WiFi.status() == WL_NO_MODULE) {
    g_netState = NET_NO_HW;
    Serial.println(F("ERRORE: modulo Wi-Fi non risponde"));
    return;
  }
  startConnect();
}

bool wifiTick() {
  if (g_netState == NET_NO_HW) return false;
  if (needSvc) { needSvc = false; return true; }
  uint32_t now = millis();
  if (applyAt && (int32_t)(now - applyAt) >= 0) { applyAt = 0; fails = 0; startConnect(); return false; }
  if (now - lastCheck < 500) return false;
  lastCheck = now;
  switch (mode) {
    case W_CONN: {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        mode = W_STA; fails = 0;
        g_netState = NET_OK;
        Serial.print(F("Wi-Fi: collegata a ")); Serial.print(wc.ssid);
        Serial.print(F(", indirizzo ")); Serial.println(WiFi.localIP());
        return true;
      }
      if (now - t0 > 20000UL) {                        // 20 s senza collegamento
        Serial.println(F("Wi-Fi: collegamento non riuscito"));
        if (++fails >= 3) { startAP(); return true; }
        startConnect();
      }
      return false;
    }
    case W_STA:
      if (WiFi.status() != WL_CONNECTED) {             // rete persa: si riprova
        Serial.println(F("Wi-Fi: rete persa"));
        startConnect();
      }
      return false;
    case W_AP:
      // ogni 10 minuti si riprova la rete salvata, ma solo se nessuno e' collegato alla scheda
      if (saved() && now - t0 > 600000UL && WiFi.status() != WL_AP_CONNECTED) { fails = 0; startConnect(); }
      return false;
  }
  return false;
}

bool wifiSetCredentials(const char* ssid, const char* pass, char* err, uint8_t errLen) {
  size_t ls = strlen(ssid), lp = strlen(pass);
  if (ls < 1 || ls > 32) { strlcpy(err, "Nome della rete: da 1 a 32 caratteri", errLen); return false; }
  if (lp && (lp < 8 || lp > 63)) { strlcpy(err, "Password: da 8 a 63 caratteri (vuota per le reti aperte)", errLen); return false; }
  strlcpy(wc.ssid, ssid, sizeof(wc.ssid));
  strlcpy(wc.pass, pass, sizeof(wc.pass));
  store();
  return true;
}

void wifiForget() {
  memset(&wc, 0, sizeof(wc));
  EEPROM.put(WIFI_EE, wc);
}

void wifiApplyLater() { applyAt = millis() + 1500; }   // il tempo di rispondere al browser

static void jsonStrW(Print& o, const char* s) {
  o.write('"');
  for (; *s; s++) { uint8_t c = *s; if (c == '"' || c == '\\') o.write('\\'); if (c >= 0x20) o.write(c); }
  o.write('"');
}

void wifiJson(Print& o) {
  static const char* const M[] = { "off", "conn", "sta", "ap" };
  o.print(F("{\"mode\":\"")); o.print(M[mode]);
  o.print(F("\",\"ssid\":")); jsonStrW(o, saved() ? wc.ssid : "");
  o.print(F(",\"rssi\":")); o.print(mode == W_STA ? WiFi.RSSI() : 0);
  o.print(F(",\"ip\":\"")); o.print(WiFi.localIP());
  o.print(F("\",\"ap\":")); jsonStrW(o, apName);
  o.print(F(",\"saved\":")); o.print(saved() ? 1 : 0);
  o.print('}');
}

void wifiScanJson(Print& o) {
  int n = WiFi.scanNetworks();
  o.print(F("{\"nets\":["));
  for (int i = 0; i < n && i < 20; i++) {
    if (i) o.print(',');
    o.print(F("{\"ssid\":")); jsonStrW(o, WiFi.SSID(i));
    o.print(F(",\"rssi\":")); o.print(WiFi.RSSI(i));
    o.print(F(",\"enc\":")); o.print(WiFi.encryptionType(i) == ENC_TYPE_NONE ? 0 : 1);
    o.print('}');
  }
  o.print(F("]}"));
}

// Comandi seriali: wifi | wifi ssid <nome> | wifi pass <password> | wifi connect | wifi clear | wifi ap
bool wifiSerial(const char* line) {
  if (strncmp_P(line, PSTR("wifi"), 4) || (line[4] && line[4] != ' ')) return false;
  const char* a = line[4] ? line + 5 : "";
  char err[64];
  if (!*a) {
    static const char* const M[] = { "spento", "in collegamento", "collegata", "rete della scheda (AP)" };
    Serial.print(F("Wi-Fi: ")); Serial.print(M[mode]);
    if (saved()) { Serial.print(F(" - rete salvata: ")); Serial.print(wc.ssid); }
    Serial.print(F(" - indirizzo ")); Serial.println(WiFi.localIP());
    if (mode == W_AP) { Serial.print(F("Rete della scheda: ")); Serial.print(apName); Serial.println(F(" (password ardulearn)")); }
    Serial.print(F("@wifi ")); wifiJson(Serial); Serial.println();
  } else if (!strncmp_P(a, PSTR("ssid "), 5)) {
    strlcpy(wc.ssid, a + 5, sizeof(wc.ssid));
    Serial.println(F("@ok"));
  } else if (!strcmp_P(a, PSTR("pass")) || !strncmp_P(a, PSTR("pass "), 5)) {
    strlcpy(wc.pass, a[4] ? a + 5 : "", sizeof(wc.pass));
    Serial.println(F("@ok"));
  } else if (!strcmp_P(a, PSTR("connect"))) {
    char s[33], p[64];
    strlcpy(s, wc.ssid, sizeof(s)); strlcpy(p, wc.pass, sizeof(p));
    if (!wifiSetCredentials(s, p, err, sizeof(err))) { Serial.print(F("@err ")); Serial.println(err); }
    else { Serial.println(F("@ok")); fails = 0; startConnect(); }
  } else if (!strcmp_P(a, PSTR("clear"))) {
    wifiForget();
    Serial.println(F("@ok rete dimenticata"));
    startAP();
  } else if (!strcmp_P(a, PSTR("ap"))) {
    Serial.println(F("@ok"));
    startAP();
  } else {
    Serial.println(F("@err comandi: wifi | wifi ssid <nome> | wifi pass <password> | wifi connect | wifi clear | wifi ap"));
  }
  return true;
}
#endif

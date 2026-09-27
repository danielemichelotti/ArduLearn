#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// ---- Wi-Fi (wifimgr.cpp) ----
// Senza rete salvata, o se non riesce a collegarsi 3 volte, la scheda crea la propria rete
// "ArduLearn-xxxx" (password ardulearn, pagina su http://192.168.4.1/) con portale di configurazione.
// Stati come g_netState del firmware ArduLearn (config.h)
enum : uint8_t { NET_NO_HW, NET_NO_LINK, NET_DHCP, NET_OK, NET_FALLBACK };

extern const char WIFI_AP_PASS[];
void wifiBegin();
void wifiTick();
void wifiMac(uint8_t* mac);              // MAC della stazione Wi-Fi (letto dall'efuse)
const char* wifiApName();
uint8_t wifiNetState();
IPAddress wifiIP();                      // indirizzo con cui la scheda si raggiunge
bool wifiIsAP();
void wifiSetHostname(const char* host);  // nome per il DHCP e mDNS (dal RA4M1)
void wifiJson(String& o);                // {"mode":..,"ssid":..,"rssi":..,"ip":..,"ap":..,"saved":..}
void wifiScanJson(String& o);
bool wifiSetCredentials(const char* ssid, const char* pass, String& err);
void wifiForget();
void wifiApplyLater();                   // si ricollega fra poco (dopo aver risposto al browser)
void wifiSerialCommand(const char* line, String& out);   // comandi "wifi ..." dal monitor seriale

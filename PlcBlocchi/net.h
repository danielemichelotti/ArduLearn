#pragma once
#include "config.h"

// =====================================================================
//  Rete: shield Ethernet W5500 (Mega) oppure Wi-Fi integrato (UNO R4 WiFi).
//  Il resto del firmware usa solo questi nomi.
// =====================================================================
#if NET_WIFI
#include <WiFiS3.h>
typedef WiFiServer NetServer;
typedef WiFiClient NetClient;
typedef WiFiUDP    NetUDP;
inline IPAddress netIP() { return WiFi.localIP(); }
#else
#include <Ethernet.h>
#include <EthernetUdp.h>
typedef EthernetServer NetServer;
typedef EthernetClient NetClient;
typedef EthernetUDP    NetUDP;
inline IPAddress netIP() { return Ethernet.localIP(); }
#endif

#if NET_WIFI
// ---- Wi-Fi dell'UNO R4 WiFi (wifi.cpp) ----
// Senza rete salvata, o se non riesce a collegarsi 3 volte, la scheda crea la propria rete
// "ArduLearn-xxxx" (password ardulearn, pagina su http://192.168.4.1/).
void wifiBegin();
bool wifiTick();                         // true quando la rete e' appena diventata disponibile
bool wifiSerial(const char* line);       // comandi seriali "wifi ..."; false se non e' un comando wifi
void wifiJson(Print& o);                 // {"mode":..,"ssid":..,"rssi":..,"ip":..,"ap":..,"saved":..}
bool wifiSetCredentials(const char* ssid, const char* pass, char* err, uint8_t errLen);
void wifiForget();
void wifiApplyLater();                   // si ricollega fra poco (dopo aver risposto al browser)
void wifiScanJson(Print& o);             // reti visibili
const char* wifiApName();
extern const char WIFI_AP_PASS[];
#endif

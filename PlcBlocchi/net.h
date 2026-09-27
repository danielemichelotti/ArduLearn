#pragma once
#include "config.h"

// =====================================================================
//  Rete: shield Ethernet W5500 (Mega) oppure il modulo ESP32-S3 dell'UNO R4 WiFi
//  con il firmware ArduLearnBridge. Il resto del firmware usa solo questi nomi.
// =====================================================================
#if NET_BRIDGE
#include <IPAddress.h>

// ---- UNO R4 WiFi (bridge.cpp) ----
// L'ESP32 gestisce Wi-Fi, pagina, slot e bozza; le richieste per il PLC arrivano dal
// collegamento seriale (Serial2) come testo HTTP: LinkServer/LinkClient le presentano
// al server web (web.cpp) come se venissero dalla rete.
class LinkClient {
public:
  explicit LinkClient(bool v = false) : valid(v) {}
  explicit operator bool() const;
  int available();
  int read();
  int read(uint8_t* buf, size_t n);
  bool connected();
  size_t write(uint8_t b) { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t n);
  void stop();
private:
  bool valid;
};

class LinkServer {
public:
  explicit LinkServer(uint16_t) {}
  void begin() {}
  LinkClient available();
};

typedef LinkServer NetServer;
typedef LinkClient NetClient;
IPAddress netIP();                       // indirizzo della scheda (lo comunica l'ESP32)
void bridgeBegin();
void bridgeTick();
bool bridgeSerial(const char* line);     // comandi seriali "wifi ...": li esegue l'ESP32
const char* bridgeApName();              // nome della rete propria "ArduLearn-xxxx"
extern const char WIFI_AP_PASS[];
#else
#include <Ethernet.h>
#include <EthernetUdp.h>
typedef EthernetServer NetServer;
typedef EthernetClient NetClient;
typedef EthernetUDP    NetUDP;
inline IPAddress netIP() { return Ethernet.localIP(); }
#endif

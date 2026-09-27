#pragma once
#include <Arduino.h>
#include "link_proto.h"

// ---- Collegamento col RA4M1 (link.cpp) ----
void linkBegin();
void linkService();            // da chiamare spesso (task "svc"): saluto, battito, comandi dal RA4M1
bool linkUp();                 // il RA4M1 risponde
void linkPause(uint32_t ms);   // il RA4M1 si sta caricando dalla USB: per un po' non lo si cerca
const char* linkRaVersion();   // firmware ArduLearn del RA4M1 ("" se sconosciuto)
uint32_t linkBaud();

// Richiesta HTTP inoltrata al RA4M1. Il corpo (bodyLen byte) viene chiesto a readBody a pezzi;
// la risposta arriva a onHead (codice, tipo, lunghezza o -1) e poi a onData (corpo, a pezzi).
struct LinkReq {
  const char* method;
  const char* uri;             // percorso con eventuale ?query
  const char* pin;             // X-Pin (puo' essere "")
  int32_t bodyLen;
  int  (*readBody)(void* ctx, uint8_t* buf, size_t max);    // byte letti, <= 0 = errore
  void (*onHead)(void* ctx, int status, const char* type, int32_t len);
  bool (*onData)(void* ctx, const uint8_t* buf, size_t n);  // false = smetti (browser chiuso)
  void* ctx;
};
// Ritorna il codice HTTP della risposta, oppure < 0: -1 RA4M1 non collegato, -2 tempo scaduto
int linkHttp(const LinkReq& r, uint32_t timeoutMs = 8000);

// Comodo per le richieste interne: risposta intera in un buffer (terminata da 0)
int linkGet(const char* uri, char* out, size_t outLen, uint32_t timeoutMs = 3000);

// Registro diagnostico: righe "[esp] ..." sul monitor seriale del RA4M1 (comando "wifi log")
void linkLog(const char* fmt, ...);
extern bool linkLogOn;
// diagnostica: cosa sta facendo il collegamento (e da quando)
extern volatile const char* linkWhere;
extern volatile uint32_t linkWhereAt;
extern volatile uint32_t linkHelloTries, linkHelloOk, linkRxBytes, linkLost;

// Invia al RA4M1 lo stato della rete (per la matrice LED, il display e /api/info)
void linkSetNet(uint8_t state, const char* ip, const char* ap);

#pragma once
#include <Arduino.h>

// ---- Server web (web.cpp) ----
void webBegin();
void webPoll();                     // task "svc": aggiorna la copia dello stato del PLC
// dati del RA4M1 per mDNS e ricerca UDP (dall'ultima /api/info)
const char* webHost();              // "" finche' il RA4M1 non ha risposto
extern volatile const char* webWhere;   // diagnostica: richiesta in corso
void webDiscoveryJson(String& o);   // {"host":..,"ip":..,"prog":..,"run":..}

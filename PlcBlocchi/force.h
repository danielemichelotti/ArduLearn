#pragma once
#include "config.h"

// =====================================================================
//  Forzature, come "Forza" di TIA Portal:
//  - pin: un ingresso forzato fa leggere al programma il valore forzato al posto del pin
//    (0/1 per i digitali, 0..1023 per gli analogici); un'uscita forzata vale il valore
//    forzato qualunque cosa calcoli il programma, anche in STOP (0/1, o 0..255 se PWM);
//  - merker %M (bit) e %MW (parole): il valore forzato prevale su quello scritto dal programma.
//  Restano attive dopo un riavvio (memoria permanente) finche' non si tolgono.
//  "Comanda" invece scrive il valore una volta sola e poi il programma riprende il controllo.
// =====================================================================
enum : uint8_t { FK_PIN = 1, FK_MBIT = 2, FK_MWORD = 3 };
const uint8_t MAX_FORCES = 16;

struct Force {
  uint8_t kind;     // FK_*
  uint8_t idx;      // pin, numero del bit %M o della parola %MW
  int32_t v;
};

extern Force   g_forces[MAX_FORCES];
extern uint8_t g_nForces;

void forceLoad();                                   // all'avvio, dalla memoria permanente
// nullptr se va bene, altrimenti il motivo (testo in flash)
const __FlashStringHelper* forceSet(uint8_t kind, uint8_t idx, int32_t v);
const __FlashStringHelper* forceModify(uint8_t kind, uint8_t idx, int32_t v);   // "Comanda"
bool forceClear(uint8_t kind, uint8_t idx);
void forceClearAll();

bool forcedValue(uint8_t kind, uint8_t idx, int32_t& v);
inline bool pinForced(uint8_t pin, int32_t& v) { return g_nForces && forcedValue(FK_PIN, pin, v); }
bool mbitForced(uint8_t i);
bool mwordForced(uint8_t i);

void forceApplyMem();     // a inizio ciclo: merker forzati
void forceTick();         // nel loop: uscite forzate (anche in STOP o senza programma)
void forceJson(Print& o); // [[k,i,v],...] con k = "p" / "m" / "w"

#pragma once
#include "engine.h"

// =====================================================================
//  Moduli: sensori e attuatori che diventano blocchi
//
//  Ogni modulo e' un file mod_*.cpp in questa cartella. Contiene:
//   - uno o piu' ModuleDef in PROGMEM (tipo di blocco 70..127, ingressi, uscite,
//     funzioni check/begin/scan/stop);
//   - un commento /* @blocchi [...] */ con la descrizione JSON dei blocchi per
//     l'editor web (nome, ingressi, uscite, parametri, spiegazione).
//  tools/build_web.py genera modules_registry.cpp e web/modules.js:
//  per aggiungere un modulo basta copiare il file e ricompilare.
// =====================================================================

struct ModuleDef {
  uint8_t type, nIn, nOut, fbMask;
  // controlla i parametri k (es. i pin: devono essere configurati come PM_MODULE)
  bool (*check)(const int32_t* k, const uint8_t* pinModes, char* err, uint8_t errLen);
  void (*begin)(Block& b);                              // all'avvio del programma
  void (*scan)(Block& b, int32_t* out, uint32_t now);   // a ogni ciclo
  void (*stop)(Block& b);                               // quando il programma si ferma
};

bool findModule(uint8_t type, ModuleDef& out);
void printModuleList(Print& o);                         // JSON: tipi disponibili nel firmware

// Aiuti per i moduli
bool modPin(int32_t pin, const uint8_t* pm, char* err, uint8_t errLen);    // pin valido e riservato al modulo
bool modFail(char* err, uint8_t errLen, const __FlashStringHelper* msg);

// Generati da tools/build_web.py (modules_registry.cpp)
extern const ModuleDef* const MODULES[] PROGMEM;
extern const uint8_t NUM_MODULES;

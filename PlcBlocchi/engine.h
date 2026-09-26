#pragma once
#include "config.h"
#include "storage.h"

struct Block {
  uint8_t  type;
  uint8_t  flags;          // stato interno (fronti, timer attivo...)
  uint8_t  in[MAX_IN];     // sorgente: indiceBlocco*2 + uscita, oppure NC
  int32_t  k[4];           // parametri / valori degli ingressi non collegati.
                           // I blocchi che non usano tutti i k ci tengono il proprio stato
                           // (istante di partenza dei timer, ultimo valore PWM): vedi T_OF().
};

namespace Engine {
  extern Block    blocks[MAX_BLOCKS];
  extern int32_t  vals[MAX_BLOCKS][MAX_OUT];
  extern uint8_t  nBlocks;
  extern uint8_t  pinModes[NUM_PINS];
  extern char     name[25];
  extern bool     loaded;
  extern bool     running;
  extern char     error[48];
  extern uint16_t scanUs;      // tempo di ciclo medio (us)
  extern uint16_t scanMaxUs;   // tempo di ciclo massimo dall'ultima lettura
  extern bool     usesOled, usesLcd;
  extern uint8_t  mbits[NUM_MBITS / 8];   // memorie %M
  extern int32_t  mwords[NUM_MWORDS];     // memorie %MW
  extern uint8_t  pageOled, pageLcd;      // pagina mostrata dai display
  extern uint16_t imgBase, imgLen;        // zona immagini nel progetto attivo

  // Controlla un'immagine senza toccare il programma in esecuzione.
  bool validate(ImgSource& src, char* err, uint8_t errLen);
  // Carica ed avvia (se cfg.run) un'immagine gia' validata.
  bool load(ImgSource& src);
  void unload();

  void setRunning(bool r);
  void scan();

  bool setVirtual(uint8_t block, int32_t v);
  bool manualPin(uint8_t pin, uint8_t v);
  bool pinDriven(uint8_t pin);

  bool isReserved(uint8_t pin);
  bool isPwm(uint8_t pin);
  uint8_t typeInputs(uint8_t type);
  uint8_t typeOutputs(uint8_t type);

  const char* text(int32_t off);   // testo per i display (UTF-8), "" se assente
  int32_t input(const Block& b, uint8_t i);
  bool readImage(uint16_t off, void* dst, uint16_t n);   // legge dalla zona immagini
#if HAS_SCRIPT
  extern int32_t  scriptVars[SCRIPT_VARS];
#endif
  extern uint8_t  scriptOverrun;          // ultimo blocco script che ha superato il tetto (+1), 0 = nessuno
  const uint8_t*  code();                  // codice degli script (dopo i testi)
  uint8_t indexOf(const Block& b);
  bool connected(const Block& b, uint8_t i);
}

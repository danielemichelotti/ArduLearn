#pragma once
#include "config.h"

// Matrice LED 12x8 dell'UNO R4 WiFi (avvisi: rete, indirizzo, stato del PLC)
#if HAS_LED_MATRIX
void ledBegin();
void ledTick();
void ledHex(Print& o);    // quello che la matrice mostra: 12 colonne in esadecimale (per la pagina)
#endif

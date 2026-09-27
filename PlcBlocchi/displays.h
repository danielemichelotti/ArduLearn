#pragma once
#include "config.h"

// OLED SSD1306 128x64 e LCD HD44780 sono facoltativi: vengono cercati
// all'avvio e poi ogni pochi secondi, quindi si possono collegare a caldo.
void displaysBegin();          // (ri)applica la configurazione in cfg
void displaysTick();           // da chiamare nel loop
bool oledPresent();
bool lcdPresent();
void displaysShowStatus();     // forza la schermata di stato per qualche secondo
void displaysSplash();        // schermata di avvio (solo OLED)
#if HAS_LED_MATRIX
struct Block;
// Messaggio con i segnaposto {1} {2} (V1, V2) come le scritte su OLED: per la matrice LED
uint8_t displaysFormat(const Block& b, char* out, uint8_t size);
uint8_t displaysNextChar(const char*& s);   // prossimo carattere UTF-8 -> codice del font 5x7 (font5x7.h)
#endif

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

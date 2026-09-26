#pragma once
#include <Arduino.h>

// Driver minimo di solo testo per display OLED 128x64 I2C:
//  - SSD1306 (tipicamente 0,96")
//  - SH1106  (tipicamente 1,3": 132 colonne, visibili dalla 2 alla 129)
// Scrive 8 righe da 21 caratteri direttamente nella memoria del display,
// una "pagina" (8 pixel di altezza) per riga: non serve un buffer grafico in RAM.
enum : uint8_t { OLED_SSD1306 = 0, OLED_SH1106 = 1 };

class TextOled {
public:
  bool begin(uint8_t addr, uint8_t type);
  void end() { ok_ = false; }
  bool ok() const { return ok_; }
  // Scrive una riga (0..7) di 21 caratteri; codici: ASCII 32..126 e 0x80..0x86 (accentate, gradi)
  void drawRow(uint8_t row, const char* text, uint8_t len);
  // Scrive una riga di pixel gia' composta: 128 colonne, bit 0 = in alto
  void drawRaw(uint8_t row, const uint8_t* cols128);
  // Colonne (6 byte) del carattere, per comporre testo e immagini
  static void glyph(uint8_t ch, uint8_t* out6);

private:
  bool ok_ = false;
  uint8_t addr_ = 0x3C, colOffset_ = 0;
  void commands(const uint8_t* list, uint8_t n);
  void setPos(uint8_t page, uint8_t col);
};

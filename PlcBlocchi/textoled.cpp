#include "textoled.h"
#include "font5x7.h"
#include <Wire.h>

// Il buffer di Wire e' di 32 byte: 1 byte di controllo + 31 di dati per trasmissione
static const uint8_t CHUNK = 31;

void TextOled::commands(const uint8_t* list, uint8_t n) {
  Wire.beginTransmission(addr_);
  Wire.write(0x00);                                  // seguono comandi
  for (uint8_t i = 0; i < n; i++) Wire.write(list[i]);
  Wire.endTransmission();
}

void TextOled::setPos(uint8_t page, uint8_t col) {
  const uint8_t c[3] = { (uint8_t)(0xB0 | page), (uint8_t)(0x00 | (col & 0x0F)), (uint8_t)(0x10 | (col >> 4)) };
  commands(c, 3);
}

bool TextOled::begin(uint8_t addr, uint8_t type) {
  addr_ = addr;
  colOffset_ = type == OLED_SH1106 ? 2 : 0;
  Wire.beginTransmission(addr_);
  if (Wire.endTransmission() != 0) { ok_ = false; return false; }

  static const uint8_t common1[] = {
    0xAE,             // display spento
    0xD5, 0x80,       // clock
    0xA8, 0x3F,       // 64 righe
    0xD3, 0x00,       // nessuno spostamento verticale
    0x40,             // riga iniziale 0
  };
  static const uint8_t ssd1306[] = { 0x8D, 0x14, 0x20, 0x02 };   // charge pump, indirizzamento a pagine
  static const uint8_t sh1106[]  = { 0xAD, 0x8B };               // convertitore DC-DC acceso
  static const uint8_t common2[] = {
    0xA1,             // colonne da sinistra a destra
    0xC8,             // righe dall'alto in basso
    0xDA, 0x12,       // configurazione pin COM
    0x81, 0xCF,       // contrasto
    0xD9, 0xF1,       // precarica
    0xDB, 0x40,       // livello VCOMH
    0xA4, 0xA6,       // mostra la RAM, non invertito
  };
  commands(common1, sizeof(common1));
  if (type == OLED_SH1106) commands(sh1106, sizeof(sh1106));
  else commands(ssd1306, sizeof(ssd1306));
  commands(common2, sizeof(common2));

  // cancella tutta la memoria (anche le 132 colonne dell'SH1106)
  for (uint8_t p = 0; p < 8; p++) {
    setPos(p, 0);
    for (uint8_t sent = 0; sent < 132; sent += CHUNK) {
      Wire.beginTransmission(addr_);
      Wire.write(0x40);                              // seguono dati
      for (uint8_t i = 0; i < CHUNK && sent + i < 132; i++) Wire.write(0);
      Wire.endTransmission();
    }
  }
  static const uint8_t on[] = { 0xAF };
  commands(on, 1);
  ok_ = true;
  return true;
}

void TextOled::glyph(uint8_t ch, uint8_t* out) {
  uint16_t idx;
  if (ch >= FONT_FIRST && ch <= FONT_ASCII_LAST) idx = ch - FONT_FIRST;
  else if (ch >= FONT_EXTRA && ch < FONT_EXTRA + FONT_EXTRA_N) idx = FONT_ASCII_LAST - FONT_FIRST + 1 + (ch - FONT_EXTRA);
  else idx = '?' - FONT_FIRST;
  memcpy_P(out, FONT5X7 + idx * 5, 5);
  out[5] = 0;                                        // spazio tra i caratteri
}

void TextOled::drawRow(uint8_t row, const char* text, uint8_t len) {
  if (!ok_) return;
  setPos(row, colOffset_);
  uint8_t col[6];
  uint8_t pos = 0;                                   // colonna 0..127
  while (pos < 128) {
    Wire.beginTransmission(addr_);
    Wire.write(0x40);
    for (uint8_t i = 0; i < CHUNK && pos < 128; i++, pos++) {
      uint8_t c = pos / 6, x = pos % 6;
      if (c < len && pos < 126) { glyph((uint8_t)text[c], col); Wire.write(col[x]); }
      else Wire.write(0);
    }
    Wire.endTransmission();
  }
}

void TextOled::drawRaw(uint8_t row, const uint8_t* cols) {
  if (!ok_) return;
  setPos(row, colOffset_);
  for (uint8_t pos = 0; pos < 128;) {
    Wire.beginTransmission(addr_);
    Wire.write(0x40);
    for (uint8_t i = 0; i < CHUNK && pos < 128; i++, pos++) Wire.write(cols[pos]);
    Wire.endTransmission();
  }
}

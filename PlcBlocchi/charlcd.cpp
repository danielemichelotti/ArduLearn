#include "config.h"
#if HAS_LCD
#include "charlcd.h"
#include <Wire.h>

// Modulo PCF8574: P0 = RS, P1 = RW, P2 = E, P3 = retroilluminazione, P4..P7 = D4..D7
static const uint8_t I2C_RS = 0x01, I2C_EN = 0x04, I2C_BL = 0x08;

bool CharLcd::beginI2C(uint8_t a, uint8_t c, uint8_t r) {
  Wire.beginTransmission(a);
  if (Wire.endTransmission() != 0) { mode = 0; return false; }
  mode = 1; addr = a; cols = c; rows = r;
  init();
  return true;
}

bool CharLcd::beginParallel(const uint8_t p[6], uint8_t c, uint8_t r) {
  memcpy(pins, p, 6);
  for (uint8_t i = 0; i < 6; i++) { pinMode(pins[i], OUTPUT); digitalWrite(pins[i], LOW); }
  mode = 2; cols = c; rows = r;
  init();
  return true;   // in parallelo non si puo' verificare la presenza
}

void CharLcd::init() {
  delay(50);
  write4(0x03, false); delayMicroseconds(4500);
  write4(0x03, false); delayMicroseconds(4500);
  write4(0x03, false); delayMicroseconds(150);
  write4(0x02, false);                // modalita' 4 bit
  command(0x28);                      // 2 righe, font 5x8
  command(0x0C);                      // display acceso, cursore spento
  command(0x06);                      // scrittura verso destra
  clear();
}

void CharLcd::clear() {
  if (!mode) return;
  command(0x01);
  delayMicroseconds(2000);
}

void CharLcd::setCursor(uint8_t c, uint8_t r) {
  if (!mode) return;
  const uint8_t offs[4] = { 0x00, 0x40, cols, (uint8_t)(0x40 + cols) };
  if (r >= rows) r = rows - 1;
  command(0x80 | (offs[r] + c));
}

void CharLcd::write(uint8_t ch) {
  if (mode) send(ch, true);
}

void CharLcd::send(uint8_t v, bool rs) {
  write4(v >> 4, rs);
  write4(v & 0x0F, rs);
}

void CharLcd::write4(uint8_t nib, bool rs) {
  if (mode == 1) {
    uint8_t d = (nib << 4) | I2C_BL | (rs ? I2C_RS : 0);
    Wire.beginTransmission(addr);
    Wire.write(d | I2C_EN);
    Wire.write(d);                    // fronte di discesa di E: il dato viene letto
    Wire.endTransmission();
  } else if (mode == 2) {
    digitalWrite(pins[0], rs);
    for (uint8_t i = 0; i < 4; i++) digitalWrite(pins[2 + i], (nib >> i) & 1);
    digitalWrite(pins[1], HIGH);
    delayMicroseconds(1);
    digitalWrite(pins[1], LOW);
    delayMicroseconds(50);
  }
}
#endif  // HAS_LCD

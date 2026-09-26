#pragma once
#include <Arduino.h>

// Driver minimo per LCD a caratteri HD44780 (16x2, 20x4...) in modalita' 4 bit,
// collegato tramite modulo I2C PCF8574 oppure direttamente in parallelo.
class CharLcd {
public:
  bool beginI2C(uint8_t addr, uint8_t cols, uint8_t rows);
  bool beginParallel(const uint8_t pins[6], uint8_t cols, uint8_t rows);
  void end() { mode = 0; }
  bool ok() const { return mode != 0; }
  bool isI2C() const { return mode == 1; }
  uint8_t address() const { return addr; }

  void clear();
  void setCursor(uint8_t col, uint8_t row);
  void write(uint8_t ch);

private:
  uint8_t mode = 0;            // 0 = nessuno, 1 = I2C, 2 = parallelo
  uint8_t addr = 0, cols = 16, rows = 2;
  uint8_t pins[6];             // RS, E, D4..D7
  void init();
  void command(uint8_t v) { send(v, false); }
  void send(uint8_t v, bool rs);
  void write4(uint8_t nibble, bool rs);
};

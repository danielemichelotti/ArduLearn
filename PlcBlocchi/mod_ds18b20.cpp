#include "config.h"
#if HAS_MODULES
// Modulo: sonda di temperatura DS18B20 (1-Wire), un sensore per pin.
// Driver senza librerie; la conversione (750 ms) avviene "in background": il ciclo non si ferma.
#include "modules.h"

/* @blocchi
[{ "t": 72, "name": "Sonda DS18B20", "short": "DS18", "desc": "Temperatura da una sonda DS18B20 (anche impermeabile), in decimi di grado: 215 = 21,5 °C. Aggiornata circa ogni secondo. Serve una resistenza da 4,7 kΩ tra il pin dati e 5 V. OK vale 1 se il sensore risponde.",
   "ins": [], "outs": ["T×10", "OK"],
   "params": [{ "k": 0, "t": "mpin", "label": "Pin dati" }],
   "sim": [215, 1] }]
*/

static bool check(const int32_t* k, const uint8_t* pm, char* err, uint8_t n) { return modPin(k[0], pm, err, n); }

static bool owReset(uint8_t pin) {
  pinMode(pin, OUTPUT); digitalWrite(pin, LOW);
  delayMicroseconds(480);
  noInterrupts();
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(70);
  bool present = !digitalRead(pin);
  interrupts();
  delayMicroseconds(410);
  return present;
}
static void owWriteBit(uint8_t pin, bool v) {
  noInterrupts();
  pinMode(pin, OUTPUT); digitalWrite(pin, LOW);
  delayMicroseconds(v ? 6 : 60);
  pinMode(pin, INPUT_PULLUP);
  interrupts();
  delayMicroseconds(v ? 64 : 10);
}
static bool owReadBit(uint8_t pin) {
  noInterrupts();
  pinMode(pin, OUTPUT); digitalWrite(pin, LOW);
  delayMicroseconds(3);
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(10);
  bool v = digitalRead(pin);
  interrupts();
  delayMicroseconds(53);
  return v;
}
static void owWrite(uint8_t pin, uint8_t v) { for (uint8_t i = 0; i < 8; i++) owWriteBit(pin, (v >> i) & 1); }
static uint8_t owRead(uint8_t pin) { uint8_t v = 0; for (uint8_t i = 0; i < 8; i++) if (owReadBit(pin)) v |= 1 << i; return v; }
static uint8_t crc8(const uint8_t* d, uint8_t n) {
  uint8_t crc = 0;
  while (n--) { uint8_t in = *d++; for (uint8_t i = 0; i < 8; i++) { uint8_t mix = (crc ^ in) & 1; crc >>= 1; if (mix) crc ^= 0x8C; in >>= 1; } }
  return crc;
}

// k1 = istante dell'ultima richiesta; flags bit0 = conversione in corso
static void begin(Block& b) { pinMode(b.k[0], INPUT_PULLUP); b.k[1] = millis(); }
static void scan(Block& b, int32_t* out, uint32_t now) {
  uint8_t pin = b.k[0];
  if (!(b.flags & 1)) {
    if (!owReset(pin)) { out[1] = 0; b.k[1] = now; return; }
    owWrite(pin, 0xCC); owWrite(pin, 0x44);    // SKIP ROM, CONVERT T
    b.flags |= 1; b.k[1] = now;
    return;
  }
  if (now - (uint32_t)b.k[1] < 800) return;
  b.flags &= ~1;
  uint8_t d[9];
  if (!owReset(pin)) { out[1] = 0; return; }
  owWrite(pin, 0xCC); owWrite(pin, 0xBE);      // SKIP ROM, READ SCRATCHPAD
  for (uint8_t i = 0; i < 9; i++) d[i] = owRead(pin);
  if (crc8(d, 8) != d[8]) { out[1] = 0; return; }
  int16_t raw = (d[1] << 8) | d[0];
  out[0] = (int32_t)raw * 10 / 16;
  out[1] = 1;
}

extern const ModuleDef MOD_DS18B20 PROGMEM = { 72, 0, 2, 0, check, begin, scan, nullptr };
#endif  // HAS_MODULES

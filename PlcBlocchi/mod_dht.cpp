#include "config.h"
#if HAS_MODULES
// Modulo: sensore di temperatura e umidita' DHT11 / DHT22 (AM2302)
// Driver senza librerie: legge il sensore ogni 2 secondi (la lettura ferma il ciclo per ~25 ms).
#include "modules.h"

/* @blocchi
[{ "t": 71, "name": "Sensore DHT11/DHT22", "short": "DHT", "desc": "Temperatura e umidità da un sensore DHT11 o DHT22 (AM2302). I valori sono in decimi: 235 = 23,5 °C, 482 = 48,2 %. Aggiornati ogni 2 secondi. Collega il pin dati con una resistenza da 10 kΩ verso 5 V (i moduli su basetta ce l'hanno già).",
   "ins": [], "outs": ["T×10", "U×10"],
   "params": [{ "k": 0, "t": "mpin", "label": "Pin dati" }, { "k": 1, "t": "sel", "label": "Modello", "opts": ["DHT11", "DHT22 / AM2302"], "def": 1 }],
   "sim": [235, 482] }]
*/

static bool check(const int32_t* k, const uint8_t* pm, char* err, uint8_t n) { return modPin(k[0], pm, err, n); }

// conta i cicli di attesa finche' il pin resta al livello indicato (0xFFFF = timeout)
#ifdef __AVR__
static uint16_t expect(volatile uint8_t* port, uint8_t mask, bool level) {
  uint16_t c = 0;
  while (((*port & mask) != 0) == level) if (++c == 0xFFFF) return 0xFFFF;
  return c;
}
#define PORT_ARGS port, mask
#else
static uint16_t expect(uint8_t pin, bool level) {
  uint16_t c = 0;
  while ((digitalRead(pin) != 0) == level) if (++c == 0xFFFF) return 0xFFFF;
  return c;
}
#define PORT_ARGS pin
#endif

static bool readDht(uint8_t pin, bool dht22, int32_t& t10, int32_t& h10) {
  uint8_t d[5] = { 0 };
  uint16_t cyc[80];
#ifdef __AVR__
  volatile uint8_t* port = portInputRegister(digitalPinToPort(pin));
  uint8_t mask = digitalPinToBitMask(pin);
#endif
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(dht22 ? 2 : 20);                     // segnale di start
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(40);
  noInterrupts();
  bool ok = expect(PORT_ARGS, false) != 0xFFFF && expect(PORT_ARGS, true) != 0xFFFF;
  for (uint8_t i = 0; ok && i < 80; i += 2) {
    cyc[i] = expect(PORT_ARGS, false);
    cyc[i + 1] = expect(PORT_ARGS, true);
    if (cyc[i] == 0xFFFF || cyc[i + 1] == 0xFFFF) ok = false;   // sensore muto: non si aspetta 80 volte
  }
  interrupts();
  if (!ok) return false;
  for (uint8_t i = 0; i < 40; i++) {
    uint16_t lo = cyc[2 * i], hi = cyc[2 * i + 1];
    if (lo == 0xFFFF || hi == 0xFFFF) return false;
    d[i / 8] <<= 1;
    if (hi > lo) d[i / 8] |= 1;             // impulso alto lungo = 1
  }
  if (((d[0] + d[1] + d[2] + d[3]) & 0xFF) != d[4]) return false;
  if (dht22) {
    h10 = (d[0] << 8) | d[1];
    t10 = ((d[2] & 0x7F) << 8) | d[3];
    if (d[2] & 0x80) t10 = -t10;
  } else {
    h10 = d[0] * 10 + d[1];
    t10 = d[2] * 10 + (d[3] & 0x0F);
    if (d[3] & 0x80) t10 = -t10;
  }
  return true;
}

static void begin(Block& b) { pinMode(b.k[0], INPUT_PULLUP); b.k[2] = millis() - 1000; }
static void scan(Block& b, int32_t* out, uint32_t now) {
  if (now - (uint32_t)b.k[2] < 2000) return;
  b.k[2] = now;
  int32_t t, h;
  if (readDht(b.k[0], b.k[1] == 1, t, h)) { out[0] = t; out[1] = h; }
}

extern const ModuleDef MOD_DHT PROGMEM = { 71, 0, 2, 0, check, begin, scan, nullptr };
#endif  // HAS_MODULES

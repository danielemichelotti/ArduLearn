#include "config.h"
#if HAS_MODULES
// Modulo: motore passo-passo
//  - 28BYJ-48 con scheda ULN2003 (4 fili IN1..IN4, mezzo passo)
//  - driver STEP/DIR (A4988, DRV8825, TB6600...)
// Due modi: "velocita'" (gira a VEL passi/s) e "posizione" (va a TARGET a VEL passi/s).
// I passi vengono generati nel ciclo del PLC: adatto a velocita' moderate (qualche centinaio di passi/s).
#include "modules.h"

/* @blocchi
[{ "t": 76, "name": "Motore passo-passo", "short": "STEP", "desc": "Muove un motore passo-passo. Modo velocità: gira a VEL passi al secondo (negativo = al contrario) finché EN vale 1. Modo posizione: va alla posizione TARGET (in passi) alla velocità VEL e si ferma lì. AZZERA mette POS a 0 (lo zero, es. su un finecorsa). FERMO vale 1 quando il motore non si muove. Con il 28BYJ-48 (scheda ULN2003) servono 4 pin, con un driver STEP/DIR bastano i primi due. I passi sono generati dal ciclo del PLC: tieni VEL entro qualche centinaio.",
   "ins": [{ "n": "EN", "fb": true, "def": 1 }, { "n": "VEL", "fb": true, "def": 300, "unit": "p/s" }, { "n": "AZZERA" }, { "n": "TARGET", "fb": true, "def": 0 }], "outs": ["POS", "FERMO"],
   "params": [{ "k": 8, "t": "sel", "label": "Modo", "opts": ["Velocità (gira finché EN)", "Posizione (va a TARGET)"], "def": 0, "pack": [2, 29, 1] },
              { "k": 9, "t": "sel", "label": "Collegamento", "opts": ["28BYJ-48 + ULN2003 (4 pin)", "Driver STEP/DIR"], "def": 0, "pack": [2, 28, 1] },
              { "k": 4, "t": "mpin", "label": "IN1 / STEP", "pack": [2, 0, 7] }, { "k": 5, "t": "mpin", "label": "IN2 / DIR", "pack": [2, 7, 7] },
              { "k": 6, "t": "mpin", "label": "IN3 (solo ULN2003)", "pack": [2, 14, 7], "opt": true }, { "k": 7, "t": "mpin", "label": "IN4 (solo ULN2003)", "pack": [2, 21, 7], "opt": true }],
   "sim": [0, 1], "lad": { "power": 0 } }]
*/

static const uint8_t HALF[8] = { 0b0001, 0b0011, 0b0010, 0b0110, 0b0100, 0b1100, 0b1000, 0b1001 };
struct Slot { uint32_t last; uint8_t id; };
static Slot slots[2] = { { 0, 255 }, { 0, 255 } };

// k2 = 4 pin da 7 bit | collegamento << 28 | modo << 29
static uint8_t pinOf(const int32_t* k, uint8_t i) { return (k[2] >> (7 * i)) & 0x7F; }
static bool stepDir(const int32_t* k) { return (k[2] >> 28) & 1; }
static bool posMode(const int32_t* k) { return (k[2] >> 29) & 1; }

static bool check(const int32_t* k, const uint8_t* pm, char* err, uint8_t n) {
  uint8_t np = stepDir(k) ? 2 : 4;
  for (uint8_t i = 0; i < np; i++) if (!modPin(pinOf(k, i), pm, err, n)) return false;
  return true;
}
static void coils(const Block& b, uint8_t pattern) {
  for (uint8_t i = 0; i < 4; i++) digitalWrite(pinOf(b.k, i), (pattern >> i) & 1);
}
// flags bit0..2 = fase (ULN2003); la posizione e' in out[0]
static void begin(Block& b) {
  uint8_t np = stepDir(b.k) ? 2 : 4;
  for (uint8_t i = 0; i < np; i++) { pinMode(pinOf(b.k, i), OUTPUT); digitalWrite(pinOf(b.k, i), LOW); }
  uint8_t id = Engine::indexOf(b);
  for (uint8_t s = 0; s < 2; s++) if (slots[s].id == 255 || slots[s].id == id) { slots[s] = { micros(), id }; break; }
}
static void scan(Block& b, int32_t* out, uint32_t now) {
  (void)now;
  uint8_t id = Engine::indexOf(b), s = slots[0].id == id ? 0 : slots[1].id == id ? 1 : 255;
  if (s == 255) return;
  if (Engine::input(b, 2)) out[0] = 0;
  int32_t v = Engine::input(b, 1);
  int8_t d;
  if (posMode(b.k)) {                     // modo posizione: verso TARGET
    int32_t tg = Engine::input(b, 3);
    d = tg > out[0] ? 1 : tg < out[0] ? -1 : 0;
    if (v < 0) v = -v;
  } else d = v > 0 ? 1 : v < 0 ? -1 : 0;
  if (!Engine::input(b, 0) || !d || !v) {
    out[1] = 1;
    if (!stepDir(b.k)) coils(b, 0);      // fermo: bobine spente
    return;
  }
  out[1] = 0;
  uint32_t t = micros(), interval = 1000000UL / (uint32_t)constrain(v < 0 ? -v : v, 1, 5000);
  if (t - slots[s].last < interval) return;
  slots[s].last = t;
  out[0] += d;
  if (stepDir(b.k)) {
    digitalWrite(pinOf(b.k, 1), d > 0);
    digitalWrite(pinOf(b.k, 0), HIGH); delayMicroseconds(3); digitalWrite(pinOf(b.k, 0), LOW);
  } else {
    uint8_t ph = (b.flags + d) & 7;
    b.flags = (b.flags & ~7) | ph;
    coils(b, HALF[ph]);
  }
}
static void stop(Block& b) {
  if (!stepDir(b.k)) coils(b, 0);
  uint8_t id = Engine::indexOf(b);
  for (uint8_t s = 0; s < 2; s++) if (slots[s].id == id) slots[s].id = 255;
}

extern const ModuleDef MOD_STEPPER PROGMEM = { 76, 4, 2, 0b1011, check, begin, scan, stop };
#endif  // HAS_MODULES

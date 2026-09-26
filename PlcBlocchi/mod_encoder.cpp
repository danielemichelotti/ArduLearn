#include "config.h"
#if HAS_MODULES
// Modulo: encoder rotativo in quadratura (es. KY-040), letto con gli interrupt: non perde passi.
// Il pin A deve essere un pin di interrupt del Mega: 2, 3, 18 o 19. Al massimo 2 encoder.
#include "modules.h"

/* @blocchi
[{ "t": 74, "name": "Encoder rotativo", "short": "ENC", "desc": "Conta gli scatti di una manopola encoder (es. KY-040): POS aumenta girando in un senso e diminuisce nell'altro, restando fra MIN e MAX (utile per scegliere una voce di menu o regolare un valore). Un fronte su CARICA porta POS al valore di VAL. DIR = +1 o −1, l'ultimo verso di rotazione. Il pin A deve essere 2, 3, 18 o 19; il pulsante della manopola si collega come ingresso digitale normale.",
   "ins": [{ "n": "CARICA" }, { "n": "VAL", "fb": true, "def": 0 }, { "n": "MIN", "fb": true, "def": -100000 }, { "n": "MAX", "fb": true, "def": 100000 }], "outs": ["POS", "DIR"],
   "params": [{ "k": 4, "t": "mpin", "label": "Pin A / CLK (2, 3, 18, 19)", "only": [2, 3, 18, 19], "pack": [0, 0, 8] }, { "k": 5, "t": "mpin", "label": "Pin B / DT", "pack": [0, 8, 8] },
              { "k": 6, "t": "sel", "label": "Impulsi per scatto", "opts": ["1 (conta ogni fronte)", "2 (KY-040 e simili)"], "def": 1, "pack": [0, 16, 8] }],
   "sim": [0, 0], "lad": { "power": 0 } }]
*/

static volatile int32_t counts[2];
static volatile int8_t dirs[2];
static uint8_t pinA[2] = { 255, 255 }, pinB[2];

static void isr(uint8_t i) {
  bool a = digitalRead(pinA[i]), bb = digitalRead(pinB[i]);
  int8_t d = (a == bb) ? -1 : 1;
  counts[i] += d; dirs[i] = d;
}
static void isr0() { isr(0); }
static void isr1() { isr(1); }

// k0 = pin A | pin B << 8 | impulsi per scatto (0 = 1, 1 = 2) << 16
static uint8_t pA(const int32_t* k) { return k[0] & 0xFF; }
static uint8_t pB(const int32_t* k) { return (k[0] >> 8) & 0xFF; }
static uint8_t div2(const int32_t* k) { return ((k[0] >> 16) & 0xFF) ? 2 : 1; }
static int8_t slotOf(const Block& b) { for (uint8_t i = 0; i < 2; i++) if (pinA[i] == pA(b.k)) return i; return -1; }

static bool check(const int32_t* k, const uint8_t* pm, char* err, uint8_t n) {
  if (!modPin(pA(k), pm, err, n) || !modPin(pB(k), pm, err, n)) return false;
#if BOARD_MEGA
  if (digitalPinToInterrupt(pA(k)) == NOT_AN_INTERRUPT || pA(k) == 20 || pA(k) == 21)
    return modFail(err, n, F("il pin A deve essere 2, 3, 18 o 19"));
#else
  if (pA(k) != 2 && pA(k) != 3) return modFail(err, n, F("il pin A deve essere 2 o 3"));
#endif
  return true;
}
static void begin(Block& b) {
  int8_t i = pinA[0] == 255 ? 0 : pinA[1] == 255 ? 1 : -1;
  if (i < 0) return;                                             // piu' di 2 encoder: ignorato
  pinA[i] = pA(b.k); pinB[i] = pB(b.k);
  pinMode(pinA[i], INPUT_PULLUP); pinMode(pinB[i], INPUT_PULLUP);
  noInterrupts(); counts[i] = 0; interrupts();
  attachInterrupt(digitalPinToInterrupt(pinA[i]), i ? isr1 : isr0, CHANGE);
}
static void scan(Block& b, int32_t* out, uint32_t now) {
  (void)now;
  int8_t i = slotOf(b);
  if (i < 0) return;
  uint8_t d = div2(b.k);
  int32_t mn = Engine::input(b, 2), mx = Engine::input(b, 3);
  bool ld = Engine::input(b, 0) != 0;
  noInterrupts();
  if (ld && !(b.flags & 1)) counts[i] = Engine::input(b, 1) * d;
  int32_t pos = counts[i] / d;
  if (pos < mn) { pos = mn; counts[i] = mn * d; }                // resta fra MIN e MAX
  if (pos > mx) { pos = mx; counts[i] = mx * d; }
  int8_t dir = dirs[i];
  interrupts();
  b.flags = (b.flags & ~1) | ld;
  out[0] = pos;
  out[1] = dir;
}
static void stop(Block& b) {
  int8_t i = slotOf(b);
  if (i < 0) return;
  detachInterrupt(digitalPinToInterrupt(pinA[i]));
  pinA[i] = 255;
}

extern const ModuleDef MOD_ENCODER PROGMEM = { 74, 4, 2, 0b1110, check, begin, scan, stop };
#endif  // HAS_MODULES

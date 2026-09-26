#include "config.h"
#if HAS_MODULES
// Modulo: buzzer / altoparlante con tone()
// Nota: tone() usa il Timer2 del Mega, quindi mentre suona il PWM sui pin 9 e 10 non funziona.
#include "modules.h"

/* @blocchi
[{ "t": 70, "name": "Suono (tone)", "short": "TONE", "desc": "Suona una nota su un buzzer passivo o un piccolo altoparlante (con resistenza da 100 Ω). Finché EN vale 1 suona alla frequenza FREQ in Hz (es. 440 = La). Mentre suona, il PWM sui pin 9 e 10 non funziona.",
   "ins": [{ "n": "EN", "fb": true, "def": 1 }, { "n": "FREQ", "fb": true, "def": 440, "unit": "Hz" }], "outs": [],
   "params": [{ "k": 2, "t": "mpin", "label": "Pin del buzzer" }],
   "lad": { "power": 0 } }]
*/

static bool check(const int32_t* k, const uint8_t* pm, char* err, uint8_t n) { return modPin(k[2], pm, err, n); }
static void begin(Block& b) { pinMode(b.k[2], OUTPUT); b.k[3] = 0; }
static void scan(Block& b, int32_t* out, uint32_t now) {
  (void)out; (void)now;
  int32_t f = Engine::input(b, 0) ? constrain(Engine::input(b, 1), 0, 20000) : 0;
  if (f < 31) f = 0;                       // tone() non va sotto 31 Hz
  if (f == b.k[3]) return;                 // cambia solo se serve
  if (f) tone(b.k[2], f); else noTone(b.k[2]);
  b.k[3] = f;
}
static void stop(Block& b) { noTone(b.k[2]); b.k[3] = 0; }

extern const ModuleDef MOD_TONE PROGMEM = { 70, 2, 0, 0b0011, check, begin, scan, stop };
#endif  // HAS_MODULES

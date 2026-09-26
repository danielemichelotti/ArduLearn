#include "config.h"
#if HAS_MODULES
// Modulo: striscia di LED RGB WS2812 / NeoPixel (libreria Adafruit NeoPixel), una striscia per programma.
#include "modules.h"
#include <Adafruit_NeoPixel.h>

/* @blocchi
[{ "t": 77, "name": "Striscia LED RGB (WS2812)", "short": "WS2812", "desc": "Configura una striscia o un anello di LED RGB WS2812 (NeoPixel) con fino a 60 LED; LUMIN. è la luminosità generale (0–255). I colori si impostano con i blocchi \"Colore LED\". Alimenta i LED a 5 V (per molti LED con alimentatore separato).",
   "ins": [{ "n": "LUMIN.", "fb": true, "def": 60 }], "outs": [],
   "params": [{ "k": 1, "t": "mpin", "label": "Pin dati (DIN)" }, { "k": 2, "t": "num", "label": "Numero di LED (1–60)", "def": 8, "min": 1, "max": 60 }],
   "lad": { "power": null } },
 { "t": 78, "name": "Colore LED", "short": "RGB", "desc": "Dà un colore (R, G, B da 0 a 255) al LED numero N della striscia WS2812 (il primo è 0). Con N = −1 colora tutti i LED.",
   "ins": [{ "n": "N", "fb": true, "def": -1 }, { "n": "R", "fb": true, "def": 255 }, { "n": "G", "fb": true, "def": 0 }, { "n": "B", "fb": true, "def": 0 }], "outs": [],
   "params": [], "lad": { "power": null } }]
*/

static Adafruit_NeoPixel* strip = nullptr;
static bool dirty = false;
static uint32_t lastShow = 0;

static bool checkStrip(const int32_t* k, const uint8_t* pm, char* err, uint8_t n) {
  if (!modPin(k[1], pm, err, n)) return false;
  if (k[2] < 1 || k[2] > 60) return modFail(err, n, F("da 1 a 60 LED"));
  return true;
}
static void beginStrip(Block& b) {
  if (strip) { delete strip; strip = nullptr; }
  strip = new Adafruit_NeoPixel(b.k[2], b.k[1], NEO_GRB + NEO_KHZ800);
  if (!strip) return;
  strip->begin(); strip->clear(); strip->show();
  b.k[3] = -1;
}
static void scanStrip(Block& b, int32_t* out, uint32_t now) {
  (void)out;
  if (!strip) return;
  int32_t br = constrain(Engine::input(b, 0), 0, 255);
  if (br != b.k[3]) { strip->setBrightness(br); b.k[3] = br; dirty = true; }
  if (dirty && now - lastShow >= 20) { strip->show(); dirty = false; lastShow = now; }
}
static void stopStrip(Block& b) {
  (void)b;
  if (!strip) return;
  strip->clear(); strip->show();
  delete strip; strip = nullptr;
}

static void scanColor(Block& b, int32_t* out, uint32_t now) {
  (void)out; (void)now;
  if (!strip) return;
  int32_t n = Engine::input(b, 0);
  uint32_t c = strip->Color(constrain(Engine::input(b, 1), 0, 255), constrain(Engine::input(b, 2), 0, 255), constrain(Engine::input(b, 3), 0, 255));
  uint16_t cnt = strip->numPixels();
  for (uint16_t i = 0; i < cnt; i++) {
    if (n >= 0 && i != n) continue;
    if (strip->getPixelColor(i) != c) { strip->setPixelColor(i, c); dirty = true; }
  }
}

extern const ModuleDef MOD_WS2812 PROGMEM = { 77, 1, 0, 0b0001, checkStrip, beginStrip, scanStrip, stopStrip };
extern const ModuleDef MOD_RGB PROGMEM = { 78, 4, 0, 0b1111, nullptr, nullptr, scanColor, nullptr };
#endif  // HAS_MODULES

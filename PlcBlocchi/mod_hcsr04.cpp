#include "config.h"
#if HAS_MODULES
// Modulo: sensore a ultrasuoni HC-SR04
// Misura ogni 60 ms; l'attesa dell'eco ferma il ciclo al massimo per il tempo della distanza massima.
#include "modules.h"

/* @blocchi
[{ "t": 73, "name": "Distanza ultrasuoni HC-SR04", "short": "SR04", "desc": "Misura la distanza di un ostacolo in millimetri, circa 16 volte al secondo, finché EN vale 1. MAX = distanza massima in cm (più è grande, più a lungo il PLC aspetta l'eco: circa 6 ms per metro). OK vale 1 se è arrivato l'eco (ostacolo entro MAX).",
   "ins": [{ "n": "EN", "fb": true, "def": 1 }, { "n": "MAX", "fb": true, "def": 200, "unit": "cm" }], "outs": ["MM", "OK"],
   "params": [{ "k": 4, "t": "mpin", "label": "Pin TRIG", "pack": [2, 0, 8] }, { "k": 5, "t": "mpin", "label": "Pin ECHO", "pack": [2, 8, 8] }],
   "sim": [350, 1], "lad": { "power": 0 } }]
*/

static uint8_t trigPin(const int32_t* k) { return k[2] & 0xFF; }
static uint8_t echoPin(const int32_t* k) { return (k[2] >> 8) & 0xFF; }

static bool check(const int32_t* k, const uint8_t* pm, char* err, uint8_t n) {
  if (!modPin(trigPin(k), pm, err, n) || !modPin(echoPin(k), pm, err, n)) return false;
  if (trigPin(k) == echoPin(k)) return modFail(err, n, F("TRIG ed ECHO devono essere pin diversi"));
  return true;
}
// k3 = istante dell'ultima misura
static void begin(Block& b) {
  pinMode(trigPin(b.k), OUTPUT); digitalWrite(trigPin(b.k), LOW);
  pinMode(echoPin(b.k), INPUT);
  b.k[3] = millis();
}
static void scan(Block& b, int32_t* out, uint32_t now) {
  if (!Engine::input(b, 0) || now - (uint32_t)b.k[3] < 60) return;
  b.k[3] = now;
  digitalWrite(trigPin(b.k), HIGH); delayMicroseconds(10); digitalWrite(trigPin(b.k), LOW);
  uint32_t us = pulseIn(echoPin(b.k), HIGH, (uint32_t)constrain(Engine::input(b, 1), 10, 400) * 58 + 500);
  if (us) { out[0] = us * 10 / 58; out[1] = 1; }     // 58 us per cm (andata e ritorno)
  else out[1] = 0;
}

extern const ModuleDef MOD_HCSR04 PROGMEM = { 73, 2, 2, 0b0011, check, begin, scan, nullptr };
#endif  // HAS_MODULES

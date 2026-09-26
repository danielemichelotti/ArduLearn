// FILE GENERATO da tools/build_web.py a partire dai file mod_*.cpp: non modificare a mano.
#include "config.h"
#if HAS_MODULES
#include "modules.h"

extern const ModuleDef MOD_DHT;
extern const ModuleDef MOD_DS18B20;
extern const ModuleDef MOD_ENCODER;
extern const ModuleDef MOD_HCSR04;
extern const ModuleDef MOD_RTC;
extern const ModuleDef MOD_SERVO;
extern const ModuleDef MOD_STEPPER;
extern const ModuleDef MOD_TONE;
extern const ModuleDef MOD_WS2812;
extern const ModuleDef MOD_RGB;

const ModuleDef* const MODULES[] PROGMEM = {
  &MOD_DHT,
  &MOD_DS18B20,
  &MOD_ENCODER,
  &MOD_HCSR04,
  &MOD_RTC,
  &MOD_SERVO,
  &MOD_STEPPER,
  &MOD_TONE,
  &MOD_WS2812,
  &MOD_RGB,
};
const uint8_t NUM_MODULES = 10;
#endif  // HAS_MODULES

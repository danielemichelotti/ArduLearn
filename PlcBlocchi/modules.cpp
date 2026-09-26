#include "config.h"
#if HAS_MODULES
#include "modules.h"

bool findModule(uint8_t type, ModuleDef& out) {
  for (uint8_t i = 0; i < NUM_MODULES; i++) {
    const ModuleDef* p = (const ModuleDef*)pgm_read_ptr(&MODULES[i]);
    if (pgm_read_byte(&p->type) == type) { memcpy_P(&out, p, sizeof(ModuleDef)); return true; }
  }
  return false;
}

void printModuleList(Print& o) {
  o.print('[');
  for (uint8_t i = 0; i < NUM_MODULES; i++) {
    const ModuleDef* p = (const ModuleDef*)pgm_read_ptr(&MODULES[i]);
    if (i) o.print(',');
    o.print(pgm_read_byte(&p->type));
  }
  o.print(']');
}

bool modFail(char* err, uint8_t errLen, const __FlashStringHelper* msg) {
  if (err) { strncpy_P(err, (const char*)msg, errLen - 1); err[errLen - 1] = 0; }
  return false;
}

bool modPin(int32_t pin, const uint8_t* pm, char* err, uint8_t errLen) {
  if (pin < 0 || pin >= NUM_PINS) return modFail(err, errLen, F("pin non scelto"));
  if (Engine::isReserved(pin)) return modFail(err, errLen, F("pin riservato"));
  if (pm[pin] != PM_MODULE) return modFail(err, errLen, F("pin non assegnato al modulo"));
  return true;
}
#endif  // HAS_MODULES

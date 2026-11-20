#include "force.h"
#include "engine.h"
#include "storage.h"

Force   g_forces[MAX_FORCES];
uint8_t g_nForces = 0;

static uint16_t lastPwm[MAX_FORCES];   // ultimo valore scritto sulle uscite forzate (0xFFFF = da scrivere)

// ---------------------------------------------------------------------
//  Memoria permanente: 0xF5, numero, poi le forzature (6 byte ciascuna)
// ---------------------------------------------------------------------
static const uint8_t FORCE_MAGIC = 0xF5;

// noinline: il buffer non deve finire nello stack di chi chiama (es. il server web)
static __attribute__((noinline)) void save() {
  uint8_t buf[2 + MAX_FORCES * 6];
  buf[0] = FORCE_MAGIC;
  buf[1] = g_nForces;
  for (uint8_t i = 0; i < g_nForces; i++) {
    uint8_t* p = buf + 2 + i * 6;
    p[0] = g_forces[i].kind;
    p[1] = g_forces[i].idx;
    uint32_t v = (uint32_t)g_forces[i].v;
    p[2] = v; p[3] = v >> 8; p[4] = v >> 16; p[5] = v >> 24;
  }
  eeSaveBlock(EE_FORCE_ADDR, buf, 2 + g_nForces * 6);
}

__attribute__((noinline)) void forceLoad() {
  uint8_t h[2];
  eeLoadBlock(EE_FORCE_ADDR, h, 2);
  g_nForces = 0;
  if (h[0] != FORCE_MAGIC || h[1] > MAX_FORCES) return;
  for (uint8_t i = 0; i < h[1]; i++) {
    uint8_t p[6];
    eeLoadBlock(EE_FORCE_ADDR + 2 + i * 6, p, 6);
    Force f = { p[0], p[1], (int32_t)((uint32_t)p[2] | ((uint32_t)p[3] << 8) | ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24)) };
    bool ok = (f.kind == FK_PIN && f.idx < NUM_PINS && !Engine::isReserved(f.idx)) ||
              (f.kind == FK_MBIT && f.idx < NUM_MBITS) || (f.kind == FK_MWORD && f.idx < NUM_MWORDS);
    if (ok) { lastPwm[g_nForces] = 0xFFFF; g_forces[g_nForces++] = f; }
  }
}

// ---------------------------------------------------------------------
static int8_t find(uint8_t kind, uint8_t idx) {
  for (uint8_t i = 0; i < g_nForces; i++) if (g_forces[i].kind == kind && g_forces[i].idx == idx) return i;
  return -1;
}

bool forcedValue(uint8_t kind, uint8_t idx, int32_t& v) {
  int8_t i = find(kind, idx);
  if (i < 0) return false;
  v = g_forces[i].v;
  return true;
}

bool mbitForced(uint8_t i)  { int32_t v; return g_nForces && forcedValue(FK_MBIT, i, v); }
bool mwordForced(uint8_t i) { int32_t v; return g_nForces && forcedValue(FK_MWORD, i, v); }

// Controlla tipo, numero e valore; il valore viene adattato al tipo di pin
static const __FlashStringHelper* check(uint8_t kind, uint8_t idx, int32_t& v) {
  switch (kind) {
    case FK_PIN: {
      if (idx >= NUM_PINS || Engine::isReserved(idx)) return F("Pin non valido");
      uint8_t m = Engine::pinModes[idx];
      if (m == PM_ANALOG) { if (v < 0 || v > 1023) return F("Valore analogico: da 0 a 1023"); }
      else if (m == PM_OUT) { if (v < 0 || v > 255 || (v > 1 && !Engine::isPwm(idx))) return F("Uscita: 0 o 1 (0..255 sui pin PWM)"); }
      else if (m == PM_IN || m == PM_IN_PULLUP) { if (v != 0 && v != 1) return F("Ingresso digitale: 0 o 1"); }
      else return F("Pin non configurato come ingresso o uscita");
      return nullptr;
    }
    case FK_MBIT:  if (idx >= NUM_MBITS) return F("Merker non valido"); v = v != 0; return nullptr;
    case FK_MWORD: if (idx >= NUM_MWORDS) return F("Parola %MW non valida"); return nullptr;
  }
  return F("Tipo non valido");
}

const __FlashStringHelper* forceSet(uint8_t kind, uint8_t idx, int32_t v) {
  const __FlashStringHelper* e = check(kind, idx, v);
  if (e) return e;
  int8_t i = find(kind, idx);
  if (i < 0) {
    if (g_nForces >= MAX_FORCES) return F("Troppe forzature (al massimo 16)");
    i = g_nForces++;
  }
  g_forces[i] = { kind, idx, v };
  lastPwm[i] = 0xFFFF;
  save();
  forceApplyMem();
  forceTick();
  return nullptr;
}

bool forceClear(uint8_t kind, uint8_t idx) {
  int8_t i = find(kind, idx);
  if (i < 0) return false;
  for (uint8_t j = i; j + 1 < g_nForces; j++) { g_forces[j] = g_forces[j + 1]; lastPwm[j] = lastPwm[j + 1]; }
  g_nForces--;
  save();
  // un'uscita non piu' forzata: in STOP torna spenta, in RUN la riscrive il programma
  if (kind == FK_PIN && Engine::pinModes[idx] == PM_OUT && !Engine::running) digitalWrite(idx, LOW);
  return true;
}

void forceClearAll() {
  while (g_nForces) forceClear(g_forces[g_nForces - 1].kind, g_forces[g_nForces - 1].idx);
}

const __FlashStringHelper* forceModify(uint8_t kind, uint8_t idx, int32_t v) {
  const __FlashStringHelper* e = check(kind, idx, v);
  if (e) return e;
  int32_t fv;
  if (forcedValue(kind, idx, fv)) return F("E' forzato: togli prima la forzatura");
  switch (kind) {
    case FK_PIN:
      if (Engine::pinModes[idx] != PM_OUT) return F("Comanda: solo uscite e merker");
      if (v > 1) analogWrite(idx, v); else digitalWrite(idx, v ? HIGH : LOW);
      break;
    case FK_MBIT:
      if (v) Engine::mbits[idx >> 3] |= 1 << (idx & 7); else Engine::mbits[idx >> 3] &= ~(1 << (idx & 7));
      break;
    case FK_MWORD:
      Engine::mwords[idx] = v;
      break;
  }
  return nullptr;
}

// ---------------------------------------------------------------------
void forceApplyMem() {
  for (uint8_t i = 0; i < g_nForces; i++) {
    const Force& f = g_forces[i];
    if (f.kind == FK_MBIT) {
      if (f.v) Engine::mbits[f.idx >> 3] |= 1 << (f.idx & 7); else Engine::mbits[f.idx >> 3] &= ~(1 << (f.idx & 7));
    } else if (f.kind == FK_MWORD) {
      Engine::mwords[f.idx] = f.v;
    }
  }
}

void forceTick() {
  for (uint8_t i = 0; i < g_nForces; i++) {
    const Force& f = g_forces[i];
    if (f.kind != FK_PIN || Engine::pinModes[f.idx] != PM_OUT) continue;
    uint16_t v = (uint16_t)constrain(f.v, 0, 255);
    if (v <= 1) {
      // digitale: si riscrive se il pin non ha il valore giusto (es. dopo "Prova uscita" o un nuovo programma)
      if (lastPwm[i] != v || digitalRead(f.idx) != (v ? HIGH : LOW)) digitalWrite(f.idx, v ? HIGH : LOW);
    } else if (lastPwm[i] != v) {
      analogWrite(f.idx, v);
    }
    lastPwm[i] = v;
  }
}

void forceJson(Print& o) {
  o.print('[');
  for (uint8_t i = 0; i < g_nForces; i++) {
    if (i) o.print(',');
    o.print(F("[\""));
    o.print(g_forces[i].kind == FK_PIN ? 'p' : g_forces[i].kind == FK_MBIT ? 'm' : 'w');
    o.print(F("\","));
    o.print(g_forces[i].idx);
    o.print(',');
    o.print(g_forces[i].v);
    o.print(']');
  }
  o.print(']');
}

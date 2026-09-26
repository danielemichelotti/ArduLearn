#include "config.h"
#if HAS_MODULES
// Modulo: servomotore, al massimo 4 servo.
// Mega: driver senza librerie. Il Timer5 conta liberamente (tick da 0,5 us) e il confronto B
// (il confronto A e' usato da tone()) genera gli impulsi uno dopo l'altro, poi la pausa fino a 20 ms.
// Nota: usando il Timer5, il PWM sui pin 44, 45 e 46 non funziona quando c'e' un servo.
// UNO R4: libreria Servo.
#include "modules.h"
#if BOARD_R4
#include <Servo.h>
#endif

/* @blocchi
[{ "t": 75, "name": "Servomotore", "short": "SERVO", "desc": "Porta un servomotore (es. SG90) all'angolo ANGOLO (0–180°): collega un filo all'ingresso (cursore, potenziometro scalato, script...) oppure lascia il valore fisso. VEL = velocità in gradi al secondo (0 = scatto immediato). EN = 0 spegne gli impulsi e il servo resta libero. Uscite: POS = angolo attuale, OK = 1 quando è arrivato. Al massimo 4 servo; con i servo il PWM sui pin 44, 45 e 46 non funziona.",
   "ins": [{ "n": "ANGOLO", "fb": true, "def": 90, "unit": "°" }, { "n": "VEL", "fb": true, "def": 0, "unit": "°/s" }, { "n": "EN", "fb": true, "def": 1 }], "outs": ["POS", "OK"],
   "params": [{ "k": 3, "t": "mpin", "label": "Pin del segnale" }],
   "sim": [90, 1], "lad": { "power": 2 } }]
*/

static const uint8_t MAXS = 4;
static uint8_t owner[MAXS] = { 255, 255, 255, 255 };            // indice del blocco
static int32_t pos100[MAXS];                    // angolo attuale x100 (per il movimento graduale)
static uint32_t lastMs[MAXS];

#if BOARD_MEGA
static const uint16_t PERIOD = 40000;           // 20 ms
static volatile uint8_t sPin[MAXS] = { 255, 255, 255, 255 };   // 255 = slot libero
static volatile bool sOn[MAXS];                 // impulsi attivi
static volatile uint16_t sTicks[MAXS];          // durata dell'impulso (tick da 0,5 us)
static volatile int8_t cur = -1;                // servo con l'impulso in corso, -1 = pausa
static volatile uint16_t used = 0;              // tick gia' usati nel periodo
static bool timerOn = false;

ISR(TIMER5_COMPB_vect) {
  if (cur >= 0 && sPin[cur] != 255) digitalWrite(sPin[cur], LOW);   // fine dell'impulso
  int8_t n = cur + 1;
  while (n < MAXS && (sPin[n] == 255 || !sOn[n])) n++;
  if (n < MAXS) {                                                   // impulso del servo successivo
    cur = n;
    digitalWrite(sPin[n], HIGH);
    OCR5B += sTicks[n];
    used += sTicks[n];
  } else {                                                          // pausa fino ai 20 ms
    cur = -1;
    OCR5B += used + 200 < PERIOD ? PERIOD - used : 200;
    used = 0;
  }
}

static void timerStart() {
  if (timerOn) return;
  noInterrupts();
  TCCR5A = 0; TCNT5 = 0;
  TCCR5B = (1 << CS51);                          // modo normale, prescaler 8
  OCR5B = 200;
  cur = -1; used = 0;
  TIFR5 = (1 << OCF5B);
  TIMSK5 |= (1 << OCIE5B);
  interrupts();
  timerOn = true;
}

static void hwAttach(uint8_t i, uint8_t pin) {
  pinMode(pin, OUTPUT); digitalWrite(pin, LOW);
  noInterrupts(); sTicks[i] = 3000; sOn[i] = false; sPin[i] = pin; interrupts();
  timerStart();
}
static void hwSet(uint8_t i, uint16_t ticks, bool on) { noInterrupts(); sTicks[i] = ticks; sOn[i] = on; interrupts(); }
static void hwDetach(uint8_t i) {
  uint8_t p = sPin[i];
  noInterrupts(); sPin[i] = 255; sOn[i] = false; interrupts();
  digitalWrite(p, LOW);
}
#else
static Servo servos[MAXS];
static uint8_t sPin[MAXS];
static void hwAttach(uint8_t i, uint8_t pin) { sPin[i] = pin; pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
static void hwSet(uint8_t i, uint16_t ticks, bool on) {          // ticks da 0,5 us
  if (!on) { if (servos[i].attached()) { servos[i].detach(); digitalWrite(sPin[i], LOW); } return; }
  if (!servos[i].attached()) servos[i].attach(sPin[i], 500, 2500);   // come sul Mega: 0,5..2,5 ms
  servos[i].writeMicroseconds(ticks / 2);
}
static void hwDetach(uint8_t i) { hwSet(i, 0, false); }
#endif
static int8_t slotOf(const Block& b) {
  uint8_t id = Engine::indexOf(b);
  for (uint8_t i = 0; i < MAXS; i++) if (owner[i] == id) return i;
  return -1;
}

static bool check(const int32_t* k, const uint8_t* pm, char* err, uint8_t n) { return modPin(k[3], pm, err, n); }
static void begin(Block& b) {
  for (uint8_t i = 0; i < MAXS; i++) if (owner[i] == 255) {
    owner[i] = Engine::indexOf(b);
    pos100[i] = -1; lastMs[i] = millis();
    hwAttach(i, b.k[3]);
    return;
  }
}
static void scan(Block& b, int32_t* out, uint32_t now) {
  int8_t i = slotOf(b);
  if (i < 0) return;
  bool en = Engine::input(b, 2) != 0;
  int32_t target = constrain(Engine::input(b, 0), 0, 180) * 100L, vel = Engine::input(b, 1);
  uint32_t dt = now - lastMs[i];
  lastMs[i] = now;
  if (pos100[i] < 0 || vel <= 0) pos100[i] = target;               // primo valore o scatto immediato
  else {
    int32_t step = min((int32_t)((uint32_t)vel * 100UL * dt / 1000UL), 18000L);
    if (step < 1) step = 1;
    if (pos100[i] < target) pos100[i] = min(pos100[i] + step, target);
    else if (pos100[i] > target) pos100[i] = max(pos100[i] - step, target);
  }
  uint16_t t = 1000 + (uint32_t)pos100[i] * 4000UL / 18000UL;       // 0,5 ms .. 2,5 ms
  hwSet(i, t, en);
  out[0] = pos100[i] / 100;
  out[1] = pos100[i] == target;
}
static void stop(Block& b) {
  int8_t i = slotOf(b);
  if (i < 0) return;
  hwDetach(i);
  owner[i] = 255;
}

extern const ModuleDef MOD_SERVO PROGMEM = { 75, 3, 2, 0b0111, check, begin, scan, stop };
#endif  // HAS_MODULES

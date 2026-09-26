#include "engine.h"
#include "modules.h"
#include "script.h"

namespace Engine {

Block    blocks[MAX_BLOCKS];
int32_t  vals[MAX_BLOCKS][MAX_OUT];
uint8_t  nBlocks = 0;
uint8_t  pinModes[NUM_PINS];
char     name[25];
bool     loaded = false;
bool     running = false;
char     error[48];
uint16_t scanUs = 0, scanMaxUs = 0;
bool     usesOled = false, usesLcd = false;
uint8_t  mbits[NUM_MBITS / 8];
int32_t  mwords[NUM_MWORDS];
uint8_t  pageOled = 0, pageLcd = 0;
uint16_t imgBase = 0, imgLen = 0;

static char     textPool[MAX_POOL];         // testi, poi codice degli script
static uint16_t textLen = 0, codeLen = 0;
#if HAS_SCRIPT
int32_t  scriptVars[SCRIPT_VARS];
#endif
uint8_t  scriptOverrun = 0;
static uint8_t  driven[(NUM_PINS + 7) / 8];     // pin pilotati da un blocco
static void modulesCall(bool start);

// ---------------------------------------------------------------------
//  Tabella dei tipi: ingressi, uscite, maschera degli ingressi che,
//  se non collegati, usano il valore costante k[i]
// ---------------------------------------------------------------------
struct TypeInfo { uint8_t type, nIn, nOut, fbMask; };
static const TypeInfo TYPES[] PROGMEM = {
  { BT_DIN, 0, 1, 0 },     { BT_DOUT, 1, 0, 0 },    { BT_AIN, 0, 1, 0 },
  { BT_PWM, 1, 0, 0 },     { BT_CONST, 0, 1, 0 },   { BT_VSWITCH, 0, 1, 0 },
  { BT_VSLIDER, 0, 1, 0 }, { BT_MONITOR, 1, 0, 0 },
  { BT_AND, 4, 1, 0 },     { BT_OR, 4, 1, 0 },      { BT_XOR, 4, 1, 0 },
  { BT_NAND, 4, 1, 0 },    { BT_NOR, 4, 1, 0 },     { BT_NOT, 1, 1, 0 },
  { BT_SR, 2, 2, 0 },      { BT_EDGE, 1, 1, 0 },    { BT_TOGGLE, 2, 1, 0 },
  { BT_TON, 2, 2, 0b0010 },{ BT_TOF, 2, 2, 0b0010 },{ BT_TP, 2, 2, 0b0010 },
  { BT_BLINK, 3, 1, 0b0111 }, { BT_CTUD, 4, 2, 0b1000 },
  { BT_MATH, 2, 1, 0b0011 },  { BT_CMP, 2, 1, 0b0011 }, { BT_SCALE, 1, 1, 0 },
  { BT_HYST, 1, 1, 0 },    { BT_SELECT, 3, 1, 0b0110 }, { BT_LIMIT, 1, 1, 0 },
  { BT_OLED, 4, 0, 0b1100 },
  { BT_PAGE, 4, 1, 0b1000 },{ BT_MGET, 0, 1, 0 },       { BT_MSET, 2, 0, 0b0011 },
  { BT_STEP, 4, 2, 0b1000 },{ BT_REPEAT, 4, 2, 0b1110 },
#if HAS_LCD
  { BT_LCD, 4, 0, 0b1100 },
#endif
#if HAS_IMAGES
  { BT_OLEDIMG, 3, 0, 0b0111 },
#endif
#if HAS_SCRIPT
  { BT_SCRIPT, 4, 2, 0 },   { BT_CSCRIPT, 4, 2, 0 },  { BT_CEXT, 4, 2, 0 },
#endif
};

static bool typeInfo(uint8_t type, TypeInfo& out) {
  if (type >= BT_MODULE_FIRST) {
#if HAS_MODULES
    ModuleDef m;
    if (!findModule(type, m)) return false;
    out.type = type; out.nIn = m.nIn; out.nOut = m.nOut; out.fbMask = m.fbMask;
    return true;
#else
    return false;
#endif
  }
  for (uint8_t i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); i++) {
    memcpy_P(&out, &TYPES[i], sizeof(TypeInfo));
    if (out.type == type) return true;
  }
  return false;
}

uint8_t typeInputs(uint8_t type)  { TypeInfo t; return typeInfo(type, t) ? t.nIn : 0; }
uint8_t typeOutputs(uint8_t type) { TypeInfo t; return typeInfo(type, t) ? t.nOut : 0; }

// Maschera di fallback per tipo, in RAM per l'uso nel ciclo di scansione
static uint8_t fbMaskOf(uint8_t type) {
  switch (type) {
    case BT_TON: case BT_TOF: case BT_TP: return 0b0010;
    case BT_BLINK: return 0b0111;
    case BT_CTUD: return 0b1000;
    case BT_MATH: case BT_CMP: return 0b0011;
    case BT_SELECT: return 0b0110;
    case BT_OLED: case BT_LCD: return 0b1100;
    case BT_OLEDIMG: return 0b0111;
    case BT_MSET: return 0b0011;
    case BT_PAGE: case BT_STEP: return 0b1000;
    case BT_REPEAT: return 0b1110;
    default:
#if HAS_MODULES
      if (type >= BT_MODULE_FIRST) { ModuleDef m; return findModule(type, m) ? m.fbMask : 0; }
#endif
      return 0;
  }
}

bool isReserved(uint8_t pin) {
  for (uint8_t p : RESERVED_PINS) if (p == pin) return true;
  return cfgPinReservedByLcd(pin);
}

bool isPwm(uint8_t pin) {
#if BOARD_MEGA
  return (pin >= 2 && pin <= 13) || (pin >= 44 && pin <= 46);
#else
  return pin == 3 || pin == 5 || pin == 6 || pin == 9 || pin == 10 || pin == 11;
#endif
}

static void pinName(uint8_t pin, char* out) {
  if (pin >= FIRST_APIN) snprintf_P(out, 5, PSTR("A%u"), pin - FIRST_APIN);
  else snprintf_P(out, 5, PSTR("D%u"), pin);
}

static inline void setDriven(uint8_t pin) { driven[pin >> 3] |= 1 << (pin & 7); }
bool pinDriven(uint8_t pin) { return pin < NUM_PINS && (driven[pin >> 3] >> (pin & 7)) & 1; }

// ---------------------------------------------------------------------
//  Lettura / validazione dell'immagine
// ---------------------------------------------------------------------
static inline uint16_t u16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static inline int32_t  i32(const uint8_t* p) {
  return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

#define FAIL(...) do { if (err) snprintf_P(err, errLen, __VA_ARGS__); return false; } while (0)

static bool parse(ImgSource& src, bool apply, char* err, uint8_t errLen) {
  uint8_t hdr[40];
  uint16_t size = src.size();
  if (size < IMG_HDR_LEN || !src.read(0, hdr, sizeof(hdr))) FAIL(PSTR("Nessun programma"));
  if (hdr[0] != 'P' || hdr[1] != 'B') FAIL(PSTR("File non valido"));
  if (hdr[2] != IMG_VERSION) FAIL(PSTR("Versione %u non supportata"), hdr[2]);

  uint8_t  n       = hdr[3];
  uint16_t total   = u16(hdr + 4);
  uint16_t tOff    = u16(hdr + 6), tLen = u16(hdr + 8);
  uint16_t mOff    = u16(hdr + 10), mLen = u16(hdr + 12);
  uint16_t iOff    = u16(hdr + 14), iLen = iOff ? mOff - iOff : 0;
  uint16_t cLen    = u16(hdr + 16);
  if (n > MAX_BLOCKS) FAIL(PSTR("Troppi blocchi (%u, max %u)"), n, MAX_BLOCKS);
  if (total > size || total > maxImage()) FAIL(PSTR("Programma troppo grande"));
  if ((uint32_t)tLen + cLen > MAX_POOL) FAIL(PSTR("Testi+script troppo lunghi (max %u byte)"), MAX_POOL);
  if (IMG_HDR_LEN + (uint16_t)n * IMG_BLOCK_LEN > tOff || (uint32_t)tOff + tLen + cLen > total ||
      (uint32_t)mOff + mLen > total || (iOff && (iOff < tOff + tLen + cLen || iOff > mOff)))
    FAIL(PSTR("Struttura non valida"));

  uint8_t pm[PIN_IMG_LEN];
  if (!src.read(40, pm, PIN_IMG_LEN)) FAIL(PSTR("Errore di lettura"));
  for (uint8_t p = NUM_PINS; p < PIN_IMG_LEN; p++)
    if (pm[p] != PM_NONE) FAIL(PSTR("Il progetto usa pin che questa scheda non ha"));
  char pn[5];
  for (uint8_t p = 0; p < NUM_PINS; p++) {
    if (pm[p] > PM_MODULE) FAIL(PSTR("Modo pin non valido"));
    pinName(p, pn);
    if (pm[p] != PM_NONE && isReserved(p)) FAIL(PSTR("Il pin %s e' riservato"), pn);
    if (pm[p] == PM_ANALOG && p < FIRST_APIN) FAIL(PSTR("%s non e' analogico"), pn);
  }

  // primo passaggio: solo i tipi (servono per controllare i collegamenti)
  uint8_t types[MAX_BLOCKS];
  for (uint8_t i = 0; i < n; i++)
    if (!src.read(IMG_HDR_LEN + i * IMG_BLOCK_LEN, &types[i], 1)) FAIL(PSTR("Errore di lettura"));

  bool oled = false, lcd = false;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t raw[IMG_BLOCK_LEN];
    if (!src.read(IMG_HDR_LEN + i * IMG_BLOCK_LEN, raw, IMG_BLOCK_LEN)) FAIL(PSTR("Errore di lettura"));
    TypeInfo ti;
    if (!typeInfo(raw[0], ti)) FAIL(PSTR("Blocco %u: tipo %u non disponibile su questa scheda"), i + 1, raw[0]);
    int32_t k[4];
    for (uint8_t j = 0; j < 4; j++) k[j] = i32(raw + 5 + 4 * j);

    for (uint8_t j = 0; j < MAX_IN; j++) {
      uint8_t s = raw[1 + j];
      if (s == NC) continue;
      if (j >= ti.nIn) FAIL(PSTR("Blocco %u: ingresso %u inesistente"), i + 1, j + 1);
      uint8_t sb = s >> 1, so = s & 1;
      if (sb >= n || so >= typeOutputs(types[sb])) FAIL(PSTR("Blocco %u: filo non valido"), i + 1);
    }

    // controlli specifici per tipo
    uint8_t pin = (uint8_t)k[0];
    switch (ti.type) {
      case BT_DIN: case BT_AIN: case BT_DOUT: case BT_PWM:
        if (k[0] < 0 || k[0] >= NUM_PINS) FAIL(PSTR("Blocco %u: pin non scelto"), i + 1);
        pinName(pin, pn);
        if (ti.type == BT_DIN && pm[pin] != PM_IN && pm[pin] != PM_IN_PULLUP)
          FAIL(PSTR("Blocco %u: %s non e' un ingresso"), i + 1, pn);
        if (ti.type == BT_AIN && pm[pin] != PM_ANALOG)
          FAIL(PSTR("Blocco %u: %s non e' analogico"), i + 1, pn);
        if ((ti.type == BT_DOUT || ti.type == BT_PWM) && pm[pin] != PM_OUT)
          FAIL(PSTR("Blocco %u: %s non e' un'uscita"), i + 1, pn);
        if (ti.type == BT_PWM && !isPwm(pin)) FAIL(PSTR("Blocco %u: %s non ha PWM"), i + 1, pn);
        break;
      case BT_MATH: if (k[2] < 0 || k[2] > 6) FAIL(PSTR("Blocco %u: operazione"), i + 1); break;
      case BT_CMP:  if (k[2] < 0 || k[2] > 5) FAIL(PSTR("Blocco %u: confronto"), i + 1); break;
      case BT_OLED: case BT_LCD:
        // k0 = riga | colonna << 8 | effetto << 16; k1 = offset del testo; ingressi V1, V2, EN, PAGINA
        if (k[1] >= (int32_t)tLen) FAIL(PSTR("Blocco %u: testo"), i + 1);
        if (ti.type == BT_OLED) oled = true; else lcd = true;
        break;
#if HAS_IMAGES
      case BT_OLEDIMG: {
        // k3 = offset | x << 16 | riga << 23; ingressi EN, FOTOGRAMMA, PAGINA
        uint16_t off = k[3] & 0xFFFF;
        uint8_t ih[4];
        if (off + 4u > iLen || !src.read(iOff + off, ih, 4)) FAIL(PSTR("Blocco %u: immagine"), i + 1);
        if (!ih[0] || ih[0] > 128 || !ih[1] || ih[1] > 64 || (ih[1] & 7) || !ih[2] ||
            off + 4u + (uint32_t)ih[2] * ih[0] * (ih[1] / 8) > iLen) FAIL(PSTR("Blocco %u: immagine"), i + 1);
        oled = true;
        break;
      }
#endif
#if HAS_SCRIPT
      case BT_CEXT:
        if (!i || types[i - 1] != BT_CSCRIPT) FAIL(PSTR("Blocco %u: estensione senza script C"), i + 1);
        break;
      case BT_SCRIPT: case BT_CSCRIPT:
        // k0 = offset del codice, k1 = lunghezza, k2 = prima variabile, k3 = numero di variabili
        if (k[0] < 0 || k[1] < 1 || k[0] + k[1] > (int32_t)cLen) FAIL(PSTR("Blocco %u: script"), i + 1);
        if (k[2] < 0 || k[3] < 0 || k[2] + k[3] > SCRIPT_VARS) FAIL(PSTR("Blocco %u: troppe variabili negli script"), i + 1);
        break;
#endif
      default:
#if HAS_MODULES
        if (ti.type >= BT_MODULE_FIRST) {
          ModuleDef m;
          findModule(ti.type, m);
          if (m.check && !m.check(k, pm, err, errLen)) {
            if (err) { char tmp[40]; strncpy(tmp, err, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0; snprintf_P(err, errLen, PSTR("Blocco %u: %s"), i + 1, tmp); }
            return false;
          }
        }
#endif
        break;
      case BT_PAGE:
        if (k[0] < 0 || k[0] > 2 || k[1] < 1 || k[1] > MAX_PAGES) FAIL(PSTR("Blocco %u: pagine"), i + 1);
        break;
      case BT_MGET: case BT_MSET: {
        uint8_t idx = k[2] & 0xFF, typ = (k[2] >> 8) & 0xFF;
        if (typ > 1 || idx >= (typ ? NUM_MWORDS : NUM_MBITS)) FAIL(PSTR("Blocco %u: memoria"), i + 1);
        if (ti.type == BT_MSET && (k[3] < 0 || k[3] > 3)) FAIL(PSTR("Blocco %u: memoria"), i + 1);
        break;
      }
    }

    if (apply) {
      Block& b = blocks[i];
      b.type = raw[0];
      b.flags = 0;
      memcpy(b.in, raw + 1, MAX_IN);
      memcpy(b.k, k, sizeof(k));
    }
  }

  if (apply) {
    nBlocks = n;
    memcpy(pinModes, pm, NUM_PINS);
    memcpy(name, hdr + 18, 22);
    name[22] = 0;
    textLen = tLen;
    codeLen = cLen;
    if (tLen + cLen && !src.read(tOff, textPool, tLen + cLen)) FAIL(PSTR("Errore di lettura"));
    if (tLen) textPool[tLen - 1] = 0;
    usesOled = oled;
    usesLcd = lcd;
    imgBase = iOff;
    imgLen = iLen;
  }
  return true;
}

bool validate(ImgSource& src, char* err, uint8_t errLen) {
  return parse(src, false, err, errLen);
}

// ---------------------------------------------------------------------
//  Pin
// ---------------------------------------------------------------------
static void applyPinModes() {
  memset(driven, 0, sizeof(driven));
  for (uint8_t i = 0; i < nBlocks; i++) {
    uint8_t t = blocks[i].type;
    if (t == BT_DOUT || t == BT_PWM) setDriven((uint8_t)blocks[i].k[0]);
  }
  for (uint8_t p = 0; p < NUM_PINS; p++) {
    if (isReserved(p)) continue;
    switch (pinModes[p]) {
      case PM_MODULE:    break;                        // lo configura il modulo
      case PM_OUT:       digitalWrite(p, LOW); pinMode(p, OUTPUT); break;
      case PM_IN_PULLUP: pinMode(p, INPUT_PULLUP); break;
      default:           pinMode(p, INPUT); break;   // libero, ingresso, analogico
    }
  }
}

static void outputsOff() {
  for (uint8_t p = 0; p < NUM_PINS; p++)
    if (pinModes[p] == PM_OUT && !isReserved(p)) digitalWrite(p, LOW);  // spegne anche il PWM
}

static void clearMemory() {
  memset(mbits, 0, sizeof(mbits));
  memset(mwords, 0, sizeof(mwords));
#if HAS_SCRIPT
  memset(scriptVars, 0, sizeof(scriptVars));
#endif
  scriptOverrun = 0;
  pageOled = pageLcd = 0;
}

static void resetState() {
  clearMemory();
  for (uint8_t i = 0; i < nBlocks; i++) {
    Block& b = blocks[i];
    if (b.type == BT_OLEDIMG) continue;
    b.flags = 0;
    if (b.type == BT_VSWITCH) continue;                     // gli ingressi virtuali restano
    if (b.type == BT_VSLIDER) { vals[i][0] = constrain(vals[i][0], b.k[0], b.k[1]); continue; }
    vals[i][0] = vals[i][1] = 0;
  }
}

bool load(ImgSource& src) {
  running = false;
  memset(vals, 0, sizeof(vals));
  clearMemory();
  if (!parse(src, true, error, sizeof(error))) {
    unload();
    return false;
  }
  error[0] = 0;
  loaded = true;
  for (uint8_t i = 0; i < nBlocks; i++)
    if (blocks[i].type == BT_VSLIDER) vals[i][0] = blocks[i].k[0];
  applyPinModes();
  setRunning(cfg.run);
  return true;
}

void unload() {
  outputsOff();
  running = false;
  loaded = false;
  nBlocks = 0;
  textLen = 0;
  usesOled = usesLcd = false;
  imgBase = imgLen = 0;
  name[0] = 0;
  memset(pinModes, 0, sizeof(pinModes));
  applyPinModes();
}

void setRunning(bool r) {
  if (!loaded) r = false;
  if (r && !running) { resetState(); modulesCall(true); }
  if (!r && running) modulesCall(false);
  if (!r) outputsOff();
  running = r;
}

bool setVirtual(uint8_t bi, int32_t v) {
  if (bi >= nBlocks) return false;
  Block& b = blocks[bi];
  if (b.type == BT_VSWITCH) vals[bi][0] = v ? 1 : 0;
  else if (b.type == BT_VSLIDER) vals[bi][0] = constrain(v, b.k[0], b.k[1]);
  else return false;
  return true;
}

bool manualPin(uint8_t pin, uint8_t v) {
  if (pin >= NUM_PINS || isReserved(pin) || pinModes[pin] != PM_OUT) return false;
  if (running && pinDriven(pin)) return false;
  digitalWrite(pin, v ? HIGH : LOW);
  return true;
}

bool readImage(uint16_t off, void* dst, uint16_t n) {
#if !HAS_IMAGES
  (void)off; (void)dst; (void)n;
  return false;
#endif
  if (!imgBase || (uint32_t)off + n > imgLen) return false;
  ImgSource* src = openActive();
  bool ok = src->read(imgBase + off, dst, n);
  delete src;
  return ok;
}

static inline bool getBit(uint8_t i) { return (mbits[i >> 3] >> (i & 7)) & 1; }
static inline void setBit(uint8_t i, bool v) {
  if (v) mbits[i >> 3] |= 1 << (i & 7); else mbits[i >> 3] &= ~(1 << (i & 7));
}

const uint8_t* code() { return (const uint8_t*)textPool + textLen; }
uint8_t indexOf(const Block& b) { return &b - blocks; }

static void modulesCall(bool start) {
#if !HAS_MODULES
  (void)start;
  return;
#endif
  for (uint8_t i = 0; i < nBlocks; i++) {
    if (blocks[i].type < BT_MODULE_FIRST) continue;
    ModuleDef m;
    if (!findModule(blocks[i].type, m)) continue;
    if (start && m.begin) m.begin(blocks[i]);
    if (!start && m.stop) m.stop(blocks[i]);
  }
}

const char* text(int32_t off) {
  if (off < 0 || off >= (int32_t)textLen) return "";
  return textPool + off;
}

// ---------------------------------------------------------------------
//  Esecuzione
// ---------------------------------------------------------------------
bool connected(const Block& b, uint8_t i) { return b.in[i] != NC; }

int32_t input(const Block& b, uint8_t i) {
  uint8_t s = b.in[i];
  if (s != NC) return vals[s >> 1][s & 1];
  return (fbMaskOf(b.type) >> i) & 1 ? b.k[i] : 0;
}

// Slot k[] libero usato come stato (istante di partenza / ultimo valore)
static inline int32_t& T_OF(Block& b) {
  switch (b.type) {
    case BT_BLINK: return b.k[3];
    case BT_PWM:   return b.k[1];
    case BT_STEP:  return b.k[1];          // ingresso T senza valore di riserva
    default:       return b.k[0];          // TON, TOF, TP: k[0] non e' usato
  }
}

void scan() {
  if (!running) return;
  uint32_t t0 = micros();
  uint32_t now = millis();

  for (uint8_t i = 0; i < nBlocks; i++) {
    Block& b = blocks[i];
    int32_t* out = vals[i];

    switch (b.type) {
      // ---- ingressi / uscite ----
      case BT_DIN: {
        bool v = digitalRead((uint8_t)b.k[0]);
        out[0] = b.k[1] ? !v : v;
        break;
      }
      case BT_AIN:   out[0] = analogRead((uint8_t)b.k[0]); break;
      case BT_DOUT: {
        uint8_t v = input(b, 0) ? HIGH : LOW;
        if (!(b.flags & 0x80) || (b.flags & 1) != v) {       // scrive solo se cambia
          digitalWrite((uint8_t)b.k[0], v);
          b.flags = 0x80 | v;
        }
        break;
      }
      case BT_PWM: {
        int32_t v = constrain(input(b, 0), 0, 255);
        if (!(b.flags & 0x80) || T_OF(b) != v) {
          analogWrite((uint8_t)b.k[0], v);
          T_OF(b) = v;
          b.flags = 0x80;
        }
        break;
      }
      case BT_CONST:   out[0] = b.k[0]; break;
      case BT_VSWITCH: case BT_VSLIDER: case BT_MONITOR: case BT_OLED: case BT_LCD: case BT_OLEDIMG: break;

      // ---- sequenze e ripetizioni ----
      case BT_STEP: {
        // Passo (GRAFCET): X = passo attivo; AVANTI = impulso di un ciclo quando la
        // condizione T e' vera (e il passo e' attivo da almeno MIN ms): va all'IN del passo dopo.
        if (!(b.flags & 1)) { b.flags |= 1; out[0] = b.k[0] ? 1 : 0; T_OF(b) = now; }
        out[1] = 0;
        if (input(b, 2)) { out[0] = b.k[0] ? 1 : 0; T_OF(b) = now; break; }
        if (out[0]) {
          if (input(b, 1) && (int32_t)(now - (uint32_t)T_OF(b)) >= input(b, 3)) { out[0] = 0; out[1] = 1; }
        } else if (input(b, 0)) { out[0] = 1; T_OF(b) = now; }
        break;
      }
      case BT_REPEAT: {
        // Ripeti N volte: sul fronte di START genera N impulsi (ON ms acceso, OFF ms spento).
        // out0 = Q, out1 = impulsi rimasti (0 = finito). flags: bit0 START precedente, bit1 fase ON
        bool st = input(b, 0) != 0;
        if (st && !(b.flags & 1) && input(b, 1) > 0) { out[1] = input(b, 1); out[0] = 1; b.flags |= 2; T_OF(b) = now; }
        b.flags = (b.flags & ~1) | (st ? 1 : 0);
        if (out[1] > 0) {
          uint32_t el = now - (uint32_t)T_OF(b);
          if ((b.flags & 2) && (int32_t)el >= input(b, 2)) { out[0] = 0; b.flags &= ~2; T_OF(b) = now; if (--out[1] == 0) break; }
          else if (!(b.flags & 2) && (int32_t)el >= input(b, 3)) { out[0] = 1; b.flags |= 2; T_OF(b) = now; }
        } else out[0] = 0;
        break;
      }
#if HAS_SCRIPT
      case BT_SCRIPT:
        if (!runScript(b, out)) scriptOverrun = i + 1;
        break;
      case BT_CSCRIPT: {
        bool ext = i + 1 < nBlocks && blocks[i + 1].type == BT_CEXT;
        if (!runScript(b, out, ext ? &blocks[i + 1] : nullptr, ext ? vals[i + 1] : nullptr)) scriptOverrun = i + 1;
        break;
      }
      case BT_CEXT: break;                 // lo aggiorna lo script C che lo precede
#endif

      // ---- memorie (LADDER) ----
      case BT_MGET: {
        uint8_t idx = b.k[2] & 0xFF;
        out[0] = (b.k[2] >> 8) & 0xFF ? mwords[idx] : getBit(idx);
        break;
      }
      case BT_MSET: {
        uint8_t idx = b.k[2] & 0xFF;
        bool word = (b.k[2] >> 8) & 0xFF, en = input(b, 0) != 0;
        int32_t cur = word ? mwords[idx] : getBit(idx), nv = cur;
        switch (b.k[3]) {
          case 0: nv = en; break;                        // bobina ( )
          case 1: if (en) nv = 1; break;                 // (S)
          case 2: if (en) nv = 0; break;                 // (R)
          case 3: if (en) nv = input(b, 1); break;       // MOVE / uscita di un box
        }
        if (word) mwords[idx] = nv; else setBit(idx, nv != 0);
        break;
      }

      // ---- pagine dei display ----
      case BT_PAGE: {
        bool nx = input(b, 0) != 0, pv = input(b, 1) != 0, st = input(b, 2) != 0;
        uint8_t n = b.k[1];
        int16_t cur = b.k[0] == 1 ? pageLcd : pageOled;
        if (nx && !(b.flags & 1)) cur = (cur + 1) % n;
        if (pv && !(b.flags & 2)) cur = (cur + n - 1) % n;
        if (st && !(b.flags & 4)) cur = constrain(input(b, 3), 0, n - 1);
        if (cur >= n) cur = n - 1;
        b.flags = (nx ? 1 : 0) | (pv ? 2 : 0) | (st ? 4 : 0);
        if (b.k[0] != 1) pageOled = cur;
        if (b.k[0] != 0) pageLcd = cur;
        out[0] = cur;
        break;
      }

      // ---- logica ----
      case BT_AND: case BT_OR: case BT_XOR: case BT_NAND: case BT_NOR: {
        uint8_t n = 0, ones = 0;
        for (uint8_t j = 0; j < MAX_IN; j++)
          if (connected(b, j)) { n++; if (input(b, j)) ones++; }
        bool all = n && ones == n, any = ones > 0;
        switch (b.type) {
          case BT_AND:  out[0] = all; break;
          case BT_OR:   out[0] = any; break;
          case BT_XOR:  out[0] = ones & 1; break;
          case BT_NAND: out[0] = !all; break;
          case BT_NOR:  out[0] = !any; break;
        }
        break;
      }
      case BT_NOT: out[0] = input(b, 0) == 0; break;
      case BT_SR: {
        bool s = input(b, 0) != 0, r = input(b, 1) != 0, q = out[0];
        if (b.k[0]) { if (r) q = false; if (s) q = true; }    // prevale SET
        else        { if (s) q = true;  if (r) q = false; }   // prevale RESET
        out[0] = q;
        out[1] = !q;
        break;
      }
      case BT_EDGE: {
        bool cur = input(b, 0) != 0, prev = b.flags & 1;
        switch (b.k[0]) {
          case 0:  out[0] = cur && !prev; break;
          case 1:  out[0] = !cur && prev; break;
          default: out[0] = cur != prev; break;
        }
        b.flags = cur;
        break;
      }
      case BT_TOGGLE: {
        bool cur = input(b, 0) != 0;
        if (cur && !(b.flags & 1)) out[0] = !out[0];
        if (input(b, 1)) out[0] = 0;
        b.flags = cur;
        break;
      }

      // ---- tempo e conteggio ----
      case BT_TON: {
        int32_t pt = input(b, 1);
        if (input(b, 0)) {
          if (!(b.flags & 1)) { T_OF(b) = now; b.flags |= 1; }
          uint32_t et = now - (uint32_t)T_OF(b);
          if ((int32_t)et >= pt) { out[0] = 1; out[1] = pt; }
          else { out[0] = 0; out[1] = et; }
        } else {
          b.flags = 0; out[0] = 0; out[1] = 0;
        }
        break;
      }
      case BT_TOF: {
        int32_t pt = input(b, 1);
        if (input(b, 0)) {
          out[0] = 1; out[1] = 0; b.flags = 2;          // bit1 = ingresso era alto
        } else {
          if (b.flags & 2) { T_OF(b) = now; b.flags = 1; }  // fronte di discesa: parte il timer
          if (b.flags & 1) {
            uint32_t et = now - (uint32_t)T_OF(b);
            if ((int32_t)et >= pt) { out[0] = 0; out[1] = pt; b.flags = 0; }
            else { out[0] = 1; out[1] = et; }
          }
        }
        break;
      }
      case BT_TP: {
        int32_t pt = input(b, 1);
        bool in = input(b, 0) != 0;
        bool rise = in && !(b.flags & 2);
        if (in) b.flags |= 2; else b.flags &= ~2;
        if (rise && !(b.flags & 1)) { T_OF(b) = now; b.flags |= 1; }
        if (b.flags & 1) {
          uint32_t et = now - (uint32_t)T_OF(b);
          if ((int32_t)et >= pt) { b.flags &= ~1; out[0] = 0; out[1] = pt; }
          else { out[0] = 1; out[1] = et; }
        } else {
          out[0] = 0;
          if (!in) out[1] = 0;
        }
        break;
      }
      case BT_BLINK: {
        if (!input(b, 0)) { out[0] = 0; b.flags = 0; break; }
        if (!(b.flags & 1)) { b.flags = 1; T_OF(b) = now; out[0] = 1; }
        int32_t tOn = input(b, 1), tOff = input(b, 2);
        uint32_t el = now - (uint32_t)T_OF(b);
        if (out[0] && (int32_t)el >= tOn)       { out[0] = 0; T_OF(b) = now; }
        else if (!out[0] && (int32_t)el >= tOff) { out[0] = 1; T_OF(b) = now; }
        break;
      }
      case BT_CTUD: {
        // k0 = 0: contatore avanti/indietro (R azzera, Q = CV >= PV)
        // k0 = 1: contatore all'indietro come CTD (R carica PV, Q = CV <= 0)
        bool cu = input(b, 0) != 0, cd = input(b, 1) != 0;
        int32_t pv = input(b, 3);
        if (input(b, 2)) out[0] = b.k[0] == 1 ? pv : 0;
        else {
          if (cu && !(b.flags & 1)) out[0]++;
          if (cd && !(b.flags & 2)) out[0]--;
        }
        b.flags = (cu ? 1 : 0) | (cd ? 2 : 0);
        out[1] = b.k[0] == 1 ? out[0] <= 0 : out[0] >= pv;
        break;
      }

      // ---- analogico e matematica ----
      case BT_MATH: {
        int32_t a = input(b, 0), c = input(b, 1);
        switch (b.k[2]) {
          case 0: out[0] = a + c; break;
          case 1: out[0] = a - c; break;
          case 2: out[0] = a * c; break;
          case 3: out[0] = c ? a / c : 0; break;
          case 4: out[0] = c ? a % c : 0; break;
          case 5: out[0] = min(a, c); break;
          case 6: out[0] = max(a, c); break;
        }
        break;
      }
      case BT_CMP: {
        int32_t a = input(b, 0), c = input(b, 1);
        switch (b.k[2]) {
          case 0: out[0] = a > c; break;
          case 1: out[0] = a >= c; break;
          case 2: out[0] = a < c; break;
          case 3: out[0] = a <= c; break;
          case 4: out[0] = a == c; break;
          case 5: out[0] = a != c; break;
        }
        break;
      }
      case BT_SCALE: {
        int32_t x = input(b, 0);
        if (b.k[1] == b.k[0]) out[0] = b.k[2];
        else out[0] = (int32_t)((int64_t)(x - b.k[0]) * (b.k[3] - b.k[2]) / (b.k[1] - b.k[0])) + b.k[2];
        break;
      }
      case BT_HYST: {
        int32_t x = input(b, 0);
        if (x <= b.k[1]) out[0] = 0;
        if (x >= b.k[0]) out[0] = 1;
        break;
      }
      case BT_SELECT: out[0] = input(b, 0) ? input(b, 2) : input(b, 1); break;
      case BT_LIMIT:  out[0] = constrain(input(b, 0), b.k[0], b.k[1]); break;
      default:
#if HAS_MODULES
        if (b.type >= BT_MODULE_FIRST) {
          ModuleDef m;
          if (findModule(b.type, m) && m.scan) m.scan(b, out, now);
        }
#endif
        break;
    }
  }

  uint16_t dt = min(micros() - t0, 65535UL);
  scanUs = scanUs ? (uint16_t)((scanUs * 15UL + dt) / 16) : dt;
  if (dt > scanMaxUs) scanMaxUs = dt;
}

} // namespace Engine

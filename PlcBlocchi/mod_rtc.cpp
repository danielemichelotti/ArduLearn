#include "config.h"
#if HAS_MODULES
// Modulo: orologio RTC DS3231 (I2C, indirizzo 0x68, sullo stesso bus del display)
// Driver senza librerie. L'ora si imposta dalla pagina (Impostazioni -> Orologio).
#include "modules.h"
#include <Wire.h>

/* @blocchi
[{ "t": 79, "name": "Orologio RTC DS3231", "short": "RTC", "desc": "Legge ora e data da un modulo orologio DS3231 (con batteria: tiene l'ora anche a scheda spenta). Collegalo ai pin SDA/SCL come il display. Scegli cosa mettere nelle due uscite; l'ora si imposta in Impostazioni → Orologio.",
   "ins": [], "outs": ["A", "B"],
   "params": [{ "k": 0, "t": "sel", "label": "Uscita A", "opts": ["ore", "minuti", "secondi", "giorno", "mese", "anno", "giorno della settimana (1 = lunedì)", "ora come HHMM (es. 1435)", "minuti dalla mezzanotte", "secondi dalla mezzanotte"], "def": 0 },
              { "k": 1, "t": "sel", "label": "Uscita B", "opts": ["ore", "minuti", "secondi", "giorno", "mese", "anno", "giorno della settimana (1 = lunedì)", "ora come HHMM (es. 1435)", "minuti dalla mezzanotte", "secondi dalla mezzanotte"], "def": 1 }],
   "sim": [10, 30], "lad": { "power": null } }]
*/

static const uint8_t ADDR = 0x68;
static uint8_t now_[7];            // secondi, minuti, ore, giorno settimana, giorno, mese, anno (binario)
static uint32_t lastRead = 0;
static bool valid = false;

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 15); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool rtcRead(uint8_t* t) {
  Wire.beginTransmission(ADDR); Wire.write(0);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(ADDR, (uint8_t)7) != 7) return false;
  for (uint8_t i = 0; i < 7; i++) t[i] = Wire.read();
  t[0] = bcd2bin(t[0] & 0x7F); t[1] = bcd2bin(t[1]); t[2] = bcd2bin(t[2] & 0x3F);
  t[3] = t[3] & 7; t[4] = bcd2bin(t[4]); t[5] = bcd2bin(t[5] & 0x1F); t[6] = bcd2bin(t[6]);
  return true;
}
// Usata da web.cpp (/api/rtc): anno a 2 cifre, giorno della settimana 1 = lunedi'
bool rtcSet(uint8_t yy, uint8_t mo, uint8_t dd, uint8_t hh, uint8_t mi, uint8_t ss, uint8_t dow) {
  Wire.beginTransmission(ADDR);
  Wire.write(0);
  Wire.write(bin2bcd(ss)); Wire.write(bin2bcd(mi)); Wire.write(bin2bcd(hh));
  Wire.write(dow); Wire.write(bin2bcd(dd)); Wire.write(bin2bcd(mo)); Wire.write(bin2bcd(yy));
  bool ok = Wire.endTransmission() == 0;
  lastRead = 0;
  return ok;
}

static int32_t field(uint8_t f) {
  switch (f) {
    case 0: return now_[2];
    case 1: return now_[1];
    case 2: return now_[0];
    case 3: return now_[4];
    case 4: return now_[5];
    case 5: return 2000 + now_[6];
    case 6: return now_[3];
    case 7: return now_[2] * 100 + now_[1];
    case 8: return now_[2] * 60 + now_[1];
    case 9: return (int32_t)now_[2] * 3600 + now_[1] * 60 + now_[0];
  }
  return 0;
}
static void scan(Block& b, int32_t* out, uint32_t now) {
  if (!lastRead || now - lastRead >= 500) { valid = rtcRead(now_); lastRead = now ? now : 1; }
  if (!valid) return;
  out[0] = field(b.k[0]);
  out[1] = field(b.k[1]);
}

extern const ModuleDef MOD_RTC PROGMEM = { 79, 0, 2, 0, nullptr, nullptr, scan, nullptr };
#endif  // HAS_MODULES

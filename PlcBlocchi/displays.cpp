#include "displays.h"
#include "engine.h"
#include "storage.h"
#if HAS_LCD
#include "charlcd.h"
#endif
#include <Wire.h>
#include "net.h"
#include "textoled.h"
#include "splash_logo.h"

static TextOled oled;
#if HAS_LCD
static CharLcd lcd;
#endif
static bool oledOk = false;
static uint16_t rowHash[OLED_ROWS];                     // per ridisegnare solo le righe cambiate
static char grid[OLED_ROWS][OLED_COLS];                 // testo da mostrare sull'OLED
#if HAS_LCD
// l'LCD viene disegnato dopo l'OLED: puo' usare lo stesso buffer di lavoro
static_assert(sizeof(grid) >= LCD_MAX_ROWS * LCD_MAX_COLS, "buffer LCD troppo piccolo");
static char (*const lcdWant)[LCD_MAX_COLS] = (char (*)[LCD_MAX_COLS])&grid[0][0];
static char lcdHave[LCD_MAX_ROWS][LCD_MAX_COLS];
#endif
static uint32_t lastDraw = 0, lastProbe = 0, statusUntil = 0, splashUntil = 0;

bool oledPresent() { return oledOk; }
#if HAS_LCD
bool lcdPresent()  { return lcd.ok(); }
#else
bool lcdPresent()  { return false; }
#endif
void displaysShowStatus() { statusUntil = millis() + 15000UL; }

static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void tryOled() {
  oledOk = oled.begin(cfg.oledAddr, cfg.oledType);
  memset(rowHash, 0xFF, sizeof(rowHash));
}

static void tryLcd() {
#if HAS_LCD
  lcd.end();
  if (cfg.lcdType == LCD_I2C) lcd.beginI2C(cfg.lcdAddr, cfg.lcdCols, cfg.lcdRows);
  else if (cfg.lcdType == LCD_PARALLEL) lcd.beginParallel(cfg.lcdPins, cfg.lcdCols, cfg.lcdRows);
  memset(lcdHave, ' ', sizeof(lcdHave));
#endif
}

void displaysBegin() {
  tryOled();
  tryLcd();
  displaysShowStatus();
}

// ---------------------------------------------------------------------
//  Testo: UTF-8 -> set di caratteri del display
// ---------------------------------------------------------------------
static uint8_t nextChar(const char*& s, bool forOled) {
  uint8_t c = *s++;
  if (c < 0x80) return c;
  if ((c == 0xC2 || c == 0xC3) && (*s & 0xC0) == 0x80) {
    uint8_t cp = ((c & 0x03) << 6) | (*s++ & 0x3F);     // U+0080..U+00FF
    static const uint8_t from[] PROGMEM = { 0xE0, 0xE8, 0xE9, 0xEC, 0xF2, 0xF9, 0xB0, 0xC8, 0xC0 };
    static const uint8_t font[] PROGMEM = { 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 'E', 'A' };  // vedi font5x7.h
    static const uint8_t ascii[] PROGMEM = { 'a', 'e', 'e', 'i', 'o', 'u', 0xDF, 'E', 'A' };
    for (uint8_t i = 0; i < sizeof(from); i++)
      if (pgm_read_byte(&from[i]) == cp) return pgm_read_byte(forOled ? &font[i] : &ascii[i]);
    return '?';
  }
  while ((*s & 0xC0) == 0x80) s++;                       // salta il resto del carattere
  return '?';
}

static void putText(char* row, uint8_t width, uint8_t& col, const char* s, bool forOled) {
  while (*s && col < width) row[col++] = nextChar(s, forOled);
}

// Numero con larghezza minima (zeri a sinistra) e decimali: 235, dec 1 -> "23.5"; 7, width 2 -> "07"
static uint8_t formatValue(char* buf, uint8_t size, int32_t v, uint8_t width, uint8_t dec) {
  char tmp[14];
  uint32_t a = v < 0 ? -(uint32_t)v : v;
  uint8_t n = 0;
  do { tmp[n++] = '0' + a % 10; a /= 10; } while (a && n < sizeof(tmp));
  while (n < dec + 1 && n < sizeof(tmp)) tmp[n++] = '0';            // almeno "0.x"
  while (n < width && n < sizeof(tmp)) tmp[n++] = '0';
  uint8_t o = 0;
  if (v < 0 && o < size - 1) buf[o++] = '-';
  while (n && o < size - 1) { if (dec && n == dec && o < size - 2) buf[o++] = '.'; buf[o++] = tmp[--n]; }
  buf[o] = 0;
  return o;
}

// Messaggio: il testo puo' contenere {1} e {2} (valori V1 e V2), anche con formato:
// {1.1} = un decimale, {1:2} = almeno 2 cifre con zeri a sinistra, {1:2.1} = entrambi.
static uint8_t formatMessage(const Block& b, char* out, uint8_t size) {
  const char* t = Engine::text(b.k[1]);
  uint8_t o = 0;
  while (*t && o < size - 1) {
    if (t[0] == '{' && (t[1] == '1' || t[1] == '2')) {
      const char* p = t + 2;
      uint8_t width = 0, dec = 0;
      if (*p == ':') { p++; while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0'); }
      if (*p == '.') { p++; while (*p >= '0' && *p <= '9') dec = dec * 10 + (*p++ - '0'); }
      if (*p == '}') {
        o += formatValue(out + o, size - o, Engine::input(b, t[1] - '1'), min(width, (uint8_t)10), min(dec, (uint8_t)4));
        t = p + 1;
        continue;
      }
    }
    out[o++] = *t++;
  }
  out[o] = 0;
  return o;
}

// Scrive nella griglia le scritte del programma che stanno sulla pagina corrente.
// k0 = riga | colonna << 8 | effetto << 16 (0 nessuno, 1 lampeggia, 2 scorre); k1 = testo
// ingressi: V1, V2, EN (0 = nascosta), PAGINA
static void renderBlocks(uint8_t type, char* g, uint8_t cols, uint8_t rows, bool forOled, uint8_t page) {
  memset(g, ' ', cols * rows);
  uint32_t now = millis();
  for (uint8_t i = 0; i < Engine::nBlocks; i++) {
    const Block& b = Engine::blocks[i];
    if (b.type != type) continue;
    uint8_t r = b.k[0] & 0xFF, c = (b.k[0] >> 8) & 0xFF, eff = (b.k[0] >> 16) & 0xFF;
    if (Engine::input(b, 3) != page || r >= rows || c >= cols || !Engine::input(b, 2)) continue;
    if (eff == 1 && (now / 500) & 1) continue;
    char msg[48], disp[48];
    formatMessage(b, msg, sizeof(msg));
    uint8_t n = 0;                                   // testo -> caratteri del display
    for (const char* s = msg; *s && n < sizeof(disp); ) disp[n++] = nextChar(s, forOled);
    char* row = g + r * cols;
    uint8_t w = cols - c;
    if (eff == 2 && n > w) {                         // scorrevole
      uint8_t len = n + 3, off = (now / 300) % len;
      for (uint8_t j = 0; j < w; j++) { uint8_t k = (off + j) % len; row[c + j] = k < n ? disp[k] : ' '; }
    } else {
      for (uint8_t j = 0; j < n && c + j < cols; j++) row[c + j] = disp[j];
    }
  }
}

#if HAS_IMAGES
// Immagine: k3 = offset | x << 16 | riga << 23 | periodo dell'animazione (x25 ms) << 26
// ingressi EN, FOTOGRAMMA (-1 non collegato = animazione automatica), PAGINA.
// flags = numero di fotogrammi (letto dall'immagine la prima volta).
static int16_t imageFrame(Block& b) {
  if (Engine::input(b, 2) != Engine::pageOled || !Engine::input(b, 0)) return -1;
  if (!b.flags) {
    uint8_t ih[4];
    if (!Engine::readImage(b.k[3] & 0xFFFF, ih, 4) || !ih[2]) return -1;
    b.flags = ih[2];
  }
  uint8_t frames = b.flags;
  int32_t f = Engine::input(b, 1);
  if (!Engine::connected(b, 1) && f < 0) {                  // animazione automatica
    uint32_t step = (uint32_t)(((uint32_t)b.k[3] >> 26) & 0x3F) * 25;
    f = step ? (millis() / step) : 0;
  }
  return (int16_t)(((f % frames) + frames) % frames);
}
#endif

static bool lineForOled = true;   // set di caratteri usato da line()

static void line(char* row, uint8_t width, const __FlashStringHelper* fmt, ...) {
  char buf[24];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf_P(buf, sizeof(buf), (const char*)fmt, ap);
  va_end(ap);
  uint8_t col = 0;
  memset(row, ' ', width);
  putText(row, width, col, buf, lineForOled);
}

static void ipText(char* out) {
  IPAddress ip = netIP();
  snprintf_P(out, 16, PSTR("%u.%u.%u.%u"), ip[0], ip[1], ip[2], ip[3]);
}

static const __FlashStringHelper* netText() {
  switch (g_netState) {
#if NET_WIFI
    case NET_NO_HW:    return F("Wi-Fi guasto");
    case NET_NO_LINK:  return F("Wi-Fi spento");
    case NET_DHCP:     return F("Collego il Wi-Fi...");
    case NET_FALLBACK: return F("Rete ArduLearn (AP)");
    default:           return F("Wi-Fi OK");
#else
    case NET_NO_HW:    return F("Shield non trovata");
    case NET_NO_LINK:  return F("Cavo scollegato");
    case NET_DHCP:     return F("Attendo DHCP...");
    case NET_FALLBACK: return F("IP automatico");
    default:           return F("Rete OK");
#endif
  }
}

static void renderStatusOled() {
  char ip[16];
  ipText(ip);
  const char* runTxt = !Engine::loaded ? "----" : Engine::running ? " RUN" : "STOP";
  line(grid[0], OLED_COLS, F("ArduLearn        %s"), runTxt);
  memset(grid[1], ' ', OLED_COLS);
  line(grid[2], OLED_COLS, netText());   // "%S" non esiste sull'R4
  line(grid[3], OLED_COLS, g_netState >= NET_OK ? F("%s") : F(""), ip);
  line(grid[4], OLED_COLS, F("%s.local"), g_hostname);
  memset(grid[5], ' ', OLED_COLS);
  if (Engine::error[0]) line(grid[6], OLED_COLS, F("%s"), Engine::error);
  else line(grid[6], OLED_COLS, F("%s"), Engine::name);
  line(grid[7], OLED_COLS, F("%u blocchi  SD:%s"), Engine::nBlocks, g_sdOk ? "si" : "no");
}

#if HAS_LCD
static void renderStatusLcd(uint8_t cols, uint8_t rows) {
  char ip[16];
  ipText(ip);
  char* g = &lcdWant[0][0];
  const char* runTxt = !Engine::loaded ? "" : Engine::running ? "RUN " : "STOP ";
  uint8_t r = 0;
  if (g_netState >= NET_OK) line(g + cols * r++, cols, F("%s"), ip);
  else line(g + cols * r++, cols, netText());
  if (rows > 2) line(g + cols * r++, cols, F("%s"), g_hostname);
  if (r < rows) line(g + cols * r++, cols, F("%s%s"), runTxt, Engine::name);
  while (r < rows) memset(g + cols * r++, ' ', cols);
}
#endif

static void drawOled() {
  bool status = !Engine::usesOled || (int32_t)(statusUntil - millis()) > 0;
  if (status) renderStatusOled();
  else renderBlocks(BT_OLED, &grid[0][0], OLED_COLS, OLED_ROWS, true, Engine::pageOled);

  for (uint8_t r = 0; r < OLED_ROWS; r++) {
    uint16_t h = 0x811C;                             // hash della riga: testo + immagini visibili
    for (uint8_t c = 0; c < OLED_COLS; c++) h = (h ^ (uint8_t)grid[r][c]) * 0x0101 + c;
    bool hasImg = false;
#if HAS_IMAGES
    if (!status) {
      for (uint8_t i = 0; i < Engine::nBlocks; i++) {
        Block& b = Engine::blocks[i];
        if (b.type != BT_OLEDIMG) continue;
        int16_t f = imageFrame(b);
        if (f < 0) continue;
        h = (h ^ (uint16_t)(f * 31 + i)) * 0x0101 + (uint16_t)(b.k[3] >> 16);
        hasImg = true;
      }
    }
#endif
    if (h == rowHash[r]) continue;
    rowHash[r] = h;
    if (!hasImg) { oled.drawRow(r, grid[r], OLED_COLS); continue; }
#if HAS_IMAGES

    // riga con immagini: testo, poi le immagini sovrapposte (OR dei pixel)
    uint8_t rowBuf[128], g6[6];
    memset(rowBuf, 0, sizeof(rowBuf));
    for (uint8_t c = 0; c < OLED_COLS; c++) {
      TextOled::glyph((uint8_t)grid[r][c], g6);
      memcpy(rowBuf + c * 6, g6, 6);
    }
    for (uint8_t i = 0; i < Engine::nBlocks; i++) {
      Block& b = Engine::blocks[i];
      if (b.type != BT_OLEDIMG) continue;
      uint16_t off = b.k[3] & 0xFFFF;
      uint8_t x = (b.k[3] >> 16) & 0x7F, top = (b.k[3] >> 23) & 7;
      if (r < top) continue;
      int16_t f = imageFrame(b);
      if (f < 0) continue;
      uint8_t ih[4];                                 // larghezza, altezza, fotogrammi, periodo
      if (!Engine::readImage(off, ih, 4)) continue;
      uint8_t pages = ih[1] / 8;
      if (r >= top + pages) continue;
      uint8_t w = min((uint16_t)ih[0], (uint16_t)(128 - x));
      uint16_t data = off + 4 + ((uint16_t)f * pages + (r - top)) * ih[0];
      uint8_t chunk[32];
      for (uint8_t done = 0; done < w; done += sizeof(chunk)) {
        uint8_t n = min((uint16_t)sizeof(chunk), (uint16_t)(w - done));
        if (!Engine::readImage(data + done, chunk, n)) break;
        for (uint8_t j = 0; j < n; j++) rowBuf[x + done + j] |= chunk[j];
      }
    }
    oled.drawRaw(r, rowBuf);
#endif
  }
}

#if HAS_LCD
static void drawLcd() {
  uint8_t cols = cfg.lcdCols, rows = cfg.lcdRows;
  bool status = !Engine::usesLcd || (int32_t)(statusUntil - millis()) > 0;
  if (status) {
    lineForOled = false;
    renderStatusLcd(cols, rows);
    lineForOled = true;
  } else {
    renderBlocks(BT_LCD, &lcdWant[0][0], cols, rows, false, Engine::pageLcd);
  }

  // aggiorna solo i caratteri cambiati (l'LCD e' lento)
  const char* want = &lcdWant[0][0];
  char* have = &lcdHave[0][0];
  for (uint8_t r = 0; r < rows; r++) {
    bool placed = false;
    for (uint8_t c = 0; c < cols; c++) {
      uint16_t i = r * cols + c;
      if (want[i] == have[i]) { placed = false; continue; }
      if (!placed) { lcd.setCursor(c, r); placed = true; }
      lcd.write(want[i]);
      have[i] = want[i];
    }
  }
}
#endif

// Schermata di avvio: il logo a tutto schermo, copiato dalla flash una fascia alla volta
void displaysSplash() {
  if (!oledOk) return;
  uint8_t row[128];
  for (uint8_t r = 0; r < OLED_ROWS; r++) {
    memcpy_P(row, SPLASH_LOGO + r * 128, 128);
    oled.drawRaw(r, row);
  }
  memset(rowHash, 0xFF, sizeof(rowHash));
  splashUntil = millis() + 2500;
}

void displaysTick() {
  uint32_t now = millis();
  if (now - lastProbe >= 3000) {
    lastProbe = now;
    if (oledOk && !i2cPresent(cfg.oledAddr)) oledOk = false;
    else if (!oledOk) tryOled();
#if HAS_LCD
    if (cfg.lcdType == LCD_I2C) {
      if (lcd.ok() && !i2cPresent(cfg.lcdAddr)) lcd.end();
      else if (!lcd.ok()) tryLcd();
    }
#endif
  }
  if (now - lastDraw < 200) return;
  lastDraw = now;
  if (oledOk && (int32_t)(splashUntil - now) <= 0) drawOled();
#if HAS_LCD
  if (lcd.ok()) drawLcd();
#endif
}

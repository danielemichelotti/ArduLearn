#include "ledmatrix.h"
#if HAS_LED_MATRIX
#include "engine.h"
#include "net.h"
#include "displays.h"
#include <Arduino_LED_Matrix.h>

// =====================================================================
//  Matrice LED 12x8 dell'UNO R4 WiFi
//  - blocchi del programma (in RUN): "Immagine su matrice" (44) e "Testo su matrice" (45),
//    solo se EN vale 1 e PAGINA e' la pagina della matrice; piu' blocchi si sovrappongono;
//  - la rete e' appena diventata disponibile o e' cambiato l'indirizzo: fa scorrere l'indirizzo
//    (in modalita' AP prima "AP"), poi torna al programma; senza blocchi attivi l'indirizzo
//    ripassa ogni 2 minuti;
//  - senza blocchi attivi, lo stato del PLC: RUN (punto che corre), STOP (quadrato), nessun
//    programma (trattino), errore nel programma (punto esclamativo), modulo Wi-Fi guasto (X).
// =====================================================================
static ArduinoLEDMatrix matrix;
static uint8_t frame[8][12], shown[8][12];
static bool shownValid = false;

// caratteri 3x5 da ' ' a '_' e il simbolo dei gradi: 3 colonne per carattere, bit 0 in alto
static const uint8_t FONT[] = {
  0x00, 0x00, 0x00,  //  
  0x00, 0x17, 0x00,  // !
  0x03, 0x00, 0x03,  // "
  0x1F, 0x0A, 0x1F,  // #
  0x12, 0x1F, 0x09,  // $
  0x19, 0x04, 0x13,  // %
  0x0A, 0x15, 0x1A,  // &
  0x00, 0x03, 0x00,  // '
  0x00, 0x0E, 0x11,  // (
  0x11, 0x0E, 0x00,  // )
  0x0A, 0x04, 0x0A,  // *
  0x04, 0x0E, 0x04,  // +
  0x10, 0x08, 0x00,  // ,
  0x04, 0x04, 0x04,  // -
  0x00, 0x10, 0x00,  // .
  0x18, 0x04, 0x03,  // /
  0x1F, 0x11, 0x1F,  // 0
  0x12, 0x1F, 0x10,  // 1
  0x1D, 0x15, 0x17,  // 2
  0x11, 0x15, 0x1F,  // 3
  0x07, 0x04, 0x1F,  // 4
  0x17, 0x15, 0x1D,  // 5
  0x1F, 0x15, 0x1D,  // 6
  0x01, 0x1D, 0x03,  // 7
  0x1F, 0x15, 0x1F,  // 8
  0x17, 0x15, 0x1F,  // 9
  0x00, 0x0A, 0x00,  // :
  0x10, 0x0A, 0x00,  // ;
  0x04, 0x0A, 0x11,  // <
  0x0A, 0x0A, 0x0A,  // =
  0x11, 0x0A, 0x04,  // >
  0x01, 0x15, 0x07,  // ?
  0x0F, 0x15, 0x17,  // @
  0x1E, 0x05, 0x1E,  // A
  0x1F, 0x15, 0x0A,  // B
  0x0E, 0x11, 0x11,  // C
  0x1F, 0x11, 0x0E,  // D
  0x1F, 0x15, 0x11,  // E
  0x1F, 0x05, 0x01,  // F
  0x0E, 0x11, 0x1D,  // G
  0x1F, 0x04, 0x1F,  // H
  0x11, 0x1F, 0x11,  // I
  0x08, 0x10, 0x0F,  // J
  0x1F, 0x04, 0x1B,  // K
  0x1F, 0x10, 0x10,  // L
  0x1F, 0x06, 0x1F,  // M
  0x1F, 0x01, 0x1E,  // N
  0x0E, 0x11, 0x0E,  // O
  0x1F, 0x05, 0x02,  // P
  0x0E, 0x19, 0x16,  // Q
  0x1F, 0x05, 0x1A,  // R
  0x12, 0x15, 0x09,  // S
  0x01, 0x1F, 0x01,  // T
  0x1F, 0x10, 0x1F,  // U
  0x0F, 0x10, 0x0F,  // V
  0x1F, 0x0C, 0x1F,  // W
  0x1B, 0x04, 0x1B,  // X
  0x03, 0x1C, 0x03,  // Y
  0x19, 0x15, 0x13,  // Z
  0x00, 0x1F, 0x11,  // [
  0x03, 0x04, 0x18,  // backslash
  0x11, 0x1F, 0x00,  // ]
  0x02, 0x01, 0x02,  // ^
  0x10, 0x10, 0x10,  // _
  0x02, 0x05, 0x02,  // gradi
};
static const uint8_t FONT_DEG = 64;

// prossimo carattere del testo (UTF-8) -> indice nel font
static uint8_t nextGlyph(const char*& s) {
  uint8_t c = *s++;
  if (c >= 'a' && c <= 'z') c -= 32;
  if (c >= 32 && c < 96) return c - 32;
  if (c == 0xC2 && (uint8_t)*s == 0xB0) { s++; return FONT_DEG; }            // gradi
  if (c == 0xC3 && ((uint8_t)*s & 0xC0) == 0x80) {                           // lettere accentate
    uint8_t cp = (uint8_t)*s++ & 0x3F;
    static const char base[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUY";            // U+00C0..U+00DD
    uint8_t i = cp & 0x1F;
    return i < sizeof(base) - 1 ? base[i] - 32 : '?' - 32;
  }
  while (((uint8_t)*s & 0xC0) == 0x80) s++;
  return '?' - 32;
}

// Testo -> colonne (bit 0 in alto): le lettere strette (". : ! '") occupano una colonna sola
static uint16_t textColumns(const char* s, uint8_t* out, uint16_t max) {
  uint16_t n = 0;
  while (*s && n < max) {
    uint8_t g = nextGlyph(s);
    const uint8_t* c = FONT + g * 3;
    bool narrow = g && !c[0] && !c[2];
    if (narrow) out[n++] = c[1];
    else for (uint8_t i = 0; i < 3 && n < max; i++) out[n++] = c[i];
    if (n < max) out[n++] = 0;                      // spazio tra i caratteri
  }
  return n;
}

static void clear() { memset(frame, 0, sizeof(frame)); }

// una colonna di testo (5 righe) alla posizione x, righe da 1 a 5
static void drawColumn(int16_t x, uint8_t bits) {
  if (x < 0 || x >= 12) return;
  for (uint8_t r = 0; r < 5; r++) if ((bits >> r) & 1) frame[r + 1][x] = 1;
}

static void show() {
  if (shownValid && !memcmp(frame, shown, sizeof(frame))) return;
  memcpy(shown, frame, sizeof(frame));
  shownValid = true;
  matrix.renderBitmap(shown, 8, 12);
}

// ---------------------------------------------------------------------
//  Blocchi del programma
// ---------------------------------------------------------------------
// Immagine: k3 = offset | periodo (x25 ms) << 26; ingressi EN, FOTOGRAMMA (-1 = animazione), PAGINA
static bool drawImage(Block& b, uint32_t now) {
  if (!Engine::input(b, 0) || Engine::input(b, 2) != Engine::pageMtx) return false;
  uint32_t k3 = b.k[3];
  uint16_t off = k3 & 0xFFFF;
  uint8_t ih[4];
  if (!Engine::readImage(off, ih, 4) || !ih[2] || ih[0] > 12) return false;
  uint8_t frames = ih[2], w = ih[0];
  int32_t f = Engine::input(b, 1);
  if (!Engine::connected(b, 1) && f < 0) {
    uint32_t step = ((k3 >> 26) & 0x3F) * 25;
    f = step ? now / step : 0;
  }
  f = ((f % frames) + frames) % frames;
  uint8_t cols[12];
  if (!Engine::readImage(off + 4 + f * w, cols, w)) return false;
  uint8_t x0 = (12 - w) / 2;                        // piu' stretta della matrice: al centro
  for (uint8_t c = 0; c < w; c++)
    for (uint8_t r = 0; r < 8; r++) if ((cols[c] >> r) & 1) frame[r][x0 + c] = 1;
  return true;
}

// Testo: k0 = ms per passo, k1 = testo con {1} {2}; ingressi V1, V2, EN, PAGINA.
// Se entra nella matrice resta fermo al centro, altrimenti scorre.
static bool drawText(Block& b, uint32_t now) {
  if (!Engine::input(b, 2) || Engine::input(b, 3) != Engine::pageMtx) return false;
  char msg[48];
  displaysFormat(b, msg, sizeof(msg));
  static uint8_t cols[200];
  uint16_t n = textColumns(msg, cols, sizeof(cols));
  if (n && !cols[n - 1]) n--;                       // senza lo spazio finale
  if (n <= 12) {
    int16_t x0 = (12 - n) / 2;
    for (uint16_t i = 0; i < n; i++) drawColumn(x0 + i, cols[i]);
  } else {
    uint32_t step = b.k[0] > 0 ? b.k[0] : 100;
    uint16_t len = n + 12;                          // entra da destra, esce a sinistra
    int16_t off = (now / step) % len;
    for (int16_t x = 0; x < 12; x++) {
      int16_t i = off + x - 12;
      if (i >= 0 && i < (int16_t)n) drawColumn(x, cols[i]);
    }
  }
  return true;
}

static bool drawProgram(uint32_t now) {
  bool any = false;
  for (uint8_t i = 0; i < Engine::nBlocks; i++) {
    Block& b = Engine::blocks[i];
    if (b.type == BT_MTXIMG) any |= drawImage(b, now);
    else if (b.type == BT_MTXTXT) any |= drawText(b, now);
  }
  return any;
}

// ---------------------------------------------------------------------
//  Indirizzo che scorre e icone di stato
// ---------------------------------------------------------------------
static uint8_t  ipCols[64];
static int16_t  scrollX = 0, ipW = 0;
static uint8_t  repeats = 0;
static uint32_t lastStep = 0, lastIpShow = 0;
static uint8_t  shownNet = 0xFF;
static IPAddress shownIp;
static bool     programShown = false;

static void startText(const char* t, uint8_t times) {
  ipW = textColumns(t, ipCols, sizeof(ipCols));
  scrollX = 12;                                     // entra da destra
  repeats = times;
}

static void drawScroll() {
  clear();
  for (int16_t i = 0; i < ipW; i++) drawColumn(scrollX + i, ipCols[i]);
}

static void drawIcon(uint32_t now) {
  clear();
  if (g_netState == NET_NO_HW) {                        // X: modulo Wi-Fi guasto
    for (uint8_t i = 0; i < 6; i++) { frame[1 + i][3 + i] = 1; frame[1 + i][8 - i] = 1; }
  } else if (g_netState == NET_DHCP) {                  // collegamento in corso: onde che si accendono
    uint8_t step = (now / 300) % 4;
    frame[6][5] = frame[6][6] = 1;
    if (step >= 1) { frame[4][4] = frame[4][7] = frame[3][5] = frame[3][6] = 1; }
    if (step >= 2) { frame[3][2] = frame[2][3] = frame[1][5] = frame[1][6] = frame[2][8] = frame[3][9] = 1; }
    if (step >= 3) { frame[2][0] = frame[1][1] = frame[0][3] = frame[0][8] = frame[1][10] = frame[2][11] = 1; }
  } else if (Engine::error[0] && !Engine::loaded) {      // errore nel programma: "!"
    for (uint8_t r = 1; r <= 4; r++) frame[r][5] = frame[r][6] = 1;
    frame[6][5] = frame[6][6] = 1;
  } else if (!Engine::loaded) {                          // nessun programma: trattino
    for (uint8_t c = 3; c <= 8; c++) frame[4][c] = 1;
  } else if (Engine::running) {                          // RUN: punto che gira sul bordo
    static const uint8_t path[][2] = { {0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{0,6},{0,7},{0,8},{0,9},{0,10},{0,11},
      {1,11},{2,11},{3,11},{4,11},{5,11},{6,11},{7,11},{7,10},{7,9},{7,8},{7,7},{7,6},{7,5},{7,4},{7,3},{7,2},{7,1},{7,0},
      {6,0},{5,0},{4,0},{3,0},{2,0},{1,0} };
    uint8_t n = sizeof(path) / sizeof(path[0]), i = (now / 60) % n;
    frame[path[i][0]][path[i][1]] = 1;
    frame[path[(i + n - 1) % n][0]][path[(i + n - 1) % n][1]] = 1;
    frame[3][5] = frame[3][6] = frame[4][5] = frame[4][6] = 1;       // centro acceso fisso
  } else {                                               // STOP: quadrato
    for (uint8_t r = 2; r <= 5; r++) for (uint8_t c = 4; c <= 7; c++) frame[r][c] = 1;
  }
}

void ledBegin() {
  matrix.begin();
  // avvio: una "A" di ArduLearn al centro
  clear();
  static const uint8_t A5[5][4] = { {0,1,1,0}, {1,0,0,1}, {1,1,1,1}, {1,0,0,1}, {1,0,0,1} };
  for (uint8_t r = 0; r < 5; r++) for (uint8_t c = 0; c < 4; c++) frame[r + 1][c + 4] = A5[r][c];
  show();
}

void ledTick() {
  uint32_t now = millis();
  // la rete e' appena diventata disponibile o e' cambiato l'indirizzo: lo si fa scorrere
  // (ogni 2 minuti solo se il programma non sta usando la matrice)
  bool up = g_netState == NET_OK || g_netState == NET_FALLBACK;
  IPAddress ip = netIP();
  bool again = !programShown && now - lastIpShow > 120000UL;
  if (up && (shownNet != g_netState || ip != shownIp || again) && !repeats) {
    char buf[24];
    snprintf_P(buf, sizeof(buf), g_netState == NET_FALLBACK ? PSTR("AP %u.%u.%u.%u") : PSTR("%u.%u.%u.%u"), ip[0], ip[1], ip[2], ip[3]);
    startText(buf, shownNet != g_netState && !programShown ? 2 : 1);
    shownNet = g_netState;
    shownIp = ip;
    lastIpShow = now;
  }
  if (!up) shownNet = g_netState;
  if (repeats) {
    if (now - lastStep < 90) return;
    lastStep = now;
    drawScroll();
    show();
    if (--scrollX < -ipW) { scrollX = 12; if (!--repeats) lastIpShow = now; }
    return;
  }
  if (now - lastStep < 40) return;
  lastStep = now;
  clear();
  programShown = Engine::running && drawProgram(now);
  if (!programShown) drawIcon(now);
  show();
}

void ledHex(Print& o) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  for (uint8_t c = 0; c < 12; c++) {
    uint8_t v = 0;
    for (uint8_t r = 0; r < 8; r++) if (shown[r][c]) v |= 1 << r;
    o.write(HEX_DIGITS[v >> 4]);
    o.write(HEX_DIGITS[v & 15]);
  }
}
#endif

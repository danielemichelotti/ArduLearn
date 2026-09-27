#include "ledmatrix.h"
#if HAS_LED_MATRIX
#include "engine.h"
#include "net.h"
#include "displays.h"
#include "font5x7.h"
#include <Arduino_LED_Matrix.h>

// =====================================================================
//  Matrice LED 12x8 dell'UNO R4 WiFi
//  - blocchi del programma (in RUN): "Immagine su matrice" (44) e "Testo su matrice" (45, font
//    5x7 dell'OLED), solo se EN vale 1 e PAGINA e' la pagina della matrice; le immagini si
//    sovrappongono, il testo copre quello che c'e' sotto;
//  - la rete e' appena diventata disponibile o e' cambiato l'indirizzo: fa scorrere l'indirizzo
//    (in modalita' AP prima "AP"), poi torna al programma; senza blocchi attivi l'indirizzo
//    ripassa ogni 2 minuti;
//  - senza blocchi attivi, lo stato del PLC: RUN (punto che corre), STOP (quadrato), nessun
//    programma (trattino), errore nel programma (punto esclamativo), modulo Wi-Fi guasto (X).
// =====================================================================
static ArduinoLEDMatrix matrix;
static uint8_t frame[8][12], shown[8][12];
static bool shownValid = false;

// Testo -> colonne con il font 5x7 dell'OLED (bit 0 in alto, riga 7 per le gambette di g, p, y...):
// proporzionale (senza le colonne vuote dei caratteri stretti), una colonna di spazio tra i caratteri
static uint16_t textColumns(const char* s, uint8_t* out, uint16_t max) {
  uint16_t n = 0;
  while (*s && n < max) {
    uint8_t ch = displaysNextChar(s), g[5];
    uint16_t idx;
    if (ch >= FONT_FIRST && ch <= FONT_ASCII_LAST) idx = ch - FONT_FIRST;
    else if (ch >= FONT_EXTRA && ch < FONT_EXTRA + FONT_EXTRA_N) idx = FONT_ASCII_LAST - FONT_FIRST + 1 + (ch - FONT_EXTRA);
    else idx = '?' - FONT_FIRST;
    memcpy_P(g, FONT5X7 + idx * 5, 5);
    uint8_t a = 0, b = 5;
    if (ch == ' ') b = 3;                            // spazio: 3 colonne
    else { while (a < 4 && !g[a]) a++; while (b > a + 1 && !g[b - 1]) b--; }
    for (uint8_t c = a; c < b && n < max; c++) out[n++] = g[c];
    if (n < max) out[n++] = 0;
  }
  if (n && !out[n - 1]) n--;                         // senza lo spazio finale
  return n;
}

static void clear() { memset(frame, 0, sizeof(frame)); }

// una colonna di testo (8 righe) alla posizione x; opaca: cancella quello che c'era sotto
static void drawColumn(int16_t x, uint8_t bits) {
  if (x < 0 || x >= 12) return;
  for (uint8_t r = 0; r < 8; r++) frame[r][x] = (bits >> r) & 1;
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
// Se entra nella matrice (circa 2 caratteri) resta fermo al centro, altrimenti scorre.
// E' opaco: sopra un'immagine della stessa pagina si legge comunque.
static bool drawText(Block& b, uint32_t now) {
  if (!Engine::input(b, 2) || Engine::input(b, 3) != Engine::pageMtx) return false;
  char msg[48];
  displaysFormat(b, msg, sizeof(msg));
  static uint8_t cols[300];
  uint16_t n = textColumns(msg, cols, sizeof(cols));
  if (n <= 12) {
    int16_t x0 = (12 - n) / 2;
    for (uint16_t i = 0; i < n; i++) drawColumn(x0 + i, cols[i]);
  } else {
    uint32_t step = b.k[0] > 0 ? b.k[0] : 100;
    uint16_t len = n + 12;                          // entra da destra, esce a sinistra
    int16_t off = (now / step) % len;
    for (int16_t x = 0; x < 12; x++) {
      int16_t i = off + x - 12;
      drawColumn(x, i >= 0 && i < (int16_t)n ? cols[i] : 0);
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
static uint8_t  ipCols[160];
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

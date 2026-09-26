#include "ledmatrix.h"
#if HAS_LED_MATRIX
#include "engine.h"
#include "net.h"
#include <Arduino_LED_Matrix.h>

// =====================================================================
//  Matrice LED 12x8 dell'UNO R4 WiFi: avvisi per chi non ha un display.
//  - si collega al Wi-Fi: animazione;
//  - rete disponibile: fa scorrere l'indirizzo (in modalita' AP prima la scritta "AP");
//    l'indirizzo ripassa ogni 2 minuti;
//  - poi lo stato del PLC: RUN (punto che corre), STOP (quadrato), nessun programma (trattino),
//    errore nel programma (punto esclamativo), modulo Wi-Fi guasto (X).
// =====================================================================
static ArduinoLEDMatrix matrix;
static uint8_t frame[8][12];

// caratteri 3x5 (colonne, bit 0 in alto) per l'indirizzo e poche lettere
static const uint8_t* glyph(char c) {
  static const uint8_t D[10][3] = { {0x1F,0x11,0x1F}, {0x00,0x1F,0x00}, {0x1D,0x15,0x17}, {0x15,0x15,0x1F}, {0x07,0x04,0x1F},
                                    {0x17,0x15,0x1D}, {0x1F,0x15,0x1D}, {0x01,0x01,0x1F}, {0x1F,0x15,0x1F}, {0x17,0x15,0x1F} };
  static const uint8_t DOT[3] = { 0x10, 0x00, 0x00 }, SP[3] = { 0, 0, 0 };
  static const uint8_t A[3] = { 0x1E, 0x05, 0x1E }, P[3] = { 0x1F, 0x05, 0x07 }, DASH[3] = { 0x04, 0x04, 0x04 };
  if (c >= '0' && c <= '9') return D[c - '0'];
  switch (c) { case '.': return DOT; case 'A': return A; case 'P': return P; case '-': return DASH; }
  return SP;
}
static uint8_t glyphWidth(char c) { return c == '.' ? 1 : 3; }

static char     text[40];
static int16_t  scrollX = 0, textW = 0;
static uint8_t  repeats = 0;
static uint32_t lastStep = 0, lastIpShow = 0;
static uint8_t  shownNet = 0xFF;
static IPAddress shownIp;

static void clear() { memset(frame, 0, sizeof(frame)); }
static void show() { matrix.renderBitmap(frame, 8, 12); }

static void startText(const char* t, uint8_t times) {
  strlcpy(text, t, sizeof(text));
  textW = 0;
  for (const char* p = text; *p; p++) textW += glyphWidth(*p) + 1;
  scrollX = 12;                                   // entra da destra
  repeats = times;
}

static void drawText() {
  clear();
  int16_t x = scrollX;
  for (const char* p = text; *p; p++) {
    const uint8_t* g = glyph(*p);
    for (uint8_t c = 0; c < glyphWidth(*p); c++, x++) {
      if (x < 0 || x >= 12) continue;
      for (uint8_t r = 0; r < 5; r++) frame[r + 1][x] = (g[c] >> r) & 1;
    }
    x++;
  }
  show();
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
  show();
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
  bool up = g_netState == NET_OK || g_netState == NET_FALLBACK;
  IPAddress ip = netIP();
  if (up && (shownNet != g_netState || ip != shownIp || now - lastIpShow > 120000UL) && !repeats) {
    char buf[24];
    snprintf_P(buf, sizeof(buf), g_netState == NET_FALLBACK ? PSTR("AP %u.%u.%u.%u") : PSTR("%u.%u.%u.%u"), ip[0], ip[1], ip[2], ip[3]);
    startText(buf, shownNet != g_netState ? 2 : 1);
    shownNet = g_netState;
    shownIp = ip;
    lastIpShow = now;
  }
  if (!up) shownNet = g_netState;
  if (repeats) {
    if (now - lastStep < 90) return;
    lastStep = now;
    drawText();
    if (--scrollX < -textW) { scrollX = 12; if (!--repeats) lastIpShow = now; }
    return;
  }
  if (now - lastStep < 60) return;
  lastStep = now;
  drawIcon(now);
}
#endif

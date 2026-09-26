// Prova del collegamento RA4M1 <-> ESP32 (Serial2) con il firmware di prova ArduLearnBridge.
// Risultati sul monitor seriale a 115200. Comandi dal monitor:
//   t          ripete tutte le prove
//   >COMANDO   invia COMANDO all'ESP32 e mostra la risposta (es. >FS 500, >PING)
const long BAUDS[] = { 230400, 460800, 921600, 1000000, 2000000 };   // 3 Mbaud: non funziona
const uint16_t ECHO_LEN = 16384, CHUNK = 256;
long linkBaud = 115200;

bool readLine(char* buf, size_t len, uint32_t timeout) {
  size_t n = 0;
  uint32_t t = millis();
  while (millis() - t < timeout) {
    int c = Serial2.read();
    if (c < 0) continue;
    if (c == '\r') continue;
    if (c == '\n') { buf[n] = 0; return true; }
    if (n < len - 1) buf[n++] = c;
  }
  buf[n] = 0;
  return false;
}

bool cmd(const char* c, char* reply, size_t len, uint32_t timeout = 1000) {
  while (Serial2.available()) Serial2.read();
  Serial2.print(c);
  Serial2.print('\n');
  return readLine(reply, len, timeout);
}

void setBaud(long b) {
  char r[64], c[24];
  snprintf(c, sizeof(c), "BAUD %ld", b);
  bool ok = cmd(c, r, sizeof(r));
  Serial2.end();
  Serial2.begin(b);
  linkBaud = b;
  delay(50);
  Serial.print("  baud "); Serial.print(b); Serial.print(ok ? " (ESP32: " : " (nessuna risposta: "); Serial.print(r); Serial.println(")");
}

bool echoTest(long b) {
  char r[64];
  static uint8_t out[CHUNK], in[CHUNK];
  if (!cmd("ECHO 16384", r, sizeof(r)) || strcmp(r, "GO")) { Serial.print("  ECHO: nessun GO ("); Serial.print(r); Serial.println(")"); return false; }
  uint32_t errors = 0, t = micros();
  for (uint16_t off = 0; off < ECHO_LEN; off += CHUNK) {
    for (uint16_t k = 0; k < CHUNK; k++) out[k] = (uint8_t)(off / CHUNK * 31 + k);
    Serial2.write(out, CHUNK);
    uint16_t got = 0;
    uint32_t t0 = millis();
    while (got < CHUNK && millis() - t0 < 500) { int c = Serial2.read(); if (c >= 0) in[got++] = c; }
    if (got < CHUNK) { Serial.print("  timeout a "); Serial.println(off + got); return false; }
    for (uint16_t k = 0; k < CHUNK; k++) if (in[k] != out[k]) errors++;
  }
  t = micros() - t;
  Serial.print("  ECHO "); Serial.print(ECHO_LEN); Serial.print(" byte andata e ritorno in "); Serial.print(t / 1000);
  Serial.print(" ms = "); Serial.print((uint32_t)(2ULL * ECHO_LEN * 1000 / t)); Serial.print(" KB/s, errori "); Serial.println(errors);
  return errors == 0;
}

void runTests() {
  char r[96];
  Serial.println("=== Prova collegamento RA4M1 <-> ESP32 ===");
  if (linkBaud != 115200) setBaud(115200);
  for (int i = 0; i < 3; i++) {
    bool ok = cmd("PING", r, sizeof(r));
    Serial.print("PING: "); Serial.println(ok ? r : "nessuna risposta");
    if (ok) break;
    delay(500);
  }
  cmd("WIFI", r, sizeof(r)); Serial.print("WIFI: "); Serial.println(r);
  Serial.println("FS 500 (500 scritture+letture da 4 KB)...");
  cmd("FS 500", r, sizeof(r), 120000); Serial.print("  "); Serial.println(r);
  for (long b : BAUDS) {
    setBaud(b);
    bool ok = cmd("PING", r, sizeof(r));
    Serial.print("  PING: "); Serial.println(ok ? r : "nessuna risposta");
    if (!ok || !echoTest(b)) { Serial.println("  -> limite raggiunto"); break; }
  }
  // ritorno a 115200: se l'ultimo baud non funzionava, l'ESP32 potrebbe non ricevere; si prova comunque
  setBaud(115200);
  bool ok = cmd("PING", r, sizeof(r));
  Serial.print("PING finale: "); Serial.println(ok ? r : "nessuna risposta (riavviare la scheda)");
  Serial.println("=== fine ===");
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);
  for (uint32_t t = millis(); !Serial && millis() - t < 3000; ) {}
  delay(2000);   // l'ESP32 finisce di avviarsi
  runTests();
}

void loop() {
  static char line[80];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < sizeof(line) - 1) line[n++] = c; continue; }
    line[n] = 0; n = 0;
    if (!strcmp(line, "t")) runTests();
    else if (line[0] == '>') {
      char r[128];
      bool ok = cmd(line + 1, r, sizeof(r), 60000);
      Serial.println(ok ? r : "nessuna risposta");
    }
  }
}

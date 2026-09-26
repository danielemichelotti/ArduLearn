#include "storage.h"
#include "engine.h"
#include <EEPROM.h>
#if HAS_SD
#include <SPI.h>
#endif

Config cfg;
bool g_sdOk = false;

static const uint8_t  CFG_MAGIC   = 0xC7;
static const uint8_t  CFG_VERSION = 1;
static const uint16_t CFG_ADDR    = 2;      // 0 = magic, 1 = versione
#if BOARD_MEGA
static const uint16_t EE_IMG_BASE = 1024;   // progetto in EEPROM: 1024..4095
#else
static const uint16_t EE_IMG_BASE = 512;    // memoria interna dell'R4: vedi config.h
#endif

// ---------------------------------------------------------------------
//  Configurazione
// ---------------------------------------------------------------------
static void cfgDefaults() {
  // MAC con il prefisso WIZnet (00:08:DC) e 3 byte casuali: ogni scheda ne genera
  // uno suo al primo avvio. Non si usa un MAC "locally administered" (bit 0x02)
  // perche' alcuni ripetitori Wi-Fi usano quel bit per i MAC che traducono.
  uint32_t seed = micros();
#if BOARD_MEGA
  for (uint8_t i = 0; i < 16; i++) seed = seed * 31 + analogRead(A15) + analogRead(A14);
#else
  for (uint8_t i = 0; i < 16; i++) seed = seed * 31 + analogRead(A2) + analogRead(A3);
#endif
  randomSeed(seed);
  cfg.mac[0] = 0x00; cfg.mac[1] = 0x08; cfg.mac[2] = 0xDC;
  for (uint8_t i = 3; i < 6; i++) cfg.mac[i] = random(256);

  strcpy_P(cfg.pin, PSTR("1234"));
  snprintf_P(cfg.host, sizeof(cfg.host), PSTR("ardulearn-%02x%02x"), cfg.mac[4], cfg.mac[5]);
  cfg.oledAddr = 0x3C;
  cfg.oledType = 0;
  cfg.lcdAddr  = 0x27;
  cfg.lcdCols  = 16;
  cfg.lcdRows  = 2;
#if BOARD_MEGA
  const uint8_t defPins[6] = { 22, 23, 24, 25, 26, 27 };
  memcpy(cfg.lcdPins, defPins, 6);
  cfg.lcdType  = LCD_I2C;
#else
  const uint8_t defPins[6] = { 2, 3, 5, 6, 7, 8 };
  memcpy(cfg.lcdPins, defPins, 6);
  cfg.lcdType  = LCD_I2C;
#endif
  cfg.run      = 1;
  cfg.ipStatic = 0;
  cfg.activeAB = 0;
  cfg.rev      = 0;
}

void cfgLoad() {
  if (EEPROM.read(0) != CFG_MAGIC || EEPROM.read(1) != CFG_VERSION) {
    cfgDefaults();
    // al primo avvio il progetto in EEPROM e' vuoto
    if (MAX_IMAGE_EE) EEPROM.update(EE_IMG_BASE, 0);
    cfgSave();
    return;
  }
  EEPROM.get(CFG_ADDR, cfg);
  cfg.pin[sizeof(cfg.pin) - 1] = 0;
  cfg.host[sizeof(cfg.host) - 1] = 0;
  if (cfg.lcdCols != 16 && cfg.lcdCols != 20) cfg.lcdCols = 16;
  if (cfg.lcdRows < 1 || cfg.lcdRows > 4) cfg.lcdRows = 2;
  if (cfg.oledType > 1) cfg.oledType = 0;
  // campi aggiunti in fondo: in una EEPROM scritta da un firmware precedente valgono 0xFF
  if (cfg.ipStatic != 1) cfg.ipStatic = 0;
}

void cfgSave() {
  EEPROM.update(0, CFG_MAGIC);
  EEPROM.update(1, CFG_VERSION);
  EEPROM.put(CFG_ADDR, cfg);
}

bool cfgPinReservedByLcd(uint8_t pin) {
  if (cfg.lcdType != LCD_PARALLEL) return false;
  for (uint8_t i = 0; i < 6; i++) if (cfg.lcdPins[i] == pin) return true;
  return false;
}

// ---------------------------------------------------------------------
//  Sorgenti
// ---------------------------------------------------------------------
EepromSource::EepromSource() : base_(EE_IMG_BASE), max_(MAX_IMAGE_EE) {}

bool EepromSource::read(uint16_t off, void* dst, uint16_t n) {
  if (!max_ || (uint32_t)off + n > max_) return false;
  for (uint16_t i = 0; i < n; i++) ((uint8_t*)dst)[i] = EEPROM.read(base_ + off + i);
  return true;
}

uint16_t EepromSource::size() {
  if (!max_) return 0;
  if (EEPROM.read(base_) != 'P' || EEPROM.read(base_ + 1) != 'B') return 0;
  uint16_t len = EEPROM.read(base_ + 4) | (EEPROM.read(base_ + 5) << 8);
  return len <= max_ ? len : 0;
}

#if HAS_SD
FileSource::FileSource(const char* path) {
  if (g_sdOk) f = SD.open(path, FILE_READ);
  ok_ = (bool)f;
}

FileSource::~FileSource() {
  if (f) f.close();
}

uint32_t FileSource::length() { return ok_ ? f.size() : 0; }

bool FileSource::readAt(uint32_t off, void* dst, uint16_t n) {
  if (!ok_ || !f.seek(off)) return false;
  return f.read((uint8_t*)dst, n) == n;
}
#else
FileSource::FileSource(const char*) {}
FileSource::~FileSource() {}
uint32_t FileSource::length() { return 0; }
bool FileSource::readAt(uint32_t, void*, uint16_t) { return false; }
#endif

// ---------------------------------------------------------------------
//  Destinazioni
// ---------------------------------------------------------------------
EepromSink::EepromSink() : base_(EE_IMG_BASE), max_(MAX_IMAGE_EE) {}

bool EepromSink::write(const uint8_t* b, uint16_t n) {
  if ((uint32_t)pos_ + n > max_) return false;
  for (uint16_t i = 0; i < n; i++) EEPROM.update(base_ + pos_ + i, b[i]);
  pos_ += n;
  return true;
}

#if !HAS_SD
void eeSlotErase(uint8_t n) {
  if (n < 1 || n > EE_SLOTS) return;
  EEPROM.update(eeSlotBase(n), 0);
  EEPROM.update(eeSlotBase(n) + 1, 0);
}
#endif

#if HAS_SD
class FileSink : public ImgSink {
public:
  explicit FileSink(const char* path) {
    SD.remove(path);                      // FILE_WRITE accoda: prima si cancella
    f = SD.open(path, FILE_WRITE);
  }
  ~FileSink() { if (f) f.close(); }
  bool ok() { return (bool)f; }
  bool write(const uint8_t* b, uint16_t n) override { return f && f.write(b, n) == n; }
private:
  File f;
};

// C'e' una microSD nello slot? Si manda il comando di reset (CMD0) e si guarda se qualcuno
// risponde: circa 1 ms. Senza scheda SD.begin() aspetterebbe ~2 s bloccando il PLC.
static uint8_t sdLastR1 = 0xFF;             // ultima risposta alla prova (per il comando "sd")
static bool sdCardAnswers() {
  SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_SD_CS, HIGH);
  for (uint8_t i = 0; i < 10; i++) SPI.transfer(0xFF);          // 80 impulsi di clock a riposo
  digitalWrite(PIN_SD_CS, LOW);
  const uint8_t cmd0[6] = { 0x40, 0, 0, 0, 0, 0x95 };
  for (uint8_t b : cmd0) SPI.transfer(b);
  uint8_t r = 0xFF;
  for (uint8_t i = 0; i < 10 && (r & 0x80); i++) r = SPI.transfer(0xFF);
  digitalWrite(PIN_SD_CS, HIGH);
  SPI.transfer(0xFF);
  SPI.endTransaction();
  // una microSD risponde 0x01 ("in attesa"): senza scheda la linea resta alta (0xFF)
  // o, se non ha la resistenza di pull-up, legge valori a caso
  sdLastR1 = r;
  return r == 0x01;
}

void storageBegin() {
  g_sdOk = sdCardAnswers() && SD.begin(PIN_SD_CS);
  // SD.open(FILE_WRITE) non crea le cartelle mancanti: si creano qui
  sdMkdir("/PB");
  sdMkdir("/PLC");
}

bool sdMkdir(const char* path) { return g_sdOk && (SD.exists(path) || SD.mkdir(path)); }
bool sdRemove(const char* path) { return g_sdOk && SD.remove(path); }
bool sdExists(const char* path) { return g_sdOk && SD.exists(path); }

SdDir::SdDir(const char* path) {
  if (g_sdOk) d = SD.open(path);
  if (d && !d.isDirectory()) d.close();
}

SdDir::~SdDir() { if (d) d.close(); }

bool SdDir::next(char* name13, uint32_t& size) {
  if (!d) return false;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    bool file = !f.isDirectory();
    if (file) { strlcpy(name13, f.name(), 13); size = f.size(); }
    f.close();
    if (file) return true;
  }
  return false;
}

// Se la microSD manca la si riprova ogni 10 s (inserita a scheda accesa)
void storageTick() {
  // ogni tentativo andato a vuoto raddoppia l'attesa (10 s ... 5 min): se la prova rapida
  // sbagliasse, SD.begin() fermerebbe comunque il PLC sempre piu' di rado
  static uint32_t last = 0, every = 10000UL;
  if (g_sdOk || millis() - last < every) return;
  last = millis();
  digitalWrite(PIN_ETH_CS, HIGH);
  if (!sdCardAnswers()) return;                // slot vuoto: si riprova fra 10 s, costa ~1 ms
  storageBegin();
  if (!g_sdOk) every = min(every * 2, 300000UL);
  if (g_sdOk) Serial.println(F("microSD inserita"));
}

// Diagnosi dal monitor seriale (comando "sd"): dice a che punto si ferma
void storageDiag(Print& o, uint8_t cs) {
  Sd2Card card;
  SdVolume vol;
  digitalWrite(PIN_ETH_CS, HIGH);
  if (cs == PIN_SD_CS) {
    bool a = sdCardAnswers();
    o.print(F("prova rapida: risposta 0x")); o.print(sdLastR1, HEX);
    o.println(a ? F(" (scheda presente)") : F(" (nessuna scheda)"));
  }
  o.print(F("microSD (CS su D")); o.print(cs); o.print(F("): "));
  if (!card.init(SPI_QUARTER_SPEED, cs)) {
    o.print(F("la scheda non risponde (errore 0x"));
    o.print(card.errorCode(), HEX);
    o.println(F("): inserita bene? contatti puliti?"));
    return;
  }
  const char* types[] = { "?", "SD1", "SD2", "SDHC/SDXC" };
  o.print(F("risponde, tipo ")); o.print(types[card.type() & 3]);
  o.print(F(", ")); o.print(card.cardSize() / 2048UL); o.println(F(" MB"));
  if (!vol.init(card)) { o.println(F("  file system non leggibile: serve FAT16 o FAT32 (non exFAT/NTFS), prima partizione")); return; }
  o.print(F("  FAT")); o.print(vol.fatType());
  o.print(F(", cluster da ")); o.print(vol.blocksPerCluster() / 2); o.println(F(" KB: OK"));
  if (!g_sdOk && cs == PIN_SD_CS) { storageBegin(); o.println(g_sdOk ? F("  montata ora") : F("  SD.begin non riuscito")); }
}
#else
// UNO R4 WiFi senza shield: niente microSD
void storageBegin() { g_sdOk = false; }
void storageTick() {}
void storageDiag(Print& o, uint8_t) { o.println(F("Questa scheda non ha la microSD: i programmi stanno nella memoria interna")); }
bool sdMkdir(const char*) { return false; }
bool sdRemove(const char*) { return false; }
bool sdExists(const char*) { return false; }
SdDir::SdDir(const char*) {}
SdDir::~SdDir() {}
bool SdDir::next(char*, uint32_t&) { return false; }
ImgSink* openFileWrite(const char*) { return nullptr; }
#endif


const char* activePath()  { return cfg.activeAB ? "/PB/ACTB.PB" : "/PB/ACTA.PB"; }
const char* stagingPath() { return cfg.activeAB ? "/PB/ACTA.PB" : "/PB/ACTB.PB"; }

ImgSource* openActive() {
  if (g_sdOk) {
    FileSource* fs = new FileSource(activePath());
    if (fs->ok()) return fs;
    delete fs;
  }
  return new EepromSource();
}

ImgSink* openStaging(uint16_t len) {
  (void)len;
  if (g_sdOk) return openFileWrite(stagingPath());
  return new EepromSink();
}

void commitStaging() {
  if (g_sdOk) cfg.activeAB ^= 1;
  cfg.rev++;
  cfgSave();
}

#if HAS_SD
ImgSink* openFileWrite(const char* path) {
  if (!g_sdOk) return nullptr;
  FileSink* s = new FileSink(path);
  if (s->ok()) return s;
  delete s;
  return nullptr;
}
#endif

bool eeHasProgram() { EepromSource e; return e.size() > 0; }

void eeErase() {
  if (!MAX_IMAGE_EE) return;
  EEPROM.update(EE_IMG_BASE, 0);
  EEPROM.update(EE_IMG_BASE + 1, 0);
}

bool eeCopyActive() {
  if (!g_sdOk || !MAX_IMAGE_EE) return false;
  FileSource src(activePath());
  uint16_t len = src.size();
  if (!src.ok() || len < IMG_HDR_LEN || len > MAX_IMAGE_EE) return false;
  eeErase();                                   // una copia a meta' non deve sembrare valida
  uint8_t buf[32];
  for (uint16_t off = 2; off < len; off += sizeof(buf)) {      // intestazione 'PB' per ultima
    uint16_t n = min((uint16_t)sizeof(buf), (uint16_t)(len - off));
    if (!src.read(off, buf, n)) return false;
    for (uint16_t i = 0; i < n; i++) EEPROM.update(EE_IMG_BASE + off + i, buf[i]);
  }
  EEPROM.update(EE_IMG_BASE, 'P');
  EEPROM.update(EE_IMG_BASE + 1, 'B');
  return true;
}

uint16_t maxImage() { return g_sdOk ? MAX_IMAGE_SD : MAX_IMAGE_EE; }

void slotPath(uint8_t n, char* out) {
  snprintf_P(out, 16, PSTR("/PB/S%02u.PB"), n);
}

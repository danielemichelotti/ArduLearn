#pragma once
#include "config.h"
#if HAS_SD
#include <SD.h>
#endif

// ---- Configurazione della scheda (EEPROM 0..1023) ----
struct Config {
  uint8_t mac[6];
  char    pin[9];         // PIN docente
  char    host[16];       // nome mDNS (senza .local)
  uint8_t oledAddr;
  uint8_t lcdType;        // LCD_NONE / LCD_I2C / LCD_PARALLEL
  uint8_t lcdAddr;
  uint8_t lcdCols, lcdRows;
  uint8_t lcdPins[6];     // RS, E, D4, D5, D6, D7 (solo parallelo)
  uint8_t run;            // avvio automatico del programma
  uint8_t activeAB;       // file attivo su SD: 0 = A, 1 = B
  uint16_t rev;           // contatore revisioni del programma
  uint8_t oledType;       // OLED_SSD1306 / OLED_SH1106 (in fondo: compatibile con le EEPROM gia' scritte)
  uint8_t ipStatic;       // 1 = IP fisso (sotto), altrimenti DHCP
  uint8_t ip[4], mask[4], gw[4];
};
extern Config cfg;

void cfgLoad();
void cfgSave();
bool cfgPinReservedByLcd(uint8_t pin);

// ---- Sorgente di un'immagine di progetto (EEPROM o file su SD) ----
class ImgSource {
public:
  virtual bool read(uint16_t off, void* dst, uint16_t n) = 0;
  virtual uint16_t size() = 0;
  virtual ~ImgSource() {}
};

// Programma nella memoria interna (EEPROM); base/max per gli slot interni dell'R4
class EepromSource : public ImgSource {
public:
  EepromSource();
  EepromSource(uint16_t base, uint16_t max) : base_(base), max_(max) {}
  bool read(uint16_t off, void* dst, uint16_t n) override;
  uint16_t size() override;
private:
  uint16_t base_, max_;
};

// File su microSD in sola lettura (anche per la pagina web, che supera i 64 KB)
class FileSource : public ImgSource {
public:
  explicit FileSource(const char* path);
  ~FileSource();
  bool ok() { return ok_; }
  bool read(uint16_t off, void* dst, uint16_t n) override { return readAt(off, dst, n); }
  uint16_t size() override { return length() > 0xFFFF ? 0xFFFF : length(); }
  uint32_t length();
  bool readAt(uint32_t off, void* dst, uint16_t n);
private:
  bool ok_ = false;
#if HAS_SD
  File f;
#endif
};

// ---- Scrittura di un'immagine ricevuta in streaming ----
class ImgSink {
public:
  virtual bool write(const uint8_t* b, uint16_t n) = 0;
  virtual ~ImgSink() {}
};

class EepromSink : public ImgSink {
public:
  EepromSink();
  EepromSink(uint16_t base, uint16_t max) : base_(base), max_(max) {}
  bool write(const uint8_t* b, uint16_t n) override;
#if BOARD_R4
  // UNO R4: la EEPROM e' emulata nella flash dati, che si cancella a blocchi di 1 KB:
  // scrivere un byte alla volta vorrebbe dire cancellare e riscrivere il blocco per ogni byte.
  // I dati si raccolgono e si scrivono a pezzi da 256 byte (e alla fine, nel distruttore).
  ~EepromSink() override { flush(); }
  bool flush();
private:
  uint8_t  buf_[256];
  uint16_t bufLen_ = 0;
#endif
private:
  uint16_t base_, max_, pos_ = 0;
};

extern bool g_sdOk;
void storageBegin();
void storageTick();              // riprova a montare la microSD se manca
void storageDiag(Print& o, uint8_t cs);      // diagnosi (comando seriale "sd")

// Progetto attivo: su SD (file A/B alternati, scambio atomico) oppure in EEPROM.
const char* activePath();          // file attivo su SD
const char* stagingPath();         // file dove scrivere il prossimo upload
ImgSource*  openActive();          // da liberare con delete
ImgSink*    openStaging(uint16_t len);
void        commitStaging();       // il file appena scritto diventa quello attivo

// Slot su SD: /PB/S01.PB ... /PB/S99.PB
uint16_t maxImage();
// Programmi nella memoria della scheda: quello attivo su microSD e la copia in EEPROM
// (la EEPROM si usa quando la microSD manca o non ha un programma attivo)
bool eeHasProgram();
void eeErase();
bool eeCopyActive();                 // copia il programma attivo della microSD in EEPROM                 // limite del progetto: dipende dalla presenza della SD
void slotPath(uint8_t n, char* out);
ImgSink* openFileWrite(const char* path);   // nullptr se la SD manca
bool sdRemove(const char* path);
bool sdExists(const char* path);
bool sdMkdir(const char* path);             // crea la cartella se manca (true se c'e')
// Elenco dei file (non delle sottocartelle) di una cartella, in una sola passata
class SdDir {
public:
  explicit SdDir(const char* path);
  ~SdDir();
  bool next(char* name13, uint32_t& size);   // false alla fine (o cartella assente)
private:
#if HAS_SD
  File d;
#endif
};

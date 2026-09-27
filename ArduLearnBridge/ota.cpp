#include "ota.h"
#include "bridge_config.h"
#include "link.h"
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include "src/bossa/BossaArduino.h"

volatile bool bridgeHold = false;
volatile int otaProgress = 0;
static uint32_t restartAt = 0;
static bool confirmed = false;

// Il core Arduino di solito conferma subito l'app nuova: qui lo si fa da soli in otaService().
// extern "C": nel core e' una funzione C (esp32-hal-misc.c); senza, non verrebbe sostituita.
extern "C" bool verifyRollbackLater() { return true; }

void otaRestartLater() { restartAt = millis() + 1500; }

void otaService() {
  if (!confirmed && millis() > 30000) {
    confirmed = true;
    esp_ota_mark_app_valid_cancel_rollback();     // se non era "in prova" non fa niente
  }
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();
}

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// Il file e' davvero un firmware per il RA4M1? Tabella dei vettori ARM: stack nella RAM
// (0x20000000..0x20008000) e indirizzo di partenza nel codice dopo il bootloader (0x4000..0x40000).
bool otaCheckRaImage(const char* path, String& err) {
  File f = LittleFS.open(path, "r");
  if (!f) { err = "File non trovato"; return false; }
  size_t size = f.size();
  uint8_t v[8];
  bool ok = f.read(v, 8) == 8;
  f.close();
  if (!ok || size < 16 * 1024 || size > 240 * 1024) { err = "Dimensione non valida per un firmware del RA4M1"; return false; }
  uint32_t sp = le32(v), reset = le32(v + 4);
  if (sp <= 0x20000000 || sp > 0x20008000 || !(reset & 1) || reset < 0x4000 || reset >= 0x40000) {
    err = "Non e' un firmware ArduLearn per la UNO R4 WiFi (file .bin del RA4M1)";
    return false;
  }
  return true;
}

class RaObserver : public FlasherObserver {
public:
  void onStatus(const char*, ...) override {}
  void onProgress(int num, int div) override { if (div) otaProgress = num * 100 / div; }
};

static void pulseReset() {
  digitalWrite(GPIO_RST, LOW);
  delay(100);
  digitalWrite(GPIO_RST, HIGH);
}

// Bootloader del RA4M1: doppio impulso di reset con BOOT alto (come il tocco a 1200 baud)
static void enterBootloader() {
  digitalWrite(GPIO_BOOT, HIGH);
  digitalWrite(GPIO_RST, LOW);  delay(100);
  digitalWrite(GPIO_RST, HIGH); delay(100);
  digitalWrite(GPIO_RST, LOW);  delay(100);
  digitalWrite(GPIO_RST, HIGH);
  delay(500);
}

bool otaFlashRa(const char* path, String& err) {
  bridgeHold = true;                       // il ponte USB smette di usare la seriale del RA4M1
  quietUntil = millis() + 120000;
  linkPause(90000);
  otaProgress = 0;
  delay(50);
  uint32_t baud = Serial.baudRate();
  String vfsPath = String("/littlefs") + path;
  bool ok = false;
  // a volte il bootloader risponde male (SAM-BA): si riprova da capo, il bootloader non si tocca mai
  for (uint8_t attempt = 1; attempt <= 3 && !ok; attempt++) {
    enterBootloader();
    while (Serial.available()) Serial.read();
    RaObserver obs;
    BossaArduino bossa(obs);
    try {
      if (!bossa.connect(Serial)) err = "Il bootloader del RA4M1 non risponde";
      else { bossa.flash(vfsPath.c_str()); ok = true; }
    } catch (std::exception& e) {
      err = String("Scrittura non riuscita: ") + e.what();
    } catch (...) {
      err = "Scrittura non riuscita";
    }
    if (!ok) { linkLog("aggiornamento RA4M1, tentativo %u: %s", attempt, err.c_str()); delay(300); }
  }
  Serial.updateBaudRate(baud ? baud : 115200);
  pulseReset();                            // il RA4M1 riparte con il firmware nuovo (o quello di prima)
  while (Serial.available()) Serial.read();
  bridgeHold = false;
  quietUntil = millis() + 3000;
  linkPause(1500);                         // il RA4M1 si riavvia: poi si rifa' subito il saluto
  otaProgress = ok ? 100 : 0;
  if (ok) err = "";
  return ok;
}

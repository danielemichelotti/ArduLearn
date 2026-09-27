#pragma once
#include <Arduino.h>

// ---- Aggiornamenti (ota.cpp) ----
// ESP32: il firmware nuovo va nella partizione app libera (web.cpp, libreria Update); dopo il
// riavvio resta "in prova" finche' otaService() non lo conferma (30 s di funzionamento): se si
// blocca prima, il bootloader torna da solo alla versione precedente.
// RA4M1: il file .bin (lo stesso che bossac carica dalla USB) si controlla e si scrive con BOSSA
// attraverso la seriale del RA4M1, dopo averlo messo nel bootloader (doppio impulso di reset).
extern volatile bool bridgeHold;       // ponte USB sospeso (la seriale del RA4M1 serve a BOSSA)
extern volatile uint32_t quietUntil;   // niente messaggi sulla USB fino a questo istante
extern volatile int otaProgress;       // avanzamento della scrittura del RA4M1 (0..100)

bool otaCheckRaImage(const char* path, String& err);
bool otaFlashRa(const char* path, String& err);
void otaRestartLater();                // riavvio dell'ESP32 fra poco (dopo aver risposto al browser)
void otaService();                     // task "svc": conferma dell'app nuova, riavvio programmato

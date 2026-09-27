#pragma once
#include <Arduino.h>

// =====================================================================
//  ArduLearn: PLC a blocchi
//   - Arduino Mega 2560 + DFRobot Ethernet & PoE Shield (DFR0850, W5500, microSD)
//   - Arduino UNO R4 WiFi da solo: Wi-Fi integrato, nessuna shield
// =====================================================================

#define FW_VERSION "2.2"

// ---- Scheda: lo stesso sorgente si compila per Mega 2560 e per UNO R4 WiFi ----
// L'EXE di caricamento sceglie il firmware giusto; la pagina chiede /api/info
// e si adatta ai pin della scheda collegata. L'Uno classico (ATmega328P, 2 KB di RAM,
// 32 KB di flash) non basta: le sole librerie di rete ne occupano la meta'.
#if defined(__AVR_ATmega2560__)
  #define BOARD_MEGA 1
  #define BOARD_NAME "mega"
  #define NET_WIFI 0                  // rete: shield Ethernet W5500
  #define NET_BRIDGE 0
  #define HAS_SD   1                  // microSD della shield
#elif defined(ARDUINO_UNOR4_WIFI)
  #define BOARD_R4 1
  #define BOARD_NAME "r4wifi"
  #define NET_WIFI 0
  #define NET_BRIDGE 1                // rete: il modulo ESP32-S3 con il firmware ArduLearnBridge
                                      // (Wi-Fi, pagina, slot e bozza); qui il PLC (vedi bridge.cpp)
  #define HAS_SD   0                  // niente shield: programma attivo nella memoria interna
  #define HAS_LED_MATRIX 1            // matrice LED 12x8 per gli avvisi
#elif defined(ARDUINO_UNOR4_MINIMA)
  #error "UNO R4 Minima: per ora non supportata. Usa Arduino Mega 2560 con shield Ethernet o Arduino UNO R4 WiFi"
#elif defined(__AVR_ATmega328P__)
  #error "Arduino Uno classico: memoria insufficiente. Servono Arduino Mega 2560 o Arduino UNO R4 WiFi"
#else
  #error "Scheda non supportata: servono Arduino Mega 2560 (con shield Ethernet) o Arduino UNO R4 WiFi"
#endif

// ---- Hardware della shield (solo Mega) ----
const uint8_t PIN_ETH_CS = 10;   // W5500
const uint8_t PIN_SD_CS  = 4;    // slot microSD

#if BOARD_MEGA
// Pin totali gestiti: D0..D53 + A0..A15 (A0 = 54 ... A15 = 69)
const uint8_t NUM_PINS    = 70;
const uint8_t FIRST_APIN  = 54;
// Pin che il programma a blocchi non puo' usare:
// 0/1 seriale USB, 4 CS microSD, 10 CS W5500, 20/21 I2C (OLED/LCD), 50-53 SPI
const uint8_t RESERVED_PINS[] = { 0, 1, 4, 10, 20, 21, 50, 51, 52, 53 };
#else
// UNO R4 WiFi senza shield: D0..D13 + A0..A5 (A0 = 14 ... A5 = 19), tutti liberi
// (la seriale USB passa dal modulo Wi-Fi; D13 e' il LED "L") tranne A4/A5 per l'I2C dei display
const uint8_t NUM_PINS    = 20;
const uint8_t FIRST_APIN  = 14;
const uint8_t RESERVED_PINS[] = { 18, 19 };
#endif

// ---- Limiti del programma ----
const uint8_t  MAX_IN     = 4;
const uint8_t  MAX_OUT    = 2;
const uint8_t  NC         = 0xFF;     // ingresso non collegato
const uint8_t  MAX_PAGES  = 16;       // pagine dei display
const uint8_t  MAX_BLOCKS = 96;
#if BOARD_MEGA
const uint16_t MAX_IMAGE_EE = 3072;   // dimensione massima di un progetto in EEPROM (byte)
#else
// memoria interna dell'R4 (8 KB): configurazione fino a 511, programma attivo 512..8191
// (slot e bozza stanno nella flash del modulo ESP32)
const uint16_t MAX_IMAGE_EE = 7680;
#endif
const uint16_t MAX_IMAGE_SD = 8192;   // ... e su microSD
const uint8_t  NUM_MBITS  = 128;      // memorie a bit  %M0.0 ... %M15.7
const uint8_t  NUM_MWORDS = 32;       // memorie intere %MW0 ... %MW62 (a 32 bit)
#if BOARD_MEGA
const uint16_t MAX_POOL   = 512;      // testi dei display + codice degli script (byte, in RAM)
const uint8_t  SCRIPT_VARS = 32;      // variabili locali di tutti gli script
#else
const uint16_t MAX_POOL   = 1024;     // l'R4 ha 32 KB di RAM
const uint8_t  SCRIPT_VARS = 128;
#endif
const uint16_t SCRIPT_BUDGET = 4000;  // istruzioni massime per script in un ciclo
#define HAS_MODULES 1                 // sensori e attuatori (mod_*.cpp)
#define HAS_SCRIPT  1                 // blocco script
#define HAS_IMAGES  1                 // immagini sull'OLED
#define HAS_LCD     1                 // display LCD a caratteri (I2C o parallelo)
#if NET_BRIDGE
#define HAS_EMBEDDED_PAGE 0           // UNO R4 WiFi: pagina, nome .local e ricerca UDP li gestisce l'ESP32
#define HAS_MDNS 0
#else
#define HAS_EMBEDDED_PAGE 1           // pagina web dentro il firmware (sul Mega la SD puo' averne una piu' nuova)
#define HAS_MDNS 1
#endif
#ifndef HAS_LED_MATRIX
#define HAS_LED_MATRIX 0
#endif

#if BOARD_R4
// Funzioni della libreria AVR che il core Renesas non ha
#include <stdio.h>
#include <string.h>
#define sscanf_P sscanf
#define vsnprintf_P vsnprintf
static inline size_t plc_strlcpy(char* d, const char* s, size_t n) {
  size_t l = strlen(s);
  if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; }
  return l;
}
#define strlcpy plc_strlcpy
#endif
const uint8_t  PIN_IMG_LEN = 70;      // modi dei pin nell'immagine: sempre 70 (formato comune)

// ---- Formato immagine del progetto (little endian) ----
//  0  'P','B'              magic
//  2  u8  versione (1)
//  3  u8  numero blocchi
//  4  u16 lunghezza totale
//  6  u16 offset testi     8  u16 lunghezza testi
// 10  u16 offset meta     12  u16 lunghezza meta (JSON dell'editor, ignorato dal firmware)
// 14  u16 offset immagini (0 = nessuna): le immagini vanno da qui all'offset meta
//     ogni immagine: u8 larghezza, u8 altezza (multiplo di 8), u8 fotogrammi,
//     u8 centesimi di secondo per fotogramma, poi i dati
//     (per fotogramma, per pagina di 8 righe, una colonna per byte, bit 0 in alto)
// 16  u16 lunghezza del codice degli script (subito dopo i testi)
// 18  char nome[22]
// 40  u8  modo pin[70]
// 110 blocchi: n x 21 byte = tipo, in[4], k[4] (int32)
const uint16_t IMG_HDR_LEN   = 110;
const uint8_t  IMG_BLOCK_LEN = 21;
const uint8_t  IMG_VERSION   = 2;

// ---- Modi dei pin (definiti nel progetto) ----
enum : uint8_t { PM_NONE = 0, PM_IN = 1, PM_IN_PULLUP = 2, PM_OUT = 3, PM_ANALOG = 4, PM_MODULE = 5 };  // 5 = gestito da un modulo

// ---- Tipi di blocco (devono coincidere con l'editor web) ----
enum : uint8_t {
  BT_DIN = 1, BT_DOUT = 2, BT_AIN = 3, BT_PWM = 4, BT_CONST = 5,
  BT_VSWITCH = 6, BT_VSLIDER = 7, BT_MONITOR = 8,
  BT_AND = 10, BT_OR = 11, BT_XOR = 12, BT_NAND = 13, BT_NOR = 14, BT_NOT = 15,
  BT_SR = 16, BT_EDGE = 17, BT_TOGGLE = 18,
  BT_TON = 20, BT_TOF = 21, BT_TP = 22, BT_BLINK = 23, BT_CTUD = 24,
  BT_MATH = 30, BT_CMP = 31, BT_SCALE = 32, BT_HYST = 33, BT_SELECT = 34, BT_LIMIT = 35,
  BT_OLED = 40, BT_LCD = 41, BT_OLEDIMG = 42, BT_PAGE = 43,
  BT_MTXIMG = 44, BT_MTXTXT = 45,      // matrice LED 12x8 (solo UNO R4 WiFi, vedi ledmatrix.cpp)
  BT_MGET = 50, BT_MSET = 51,          // lettura / scrittura memorie (usati dal LADDER)
  BT_STEP = 60, BT_REPEAT = 61, BT_SCRIPT = 62,
  BT_CSCRIPT = 63,                     // script in stile C/Arduino: fino a 8 ingressi e 4 uscite
  BT_CEXT = 64,                        // subito dopo un BT_CSCRIPT: ingressi 5..8 e uscite 3..4
  BT_MODULE_FIRST = 70,                // 70..127: blocchi dei moduli (mod_*.cpp)
};

// ---- Display ----
enum : uint8_t { LCD_NONE = 0, LCD_I2C = 1, LCD_PARALLEL = 2 };
const uint8_t OLED_COLS = 21, OLED_ROWS = 8;   // testo 6x8 su 128x64
const uint8_t LCD_MAX_COLS = 20, LCD_MAX_ROWS = 4;

// ---- Stato rete (definito in PlcBlocchi.ino) ----
enum : uint8_t { NET_NO_HW, NET_NO_LINK, NET_DHCP, NET_OK, NET_FALLBACK };
extern uint8_t g_netState;
extern char    g_hostname[16];
void netHostnameChanged();
int  freeRam();   // da chiamare dopo aver cambiato cfg.host

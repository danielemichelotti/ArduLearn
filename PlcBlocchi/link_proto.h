#pragma once
// =====================================================================
//  Collegamento ESP32 <-> RA4M1 (UART1 dell'ESP32 = Serial2 del RA4M1)
//  COPIA IDENTICA in ArduLearnBridge/link_proto.h e PlcBlocchi/link_proto.h
//
//  Pacchetto: 0xA5, tipo, lunghezza (2 byte, little endian), dati, CRC16-CCITT
//  (2 byte, su tipo + lunghezza + dati). L'ESP32 comanda, il RA4M1 risponde.
//  Le richieste HTTP del browser passano cosi' come sono: l'ESP32 manda il testo
//  della richiesta (S poi Q), il RA4M1 conferma ogni pacchetto (K) e risponde con
//  il testo della risposta HTTP (R ... E): il server web del RA4M1 resta lo stesso.
//  Velocita' fissa (LINK_BAUD) fin dall'avvio: sul RA4M1 cambiarla vuol dire end()/begin()
//  della seriale, che a volte lasciava la ricezione senza interrupt (collegamento muto).
// =====================================================================
#include <stdint.h>
#include <stddef.h>

#define LINK_SOF          0xA5
#define LINK_MAX          256        // dati massimi in un pacchetto
#define LINK_BAUD         1000000    // 10 us per byte: margine per il RA4M1 anche col PLC occupato

// ESP32 -> RA4M1
#define LINK_HELLO   'H'   // "m=<mac 12 cifre esadecimali> v=<versione>"  -> 'h'
#define LINK_PING    'P'   // -> 'p'
#define LINK_NET     'N'   // "s=<stato rete> ip=<a.b.c.d> ap=<nome rete propria>"  -> 'n'
#define LINK_START   'S'   // inizio di una richiesta HTTP                          -> 'K'
#define LINK_MORE    'Q'   // seguito della richiesta                               -> 'K'
#define LINK_ABORT   'A'   // richiesta abbandonata (il browser ha chiuso)
#define LINK_TEXT    'T'   // testo da scrivere sul monitor seriale del RA4M1
// RA4M1 -> ESP32
#define LINK_HELLO_R 'h'   // "fw=<versione> b=<scheda>"
#define LINK_PONG    'p'
#define LINK_NET_R   'n'
#define LINK_ACK     'K'
#define LINK_RESP    'R'   // pezzo della risposta HTTP
#define LINK_END     'E'   // fine della risposta
#define LINK_CMD     'C'   // comando dal monitor seriale del RA4M1 (es. "wifi ssid ...")

static inline uint16_t linkCrc(const uint8_t* p, size_t n, uint16_t c = 0xFFFF) {
  while (n--) {
    c ^= (uint16_t)(*p++) << 8;
    for (uint8_t i = 0; i < 8; i++) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1;
  }
  return c;
}

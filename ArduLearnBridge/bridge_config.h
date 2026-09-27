#pragma once
// =====================================================================
//  ArduLearn Bridge: firmware del modulo ESP32-S3 dell'Arduino UNO R4 WiFi
//  Pin e comportamento USB come nel firmware "USB bridge" di Arduino
//  (github.com/arduino/uno-r4-wifi-usb-bridge), per restare compatibili con
//  IDE, arduino-cli e l'aggiornamento/ripristino del firmware.
// =====================================================================

#define BRIDGE_FW_VERSION "0.3.3"
#define BRIDGE_FW_MAJOR   0
#define BRIDGE_FW_MINOR   3
#define BRIDGE_FW_PATCH   3

// RA4M1: BOOT e RESET comandati dall'ESP32 (tocco a 1200 baud = bootloader)
#define GPIO_BOOT 9
#define GPIO_RST  4

// UART0 <-> Serial del RA4M1 (quello che il PC vede sulla USB)
#define PIN_USER_RX 44
#define PIN_USER_TX 43
// UART1 <-> Serial2 del RA4M1 (nel firmware Arduino: comandi AT; qui: collegamento ArduLearn)
#define PIN_LINK_RX 6
#define PIN_LINK_TX 5

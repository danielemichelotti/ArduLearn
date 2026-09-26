#pragma once
// Adattatore CMSIS-DAP (HID) come nel firmware "USB bridge" di Arduino
// (github.com/arduino/uno-r4-wifi-usb-bridge, DAP.h): serve all'IDE per il debug del RA4M1
// e, soprattutto, il feature report 0xAA riavvia l'ESP32 nella modalita' download della ROM:
// e' cosi' che l'IDE (Strumenti -> Aggiornamento firmware) rimette il firmware originale.
#include "USB.h"
#include "USBHID.h"

extern "C" {
  #include "esp32-hal-tinyusb.h"
  #include "freedap.h"
}

#include "bridge_config.h"

extern USBHID HID;

#define TUD_HID_REPORT_DESC_R4_INOUT_FEATURE(report_size, ...) \
    HID_USAGE_PAGE_N ( HID_USAGE_PAGE_VENDOR, 2   ),\
    HID_USAGE        ( 0x01                       ),\
    HID_COLLECTION   ( HID_COLLECTION_APPLICATION ),\
      __VA_ARGS__ \
      HID_USAGE       ( 0x02                                   ),\
      HID_LOGICAL_MIN ( 0x00                                   ),\
      HID_LOGICAL_MAX_N ( 0xff, 2                              ),\
      HID_REPORT_SIZE ( 8                                      ),\
      HID_REPORT_COUNT( report_size                            ),\
      HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),\
      HID_USAGE       ( 0x03                                    ),\
      HID_LOGICAL_MIN ( 0x00                                    ),\
      HID_LOGICAL_MAX_N ( 0xff, 2                               ),\
      HID_REPORT_SIZE ( 8                                       ),\
      HID_REPORT_COUNT( report_size                             ),\
      HID_OUTPUT      ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE  ),\
      HID_USAGE       ( 0x04),\
      HID_LOGICAL_MIN ( 0x00),\
      HID_LOGICAL_MAX_N ( 0xff, 2),\
      HID_REPORT_SIZE (8),\
      HID_REPORT_COUNT(report_size),\
      HID_FEATURE     (HID_DATA | HID_VARIABLE | HID_ABSOLUTE),\
    HID_COLLECTION_END \

static uint8_t const report_descriptor[] = {
  TUD_HID_REPORT_DESC_R4_INOUT_FEATURE(CFG_TUD_HID_EP_BUFSIZE)
};

class DAPHIDDevice : public USBHIDDevice {
public:
  DAPHIDDevice() {
    static bool initialized = false;
    if (!initialized) {
      initialized = true;
      HID.addDevice(this, sizeof(report_descriptor));
    }
  }
  void begin() {
    HID.begin();
    dap_init();
  }
  uint16_t _onGetDescriptor(uint8_t* buffer) override {
    memcpy(buffer, report_descriptor, sizeof(report_descriptor));
    return sizeof(report_descriptor);
  }
  void _onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) override {
    static uint8_t tx[CFG_TUD_HID_EP_BUFSIZE];
    dap_process_request((uint8_t*)buffer, len, tx, sizeof(tx));
    HID.SendReport(report_id, tx, sizeof(tx), 5);
  }
  void _onSetFeature(uint8_t report_id, const uint8_t* buffer, uint16_t len) override {
    if (buffer[0] == 0xAA) usb_persist_restart(RESTART_BOOTLOADER);   // ESP32 in modalita' download
  }
  uint16_t _onGetFeature(uint8_t report_id, uint8_t* buffer, uint16_t len) override {
    buffer[0] = BRIDGE_FW_MAJOR;
    buffer[1] = BRIDGE_FW_MINOR;
    buffer[2] = BRIDGE_FW_PATCH;
    return 3;
  }
};

extern DAPHIDDevice DAP;

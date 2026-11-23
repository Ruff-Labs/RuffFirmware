#pragma once
#include <RF24.h>
#include "radio_protocol.h"

// Radio wiring, the same on every Nano: SPI on D11 (MOSI) / D12 (MISO) / D13 (SCK), plus these.
// Power the PA/LNA module from its own 3.3 V regulator (the socket adapter board), not the Nano's
// 3V3 pin: it draws 100+ mA while sending.
constexpr uint8_t PIN_RF_CE  = 9;
constexpr uint8_t PIN_RF_CSN = 10;
constexpr uint8_t PIN_RF_IRQ = 8;   // only the sensor node uses it (timestamps sync pings)

// The PA/LNA modules are loud: at a few metres on a bench RF24_PA_MAX can overload the other
// end's receiver. HIGH is plenty for a flyball ring; try MAX only if radio_test shows losses.
constexpr rf24_pa_dbm_e RF_POWER = RF24_PA_HIGH;

// Settings both ends must share (radio_protocol.h). False if the module doesn't answer on SPI:
// check the wiring and its power.
inline bool setup_radio(RF24& rf) {
    if (!rf.begin() || !rf.isChipConnected()) return false;
    rf.setChannel(radio::kChannel);
    rf.setDataRate(RF24_250KBPS);
    rf.setPALevel(RF_POWER);
    rf.setCRCLength(RF24_CRC_16);
    rf.setAutoAck(true);
    rf.enableDynamicPayloads();
    rf.setRetries(radio::kRetryDelay, radio::kRetryCount);
    return true;
}

// A number that's different on every power-up (a counter in EEPROM), so the BeaglePlay can tell
// that this board restarted and its clock and edge numbers began again.
#include <EEPROM.h>
inline uint8_t next_boot_id(int eeprom_addr) {
    const uint8_t id = EEPROM.read(eeprom_addr) + 1;
    EEPROM.write(eeprom_addr, id);
    return id;
}

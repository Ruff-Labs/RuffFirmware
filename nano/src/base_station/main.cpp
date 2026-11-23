// Base station: the Nano Every on USB to the BeaglePlay (it shows up as /dev/ttyACM0, and as
// /dev/ruff-radio with the udev rule from scripts/setup.sh). It relays every radio packet to
// the BeaglePlay, and pings the sensor node 10 times a second so the BeaglePlay can work out
// the node's clock. All the clock maths is on the BeaglePlay (firmware/src/radio/ClockFit.hpp).
//
// Wiring: D9 CE, D10 CSN, D11-13 SPI (rf24_setup.h). The IRQ pin isn't needed here.
// Everything sent over USB is a binary frame (radio_frame.h), so the serial monitor shows noise.
#include <Arduino.h>
#include <SPI.h>
#include "radio_frame.h"
#include "radio_protocol.h"
#include "rf24_setup.h"

using namespace radio;

constexpr int      EEPROM_BOOT  = 0;
constexpr uint16_t PING_EVERY_MS = 100;

RF24    rf(PIN_RF_CE, PIN_RF_CSN);
uint8_t boot_id;
bool    radio_ok = false;

static void send_frame(const void* body, size_t n) {
    uint8_t out[kMaxFrame + 8];
    const size_t len = make_frame(static_cast<const uint8_t*>(body), n, out);
    Serial.write(out, len);
}

static void send_hello() {
    SerialHello h;
    h.boot = boot_id;
    h.t_us = micros();
    send_frame(&h, sizeof h);
}

// Ping the sensor node and report when it went out and when its ack came back.
static void ping_node(uint8_t id) {
    RadioSyncPing ping;
    ping.id = id;

    SerialPingSent s;
    s.node = kSensorNodeId;
    s.id   = id;
    rf.stopListening();
    rf.openWritingPipe(kSensorNodeAddress);
    s.t_tx_us  = micros();
    s.acked    = rf.write(&ping, sizeof ping);
    s.t_ack_us = micros();
    rf.startListening();
    send_frame(&s, sizeof s);
}

// Every packet the radio has, to the BeaglePlay as it came in.
static void relay_radio() {
    while (rf.available()) {
        uint8_t body[sizeof(SerialRadio) + 32];
        const uint8_t n = rf.getDynamicPayloadSize();
        if (n == 0 || n > 32) { rf.flush_rx(); return; }   // corrupt length: the datasheet says flush
        SerialRadio hdr;
        hdr.node = kSensorNodeId;                          // only one node so far (pipe 1)
        memcpy(body, &hdr, sizeof hdr);
        rf.read(body + sizeof hdr, n);
        send_frame(body, sizeof hdr + n);
    }
}

void setup() {
    Serial.begin(kSerialBaud);
    boot_id  = next_boot_id(EEPROM_BOOT);
    radio_ok = setup_radio(rf);
    if (radio_ok) {
        rf.openReadingPipe(1, kBaseAddress);
        rf.startListening();
    }
}

void loop() {
    static uint32_t last_hello = 0, last_ping = 0, last_retry = 0;
    static uint8_t  ping_id = 0;
    const uint32_t  now = millis();

    // No radio: keep saying hello (so the BeaglePlay sees the base station), and keep trying.
    // The BeaglePlay reports the sensors offline because no heartbeats arrive.
    if (!radio_ok && now - last_retry >= 1000) {
        last_retry = now;
        radio_ok = setup_radio(rf);
        if (radio_ok) {
            rf.openReadingPipe(1, kBaseAddress);
            rf.startListening();
        }
    }

    if (radio_ok) relay_radio();

    if (now - last_hello >= kHelloMs) {
        last_hello = now;
        send_hello();
    }
    if (radio_ok && now - last_ping >= PING_EVERY_MS) {
        last_ping = now;
        ping_node(++ping_id);
    }
}

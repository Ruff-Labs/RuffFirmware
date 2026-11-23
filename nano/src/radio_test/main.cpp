// Range test: try this at the field before relying on the radio link.
//
// Flash it on two Nanos with radios (rf24_setup.h wiring):
//   far end   jumper D2 to GND. It only listens (the radio acknowledges by itself). Battery power.
//   near end  no jumper, USB to a laptop, serial monitor at 115200 (pio device monitor).
// The near end sends a 32-byte packet every 20 ms and prints once a second:
//   sent 50  lost 0 (0.0%)  retries/packet 0.12  slowest 3.1 ms
// Walk the far end around the ring. "lost" is what would be late, never lost, in a race (the
// sensor node keeps resending), but more than a few % or retries above ~3 means a weak link:
// move the base station, raise the antennas, or try RF_POWER = RF24_PA_MAX in rf24_setup.h.
#include <Arduino.h>
#include <SPI.h>
#include "radio_protocol.h"
#include "rf24_setup.h"

constexpr uint8_t PIN_ROLE = 2;   // to GND = far end (listener)

RF24 rf(PIN_RF_CE, PIN_RF_CSN);
bool listener = false;

void setup() {
    Serial.begin(radio::kSerialBaud);
    pinMode(PIN_ROLE, INPUT_PULLUP);
    listener = digitalRead(PIN_ROLE) == LOW;

    while (!setup_radio(rf)) {
        Serial.println(F("radio_test: radio not answering, check its wiring and 3.3 V supply"));
        delay(1000);
    }
    if (listener) {
        rf.openReadingPipe(1, radio::kSensorNodeAddress);
        rf.startListening();
        Serial.println(F("radio_test: far end, listening"));
    } else {
        rf.openWritingPipe(radio::kSensorNodeAddress);
        rf.stopListening();
        Serial.println(F("radio_test: near end, sending"));
    }
}

void loop() {
    if (listener) {
        uint8_t buf[32];
        while (rf.available()) rf.read(buf, sizeof buf);
        return;
    }

    static uint32_t last_send = 0, last_print = 0, slowest_us = 0;
    static uint16_t sent = 0, lost = 0, retries = 0;
    static uint8_t  payload[32];

    if (millis() - last_send >= 20) {
        last_send = millis();
        payload[0]++;
        const uint32_t t0 = micros();
        const bool ok = rf.write(payload, sizeof payload);
        const uint32_t took = micros() - t0;
        sent++;
        if (!ok) lost++;
        retries += rf.getARC();
        if (took > slowest_us) slowest_us = took;
    }

    if (millis() - last_print >= 1000) {
        last_print = millis();
        Serial.print(F("sent "));     Serial.print(sent);
        Serial.print(F("  lost "));   Serial.print(lost);
        Serial.print(F(" ("));        Serial.print(sent ? 100.0 * lost / sent : 0.0, 1);
        Serial.print(F("%)  retries/packet "));
        Serial.print(sent ? float(retries) / sent : 0.0, 2);
        Serial.print(F("  slowest ")); Serial.print(slowest_us / 1000.0, 1);
        Serial.println(F(" ms"));
        sent = lost = retries = 0;
        slowest_us = 0;
    }
}

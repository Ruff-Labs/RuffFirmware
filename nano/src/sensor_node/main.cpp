// Sensor node: the Nano Every in the sensor unit. Timestamps every change on the 6 sensor inputs
// and sends them to the base station; the BeaglePlay does everything else (passes, the race).
//
// Wiring (input index = lane * 3 + signal, as in radio_protocol.h):
//   D2 lane 1 BACK    D3 lane 1 FORWARD    D4 lane 1 ALIGN
//   D5 lane 2 BACK    D6 lane 2 FORWARD    D7 lane 2 ALIGN
//   D8 radio IRQ, D9 CE, D10 CSN, D11-13 SPI (rf24_setup.h)
// Inputs must be 0-5 V: a 12 V sensor output needs an optocoupler or a divider.
//
// With a USB cable plugged in, every edge is printed at 115200 baud for bench testing.
#include <Arduino.h>
#include <SPI.h>
#include "radio_protocol.h"
#include "rf24_setup.h"

using namespace radio;

// ---------- settings ----------
constexpr uint8_t INPUT_PINS[kInputs] = { 2, 3, 4, 5, 6, 7 };
constexpr bool    ACTIVE_HIGH = true;    // TODO: match the sensors (false = active when pulled low)
constexpr uint8_t INPUT_MODE  = INPUT;   // TODO: INPUT_PULLUP for open-collector sensor outputs
constexpr int     EEPROM_BOOT = 0;

RF24 rf(PIN_RF_CE, PIN_RF_CSN);
uint8_t boot_id;

// ---------- edge queue (written by the pin interrupts) ----------
// Edges stay queued until the base station acknowledges them, so a radio dropout loses nothing:
// they arrive late, with their original times. 64 edges = about 10 dogs' worth.
constexpr uint8_t QUEUE = 64;   // power of 2
volatile uint8_t  q_code[QUEUE];
volatile uint32_t q_time[QUEUE];
volatile uint8_t  q_head = 0;          // next free slot (interrupts)
volatile uint8_t  q_tail = 0;          // oldest unsent edge (loop)
volatile uint16_t next_seq = 0;        // seq the next queued edge gets
volatile uint8_t  levels = 0;          // bit i = input i active, as last queued
volatile uint16_t dropped = 0;         // queue was full
uint16_t          tail_seq = 0;        // seq of q_tail

volatile uint32_t t_radio_irq = 0;     // when the radio last received something (a sync ping)

static bool active(uint8_t i) {
    return (digitalRead(INPUT_PINS[i]) == HIGH) == ACTIVE_HIGH;
}

// One handler per pin. The level is read in the handler: if it's back where it was already
// reported, the pin bounced faster than we could look, and nothing is queued.
template <uint8_t I>
void on_change() {
    const uint32_t t   = micros();
    const bool     on  = active(I);
    const uint8_t  bit = 1 << I;
    if (bool(levels & bit) == on) return;

    const uint8_t next = (q_head + 1) & (QUEUE - 1);
    levels ^= bit;                     // the level is right even if the edge can't be queued
    if (next == q_tail) { dropped++; return; }
    q_code[q_head] = I | (on ? kEdgeRising : 0);
    q_time[q_head] = t;
    q_head = next;
    next_seq++;
}

void on_radio_irq() { t_radio_irq = micros(); }

// ---------- sending ----------
static bool send(const void* p, uint8_t n) {
    rf.stopListening();
    const bool ok = rf.write(p, n);    // blocks until acked or out of retries (a few ms at worst)
    rf.startListening();
    return ok;
}

// Up to kMaxEdgesPerPacket from the front of the queue; they're dropped from it once acked.
static void send_edges() {
    RadioEdges pkt;
    pkt.boot      = boot_id;
    pkt.first_seq = tail_seq;
    pkt.count     = 0;
    uint8_t i = q_tail;
    while (i != q_head && pkt.count < kMaxEdgesPerPacket) {
        pkt.edges[pkt.count].code = q_code[i];
        pkt.edges[pkt.count].t_us = q_time[i];
        pkt.count++;
        i = (i + 1) & (QUEUE - 1);
    }
    const uint8_t size = sizeof pkt - sizeof pkt.edges + pkt.count * sizeof(Edge);
    if (!send(&pkt, size)) return;     // try again next loop

    if (Serial) {
        for (uint8_t k = 0; k < pkt.count; ++k) {
            const Edge& e = pkt.edges[k];
            Serial.print(F("edge #")); Serial.print(pkt.first_seq + k);
            Serial.print(F(" lane ")); Serial.print((e.code & 0x0F) / 3 + 1);
            Serial.print((e.code & 0x0F) % 3 == 0 ? F(" BACK ") : (e.code & 0x0F) % 3 == 1 ? F(" FORWARD ") : F(" ALIGN "));
            Serial.print(e.code & kEdgeRising ? F("on  ") : F("off "));
            Serial.println(e.t_us);
        }
    }
    q_tail   = i;
    tail_seq += pkt.count;
}

// Only sent with the queue empty, so next_seq/levels describe exactly the edges already sent.
// False if an edge came in first (it goes out before the heartbeat).
static bool send_heartbeat() {
    RadioHeartbeat hb;
    hb.boot = boot_id;
    noInterrupts();
    if (q_tail != q_head) { interrupts(); return false; }
    hb.levels   = levels;
    hb.next_seq = next_seq;
    hb.dropped  = dropped;
    hb.t_us     = micros();
    interrupts();
    return send(&hb, sizeof hb);
}

static void answer_ping(const RadioSyncPing& ping, uint32_t t_rx) {
    RadioSyncReply r;
    r.boot    = boot_id;
    r.id      = ping.id;
    r.t_rx_us = t_rx;
    send(&r, sizeof r);
}

// ---------- setup / loop ----------
void setup() {
    Serial.begin(kSerialBaud);
    boot_id = next_boot_id(EEPROM_BOOT);

    for (uint8_t i = 0; i < kInputs; ++i) {
        pinMode(INPUT_PINS[i], INPUT_MODE);
        if (active(i)) levels |= 1 << i;   // starting levels are reported by the first heartbeat
    }
    attachInterrupt(digitalPinToInterrupt(INPUT_PINS[0]), on_change<0>, CHANGE);
    attachInterrupt(digitalPinToInterrupt(INPUT_PINS[1]), on_change<1>, CHANGE);
    attachInterrupt(digitalPinToInterrupt(INPUT_PINS[2]), on_change<2>, CHANGE);
    attachInterrupt(digitalPinToInterrupt(INPUT_PINS[3]), on_change<3>, CHANGE);
    attachInterrupt(digitalPinToInterrupt(INPUT_PINS[4]), on_change<4>, CHANGE);
    attachInterrupt(digitalPinToInterrupt(INPUT_PINS[5]), on_change<5>, CHANGE);

    while (!setup_radio(rf)) {
        Serial.println(F("sensor node: radio not answering, check its wiring and 3.3 V supply"));
        delay(1000);
    }
    rf.maskIRQ(true, true, false);     // IRQ pin only for "received"
    pinMode(PIN_RF_IRQ, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_RF_IRQ), on_radio_irq, FALLING);
    rf.openWritingPipe(kBaseAddress);
    rf.openReadingPipe(1, kSensorNodeAddress);
    rf.startListening();
    Serial.print(F("sensor node: ready, boot ")); Serial.println(boot_id);
}

void loop() {
    static uint32_t last_hb = 0;

    // a sync ping: answer at once, with the time the radio's IRQ fired
    while (rf.available()) {
        const uint32_t t_rx = t_radio_irq;
        uint8_t buf[32];
        const uint8_t n = rf.getDynamicPayloadSize();
        if (n == 0 || n > sizeof buf) { rf.flush_rx(); break; }
        rf.read(buf, n);
        if (buf[0] == R_SYNC_PING && n >= sizeof(RadioSyncPing))
            answer_ping(*reinterpret_cast<RadioSyncPing*>(buf), t_rx);
    }

    if (q_tail != q_head) {
        send_edges();
    } else if (millis() - last_hb >= kHeartbeatMs) {
        if (send_heartbeat()) last_hb = millis();
    }
}

#pragma once
#include <stdint.h>

// The radio link between the sensor node, the base station and the BeaglePlay. Shared by the Nano
// sketches (nano/src) and the BeaglePlay firmware (firmware/src/radio), so it must stay plain C++
// with no standard library: avr-gcc has none.
//
//   sensor node --nRF24--> base station --USB serial--> BeaglePlay
//
// Radio packets (node <-> base) are at most 32 bytes, first byte = RadioType.
// Serial frames (base -> BeaglePlay) are COBS-encoded and 0-terminated, see radio_frame.h:
//   [SerialType][body][crc16 lo][crc16 hi]
//
// Times on the wire are the LOW 32 BITS of the sender's micros() (wraps every ~71 min). The
// BeaglePlay unwraps them and converts node time -> base time -> CLOCK_MONOTONIC (ClockFit.hpp).
// Change a layout -> bump kRadioVersion; the base station and the BeaglePlay drop other versions.
namespace radio {

constexpr uint8_t kRadioVersion = 1;

// ---------- radio settings (both ends must match) ----------
// Channel 78 = 2.478 GHz: inside the 2.4 GHz ISM band (it ends at 2.4835 GHz, channel 83), and
// above Wi-Fi channel 11, so the venue Wi-Fi is less in the way. 250 kbps = the longest range.
constexpr uint8_t kChannel     = 78;
constexpr uint8_t kRetryDelay  = 5;    // (5 + 1) * 250 us between retries
constexpr uint8_t kRetryCount  = 15;   // the most the nRF24 allows

// 5-byte pipe addresses. The base listens on kBaseAddress; each node on its own.
constexpr uint8_t kBaseAddress[5]       = { 'R', 'U', 'F', 'B', '0' };
constexpr uint8_t kSensorNodeAddress[5] = { 'R', 'U', 'F', 'N', '1' };

constexpr uint8_t kSensorNodeId = 1;   // node ids in serial frames (the light node will be 2)

// ---------- sensor inputs ----------
// Input index on the sensor node = lane * 3 + Signal (BACK = 0, FORWARD = 1, ALIGN = 2), the same
// order as the BeaglePlay's Signal enum (SensorEvents.hpp).
constexpr uint8_t kInputs = 6;

// One edge: code = input index (bits 0-3) | 0x80 if the input went active.
struct __attribute__((packed)) Edge {
    uint8_t  code;
    uint32_t t_us;
};
constexpr uint8_t kEdgeRising = 0x80;

// ---------- radio packets ----------
enum RadioType : uint8_t {
    R_EDGES      = 1,   // node -> base
    R_HEARTBEAT  = 2,   // node -> base, every kHeartbeatMs
    R_SYNC_PING  = 3,   // base -> node
    R_SYNC_REPLY = 4,   // node -> base, right after a ping
};

constexpr uint16_t kHeartbeatMs = 200;
constexpr uint8_t  kMaxEdgesPerPacket = 5;

// Edges in the order they happened. first_seq numbers the first one; the rest follow on. The node
// resends a packet until it's acknowledged, so the BeaglePlay can get one twice: seq drops repeats.
struct __attribute__((packed)) RadioEdges {
    uint8_t  type = R_EDGES;
    uint8_t  boot;                  // random per power-up: a change means the node restarted
    uint16_t first_seq;
    uint8_t  count;
    Edge     edges[kMaxEdgesPerPacket];
};

struct __attribute__((packed)) RadioHeartbeat {
    uint8_t  type = R_HEARTBEAT;
    uint8_t  boot;
    uint8_t  version = kRadioVersion;
    uint8_t  levels;                // bit i = input i active, read at t_us
    uint16_t next_seq;              // seq the next edge will get (every edge before it was sent first)
    uint16_t dropped;               // edges lost because the node's buffer was full (since power-up)
    uint32_t t_us;
};

struct __attribute__((packed)) RadioSyncPing {
    uint8_t type = R_SYNC_PING;
    uint8_t id;
};

struct __attribute__((packed)) RadioSyncReply {
    uint8_t  type = R_SYNC_REPLY;
    uint8_t  boot;
    uint8_t  id;                    // the ping's id
    uint32_t t_rx_us;               // node time the ping arrived (radio IRQ)
};

// ---------- serial frames, base station -> BeaglePlay ----------
enum SerialType : uint8_t {
    S_HELLO     = 1,   // every kHelloMs: the base station's clock, for base -> BeaglePlay time
    S_RADIO     = 2,   // a packet the base received: SerialRadio + the packet's bytes
    S_PING_SENT = 3,   // a sync ping went out (SerialPingSent)
};

constexpr uint16_t kHelloMs   = 100;
constexpr uint32_t kSerialBaud = 115200;

// Fixed size, so every hello takes the same time on the wire (the clock fit relies on that).
struct __attribute__((packed)) SerialHello {
    uint8_t  type = S_HELLO;
    uint8_t  version = kRadioVersion;
    uint8_t  boot;                  // random per base station power-up
    uint32_t t_us;                  // base micros() just before sending
};

struct __attribute__((packed)) SerialRadio {
    uint8_t type = S_RADIO;
    uint8_t node;                   // kSensorNodeId, ...
    // followed by the radio packet (1..32 bytes)
};

// t_tx_us: just before the ping was handed to the radio; t_ack_us: when its ack came back.
// The node received it in between, so a short gap = an exact sample.
struct __attribute__((packed)) SerialPingSent {
    uint8_t  type = S_PING_SENT;
    uint8_t  node;
    uint8_t  id;
    uint8_t  acked;                 // 0 = no ack after all the retries: no reply is coming
    uint32_t t_tx_us;
    uint32_t t_ack_us;
};

constexpr uint8_t kMaxFrame = 40;   // longest unencoded frame: SerialRadio + 32 bytes + CRC

// every radio packet must fit the nRF24's 32-byte payload
static_assert(sizeof(RadioEdges) <= 32 && sizeof(RadioHeartbeat) <= 32 && sizeof(RadioSyncReply) <= 32,
              "radio packets are limited to 32 bytes");
static_assert(sizeof(SerialRadio) + 32 + 2 <= kMaxFrame, "kMaxFrame too small");

}  // namespace radio

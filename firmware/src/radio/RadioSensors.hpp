#pragma once
#include "radio/ClockFit.hpp"
#include "radio/SerialLink.hpp"
#include "sensors/SensorEvents.hpp"
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

// The sensor node over the radio (RUFF_SENSORS=radio, the default): frames from the base station
// on USB serial (nano/include/radio_protocol.h) become the same LaneSignals the GPIO sensors give.
//
// Times: the node stamps each edge with its own clock. ClockFit turns that into base station time
// (from the sync pings) and then into CLOCK_MONOTONIC (from the base's hellos), so an edge that
// arrives late, even seconds late after a dropout, still gets the moment it happened.
//
// Online = the base station and the sensor node are both heard from within kOfflineAfter, and both
// clocks are fitted. Until then edges wait in a queue, so none are lost while the link settles.
class RadioSensors : public Sensors {
public:
    static constexpr uint64_t kOfflineAfterNs = 1'000'000'000ull;

    // Opens the port on a background thread. offset_us is added to every edge time (calibration,
    // RUFF_RADIO_OFFSET_US: see the README's accuracy test).
    explicit RadioSensors(const std::string& device, double offset_us = 0);

    // No serial port: frames come only from feed(). For tests and the radio_monitor tool.
    RadioSensors();

    void poll() override;
    bool online() const override { return online_; }

    // One frame body (CRC removed) that arrived at t_rx_ns. poll() calls this for each frame from
    // the port; tests call it directly.
    void feed(uint64_t t_rx_ns, const uint8_t* body, size_t n);

    // poll() without reading the port, with "now" given: applies what feed() queued. For tests.
    void poll_at(uint64_t now_ns);

    struct Stats {
        unsigned duplicates = 0;   // edges received twice (a lost ack): normal, harmless
        unsigned lost       = 0;   // edges that never arrived (bad serial frames): their passes are missing
        unsigned corrected  = 0;   // levels a heartbeat had to fix (follows a lost edge)
        unsigned node_dropped = 0; // edges the node couldn't queue (its buffer was full)
        unsigned node_reboots = 0;
        unsigned base_reboots = 0;
        unsigned bad_frames = 0;
    };
    const Stats&    stats() const { return stats_; }
    const ClockFit& node_clock() const { return node_fit_; }   // node us -> base us
    const ClockFit& base_clock() const { return base_fit_; }   // base us -> monotonic us
    bool            base_heard() const { return heard_base_; }
    bool            node_heard() const { return heard_node_; }

private:
    // a node edge or heartbeat, waiting to be applied in order
    struct Item {
        bool     heartbeat;
        uint8_t  boot;
        uint16_t seq;              // edge: its seq; heartbeat: next_seq
        uint8_t  code;             // edge: input | kEdgeRising; heartbeat: levels
        uint64_t t_node_us;        // unwrapped
        uint16_t dropped;          // heartbeat only
    };
    struct Ping { uint8_t id; bool used; uint64_t t_tx, t_ack; };

    void on_hello(uint64_t t_rx_ns, size_t frame_len, const uint8_t* p, size_t n);
    void on_ping_sent(const uint8_t* p, size_t n);
    void on_radio(uint64_t t_rx_ns, const uint8_t* p, size_t n);
    void node_boot(uint8_t boot);
    bool apply(const Item& it);    // false = stop for this poll (a pass just completed)
    uint64_t to_monotonic_ns(uint64_t t_node_us) const;
    void update_online(uint64_t now_ns);

    std::unique_ptr<SerialLink> link_;
    double offset_us_ = 0;

    // base station
    bool     heard_base_ = false;
    uint8_t  base_boot_  = 0;
    uint64_t last_hello_ns_ = 0;
    Unwrap32 base_time_;
    ClockFit base_fit_;
    Ping     pings_[8] = {};

    // sensor node
    bool     heard_node_ = false;
    uint8_t  node_boot_  = 0;
    uint64_t last_heartbeat_ns_ = 0;
    bool     have_levels_ = false;
    bool     have_seq_    = false;
    uint16_t expected_seq_ = 0;
    Unwrap32 node_time_;
    ClockFit node_fit_;
    std::deque<Item> pending_;

    bool  online_ = false;
    Stats stats_;
};

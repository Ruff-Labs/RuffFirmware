#pragma once
#include <gpiod.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

// Beam sensors: 2 lanes x 3 active-high signals, read without a thread.
//   ALIGN    high = beam reflecting off the reflector across the lane: lane ready
//   FORWARD  then BACK = one valid pass (a dog crossing in the right direction)
// Edge times are kernel timestamps (CLOCK_MONOTONIC ns), so reading them a tick late doesn't
// cost accuracy. Pin numbers and which lanes are wired: LANE_PINS in SensorEvents.cpp.
//
// TODO: ASSUMED, not yet checked on hardware: a crossing goes ALIGN -> FORWARD -> BACK -> ALIGN,
//       with ALIGN dropping while the beam is broken. Verify with gpiomon once the sensors are wired.

constexpr int NUM_LANES   = 2;   // LEFT = 0, RIGHT = 1 (state_machine.hpp); Timer.hpp uses this too
constexpr int NUM_SIGNALS = 3;

enum Signal { BACK = 0, FORWARD = 1, ALIGN = 2 };

// everything known about one lane's 3 pins
struct LaneSignals {
    bool     high[NUM_SIGNALS]   = {};  // current level (true = active)
    bool     rose[NUM_SIGNALS]   = {};  // went active during the last poll() (one-shot)
    uint64_t t_rise[NUM_SIGNALS] = {};  // ns, last rise
    uint64_t t_fall[NUM_SIGNALS] = {};  // ns, last fall

    bool     forward_armed = false;     // FORWARD broke, waiting for BACK
    bool     passed        = false;     // a valid pass during the last poll() (one-shot: each pass = one split)
    uint64_t t_pass        = 0;         // ns, when FORWARD broke for the latest pass
};

class Sensors {
public:
    // Requests every enabled lane's pins as inputs with edge detection, and reads their starting
    // levels (a lane may already be aligned at boot). Throws if the chip or lines aren't available.
    explicit Sensors(const std::string& chipPath = "/dev/gpiochip2");

    Sensors(const Sensors&) = delete;
    Sensors& operator=(const Sensors&) = delete;

    // Handles every edge waiting since the last call, in the order they happened (so FORWARD ->
    // BACK inside one poll still counts), and returns right away. Call once per tick.
    void poll();

    const LaneSignals& lane(int lane) const { return lanes_[lane]; }

    // Wired up in LANE_PINS. The race only waits for enabled lanes (one-lane mode for testing).
    bool enabled(int lane) const;

    // Forget edges and timestamps, keep the current levels. Called when the light sequence starts.
    void reset();

private:
    struct PinRef { int lane; Signal sig; };
    using LineMap = std::unordered_map<unsigned, PinRef>;

    static gpiod::line_request request_lines(const std::string& chipPath, LineMap& map);

    LineMap line_map_;                  // GPIO offset -> lane/signal (first: request_lines fills it)
    gpiod::line_request request_;
    gpiod::edge_event_buffer buffer_;
    std::array<LaneSignals, NUM_LANES> lanes_{};
};

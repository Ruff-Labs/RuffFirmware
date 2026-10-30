#pragma once
// Beam sensors: 2 lanes x 3 active-high signals (BACK / FORWARD / ALIGN), read without a thread.
// poll() never blocks: call it once per state machine tick.
//   ALIGN    high = beam reflecting across the lane, lane ready
//   FORWARD  then BACK = one valid pass (a dog crossing in the right direction)
// Edge times are kernel timestamps (CLOCK_MONOTONIC ns), so reading them a tick late doesn't hurt accuracy.
// Pin numbers are LANE_PINS in SensorEvents.cpp.
#include <gpiod.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

constexpr int NUM_LANES   = 2;  // matches Lane { LEFT = 0, RIGHT = 1 } in state_machine.cpp; Timer.hpp uses this one too
constexpr int NUM_SIGNALS = 3;

enum Signal {
    BACK    = 0,
    FORWARD = 1,
    ALIGN   = 2
};

// everything we know about one lane's 3 pins
struct LaneSignals {
    bool     high[NUM_SIGNALS]   = {};  // current level (true = signal active)
    bool     rose[NUM_SIGNALS]   = {};  // went active since the last poll() (cleared every poll)
    uint64_t t_rise[NUM_SIGNALS] = {};  // kernel timestamp (ns, CLOCK_MONOTONIC) of last accepted rise
    uint64_t t_fall[NUM_SIGNALS] = {};  // kernel timestamp of last fall

    // pass detection: FORWARD must break before BACK to count
    bool     forward_armed = false;     // FORWARD broke, waiting on BACK
    bool     passed        = false;     // valid pass seen during the last poll() (one-shot, cleared every poll
                                        // so every dog's pass shows up once and becomes a Timer split)
    uint64_t t_pass        = 0;         // when FORWARD broke for the latest pass
};

class Sensors {
public:
    explicit Sensors(const std::string& chipPath = "/dev/gpiochip2");

    // Prevent copying (we own hardware resources)
    Sensors(const Sensors&) = delete;
    Sensors& operator=(const Sensors&) = delete;

    void poll();                                   // drain pending edges, returns right away
    const LaneSignals& lane(int lane) const { return lanes_[lane]; }
    void reset();                                  // forget edges/timestamps, keep current levels

private:
    struct PinRef { int lane; Signal sig; };
    using LineMap = std::unordered_map<unsigned, PinRef>;

    static gpiod::line_request request_lines(const std::string& chipPath, LineMap& map);

    LineMap line_map_;                  // GPIO offset -> lane/signal (declared first: request_ fills it)
    gpiod::line_request request_;
    gpiod::edge_event_buffer buffer_;
    std::array<LaneSignals, NUM_LANES> lanes_{};
};

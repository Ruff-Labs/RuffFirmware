#pragma once
#include <array>
#include <cstdint>

// Beam sensors: 2 lanes x 3 active-high signals.
//   ALIGN    high = beam reflecting off the reflector across the lane: lane ready
//   FORWARD  then BACK = one valid pass (a dog crossing in the right direction)
// Edge times are CLOCK_MONOTONIC ns taken where the edge happened (the kernel for GPIO, the sensor
// node's clock converted for radio), so reading them a tick late doesn't cost accuracy.
//
// Two ways in, chosen in main.cpp by RUFF_SENSORS in /etc/ruff/firmware.env:
//   radio  RadioSensors (radio/RadioSensors.hpp): the sensor node over the nRF24 link (default)
//   gpio   GpioSensors (GpioSensors.hpp): sensors wired straight to the BeaglePlay's pins
//
// TODO: ASSUMED, not yet checked on hardware: a crossing goes ALIGN -> FORWARD -> BACK -> ALIGN,
//       with ALIGN dropping while the beam is broken. Verify once the sensors are wired.

constexpr int NUM_LANES   = 2;   // LEFT = 0, RIGHT = 1 (state_machine.hpp); Timer.hpp uses this too
constexpr int NUM_SIGNALS = 3;

enum Signal { BACK = 0, FORWARD = 1, ALIGN = 2 };   // same order as the sensor node's inputs

// false = the lane isn't wired: it's ignored and races start without it (one-lane mode).
// Set it to true once the lane is wired; nothing else changes.
constexpr bool LANE_ENABLED[NUM_LANES] = { true, false };

// everything known about one lane's 3 signals
struct LaneSignals {
    bool     high[NUM_SIGNALS]   = {};  // current level (true = active)
    bool     rose[NUM_SIGNALS]   = {};  // went active during the last poll() (one-shot)
    uint64_t t_rise[NUM_SIGNALS] = {};  // ns, last rise
    uint64_t t_fall[NUM_SIGNALS] = {};  // ns, last fall

    bool     forward_armed = false;     // FORWARD broke, waiting for BACK
    bool     passed        = false;     // a valid pass during the last poll() (one-shot: each pass = one split)
    uint64_t t_pass        = 0;         // ns, when FORWARD broke for the latest pass
};

// What the state machine reads. The backends differ only in where edges come from; the pass logic
// (edge()) is shared.
class Sensors {
public:
    virtual ~Sensors() = default;
    Sensors(const Sensors&) = delete;
    Sensors& operator=(const Sensors&) = delete;

    // Handles every edge waiting since the last call, in the order they happened (so FORWARD ->
    // BACK inside one poll still counts), and returns right away. Call once per tick.
    virtual void poll() = 0;

    const LaneSignals& lane(int lane) const { return lanes_[lane]; }

    // Wired up (LANE_ENABLED). The race only waits for enabled lanes.
    virtual bool enabled(int lane) const;

    // false = the readings can't be trusted right now (radio link down). Always true for GPIO.
    virtual bool online() const { return true; }

    // Forget edges and timestamps, keep the current levels. Called when the light sequence starts.
    virtual void reset();

protected:
    Sensors() = default;

    void begin_poll();                                      // clears the one-shots
    void edge(int lane, Signal sig, bool rising, uint64_t t_ns);   // a level change: times + pass logic
    void set_level(int lane, Signal sig, bool high);        // a level known without an edge (startup)

    std::array<LaneSignals, NUM_LANES> lanes_{};
};

#pragma once
#include "sensors/SensorEvents.hpp"   // NUM_LANES, Signal
#include <common/protocol.h>
#include <chrono>

// Spots sensor problems from the signal levels alone, for the GUI's Sensors screen
// (protocol::SensorFault). A disconnected sensor reads the same as "beam clear + not aligned",
// so it can't be told apart from a misaligned one; these are the faults the levels CAN show:
//
//   Blocked  FORWARD or BACK broken for BLOCKED_AFTER without a break: something sitting in the
//            beam, or a sensor stuck on. (A dog crossing breaks it for well under a second.)
//   Wiring   ALIGN, FORWARD and BACK all active for WIRING_AFTER: a working sensor never reports
//            all three at once, so a wire is shorted or swapped.
//   Offline  the radio link to the sensor node is down (Sensors::online()); wins over the rest.
//
// No hardware: update() takes the levels poll_inputs() already read, so tests can drive it.
class SensorHealth {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto BLOCKED_AFTER = std::chrono::seconds(10);
    static constexpr auto WIRING_AFTER  = std::chrono::milliseconds(200);   // ignore a passing glitch

    // Call every tick with the current levels of one lane.
    void update(int lane, bool align, bool forward, bool back, Clock::time_point now);

    // Call every tick with Sensors::online().
    void set_online(bool online) { online_ = online; }

    protocol::SensorFault fault(int lane, Clock::time_point now) const;

private:
    struct Held {
        bool              on = false;
        Clock::time_point since{};   // when it last went on
    };
    static void track(Held& h, bool on, Clock::time_point now);

    Held forward_[NUM_LANES], back_[NUM_LANES], all_three_[NUM_LANES];
    bool online_ = true;
};

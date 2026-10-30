// SensorEvents.cpp  —  2 lanes x 3 active-high signals, polled from the state machine loop
#include "sensors/SensorEvents.hpp"
#include <chrono>

// ---------- settings ----------
// TODO: no glitch filter, the signals come in as clean 1/0. If they bounce on hardware, ignore
//       a rise that comes within ~50 ms of the last fall on the same pin.

// run gpioinfo on the beagle to find the offsets, e.g.
//         line  26:      "P8_14"       unused   input  active-high

struct LanePins {
    bool     enabled;               // only enabled lanes get requested from the chip
    unsigned line[NUM_SIGNALS];     // GPIO offsets, indexed by Signal: { BACK, FORWARD, ALIGN }
};

//TODO: change these to the real offsets
const std::array<LanePins, NUM_LANES> LANE_PINS = {{
    { true,  {  7,  8,  9 } },   // lane 1
    { false, { 10, 11, 12 } },   // lane 2 (not wired yet)
}};

// ---------- setup ----------
gpiod::line_request Sensors::request_lines(const std::string& chipPath, LineMap& map) {
    gpiod::line_settings settings;
    settings.set_direction(gpiod::line::direction::INPUT)
            .set_edge_detection(gpiod::line::edge::BOTH)
            .set_event_clock(gpiod::line::clock::MONOTONIC)   // same clock as std::chrono::steady_clock
            .set_bias(gpiod::line::bias::AS_IS);              // TODO: PULL_DOWN if the source only drives high

    gpiod::line::offsets offsets;
    for (int l = 0; l < NUM_LANES; ++l) {
        if (!LANE_PINS[l].enabled) continue;
        for (int s = 0; s < NUM_SIGNALS; ++s) {
            map[LANE_PINS[l].line[s]] = { l, Signal(s) };
            offsets.push_back(LANE_PINS[l].line[s]);
        }
    }

    return gpiod::chip(chipPath)
        .prepare_request()
        .set_consumer("ruff-sensors")
        .add_line_settings(offsets, settings)   // all enabled pins in ONE request
        .do_request();
}

Sensors::Sensors(const std::string& chipPath)
    : request_(request_lines(chipPath, line_map_)),
      buffer_(64) {
    // edges only report changes, so grab the starting levels (a dog may already be aligned at boot)
    for (auto& [line, pin] : line_map_)
        lanes_[pin.lane].high[pin.sig] = request_.get_value(line) == gpiod::line::value::ACTIVE;
}

// ---------- pass logic ----------
// edges are handled in the order they happened, so FORWARD -> BACK inside one poll still works
// TODO: ASSUMED (not yet checked on hardware): a dog crossing goes ALIGN -> FORWARD -> BACK -> ALIGN,
// with ALIGN dropping while the beam is broken. Verify with gpiomon once the sensors are wired.
static void track_pass(LaneSignals& ln, Signal sig, uint64_t t) {
    switch (sig) {
        case ALIGN:                       // beam back on the reflector = lane ready; doesn't touch the pass
            break;                          // (it comes back right after a dog clears the beam)
        case FORWARD:
            if (!ln.forward_armed) {        // keep the first break if FORWARD bounces
                ln.forward_armed = true;
                ln.t_pass = t;
            }
            break;
        case BACK:
            if (ln.forward_armed) ln.passed = true;   // FORWARD then BACK = valid pass
            // BACK without FORWARD first = wrong direction, ignored
            ln.forward_armed = false;
            break;
    }
}

// ---------- non-blocking read ----------
void Sensors::poll() {
    for (auto& ln : lanes_) {
        for (bool& r : ln.rose) r = false;
        ln.passed = false;   // one-shot like rose[], so every pass shows up exactly once
    }

    // timeout 0 = just check, never sleep. Timestamps come from the kernel at the
    // moment of the edge, so reading them a tick late doesn't hurt accuracy.
    while (request_.wait_edge_events(std::chrono::nanoseconds(0))) {
        request_.read_edge_events(buffer_);
        for (const auto& ev : buffer_) {
            const PinRef& pin = line_map_.at(ev.line_offset());
            LaneSignals&  ln  = lanes_[pin.lane];
            const uint64_t t  = ev.timestamp_ns().ns();
            const bool rising = ev.type() == gpiod::edge_event::event_type::RISING_EDGE; // active high

            ln.high[pin.sig] = rising;
            if (rising) {
                ln.t_rise[pin.sig] = t;
                ln.rose[pin.sig]   = true;
                track_pass(ln, pin.sig, t);
            } else {
                ln.t_fall[pin.sig] = t;
            }
        }
    }
}

void Sensors::reset() {
    for (auto& ln : lanes_) {
        for (int s = 0; s < NUM_SIGNALS; ++s) {
            ln.rose[s]   = false;
            ln.t_rise[s] = 0;
            ln.t_fall[s] = 0;
        }
        ln.forward_armed = false;
        ln.passed        = false;
        ln.t_pass        = 0;
    }
}

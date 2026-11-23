#include "sensors/SensorEvents.hpp"
#include <chrono>
#include <cstdio>

// ---------- settings ----------
// GPIO offsets: run gpioinfo on the board, e.g.  line 26: "P8_14" unused input active-high
// enabled = false: the lane isn't wired. Its pins aren't requested and races start without it
// (one-lane mode). Set it to true once it's wired; nothing else changes.
// TODO: real offsets. And there's no glitch filter: if the signals bounce on hardware, ignore a
//       rise within ~50 ms of the last fall on the same pin.
struct LanePins {
    bool     enabled;
    unsigned line[NUM_SIGNALS];     // indexed by Signal: { BACK, FORWARD, ALIGN }
};

const std::array<LanePins, NUM_LANES> LANE_PINS = {{
    { true,  {  7,  8,  9 } },   // lane 1
    { false, { 10, 11, 12 } },   // lane 2 (not wired yet)
}};

// ---------- setup ----------
gpiod::line_request Sensors::request_lines(const std::string& chipPath, LineMap& map) {
    gpiod::line_settings settings;
    settings.set_direction(gpiod::line::direction::INPUT)
            .set_edge_detection(gpiod::line::edge::BOTH)
            .set_event_clock(gpiod::line::clock::MONOTONIC)   // same clock as steady_clock
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
        .add_line_settings(offsets, settings)
        .do_request();
}

Sensors::Sensors(const std::string& chipPath)
    : request_(request_lines(chipPath, line_map_)),
      buffer_(64) {
    for (auto& [line, pin] : line_map_)
        lanes_[pin.lane].high[pin.sig] = request_.get_value(line) == gpiod::line::value::ACTIVE;
    for (int l = 0; l < NUM_LANES; ++l)
        if (!enabled(l))
            std::printf("sensors: lane %d is disabled in LANE_PINS: one-lane mode, races start without it\n", l + 1);
}

bool Sensors::enabled(int lane) const {
    return lane >= 0 && lane < NUM_LANES && LANE_PINS[lane].enabled;
}

// ---------- pass logic ----------
static void track_pass(LaneSignals& ln, Signal sig, uint64_t t) {
    switch (sig) {
        case ALIGN:                       // comes back right after a dog clears the beam; not part of a pass
            break;
        case FORWARD:
            if (!ln.forward_armed) {      // keep the first break if FORWARD bounces
                ln.forward_armed = true;
                ln.t_pass = t;
            }
            break;
        case BACK:
            if (ln.forward_armed) ln.passed = true;   // BACK without FORWARD first = wrong way, ignored
            ln.forward_armed = false;
            break;
    }
}

void Sensors::poll() {
    for (auto& ln : lanes_) {
        for (bool& r : ln.rose) r = false;
        ln.passed = false;
    }

    while (request_.wait_edge_events(std::chrono::nanoseconds(0))) {   // 0 = just check, never sleep
        request_.read_edge_events(buffer_);
        for (const auto& ev : buffer_) {
            const PinRef&  pin    = line_map_.at(ev.line_offset());
            LaneSignals&   ln     = lanes_[pin.lane];
            const uint64_t t      = ev.timestamp_ns().ns();
            const bool     rising = ev.type() == gpiod::edge_event::event_type::RISING_EDGE;

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

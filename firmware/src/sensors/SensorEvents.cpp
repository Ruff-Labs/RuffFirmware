#include "sensors/SensorEvents.hpp"

bool Sensors::enabled(int lane) const {
    return lane >= 0 && lane < NUM_LANES && LANE_ENABLED[lane];
}

void Sensors::begin_poll() {
    for (auto& ln : lanes_) {
        for (bool& r : ln.rose) r = false;
        ln.passed = false;
    }
}

void Sensors::set_level(int lane, Signal sig, bool high) {
    if (lane >= 0 && lane < NUM_LANES) lanes_[lane].high[sig] = high;
}

void Sensors::edge(int lane, Signal sig, bool rising, uint64_t t) {
    if (lane < 0 || lane >= NUM_LANES) return;
    LaneSignals& ln = lanes_[lane];
    ln.high[sig] = rising;
    if (!rising) {
        ln.t_fall[sig] = t;
        return;
    }
    ln.t_rise[sig] = t;
    ln.rose[sig]   = true;

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

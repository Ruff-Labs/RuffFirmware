#include "sensors/SensorHealth.hpp"

void SensorHealth::track(Held& h, bool on, Clock::time_point now) {
    if (on && !h.on) h.since = now;
    h.on = on;
}

void SensorHealth::update(int lane, bool align, bool forward, bool back, Clock::time_point now) {
    if (lane < 0 || lane >= NUM_LANES) return;
    track(forward_[lane],   forward,                   now);
    track(back_[lane],      back,                      now);
    track(all_three_[lane], align && forward && back,  now);
}

protocol::SensorFault SensorHealth::fault(int lane, Clock::time_point now) const {
    using protocol::SensorFault;
    if (lane < 0 || lane >= NUM_LANES) return SensorFault::None;

    const auto held = [now](const Held& h, Clock::duration d) { return h.on && now - h.since >= d; };
    if (held(all_three_[lane], WIRING_AFTER)) return SensorFault::Wiring;
    if (held(forward_[lane], BLOCKED_AFTER) || held(back_[lane], BLOCKED_AFTER)) return SensorFault::Blocked;
    return SensorFault::None;
}

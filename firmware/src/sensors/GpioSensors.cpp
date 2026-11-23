#include "sensors/GpioSensors.hpp"
#include <chrono>
#include <cstdio>

// GPIO offsets per lane, indexed by Signal: { BACK, FORWARD, ALIGN }. Run gpioinfo on the board,
// e.g.  line 26: "P8_14" unused input active-high
// TODO: real offsets. No glitch filter: if the signals bounce, ignore a rise within ~50 ms of the
//       last fall on the same pin.
const unsigned LANE_PINS[NUM_LANES][NUM_SIGNALS] = {
    {  7,  8,  9 },   // lane 1
    { 10, 11, 12 },   // lane 2
};

gpiod::line_request GpioSensors::request_lines(const std::string& chipPath, LineMap& map) {
    gpiod::line_settings settings;
    settings.set_direction(gpiod::line::direction::INPUT)
            .set_edge_detection(gpiod::line::edge::BOTH)
            .set_event_clock(gpiod::line::clock::MONOTONIC)   // same clock as steady_clock
            .set_bias(gpiod::line::bias::AS_IS);              // TODO: PULL_DOWN if the source only drives high

    gpiod::line::offsets offsets;
    for (int l = 0; l < NUM_LANES; ++l) {
        if (!LANE_ENABLED[l]) continue;
        for (int s = 0; s < NUM_SIGNALS; ++s) {
            map[LANE_PINS[l][s]] = { l, Signal(s) };
            offsets.push_back(LANE_PINS[l][s]);
        }
    }

    return gpiod::chip(chipPath)
        .prepare_request()
        .set_consumer("ruff-sensors")
        .add_line_settings(offsets, settings)
        .do_request();
}

GpioSensors::GpioSensors(const std::string& chipPath)
    : request_(request_lines(chipPath, line_map_)),
      buffer_(64) {
    for (auto& [line, pin] : line_map_)
        set_level(pin.lane, pin.sig, request_.get_value(line) == gpiod::line::value::ACTIVE);
    for (int l = 0; l < NUM_LANES; ++l)
        if (!enabled(l))
            std::printf("sensors: lane %d is disabled in LANE_ENABLED: one-lane mode, races start without it\n", l + 1);
}

void GpioSensors::poll() {
    begin_poll();
    while (request_.wait_edge_events(std::chrono::nanoseconds(0))) {   // 0 = just check, never sleep
        request_.read_edge_events(buffer_);
        for (const auto& ev : buffer_) {
            const PinRef& pin = line_map_.at(ev.line_offset());
            edge(pin.lane, pin.sig, ev.type() == gpiod::edge_event::event_type::RISING_EDGE,
                 ev.timestamp_ns().ns());
        }
    }
}

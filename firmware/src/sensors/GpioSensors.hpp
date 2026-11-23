#pragma once
#include "sensors/SensorEvents.hpp"
#include <gpiod.hpp>
#include <string>
#include <unordered_map>

// Sensors wired straight to the BeaglePlay's pins (RUFF_SENSORS=gpio), read through libgpiod
// without a thread. Edge times are kernel timestamps. Pin numbers: LANE_PINS in GpioSensors.cpp.
class GpioSensors : public Sensors {
public:
    // Requests every enabled lane's pins as inputs with edge detection, and reads their starting
    // levels (a lane may already be aligned at boot). Throws if the chip or lines aren't available.
    explicit GpioSensors(const std::string& chipPath = "/dev/gpiochip2");

    void poll() override;

private:
    struct PinRef { int lane; Signal sig; };
    using LineMap = std::unordered_map<unsigned, PinRef>;

    static gpiod::line_request request_lines(const std::string& chipPath, LineMap& map);

    LineMap line_map_;                  // GPIO offset -> lane/signal (first: request_lines fills it)
    gpiod::line_request request_;
    gpiod::edge_event_buffer buffer_;
};

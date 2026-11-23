#pragma once
// Minimal stand-in for libgpiod v2's C++ API, so the firmware headers compile in the unit tests
// (and on a Mac, which has no libgpiod). Nothing here touches hardware; the tests use
// fake_hardware.cpp instead of the real LightTree/Sensors/Remote .cpp files.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace gpiod {
namespace line {
using offset = unsigned;
using offsets = std::vector<unsigned>;
enum class value { INACTIVE, ACTIVE };
enum class direction { INPUT, OUTPUT };
enum class edge { NONE, RISING, FALLING, BOTH };
enum class clock { MONOTONIC, REALTIME };
enum class bias { AS_IS, PULL_UP, PULL_DOWN, DISABLED };
}
struct timestamp { std::uint64_t ns() const { return 0; } };
struct edge_event {
    enum class event_type { RISING_EDGE, FALLING_EDGE };
    event_type type() const { return event_type::RISING_EDGE; }
    line::offset line_offset() const { return 0; }
    timestamp timestamp_ns() const { return {}; }
};
struct edge_event_buffer {
    edge_event_buffer(std::size_t = 0) {}
    const edge_event* begin() const { return nullptr; }
    const edge_event* end() const { return nullptr; }
};
struct line_settings {
    line_settings& set_direction(line::direction) { return *this; }
    line_settings& set_edge_detection(line::edge) { return *this; }
    line_settings& set_event_clock(line::clock) { return *this; }
    line_settings& set_bias(line::bias) { return *this; }
    line_settings& set_output_value(line::value) { return *this; }
    line_settings& set_debounce_period(std::chrono::microseconds) { return *this; }
};
struct line_request {
    void set_value(line::offset, line::value) {}
    line::value get_value(line::offset) { return line::value::INACTIVE; }
    bool wait_edge_events(std::chrono::nanoseconds) { return false; }
    std::size_t read_edge_events(edge_event_buffer&) { return 0; }
};
struct request_builder {
    request_builder& set_consumer(const std::string&) { return *this; }
    request_builder& add_line_settings(const line::offsets&, const line_settings&) { return *this; }
    line_request do_request() { return {}; }
};
struct chip {
    explicit chip(const std::string&) {}
    request_builder prepare_request() { return {}; }
};
}

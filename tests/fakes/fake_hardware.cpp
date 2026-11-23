// fake_hardware.cpp  —  laptop versions of LightTree.cpp, SensorEvents.cpp and Remote.cpp
#include "fakes/fake_hardware.hpp"
#include <stdexcept>

FakeHardware& fake() {
    static FakeHardware hw;
    return hw;
}

void fake_reset() {
    fake() = FakeHardware{};
}

// same names as LightTree::PIN_MAP in LightTree.cpp, so a typo in the state machine fails a test
static const char* const LIGHT_NAMES[] = {
    "left_align", "center_ready", "right_align", "yellow_1", "yellow_2",
    "left_red", "right_red", "left_green", "right_green", "blue",
};

static bool known_light(const std::string& name) {
    for (const char* n : LIGHT_NAMES)
        if (name == n) return true;
    return false;
}

bool light(const std::string& name) {
    if (!known_light(name)) throw std::runtime_error("test asked for unknown light: " + name);
    auto it = fake().lights.find(name);
    return it != fake().lights.end() && it->second;
}

// ---------- LightTree ----------
LightTree::LightTree(const std::string&) : request_() {}
LightTree::~LightTree() { allOff(); }

void LightTree::set(const std::string& name, bool state) {
    if (!known_light(name)) throw std::runtime_error("Unknown light: " + name);   // like the real one
    fake().lights[name] = state;
}
void LightTree::allOff() {
    for (const char* n : LIGHT_NAMES) fake().lights[n] = false;
}
void LightTree::on(const std::string& name)  { set(name, true); }
void LightTree::off(const std::string& name) { set(name, false); }
void LightTree::onGroup(const std::vector<std::string>& names)  { for (const auto& n : names) on(n); }
void LightTree::offGroup(const std::vector<std::string>& names) { for (const auto& n : names) off(n); }

// ---------- Sensors ----------
Sensors::Sensors(const std::string&) : request_(), buffer_(0) {}

void Sensors::poll() {
    for (int l = 0; l < NUM_LANES; ++l) {
        LaneSignals& src = fake().lanes[l];
        lanes_[l] = src;
        for (bool& r : src.rose) r = false;   // one-shots, like the real poll()
        src.passed = false;
    }
}

bool Sensors::enabled(int lane) const { return lane >= 0 && lane < NUM_LANES && fake().lane_enabled[lane]; }

void Sensors::reset() {   // same as the real reset(): forget edges, keep levels
    for (int l = 0; l < NUM_LANES; ++l) {
        for (LaneSignals* ln : { &lanes_[l], &fake().lanes[l] }) {
            for (int s = 0; s < NUM_SIGNALS; ++s) {
                ln->rose[s]   = false;
                ln->t_rise[s] = 0;
                ln->t_fall[s] = 0;
            }
            ln->forward_armed = false;
            ln->passed        = false;
            ln->t_pass        = 0;
        }
    }
}

// ---------- Remote ----------
bool Remote::begin() { return true; }

RemoteInputs Remote::poll() {
    RemoteInputs out = fake().remote;
    fake().remote    = RemoteInputs{};        // presses last one tick
    out.judge_ready  = fake().ready_latched;  // READY is a level
    return out;
}

void Remote::clear_ready() { fake().ready_latched = false; }

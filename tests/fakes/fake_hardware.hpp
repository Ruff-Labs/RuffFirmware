#pragma once
// Stand-ins for the hardware, so the state machine runs on a laptop. LightTree and Remote keep
// the real headers (fake_hardware.cpp replaces the .cpp files that talk to GPIO); the sensors are
// FakeSensors, one more Sensors backend. A test sets what the "hardware" reports in fake() and
// reads back which lights are on.
#include "light_tree/LightTree.hpp"
#include "remote/Remote.hpp"
#include "sensors/SensorEvents.hpp"
#include <map>
#include <string>

struct FakeHardware {
    std::map<std::string, bool> lights;   // light name -> on (only names LightTree::PIN_MAP has)
    LaneSignals  lanes[NUM_LANES];        // what the next Sensors::poll() reports; rose[]/passed are one-shots
    RemoteInputs remote;                  // one-shots for the next Remote::poll()
    bool         ready_latched = false;   // the judge's READY toggle (cleared by Remote::clear_ready())
    bool         lane_enabled[NUM_LANES] = { true, true };   // LANE_ENABLED (false = one-lane mode)
    bool         sensors_online = true;                       // Sensors::online() (radio link up)
};

// Reports fake().lanes; rose[]/passed are one-shots, like the real backends.
class FakeSensors : public Sensors {
public:
    FakeSensors() = default;
    void poll() override;
    bool enabled(int lane) const override;
    bool online() const override;
    void reset() override;
};

FakeHardware& fake();
void fake_reset();                        // everything off / released, before each test
bool light(const std::string& name);      // is this light on?

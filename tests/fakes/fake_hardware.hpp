#pragma once
// Stand-ins for the three hardware classes (LightTree, Sensors, Remote), so the state machine
// runs on a laptop. They keep the real headers' interfaces; fake_hardware.cpp replaces the
// .cpp files that talk to GPIO. A test sets what the "hardware" reports in fake() and reads
// back which lights are on.
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
    bool         lane_enabled[NUM_LANES] = { true, true };   // LANE_PINS[].enabled (false = one-lane mode)
};

FakeHardware& fake();
void fake_reset();                        // everything off / released, before each test
bool light(const std::string& name);      // is this light on?

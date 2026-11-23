#pragma once
#include <gpiod.hpp>
#include <map>
#include <string>
#include <vector>

// Light tree: one GPIO output per light, switched by name. Names (PIN_MAP in LightTree.cpp):
//   left_align, center_ready, right_align, blue, yellow_1, yellow_2,
//   left_red, right_red, left_green, right_green
// The race sequence itself is run by the state machine (SEQUENCE[] in state_machine.cpp).
// TODO: check PIN_MAP and the chip against the real wiring with tree_test.
class LightTree {
public:
    // Requests every light's line as an output, all off. Throws if the chip or lines aren't available.
    explicit LightTree(const std::string& chipName = "gpiochip1");
    ~LightTree();   // all lights off; the lines are released with request_

    LightTree(const LightTree&) = delete;
    LightTree& operator=(const LightTree&) = delete;

    // set/on/off throw std::runtime_error for a name that isn't in PIN_MAP
    void on(const std::string& name);
    void off(const std::string& name);
    void set(const std::string& name, bool state);
    void allOff();
    void onGroup(const std::vector<std::string>& names);
    void offGroup(const std::vector<std::string>& names);

private:
    static gpiod::line_request make_request(const std::string& chip);

    gpiod::line_request request_;
    static const std::map<std::string, unsigned int> PIN_MAP;   // light name -> GPIO line offset
};

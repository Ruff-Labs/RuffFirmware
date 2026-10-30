#pragma once
#include <gpiod.hpp>
#include <map>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

// Light tree: one GPIO output per light, switched by name.
// Names (PIN_MAP in LightTree.cpp): left_align, center_ready, right_align, blue, yellow_1, yellow_2,
//                                   left_red, right_red, left_green, right_green
// set()/on()/off() throw std::runtime_error on an unknown name.
// The race sequence itself is run by the state machine (SEQUENCE[] in state_machine.cpp).
class LightTree {
public:
    // Constructor: opens the chip and requests all lines
    explicit LightTree(const std::string& chipName = "gpiochip1");

    // Destructor: ensures all lights off + lines released (RAII)
    ~LightTree();

    // Prevent copying (we own hardware resources)
    LightTree(const LightTree&) = delete;
    LightTree& operator=(const LightTree&) = delete;

    // Single-light control
    void on(const std::string& name);
    void off(const std::string& name);
    void set(const std::string& name, bool state);
    void allOff();

    // Group control
    void onGroup(const std::vector<std::string>& names);
    void offGroup(const std::vector<std::string>& names);

    // Blocking demo sequences, only used by tree_test (TreeTest.cpp)
    //FIXME: delete after testing.
    void runSportsmanTree();   // ambers 0.5s apart
    void runProTree();         // all ambers at once, 0.4s to green

private:
    gpiod::line_request request_;   // all the light lines, held while the LightTree exists

    // Opens the chip and requests every line in PIN_MAP as an output (member so it can read PIN_MAP)
    static gpiod::line_request make_request(const std::string& chip);

    // Helper for delays
    void wait(std::chrono::milliseconds ms);

    // The pin map
    static const std::map<std::string, unsigned int> PIN_MAP;
};
#pragma once
//sudo apt install libgpiod-dev (INSTALL DEV HEADERS)
#include <gpiod.hpp>
#include <map>
#include <string>
#include <vector>
#include <chrono>

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

    // Group and pair control
    void onGroup(const std::vector<std::string>& names);
    void offGroup(const std::vector<std::string>& names);

    void onPair(const std::vector<std::string>& names);
    void onPair(const std::vector<std::string>& names);

    // Pre-built race sequences
    //FIXME: delete after testing.
    void runSportsmanTree();   // ambers 0.5s apart
    void runProTree();         // all ambers at once, 0.4s to green

private:
    //gpiod::chip chip_;
    //std::map<std::string, gpiod::line> lines_;

    // Helper for delays
    void wait(std::chrono::milliseconds ms);

    // The pin map
    static const std::map<std::string, unsigned int> PIN_MAP;
};
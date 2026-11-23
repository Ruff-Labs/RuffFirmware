#include "light_tree/LightTree.hpp"
#include <chrono>
#include <iostream>
#include <thread>

// Light tree test program, built as build/firmware/tree_test (stop the firmware service first):
//   sudo ./build/firmware/tree_test     # each light on for 0.6 s, one at a time, in PIN_MAP order
// Use it to check the wiring: every name printed should match the light that comes on.

int main() {
    try {
        LightTree tree("gpiochip1");
        for (const char* name : {
                 "left_align", "center_ready", "right_align",
                 "blue", "yellow_1", "yellow_2",
                 "left_red", "right_red",
                 "left_green", "right_green" }) {
            std::cout << "Testing: " << name << std::endl;
            tree.on(name);
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            tree.off(name);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

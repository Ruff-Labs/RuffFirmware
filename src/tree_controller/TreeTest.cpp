#include "LightTree.hpp"
#include <iostream>
#include <string>

//1. install dev package
//sudo apt install libgpiod-dev

//make sure your in the file folder
// g++ -std=c++17 -Wall -o tree TreeTest.cpp LightTree.cpp -lgpiodcxx

// sudo ./tree test        # test each light one by one
// sudo ./tree sportsman   # run sportsman sequence
// sudo ./tree pro         # run pro sequence


int main(int argc, char* argv[]) {
    try {
        LightTree tree("gpiochip1");

        // Simple menu based on command-line argument
        std::string mode = (argc > 1) ? argv[1] : "sportsman";

        if (mode == "sportsman") {
            tree.runSportsmanTree();
        } else if (mode == "pro") {
            tree.runProTree();
        } else if (mode == "test") {
            // Cycle each light one at a time
            for (const auto& name : {
                    "left_align", "center_ready", "right_align",
                    "yellow_1", "yellow_2",
                    "left_red", "right_red",
                    "left_green", "right_green", "blue"}) {
                std::cout << "Testing: " << name << "\n";
                tree.on(name);
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                tree.off(name);
            }
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [sportsman|pro|test]\n";
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
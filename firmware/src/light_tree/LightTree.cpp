#include "LightTree.hpp"
#include <iostream>
#include <thread>
#include <stdexcept>

using namespace std::chrono_literals;

// ---- Pin map: light name -> GPIO line offset ----
const std::map<std::string, unsigned int> LightTree::PIN_MAP = {
    {"left_align",   1},
    {"center_ready", 2},
    {"right_align",  3},
    {"yellow_1",     4},
    {"yellow_2",     9},
    {"left_red",    10},
    {"right_red",   11},
    {"left_green",  12},
    {"right_green", 13},
    {"blue",        14},
};

LightTree::LightTree(const std::string& chipName)
    : chip_(chipName)
{
    for (const auto& [name, offset] : PIN_MAP) {
        gpiod::line line = chip_.get_line(offset);
        line.request(
            {"light_tree", gpiod::line_request::DIRECTION_OUTPUT, 0},
            0  // initial value = LOW (off)
        );
        lines_[name] = std::move(line);
    }
    std::cout << "LightTree initialized on " << chipName
              << " (" << lines_.size() << " lines)\n";
}

LightTree::~LightTree() {

    allOff();
    for (auto& [name, line] : lines_) {
        line.release();
    }
    std::cout << "LightTree shut down.\n";
}

void LightTree::set(const std::string& name, bool state) {
    auto it = lines_.find(name);
    if (it == lines_.end()) {
        throw std::runtime_error("Unknown light: " + name);
    }
    it->second.set_value(state ? 1 : 0);
}

void LightTree::on(const std::string& name)  { set(name, true);  }
void LightTree::off(const std::string& name) { set(name, false); }

void LightTree::allOff() {
    for (auto& [name, line] : lines_) {
        line.set_value(0);
    }
}

void LightTree::onGroup(const std::vector<std::string>& names) {
    for (const auto& n : names) on(n);
}

void LightTree::offGroup(const std::vector<std::string>& names) {
    for (const auto& n : names) off(n);
}

void LightTree::wait(std::chrono::milliseconds ms) {
    std::this_thread::sleep_for(ms);
}

// ---- Sportsman tree: ambers count down 0.5s apart ----
void LightTree::runSportsmanTree() {
    std::cout << "Running Sportsman tree...\n";
    allOff();
    wait(1s);

    // pre-stage / stage
    onGroup({"left_align", "right_align", "center_ready"});
    wait(1s);

    // yellow sequence
    on("yellow_1");
    wait(500ms);
    off("yellow_1");
    on("yellow_2");
    wait(500ms);
    off("yellow_2");

    // green
    onGroup({"left_green", "right_green"});
    wait(3s);

    allOff();
}

// ---- Pro tree: all ambers at once, 0.4s to green ----
void LightTree::runProTree() {
    std::cout << "Running Pro tree...\n";
    allOff();
    wait(1s);

    onGroup({"left_align", "right_align", "center_ready"});
    wait(1s);

    // yellow
    onGroup({"yellow_1", "yellow_2"});
    wait(400ms);
    offGroup({"yellow_1", "yellow_2"});

    // green
    onGroup({"left_green", "right_green"});
    wait(3s);

    allOff();
}
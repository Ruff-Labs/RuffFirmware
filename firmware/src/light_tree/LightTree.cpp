#include "light_tree/LightTree.hpp"
#include <iostream>
#include <thread>
#include <stdexcept>

using namespace std::chrono_literals; // 1s, 500ms, ...

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

gpiod::line_request LightTree::make_request(const std::string& chip) {
    gpiod::line::offsets offs;
    for (const auto& [name, off] : PIN_MAP) offs.push_back(off);
    return gpiod::chip("/dev/" + chip).prepare_request()
        .set_consumer("light_tree")
        .add_line_settings(offs, gpiod::line_settings()
            .set_direction(gpiod::line::direction::OUTPUT)
            .set_output_value(gpiod::line::value::INACTIVE))
        .do_request();
}

LightTree::LightTree(const std::string& chipName) : request_(make_request(chipName)) {}

LightTree::~LightTree() { allOff(); }   // request_ releases the lines itself

void LightTree::set(const std::string& name, bool state) {
    auto it = PIN_MAP.find(name);
    if (it == PIN_MAP.end()) throw std::runtime_error("Unknown light: " + name);
    request_.set_value(it->second, state ? gpiod::line::value::ACTIVE
                                         : gpiod::line::value::INACTIVE);
}

void LightTree::allOff() {
    for (const auto& [name, off] : PIN_MAP) request_.set_value(off, gpiod::line::value::INACTIVE);
}

void LightTree::on(const std::string& name)  { set(name, true);  }
void LightTree::off(const std::string& name) { set(name, false); }

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


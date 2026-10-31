#pragma once
#include <cstdint>
//TODO: change this is gonna be the message exchange between the firmware and gui
namespace protocol {

struct RaceStatus {
    uint8_t state;            // RaceState as a number (Idle=0, Ready=1, ...)
    uint8_t judge_ready;
    double  clock_ms;         // Timer::elapsed_ms(): negative in countdown, frozen after STOP
    uint8_t aligned[2];       // per lane: ALIGN high
    uint8_t beam_broken[2];   // per lane: FORWARD/BACK broken right now
    uint8_t n_splits[2];
    double  race_ms[2];       // final time, or RACE_DNF
    double  start_ms[2];      // reaction time (negative = false start)
    uint8_t false_starts[2];
    int8_t  winner;           // -1 = none
};




constexpr uint16_t kGuiPort  = 5555;   // if they talk over a socket
constexpr int      kMaxLeds  = 8;

// Virtual remote (testing without the RF receiver): the GUI sends ONE byte per button press
// as a UDP packet to the firmware on this port. The byte is a RemoteButton value.
constexpr uint16_t kRemotePort = 5556;

enum class RemoteButton : uint8_t {
    Ready = 1,
    Start = 2,
    Stop  = 3,
    Reset = 4,
    Abort = 5,
};

}  // namespace protocol
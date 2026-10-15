#pragma once
#include <array>
#include <chrono>
#include <cstdint>

// All race times are CLOCK_MONOTONIC nanoseconds, the same clock gpiod uses for
// edge timestamps (SensorEvent.t_ns) and steady_clock uses in state_machine.cpp.
// So a sensor timestamp can be subtracted from t_green directly.

constexpr int NUM_LANES = 2;    // matches Lane { LEFT = 0, RIGHT = 1 } in state_machine.cpp
constexpr int MAX_DOGS  = 10;   // FirstPass + Dog1..Dog9, same as the old CSV

enum class LaneStatus { Waiting, Running, Finished, FalseStart, DNF };

struct LaneTime {
    LaneStatus status    = LaneStatus::Waiting;
    uint64_t   t_finish  = 0;                 // ns, 0 = not finished
    double     foul_ms   = 0.0;               // negative = left early (false start)
    std::array<double, MAX_DOGS> splits_ms{}; // per-dog pass times
    int        n_splits  = 0;
};

class Timer {
public:
    static uint64_t now_ns();                         // monotonic "now" in ns

    void start(uint64_t t_green_ns);                  // green light: race clock = 0
    void split(int lane, uint64_t t_ns);              // a dog crossed (one try_pair() result)
    void end(int lane, uint64_t t_ns);                // lane finished
    void foul(int lane, uint64_t t_ns);               // beam broken before green
    void reset();                                     // wipe for the next race

    bool     running() const { return running_; }
    double   elapsed_ms(uint64_t now) const;          // live clock for the GUI
    double   race_ms(int lane) const;                 // final time for a lane
    uint64_t t_green() const { return t_green_; }
    std::chrono::system_clock::time_point wall_start() const { return wall_start_; }
    const LaneTime& lane(int l) const { return lanes_[l]; }

private:
    bool     running_ = false;
    uint64_t t_green_ = 0;
    std::chrono::system_clock::time_point wall_start_{};  // real date/time for the database
    std::array<LaneTime, NUM_LANES> lanes_{};
};

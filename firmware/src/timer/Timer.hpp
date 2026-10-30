#pragma once
#include "sensors/SensorEvents.hpp"   // NUM_LANES
#include <array>
#include <chrono>
#include <cstdint>

// Race clock, following the UFLI DSS sequence (Lee, 2/05/2025).
//
// How a heat runs:
//   remote START      → light sequence (Blue, Yellow, Yellow, Green), clock counts DOWN to green
//   green             → clock counts UP from 0
//   start dog at #1   → start time vs green (negative = false start, positive = late)
//   each pass at #1   → split = time since green
//   remote STOP       → judge stops the clock; each lane's final time = its last split
//   winner            → lowest final time (picked in state_machine.cpp)
//
// All race times are CLOCK_MONOTONIC nanoseconds, the same clock gpiod uses for
// edge timestamps (LaneSignals::t_rise / t_pass) and steady_clock uses in state_machine.cpp.
// So a sensor timestamp can be subtracted from t_green directly.

constexpr int MAX_DOGS  = 10;   // FirstPass + Dog1..Dog9, same as the old CSV

constexpr uint64_t START_LOCKOUT_NS = 3'000'000'000ull;  // sensors off 3 s after the start dog (spec 3.C.d)
constexpr int      MIN_EXCHANGES    = 4;                 // a lane needs 4+ passes to get a final time (spec 3.E)
                                                         // TODO 6: ask Lee if "4 exchanges" means 4 splits
constexpr double   RACE_DNF         = -1.0;              // race_ms() for a lane with no final time

// A false start does NOT take a lane out of the heat, so there's no FalseStart status:
// it shows up as a negative start_ms instead.
enum class LaneStatus { Waiting, Running, Finished, DNF };

struct LaneTime {
    LaneStatus status    = LaneStatus::Waiting;
    bool       has_start = false;                // start dog's first #1 trigger seen
    uint64_t   t_start   = 0;                    // ns, start dog's first #1 trigger
    double     start_ms  = 0.0;                  // vs green: negative = early/false start, positive = late
    uint64_t   t_last    = 0;                    // ns, last dog through #1
    uint64_t   t_finish  = 0;                    // ns, = t_last once the judge stops the clock, 0 = not finished
    std::array<double, MAX_DOGS> splits_ms{};    // per-dog pass times, elapsed since green
    std::array<bool,   MAX_DOGS> early_pass{};   // incoming dog hit #1 before the returning dog hit #2
    int        n_splits  = 0;
};

class Timer {
public:
    static uint64_t now_ns();                         // monotonic "now" in ns

    void set_expected_green(uint64_t t_ns);           // light sequence began: when green WILL come on
    void start(uint64_t t_green_ns);                  // green light: race clock = 0
    void start_time(int lane, uint64_t t_ns);         // start dog's #1 trigger (early or late)
    void split(int lane, uint64_t t_ns, bool early = false);  // a dog crossed #1
    void stop(uint64_t t_ns);                         // judge's remote button 2: final times for both lanes
    void reset();                                     // "Reset Heat": wipe for the next heat

    bool     running() const { return running_; }
    double   elapsed_ms(uint64_t now) const;          // live clock for the GUI (negative = countdown)
    double   race_ms(int lane) const;                 // final time for a lane, RACE_DNF if none
    uint64_t t_green() const { return t_green_; }
    std::chrono::system_clock::time_point wall_start() const { return wall_start_; }
    const LaneTime& lane(int l) const { return lanes_[l]; }

private:
    bool     running_          = false;
    uint64_t t_expected_green_ = 0;
    uint64_t t_green_          = 0;
    uint64_t t_stop_           = 0;                       // remote button 2 press, freezes elapsed_ms()
    std::chrono::system_clock::time_point wall_start_{};  // real date/time for the database
    std::array<LaneTime, NUM_LANES> lanes_{};
};

// Warmup / break clock (spec 3.F.d), separate from the race Timer.
// remote button 3: each press adds 1 min; 3 s after the last press it starts counting down.
// A press while it's counting stops it. The head table can also start one directly (breaks, lunch).
class Countdown {
public:
    void   press(uint64_t now);                       // remote button 3
    void   start(uint64_t now, uint64_t total_ns);    // head table: start right away
    void   stop();
    bool   counting(uint64_t now) const;
    double remaining_ms(uint64_t now) const;          // 0 when idle or done

private:
    bool     armed_   = false;
    uint64_t total_   = 0;                            // ns on the clock
    uint64_t t_begin_ = 0;                            // when it starts counting down
};

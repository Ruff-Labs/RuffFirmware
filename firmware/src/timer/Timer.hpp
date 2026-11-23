#pragma once
#include "sensors/SensorEvents.hpp"   // NUM_LANES
#include <common/protocol.h>          // RACE_DNF and LaneStatus are shared with the GUI
#include <array>
#include <chrono>
#include <cstdint>

// Race clock, following the UFLI DSS sequence (Lee, 2/05/2025):
//   remote START      light sequence (blue, yellow, yellow, green); the clock counts DOWN to green
//   green             the clock counts UP from 0
//   start dog at #1   start time vs green (negative = false start, positive = late)
//   each pass at #1   split = time since green
//   remote STOP       each lane's final time = its last split; the lowest wins (state_machine.cpp)
//
// All times are CLOCK_MONOTONIC nanoseconds: the clock gpiod stamps sensor edges with, and
// steady_clock in state_machine.cpp. So a sensor timestamp can be compared with t_green directly.

constexpr int      MAX_DOGS         = 10;                    // FirstPass + Dog1..Dog9, as in the CSV
constexpr uint64_t START_LOCKOUT_NS = 3'000'000'000ull;      // passes ignored for 3 s after the start dog (spec 3.C.d)
constexpr int      MIN_EXCHANGES    = 4;                     // passes needed for a final time (spec 3.E)
                                                             // TODO: ask Lee if "4 exchanges" means 4 splits
constexpr double   RACE_DNF         = protocol::kRaceDnf;    // race_ms() for a lane with no final time
static_assert(NUM_LANES == protocol::kNumLanes, "sensor lanes and protocol lanes must match");

using protocol::LaneStatus;   // Waiting, Running, Finished, DNF (a false start is a negative start_ms, not a status)

struct LaneTime {
    LaneStatus status    = LaneStatus::Waiting;
    bool       has_start = false;                // start dog's first #1 trigger seen
    uint64_t   t_start   = 0;                    // ns, that trigger
    double     start_ms  = 0.0;                  // vs green: negative = early / false start, positive = late
    uint64_t   t_last    = 0;                    // ns, last dog through #1
    uint64_t   t_finish  = 0;                    // ns, = t_last once STOP is pressed; 0 = not finished
    std::array<double, MAX_DOGS> splits_ms{};    // per pass, ms since green
    std::array<bool,   MAX_DOGS> early_pass{};   // incoming dog hit #1 before the returning dog hit #2
    int        n_splits  = 0;
};

class Timer {
public:
    static uint64_t now_ns();                    // CLOCK_MONOTONIC "now"

    // The light sequence started: green is due at t_ns (sequence start + every step's duration).
    // Lets a dog leaving before green get a negative start time.
    void set_expected_green(uint64_t t_ns);

    // Green: the clock is 0 at t_green_ns. Start times taken before green are re-measured
    // against the real green, and every lane is running (a false start doesn't stop a lane).
    void start(uint64_t t_green_ns);

    // The start dog's #1 trigger. Only the FIRST one per lane counts, and it isn't a split.
    // Ignored if no sequence is running (nothing to measure against).
    void start_time(int lane, uint64_t t_ns);

    // A dog crossed #1: one split, ms since green. Dropped if the clock isn't running, the lane
    // has MAX_DOGS already, it's at/before green, or within START_LOCKOUT_NS of the start dog.
    // early: the incoming dog hit #1 before the returning dog hit #2.
    // TODO: SensorEvents doesn't report early passes yet, so callers pass false.
    void split(int lane, uint64_t t_ns, bool early = false);

    // The judge's STOP (remote button 2) at t_ns. Lanes with MIN_EXCHANGES+ splits finish at
    // their last split (not the press); the rest are DNF.
    void stop(uint64_t t_ns);

    // "Reset Heat": wipe the clock and lane times. False starts live in the state machine's
    // RaceData, so they survive this (only "Reset Race" clears them).
    void reset();

    bool     running() const { return running_; }

    // Live clock for the GUI: negative while counting down to green, time since green while
    // running, frozen at the STOP press after.
    double   elapsed_ms(uint64_t now) const;

    double   race_ms(int lane) const;            // final time (green -> last split), RACE_DNF if none
    uint64_t t_green() const { return t_green_; }
    std::chrono::system_clock::time_point wall_start() const { return wall_start_; }   // StartTime in the CSV
    const LaneTime& lane(int l) const { return lanes_[l]; }

private:
    bool     running_          = false;
    uint64_t t_expected_green_ = 0;
    uint64_t t_green_          = 0;
    uint64_t t_stop_           = 0;
    std::chrono::system_clock::time_point wall_start_{};
    std::array<LaneTime, NUM_LANES> lanes_{};
};

// Warmup / break clock (spec 3.F.d), separate from the race Timer. Nothing is worked out on a
// tick: every reading is computed from when it started.
// TODO: needs remote button 3 + a GUI button (breaks, lunch), or delete this class.
class Countdown {
public:
    // Remote button 3: each press adds a minute, and the countdown starts 3 s after the last
    // press. A press while it's counting stops it.
    void   press(uint64_t now);
    void   start(uint64_t now, uint64_t total_ns);   // head table: start right away
    void   stop();
    bool   counting(uint64_t now) const;
    double remaining_ms(uint64_t now) const;         // 0 when idle or done

private:
    bool     armed_   = false;
    uint64_t total_   = 0;    // ns on the clock
    uint64_t t_begin_ = 0;    // when it starts counting down
};

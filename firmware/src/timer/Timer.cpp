// Timer.cpp  —  race clock: start (green) → splits → end, plus foul (false start)
//
// Steps:
//   1. now_ns(): one shared monotonic clock
//   2. start():  record green time
//   3. split():  per-dog pass times
//   4. end():    lane finish time
//   5. foul():   false start, negative reaction time
//   6. reset():  clear everything
//   7. readouts: elapsed_ms() for the live GUI, race_ms() for results
//   8. wire into state_machine.cpp (replace RaceData timing fields)
#include "timer/Timer.hpp"
#include <ctime>

// ---------- settings ----------
// TODO(5): add any timing rules here, e.g. a reaction-time window for fouls
// constexpr uint64_t FOUL_WINDOW_NS = ...;

static double ns_to_ms(int64_t ns) { return ns / 1e6; }   // same as ms() in SensorEvents.cpp

// ---------- 1. clock ----------
uint64_t Timer::now_ns() {
    // TODO(1): read CLOCK_MONOTONIC so it matches gpiod event timestamps
    //   timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    //   return uint64_t(ts.tv_sec) * 1'000'000'000ull + ts.tv_nsec;
    return 0;
}

// ---------- 2. start ----------
void Timer::start(uint64_t t_green_ns) {
    // TODO(2): call from Sequence → Running in race_tick(), where m.data.t_green is set now
    //   t_green_    = t_green_ns;
    //   wall_start_ = std::chrono::system_clock::now();   // StartTime column in the CSV
    //   running_    = true;
    //   for each lane not already FalseStart: status = Running
    (void)t_green_ns;
}

// ---------- 3. split ----------
void Timer::split(int lane, uint64_t t_ns) {
    // TODO(3): call once per try_pair() result for that lane (SensorEvents.cpp)
    //   ignore if !running_ or lane isn't Running, or n_splits == MAX_DOGS
    //   decide what a split means:
    //     time since green?       ns_to_ms(t_ns - t_green_)
    //     time since last split?  ns_to_ms(t_ns - previous split)
    //   (the old Python summed splits to get RaceTime, so that one used "since last")
    //   LaneTime& L = lanes_[lane];
    //   L.splits_ms[L.n_splits++] = ...;
    (void)lane; (void)t_ns;
}

// ---------- 4. end ----------
void Timer::end(int lane, uint64_t t_ns) {
    // TODO(4): ignore if this lane is already Finished/FalseStart (prevents double finish)
    //   lanes_[lane].t_finish = t_ns;
    //   lanes_[lane].status   = LaneStatus::Finished;
    //   if no lane is still Running: running_ = false
    // TODO(4): use the gpiod event timestamp (Gate.t_enter), not now_ns(), for sub-ms accuracy
    (void)lane; (void)t_ns;
}

// ---------- 5. foul ----------
void Timer::foul(int lane, uint64_t t_ns) {
    // TODO(5): called from the Sequence state when beam_broken[lane] goes true before green
    //   problem: green hasn't happened yet, so t_green_ is 0
    //   option: pass the *expected* green time (sequence start + sum of SEQUENCE durations)
    //   lanes_[lane].foul_ms = ns_to_ms(int64_t(t_ns) - int64_t(expected_green));   // negative
    //   lanes_[lane].status  = LaneStatus::FalseStart;
    // TODO(5): this is the number isFault() in SensorEvents.cpp wants to display
    (void)lane; (void)t_ns;
}

// ---------- 6. reset ----------
void Timer::reset() {
    running_    = false;
    t_green_    = 0;
    wall_start_ = {};
    lanes_      = {};
}

// ---------- 7. readouts ----------
double Timer::elapsed_ms(uint64_t now) const {
    // TODO(7): live stopwatch for the GUI RaceScreen (replaces its UI-only QML Timer)
    if (!running_) return 0;
    return ns_to_ms(now - t_green_);
    (void)now;
    return 0.0;
}

double Timer::race_ms(int lane) const {
    // TODO(7): final time; only valid when status == Finished
    //   return ns_to_ms(lanes_[lane].t_finish - t_green_);
    // TODO(7): decide what DNF / FalseStart return (e.g. -1 or NaN) and keep it consistent with Database
    (void)lane;
    (void)ns_to_ms;   // TODO: remove once used
    return 0.0;
}

// ---------- 8. wire-up (do this in state_machine.cpp later) ----------
// TODO(8):
//   - replace RaceData::t_green / t_finish / finished / false_start with a Timer
//   - Sequence → Running:    timer.start(Timer::now_ns());
//   - Sequence beam break:   timer.foul(lane, t);
//   - Running lane_done:     timer.end(lane, gate_t_enter);
//   - each try_pair():       timer.split(lane, t);
//   - Reset:                 timer.reset();
//   - pick_winner(): compare timer.lane(l).status / race_ms(l)
//   - steady_clock::now() there is the same clock as now_ns(), so the two can be mixed during the switch

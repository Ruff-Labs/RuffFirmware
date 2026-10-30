// Timer.cpp  —  race clock (see Timer.hpp)
//
// The Timer code itself is done. Left to finish (search "TODO"):
//   [ ] 2. warmup clock: needs a remote button 3 + a GUI button, or delete Countdown   (TODO 2)
//   [ ] 4. SensorEvents: report which pass was early, and confirm which
//          pin is sensor #1 vs #2 (FORWARD / BACK)                                     (TODO 4)
//   [ ] 5. Database.cpp: save start_ms + early_pass, write RACE_DNF as DNF             (TODO 5)
//   [ ] 6. ask: is "4 or more exchanges" = 4 splits per lane?    (Timer.hpp, MIN_EXCHANGES)
//   [ ] 7. test on the BeaglePlay with real sensors
#include "timer/Timer.hpp"
#include "sensors/SensorEvents.hpp"
#include <algorithm>
#include <ctime>

constexpr uint64_t MINUTE_NS   = 60'000'000'000ull;
constexpr uint64_t ARM_WAIT_NS = 3'000'000'000ull;   // countdown starts 3 s after the last press

static double ns_to_ms(int64_t ns) { return ns / 1e6; }

static bool valid_lane(int lane) { return lane >= 0 && lane < NUM_LANES; }

// ---------- clock ----------
uint64_t Timer::now_ns() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1'000'000'000ull + ts.tv_nsec;
}

// ---------- green ----------
// The state machine knows the sequence lengths (SEQUENCE[] in state_machine.cpp),
// so it passes the absolute time green is due: sequence start + sum of the step durations.
void Timer::set_expected_green(uint64_t t_ns) {
    t_expected_green_ = t_ns;
}

void Timer::start(uint64_t t_green_ns) {
    t_green_    = t_green_ns;
    t_stop_     = 0;
    wall_start_ = std::chrono::system_clock::now();   // StartTime column in the CSV
    running_    = true;
    for (LaneTime& L : lanes_) {
        L.status = LaneStatus::Running;   // a false start no longer takes a lane out of the heat
        // a start trigger before green was measured against the expected green; redo it against the real one
        if (L.has_start) L.start_ms = ns_to_ms(int64_t(L.t_start - t_green_));
    }
}

// ---------- start time ----------
// Only the start dog's FIRST #1 trigger counts; it's not a split.
void Timer::start_time(int lane, uint64_t t_ns) {
    if (!valid_lane(lane)) return;
    LaneTime& L = lanes_[lane];
    if (L.has_start) return;

    uint64_t green = t_green_ ? t_green_ : t_expected_green_;
    if (!green) return;   // no sequence running, nothing to measure against

    L.has_start = true;
    L.t_start   = t_ns;
    L.start_ms  = ns_to_ms(int64_t(t_ns - green));   // negative = early / false start
}

// ---------- split ----------
// TODO 4: `early` has to come from SensorEvents: incoming dog hit #1 before the returning
//         dog hit #2 (still gets a time, but red LED 2 s on that lane)
void Timer::split(int lane, uint64_t t_ns, bool early) {
    if (!running_ || !valid_lane(lane)) return;
    LaneTime& L = lanes_[lane];
    if (L.status != LaneStatus::Running || L.n_splits == MAX_DOGS) return;
    if (t_ns <= t_green_) return;   // latched before green
    if (L.has_start && t_ns - L.t_start < START_LOCKOUT_NS) return;   // still the start dog

    L.splits_ms[L.n_splits]  = ns_to_ms(int64_t(t_ns - t_green_));   // since green, not since last split
    L.early_pass[L.n_splits] = early;
    L.n_splits++;
    L.t_last = t_ns;
}

// ---------- stop ----------
// Remote button 2. t_ns is the button press; each lane's final time is its last #1 trigger, not the press.
void Timer::stop(uint64_t t_ns) {
    if (!running_) return;
    running_ = false;
    t_stop_  = t_ns;
    for (LaneTime& L : lanes_) {
        if (L.n_splits >= MIN_EXCHANGES) {
            L.status   = LaneStatus::Finished;
            L.t_finish = L.t_last;
        } else {
            L.status = LaneStatus::DNF;
        }
    }
}

// ---------- reset ----------
// "Reset Heat": clears the clock and lane times only. False start lights/counts live outside
// the Timer (RaceData) and are only cleared by "Reset Race" (spec 3.F).
void Timer::reset() {
    running_          = false;
    t_expected_green_ = 0;
    t_green_          = 0;
    t_stop_           = 0;
    wall_start_       = {};
    lanes_            = {};
}

// ---------- readouts ----------
// Live stopwatch for the GUI RaceScreen:
//   countdown → negative, counting to green
//   running   → time since green
//   stopped   → frozen at the remote button 2 press
double Timer::elapsed_ms(uint64_t now) const {
    if (running_) return ns_to_ms(int64_t(now - t_green_));
    if (t_stop_)  return ns_to_ms(int64_t(t_stop_ - t_green_));
    if (t_expected_green_) return std::min(0.0, ns_to_ms(int64_t(now - t_expected_green_)));
    return 0.0;
}

// TODO 5: Database.cpp has to write RACE_DNF as "DNF", and also save start_ms and early_pass
double Timer::race_ms(int lane) const {
    if (!valid_lane(lane)) return RACE_DNF;
    const LaneTime& L = lanes_[lane];
    if (L.status != LaneStatus::Finished) return RACE_DNF;
    return L.splits_ms[L.n_splits - 1];   // green → last dog through #1
}

// ---------- warmup / break countdown ----------
// TODO 2: needs a remote button (Lee's button 3) and a GUI button for breaks/lunch.
//         Delete this class if the warmup clock isn't wanted.
// No tick needed: everything is worked out from t_begin_ when it's read.
void Countdown::press(uint64_t now) {
    if (counting(now)) { stop(); return; }   // press while counting = stop
    if (!armed_ || remaining_ms(now) <= 0) total_ = 0;   // fresh start (idle, or the last one ran out)
    armed_   = true;
    total_  += MINUTE_NS;
    t_begin_ = now + ARM_WAIT_NS;            // every press pushes the start back 3 s
}

void Countdown::start(uint64_t now, uint64_t total_ns) {
    armed_   = true;
    total_   = total_ns;
    t_begin_ = now;
}

void Countdown::stop() {
    armed_ = false;
    total_ = 0;
}

bool Countdown::counting(uint64_t now) const {
    return armed_ && now >= t_begin_ && remaining_ms(now) > 0;
}

double Countdown::remaining_ms(uint64_t now) const {
    if (!armed_) return 0.0;
    if (now < t_begin_) return ns_to_ms(int64_t(total_));   // still taking presses
    uint64_t done = now - t_begin_;
    return done >= total_ ? 0.0 : ns_to_ms(int64_t(total_ - done));
}

#include "timer/Timer.hpp"
#include <algorithm>
#include <ctime>

constexpr uint64_t MINUTE_NS   = 60'000'000'000ull;
constexpr uint64_t ARM_WAIT_NS = 3'000'000'000ull;   // Countdown starts this long after the last press

static double ns_to_ms(int64_t ns) { return ns / 1e6; }
static bool valid_lane(int lane) { return lane >= 0 && lane < NUM_LANES; }

uint64_t Timer::now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1'000'000'000ull + ts.tv_nsec;
}

// ---------- race clock ----------
void Timer::set_expected_green(uint64_t t_ns) {
    t_expected_green_ = t_ns;
}

void Timer::start(uint64_t t_green_ns) {
    t_green_    = t_green_ns;
    t_stop_     = 0;
    wall_start_ = std::chrono::system_clock::now();
    running_    = true;
    for (LaneTime& L : lanes_) {
        L.status = LaneStatus::Running;
        if (L.has_start) L.start_ms = ns_to_ms(int64_t(L.t_start - t_green_));   // was vs the expected green
    }
}

void Timer::start_time(int lane, uint64_t t_ns) {
    if (!valid_lane(lane)) return;
    LaneTime& L = lanes_[lane];
    if (L.has_start) return;

    const uint64_t green = t_green_ ? t_green_ : t_expected_green_;
    if (!green) return;

    L.has_start = true;
    L.t_start   = t_ns;
    L.start_ms  = ns_to_ms(int64_t(t_ns - green));
}

void Timer::split(int lane, uint64_t t_ns, bool early) {
    if (!running_ || !valid_lane(lane)) return;
    LaneTime& L = lanes_[lane];
    if (L.status != LaneStatus::Running || L.n_splits == MAX_DOGS) return;
    if (t_ns <= t_green_) return;                                     // latched before green
    if (L.has_start && t_ns - L.t_start < START_LOCKOUT_NS) return;   // still the start dog

    L.splits_ms[L.n_splits]  = ns_to_ms(int64_t(t_ns - t_green_));    // since green, not since the last split
    L.early_pass[L.n_splits] = early;
    L.n_splits++;
    L.t_last = t_ns;
}

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

void Timer::reset() {
    running_          = false;
    t_expected_green_ = 0;
    t_green_          = 0;
    t_stop_           = 0;
    wall_start_       = {};
    lanes_            = {};
}

double Timer::elapsed_ms(uint64_t now) const {
    if (running_)          return ns_to_ms(int64_t(now - t_green_));
    if (t_stop_)           return ns_to_ms(int64_t(t_stop_ - t_green_));
    if (t_expected_green_) return std::min(0.0, ns_to_ms(int64_t(now - t_expected_green_)));
    return 0.0;
}

double Timer::race_ms(int lane) const {
    if (!valid_lane(lane)) return RACE_DNF;
    const LaneTime& L = lanes_[lane];
    if (L.status != LaneStatus::Finished) return RACE_DNF;
    return L.splits_ms[L.n_splits - 1];
}

// ---------- warmup / break countdown ----------
void Countdown::press(uint64_t now) {
    if (counting(now)) { stop(); return; }
    if (!armed_ || remaining_ms(now) <= 0) total_ = 0;   // fresh start: idle, or the last one ran out
    armed_   = true;
    total_  += MINUTE_NS;
    t_begin_ = now + ARM_WAIT_NS;                        // every press pushes the start back
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
    const uint64_t done = now - t_begin_;
    return done >= total_ ? 0.0 : ns_to_ms(int64_t(total_ - done));
}

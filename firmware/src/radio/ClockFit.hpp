#pragma once
#include <cstdint>
#include <deque>

// Converts times from one clock to another (sensor node -> base station, base station ->
// CLOCK_MONOTONIC) from pairs of readings taken "at the same moment".
//
// Each pair is only as good as the delay between the two readings, which varies (radio retries,
// USB). So the samples are grouped into 1 s windows of the source clock and only the best one per
// window (lowest err) is kept. A straight line through the last kWindows of those gives the offset
// AND the drift: the Nano Every has no crystal, its clock can run 0.1-1 % fast or slow, which
// would add tens of ms over a heat if only the offset were corrected.
class ClockFit {
public:
    static constexpr int    kWindows   = 30;    // fit over the last ~30 s
    static constexpr double kWindowUs  = 1e6;
    static constexpr int    kMinWindows = 3;    // before this, ready() is false

    // A source time x (us) read together with destination time y (us). err: how far off the pair
    // can be (e.g. the round trip of a ping); the lowest per window wins.
    void add(double x, double y, double err);

    // Enough samples to trust convert().
    bool ready() const { return int(win_.size()) >= kMinWindows; }

    // Source -> destination time. With no samples, returns x unchanged.
    double convert(double x) const;

    // How fast the source clock runs vs the destination, ppm (+6000 = 0.6 % fast)
    double drift_ppm() const { return (1 / (1 + slope_) - 1) * 1e6; }
    int    windows() const { return int(win_.size()); }
    void   clear();

private:
    struct Sample { int64_t window; double x, d, err; };   // d = y - x
    void refit();

    std::deque<Sample> win_;
    double x_ref_ = 0, offset_ = 0, slope_ = 0;   // y - x = offset + slope * (x - x_ref)
};

// Extends a 32-bit microsecond counter (wraps every ~71 min) to 64 bits, given values that come
// roughly in order: anything within ~35 min of the latest value is placed next to it.
class Unwrap32 {
public:
    uint64_t operator()(uint32_t v);
    void reset() { have_ = false; }

private:
    bool     have_ = false;
    uint64_t last_ = 0;
};

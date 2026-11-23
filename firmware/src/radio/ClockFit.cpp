#include "radio/ClockFit.hpp"
#include <cmath>

void ClockFit::add(double x, double y, double err) {
    const int64_t w = int64_t(std::floor(x / kWindowUs));
    const Sample  s{ w, x, y - x, err };

    if (!win_.empty() && w < win_.back().window) {
        // an older window: the source clock went back, i.e. it restarted. The caller should have
        // cleared us; start again rather than fit across two clocks.
        clear();
    }
    if (!win_.empty() && win_.back().window == w) {
        if (err < win_.back().err) win_.back() = s;
    } else {
        win_.push_back(s);
        while (int(win_.size()) > kWindows) win_.pop_front();
    }
    refit();
}

// Least squares through the kept samples: d = offset + slope * (x - x_ref)
void ClockFit::refit() {
    const int n = int(win_.size());
    if (n == 0) return;
    x_ref_ = win_.back().x;
    if (n == 1) {
        offset_ = win_.back().d;
        slope_  = 0;
        return;
    }
    double sx = 0, sd = 0, sxx = 0, sxd = 0;
    for (const Sample& s : win_) {
        const double u = s.x - x_ref_;
        sx += u; sd += s.d; sxx += u * u; sxd += u * s.d;
    }
    const double den = n * sxx - sx * sx;
    slope_  = den > 0 ? (n * sxd - sx * sd) / den : 0.0;
    offset_ = (sd - slope_ * sx) / n;
}

double ClockFit::convert(double x) const {
    if (win_.empty()) return x;
    return x + offset_ + slope_ * (x - x_ref_);
}

void ClockFit::clear() {
    win_.clear();
    x_ref_ = offset_ = slope_ = 0;
}

uint64_t Unwrap32::operator()(uint32_t v) {
    if (!have_) {
        have_ = true;
        last_ = v;
        return last_;
    }
    const int32_t  diff = int32_t(v - uint32_t(last_));
    const uint64_t out  = uint64_t(int64_t(last_) + diff);
    if (diff > 0) last_ = out;
    return out;
}

#include "remote/Remote.hpp"
#include <chrono>
#include <cstdio>

using namespace std::chrono_literals;

// ---------- settings ----------
// Run `gpioinfo` on the board. If the receiver outputs 5 V, level-shift it to 3.3 V first!
// The pins must not collide with LANE_PINS in SensorEvents.cpp.
constexpr const char* REMOTE_CHIP       = "/dev/gpiochip?";
constexpr bool        REMOTE_ACTIVE_LOW = false;   // most RF receivers are active-high (1 = pressed)

constexpr unsigned PIN_READY = 0;   // receiver D0 -> P9_?? -> line ?
constexpr unsigned PIN_START = 0;   // receiver D1 -> P9_?? -> line ?
constexpr unsigned PIN_STOP  = 0;   // receiver D? -> P9_?? -> line ?  (no spare button? share START/RESET)
constexpr unsigned PIN_RESET = 0;   // receiver D2 -> P9_?? -> line ?
constexpr unsigned PIN_ABORT = 0;   // receiver D3 -> P9_?? -> line ?

constexpr auto     REMOTE_DEBOUNCE   = 30ms;          // the kernel ignores flicker shorter than this
constexpr uint64_t REMOTE_LOCKOUT_NS = 250'000'000;   // ignore re-presses within 250 ms (RF chatter)

// ---------- setup ----------
bool Remote::begin() {
    virtual_.begin();

    buttons_ = {{
        { PIN_READY, RemoteCmd::Ready },
        { PIN_START, RemoteCmd::Start },
        { PIN_STOP,  RemoteCmd::Stop  },
        { PIN_RESET, RemoteCmd::Reset },
        { PIN_ABORT, RemoteCmd::Abort },
    }};

    try {
        gpiod::line_settings settings;
        settings.set_direction(gpiod::line::direction::INPUT)
                .set_edge_detection(gpiod::line::edge::BOTH)     // press and release
                .set_event_clock(gpiod::line::clock::MONOTONIC)  // same clock as the sensors and Timer
                .set_bias(REMOTE_ACTIVE_LOW ? gpiod::line::bias::PULL_UP
                                            : gpiod::line::bias::PULL_DOWN)
                .set_debounce_period(REMOTE_DEBOUNCE);

        gpiod::line::offsets offsets;
        for (const auto& b : buttons_) offsets.push_back(b.line);

        req_ = gpiod::chip(REMOTE_CHIP)
            .prepare_request()
            .set_consumer("ruff-remote")
            .add_line_settings(offsets, settings)
            .do_request();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "remote GPIO error: %s\n", ex.what());
        return false;
    }
    return true;
}

// ---------- per tick ----------
RemoteInputs Remote::poll() {
    RemoteInputs out;

    while (req_ && req_->wait_edge_events(0ns)) {
        req_->read_edge_events(buf_);
        for (const auto& ev : buf_) {
            Button* b = find(ev.line_offset());
            if (!b) continue;
            const bool     rose   = ev.type() == gpiod::edge_event::event_type::RISING_EDGE;
            const bool     active = REMOTE_ACTIVE_LOW ? !rose : rose;
            const uint64_t t      = ev.timestamp_ns().ns();
            if (active) on_press(*b, t, out);
            else        on_release(*b, t, out);
        }
    }

    // GUI: one packet = one press, no debounce needed
    RemoteCmd              cmd;
    uint64_t               t;
    VirtualRemote::Payload payload;
    while (virtual_.next(cmd, t, payload)) {
        if      (cmd == RemoteCmd::SetupRace) out.setup = payload.setup;
        else if (cmd == RemoteCmd::SetWifi)   out.wifi  = payload.wifi;
        else                                  apply(cmd, t, out);
    }

    out.judge_ready = ready_latched_;
    return out;
}

void Remote::on_press(Button& b, uint64_t t_ns, RemoteInputs& out) {
    if (b.down) return;
    b.down = true;
    if (t_ns - b.t_last_ns < REMOTE_LOCKOUT_NS) return;
    b.t_last_ns = t_ns;
    apply(b.cmd, t_ns, out);
}

void Remote::apply(RemoteCmd cmd, uint64_t t_ns, RemoteInputs& out) {
    switch (cmd) {
      case RemoteCmd::Ready:
        ready_latched_ = !ready_latched_;
        break;

      case RemoteCmd::Start:
        out.start_pressed = true;
        break;

      case RemoteCmd::Stop:
        out.stop_pressed = true;
        out.t_stop_ns    = t_ns;   // the press time, not the tick time
        break;

      case RemoteCmd::Reset:
        out.judge_ack = true;
        break;

      case RemoteCmd::Abort:
        out.abort = true;
        break;

      case RemoteCmd::ResetRace:
        out.reset_race = true;
        break;

      case RemoteCmd::Restart:
        out.restart = true;
        break;

      case RemoteCmd::SetupRace:   // carry a payload: handled in poll()
      case RemoteCmd::SetWifi:
        break;

      case RemoteCmd::None:
        break;
    }
}

void Remote::on_release(Button& b, uint64_t t_ns, RemoteInputs& out) {
    b.down = false;
    // 3-button remote: if (b.cmd == RemoteCmd::Reset && t_ns - b.t_last_ns >= 2'000'000'000) out.abort = true;
    (void)t_ns; (void)out;
}

void Remote::clear_ready() {
    ready_latched_ = false;
}

Remote::Button* Remote::find(unsigned line) {
    for (auto& b : buttons_)
        if (b.line == line) return &b;
    return nullptr;
}

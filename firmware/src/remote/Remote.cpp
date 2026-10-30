// Remote.cpp  —  judge remote (see Remote.hpp)
//
// Left to do:
//   TODO 1  settings    chip, active level, pin numbers               <- needs the receiver in hand
//   TODO 3  decisions   RESET in Idle? which buttons on a 4-button remote?  <- rules questions
// Until TODO 1 is filled in, the GPIO part of begin() fails and only the GUI's virtual remote works.
#include "remote/Remote.hpp"
#include <chrono>
#include <cstdio>

using namespace std::chrono_literals;

// ============================================================
// TODO 1: settings (run `gpioinfo` on the BeagleBone)
//   - if the receiver outputs 5 V, level-shift to 3.3 V before the BeagleBone pin!
//   - pins must not collide with LANE_PINS in SensorEvents.cpp
// ============================================================
constexpr const char* REMOTE_CHIP       = "/dev/gpiochip?";   // which chip the pins are on
constexpr bool        REMOTE_ACTIVE_LOW = false;              // most RF receivers are active-high (1 = pressed)

constexpr unsigned PIN_READY = 0;   // receiver D0 -> P9_?? -> line ?
constexpr unsigned PIN_START = 0;   // receiver D1 -> P9_?? -> line ?
constexpr unsigned PIN_STOP  = 0;   // receiver D? -> P9_?? -> line ?   stops the race clock (Lee's button 2).
                                    // TODO 3c: if the remote has no spare button, share one with START/RESET
constexpr unsigned PIN_RESET = 0;   // receiver D2 -> P9_?? -> line ?
constexpr unsigned PIN_ABORT = 0;   // receiver D3 -> P9_?? -> line ?   (see TODO 3b)

// tuning, fine as-is
constexpr auto     REMOTE_DEBOUNCE   = 30ms;           // kernel ignores flicker shorter than this
constexpr uint64_t REMOTE_LOCKOUT_NS = 250'000'000;    // 250 ms: ignore re-presses inside this window

// ---------- setup ----------
bool Remote::begin() {
    virtual_.begin();   // GUI buttons work even while the receiver isn't set up (TODO 1)

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
                .set_edge_detection(gpiod::line::edge::BOTH)   // press AND release
                .set_event_clock(gpiod::line::clock::MONOTONIC) // same clock as the sensors, so the STOP press
                                                                 // timestamp can go straight into Timer::stop()
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

// ---------- per-tick poll (non-blocking) ----------
RemoteInputs Remote::poll() {
    RemoteInputs out;   // one-shots start false every tick, so a press lasts exactly one tick

    while (req_ && req_->wait_edge_events(0ns)) {   // 0 = just check, never wait
        req_->read_edge_events(buf_);
        for (const auto& ev : buf_) {
            Button* b = find(ev.line_offset());
            if (!b) continue;
            bool rose   = ev.type() == gpiod::edge_event::event_type::RISING_EDGE;
            bool active = REMOTE_ACTIVE_LOW ? !rose : rose;
            uint64_t t  = ev.timestamp_ns().ns();
            if (active) on_press(*b, t, out);
            else        on_release(*b, t, out);
        }
    }

    // GUI presses: no debounce/lockout needed, one packet = one press
    RemoteCmd cmd;
    uint64_t  t;
    while (virtual_.next(cmd, t)) apply(cmd, t, out);

    out.judge_ready = ready_latched_;   // READY is a level, re-sent every tick
    return out;
}

// ---------- press ----------
void Remote::on_press(Button& b, uint64_t t_ns, RemoteInputs& out) {
    if (b.down) return;                                  // already held: fire once per press
    b.down = true;
    if (t_ns - b.t_last_ns < REMOTE_LOCKOUT_NS) return;  // RF chatter
    b.t_last_ns = t_ns;
    apply(b.cmd, t_ns, out);
}

// ---------- what each button does (real RF buttons and the GUI's virtual remote) ----------
void Remote::apply(RemoteCmd cmd, uint64_t t_ns, RemoteInputs& out) {
    switch (cmd) {
      case RemoteCmd::Ready:
        ready_latched_ = !ready_latched_;   // toggle: 1st press = ready, 2nd press = not ready
        break;

      case RemoteCmd::Start:
        out.start_pressed = true;
        break;

      case RemoteCmd::Stop:
        out.stop_pressed = true;   // only matters in Running
        out.t_stop_ns    = t_ns;   // the press time, not the tick time
        break;

      case RemoteCmd::Reset:
        out.judge_ack = true;   // clears the Finished screen early
        // TODO 3a: should RESET while in Idle/Ready also clear ready_latched_?
        break;

      case RemoteCmd::Abort:
        out.abort = true;   // any race state -> Reset
        break;

      case RemoteCmd::None:
        break;
    }
}

// ---------- release ----------
void Remote::on_release(Button& b, uint64_t t_ns, RemoteInputs& out) {
    b.down = false;   // can fire again

    // TODO 3b: only 3 buttons on the remote? drop PIN_ABORT and make "hold RESET 2 s" = abort:
    //   if (b.cmd == RemoteCmd::Reset && t_ns - b.t_last_ns >= 2'000'000'000) out.abort = true;
    (void)t_ns; (void)out;
}

// ---------- helpers ----------
void Remote::clear_ready() {
    ready_latched_ = false;   // called from RaceState::Reset so the next race isn't pre-armed
}

Remote::Button* Remote::find(unsigned line) {
    for (auto& b : buttons_)
        if (b.line == line) return &b;
    return nullptr;
}

// TODO: bench test once the receiver is wired: loop remote.poll() and print each command.
//       Check exactly one event per press (fast taps, long hold, far range).

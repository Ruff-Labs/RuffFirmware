// Remote.cpp  —  judge remote (RF receiver) → READY / START / RESET / ABORT
//
// WHAT'S LEFT TO FILL IN (top to bottom, in this order):
//   TODO 1  settings      chip, active level, pin numbers         <- needs the receiver in hand
//   TODO 2  on_press()    what each button does                    <- one line per button
//   TODO 3  decisions     RESET in Idle? 3 or 4 buttons?           <- rules questions
//
// Already done: GPIO setup, reading presses without blocking, debounce, hookup in state_machine.cpp.
// Until TODO 1 is filled in, begin() fails and the remote just reports "nothing pressed".
#include "remote/Remote.hpp"
#include <chrono>
#include <cstdio>

using namespace std::chrono_literals;

// ============================================================
// TODO 1: settings (run `gpioinfo` on the BeagleBone)
//   - if the receiver outputs 5 V, level-shift to 3.3 V before the BeagleBone pin!
//   - pins must not collide with LANE_PINS in SensorEvents.cpp
// ============================================================
constexpr const char* REMOTE_CHIP       = "/dev/gpiochip?";   // which chip the pins are on CHECK THIS 
constexpr bool        REMOTE_ACTIVE_LOW = false;              // most RF receivers are active-high (1 = pressed)

constexpr unsigned PIN_READY = 0;   // receiver D0 -> P9_?? -> line ?
constexpr unsigned PIN_START = 0;   // receiver D1 -> P9_?? -> line ?
constexpr unsigned PIN_RESET = 0;   // receiver D2 -> P9_?? -> line ?
constexpr unsigned PIN_ABORT = 0;   // receiver D3 -> P9_?? -> line ?   (see TODO 3b)

// tuning, fine as-is
constexpr auto     REMOTE_DEBOUNCE   = 30ms;           // kernel ignores flicker shorter than this
constexpr uint64_t REMOTE_LOCKOUT_NS = 250'000'000;    // 250 ms: ignore re-presses inside this window

// ---------- setup ----------
bool Remote::begin() {
    buttons_ = {{
        { PIN_READY, RemoteCmd::Ready },
        { PIN_START, RemoteCmd::Start },
        { PIN_RESET, RemoteCmd::Reset },
        { PIN_ABORT, RemoteCmd::Abort },
    }};

    try {
        gpiod::line_settings settings;
        settings.set_direction(gpiod::line::direction::INPUT)
                .set_edge_detection(gpiod::line::edge::BOTH)   // press AND release
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

    out.judge_ready = ready_latched_;   // READY is a level, re-sent every tick
    return out;
}

// ---------- press ----------
void Remote::on_press(Button& b, uint64_t t_ns, RemoteInputs& out) {
    if (b.down) return;                                  // already held: fire once per press
    b.down = true;
    if (t_ns - b.t_last_ns < REMOTE_LOCKOUT_NS) return;  // RF chatter
    b.t_last_ns = t_ns;

    // ============================================================
    // TODO 2: what each button does (one line each)
    //   the state machine reads these from RaceInputs:
    //     ready_latched_     -> judge_ready    (must STAY on, Ready drops to Idle if it goes false)
    //     out.start_pressed  -> start_pressed  (only matters in Ready)
    //     out.judge_ack      -> judge_ack      (clears the Finished screen early)
    //     out.abort          -> abort          (sends any race state to Reset)
    // ============================================================
    switch (b.cmd) {
      case RemoteCmd::Ready:
        // TODO 2: toggle ready_latched_ on/off
        ready_latched_ = false; // not sure if this is right
        break;

      case RemoteCmd::Start:
        out.start_pressed = true;
        break;

      case RemoteCmd::Reset:
        out.judge_ack = true;

        // TODO 3a: should RESET while in Idle/Ready also clear ready_latched_?
        // TODO: figure out if this the case
        break;

      case RemoteCmd::Abort:
        // TODO 2: set out.abort
        out.abort = true;
        break;

    //   default:
    //     break;
    }
    (void)out;   // remove once TODO 2 is filled in
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

// Bench test idea: a tiny main() that loops remote.poll() and prints each command.
// Check exactly one event per press (fast taps, long hold, far range).

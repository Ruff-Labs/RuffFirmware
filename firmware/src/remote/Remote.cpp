// Remote.cpp  —  judge remote (RF receiver) → READY / START / RESET / ABORT
#include "Remote.hpp"
#include <chrono>
#include <cstdio>

using namespace std::chrono_literals;

// ---------- settings ----------
// TODO(1): identify the hardware
//   - receiver model / channels (D0–D3? VT pin?)
//   - momentary vs latching outputs (want momentary)
//   - output voltage: if 5 V, level-shift down to 3.3 V before the BeagleBone pin!
//   - active-high or active-low
constexpr const char* REMOTE_CHIP       = "/dev/gpiochip?";   // TODO(1): find with `gpioinfo`
constexpr bool        REMOTE_ACTIVE_LOW = false;              // TODO(1): most RF receivers are active-high
constexpr auto        REMOTE_DEBOUNCE   = 30ms;               // kernel debounce (step 6)
constexpr uint64_t    REMOTE_LOCKOUT_NS = 250'000'000;        // 250 ms: ignore re-presses inside this window

// TODO(1): fill in real line offsets; make sure none collide with SENSORS in SensorEvents.cpp
//   button | receiver pin | header pin | line
//   A      | D0           | P9_??      | ?
//   B      | D1           | P9_??      | ?
//   C      | D2           | P9_??      | ?
//   D      | D3           | P9_??      | ?

// ---------- setup ----------
bool Remote::begin() {
    // TODO(7): map each physical button to a command
    buttons_ = {{
        { /*line*/ 0, RemoteCmd::Ready },
        { /*line*/ 0, RemoteCmd::Start },
        { /*line*/ 0, RemoteCmd::Reset },
        { /*line*/ 0, RemoteCmd::Abort },   // TODO(11): drop if only 3 buttons (use hold-RESET instead)
    }};

    // TODO(2): configure the GPIO lines
    //   gpiod::line_settings settings;
    //   settings.set_direction(INPUT)
    //           .set_edge_detection(BOTH)              // need both: press AND release (step 5)
    //           .set_bias(PULL_DOWN or PULL_UP)        // match REMOTE_ACTIVE_LOW
    //           .set_debounce_period(REMOTE_DEBOUNCE); // TODO(6): kernel debounce
    //
    //   gpiod::line::offsets offsets;  for each button: offsets.push_back(b.line);
    //
    //   req_ = gpiod::chip(REMOTE_CHIP).prepare_request()
    //            .set_consumer("ruff-remote")
    //            .add_line_settings(offsets, settings)
    //            .do_request();
    //
    //   wrap in try/catch: print the error and return false on failure (same as sensor thread in SensorEvents.cpp)

    return false;   // TODO(2): return true once the request succeeds
}

// ---------- per-tick poll ----------
RemoteInputs Remote::poll() {
    RemoteInputs out;   // TODO(12): one-shots start false every tick, so a press only lasts one tick

    if (!req_) return out;

    // TODO(3): non-blocking check — timeout 0 so the 1 kHz race loop never stalls
    //   if (!req_->wait_edge_events(0ns)) { out.judge_ready = ready_latched_; return out; }
    //   req_->read_edge_events(buf_);
    //
    //   for (const auto& ev : buf_) {
    //       Button* b = find(ev.line_offset());
    //       if (!b) continue;
    //       bool rose   = ev.type() == gpiod::edge_event::event_type::RISING_EDGE;
    //       bool active = REMOTE_ACTIVE_LOW ? !rose : rose;
    //       uint64_t t  = ev.timestamp_ns().ns();
    //       if (active) on_press(*b, t, out);
    //       else        on_release(*b, t, out);
    //   }

    out.judge_ready = ready_latched_;   // READY is a level, re-sent every tick (step 8)
    return out;
}

// ---------- press ----------
void Remote::on_press(Button& b, uint64_t t_ns, RemoteInputs& out) {
    // TODO(4)/(12): ignore if already held — fire on the press edge only, never while held
    //   if (b.down) return;
    //   b.down = true;

    // TODO(6): software lockout — RF receivers can chatter even after kernel debounce
    //   if (t_ns - b.t_last_ns < REMOTE_LOCKOUT_NS) return;
    //   b.t_last_ns = t_ns;

    switch (b.cmd) {
      case RemoteCmd::Ready:
        // TODO(8): toggle the latch — state machine drops Ready→Idle if judge_ready goes false,
        //          so this must stay on, not pulse
        //   ready_latched_ = !ready_latched_;
        break;

      case RemoteCmd::Start:
        // TODO(9): one-shot. race_tick() already ignores it outside RaceState::Ready
        //   out.start_pressed = true;
        break;

      case RemoteCmd::Reset:
        // TODO(10): one-shot → judge_ack (clears Finished early)
        //   out.judge_ack = true;
        // TODO(10): decide if RESET in Idle/Ready should also clear ready_latched_
        break;

      case RemoteCmd::Abort:
        // TODO(11): one-shot. race_tick() sends any non-Idle state to Reset
        //   out.abort = true;
        break;

      default:
        break;
    }
    (void)t_ns; (void)out;   // TODO: remove once implemented
}

// ---------- release ----------
void Remote::on_release(Button& b, uint64_t t_ns, RemoteInputs& out) {
    // TODO(5): mark released so the button can fire again
    //   b.down = false;

    // TODO(11) optional: hold-RESET-for-2s = ABORT if there's no 4th button
    //   if (b.cmd == RemoteCmd::Reset && t_ns - b.t_last_ns >= 2'000'000'000) out.abort = true;
    //   (or detect the hold in poll() while still down, so abort fires without waiting for release)

    (void)b; (void)t_ns; (void)out;   // TODO: remove once implemented
}

// ---------- helpers ----------
void Remote::clear_ready() {
    // TODO(8): call from RaceState::Reset in state_machine.cpp
    ready_latched_ = false;
}

Remote::Button* Remote::find(unsigned line) {
    for (auto& b : buttons_)
        if (b.line == line) return &b;
    return nullptr;
}

// ---------- wire-up (do this in state_machine.cpp later) ----------
// TODO(13):
//   static Remote remote;                         // begin() it once in run_race() or main
//   void poll_inputs(RaceInputs& in) {
//       RemoteInputs r   = remote.poll();
//       in.judge_ready   = r.judge_ready;
//       in.start_pressed = r.start_pressed;
//       in.judge_ack     = r.judge_ack;
//       in.abort         = r.abort;
//       ...sensors...
//   }
//   case RaceState::Reset:  ... remote.clear_ready();
//
// TODO(13): bench test first — a tiny main() that loops remote.poll() and prints each command with
//           a timestamp; check exactly one event per press (fast taps, long hold, far range)

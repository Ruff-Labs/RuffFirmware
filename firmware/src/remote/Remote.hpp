#pragma once
#include "remote/VirtualRemote.hpp"
#include <gpiod.hpp>
#include <array>
#include <cstdint>
#include <optional>

// Judge remote, plus the GUI's head-table commands. Presses come from two places and are
// handled the same way, so the state machine can't tell them apart:
//   - the RF receiver: one GPIO line per button (pins in Remote.cpp), debounced, with a lockout
//   - the GUI (VirtualRemote): the Remote screen's buttons, and head-table commands
//
//   READY      toggles judge_ready (a level: Ready drops back to Idle when it goes off)
//   START      starts the light sequence (only in Ready)
//   STOP       stops the race clock (only in Running)
//   RESET      judge_ack: leaves the Finished screen early
//   ABORT      any race state -> Reset (Reset Heat)
//   GUI only:  ResetRace, SetupRace (lane names + heats), SetWifi, Restart
//
// TODO: fill in the receiver's chip + pins (Remote.cpp). Until then begin() returns false and
//       only the GUI works. Then bench test: exactly one event per press (fast taps, long hold, range).
// TODO: rules questions: should RESET in Idle/Ready also clear READY? Only 3 buttons on the remote?
//       Then drop ABORT and make "hold RESET 2 s" = abort, in on_release().

enum class RemoteCmd { None, Ready, Start, Stop, Reset, Abort, ResetRace, SetupRace, SetWifi, Restart };

// What the remote hands the state machine each tick (poll_inputs() copies it into RaceInputs).
// Everything except judge_ready is a one-shot: true / set for exactly one tick.
struct RemoteInputs {
    bool     judge_ready   = false;   // level: stays true until toggled off or clear_ready()
    bool     start_pressed = false;
    bool     stop_pressed  = false;
    uint64_t t_stop_ns     = 0;       // kernel timestamp of the STOP press, for Timer::stop()
    bool     judge_ack     = false;
    bool     abort         = false;
    bool     reset_race    = false;
    std::optional<protocol::RaceSetup>  setup;
    std::optional<protocol::WifiConfig> wifi;
    bool     restart       = false;
};

class Remote {
public:
    // Starts the GUI link, then requests the receiver's GPIO lines. False if the GPIO part
    // failed: the GUI's buttons still work.
    bool begin();

    // Non-blocking; call once per tick.
    RemoteInputs poll();

    // Drop READY, so the next heat isn't pre-armed. Called by Reset / Reset Race.
    void clear_ready();

private:
    struct Button {
        unsigned  line;
        RemoteCmd cmd;
        bool      down      = false;   // held: fire once per press
        uint64_t  t_last_ns = 0;       // last accepted press, for the lockout
    };

    void on_press(Button& b, uint64_t t_ns, RemoteInputs& out);
    void on_release(Button& b, uint64_t t_ns, RemoteInputs& out);
    void apply(RemoteCmd cmd, uint64_t t_ns, RemoteInputs& out);   // what a command does
    Button* find(unsigned line);

    std::array<Button, 5>              buttons_;   // the physical buttons, Ready..Abort
    std::optional<gpiod::line_request> req_;
    gpiod::edge_event_buffer           buf_{16};
    bool                               ready_latched_ = false;
    VirtualRemote                      virtual_;
};

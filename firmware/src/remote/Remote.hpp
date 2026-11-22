#pragma once
#include "remote/VirtualRemote.hpp"
#include <gpiod.hpp>
#include <array>
#include <cstdint>
#include <optional>

// Judge remote: READY / START / STOP / RESET / ABORT.
// Presses come from two places, handled the same way:
//   - the RF receiver: one GPIO line per button (pins in Remote.cpp), with debounce + lockout
//   - the GUI's virtual remote (VirtualRemote.hpp), for testing without the receiver
// What each button does:
//   READY  toggles judge_ready (it stays on; Ready drops back to Idle if it goes off)
//   START  starts the light sequence (only in Ready)
//   STOP   stops the race clock (only in Running)
//   RESET  judge_ack: clears the Finished screen early
//   ABORT  sends any race state to Reset

// Ready..Abort are the judge remote's buttons; ResetRace and SetupRace only come from the GUI
// (head table), over the same link as the virtual remote.
enum class RemoteCmd { None, Ready, Start, Stop, Reset, Abort, ResetRace, SetupRace };

// What the remote hands to the state machine each tick.
// poll_inputs() copies these into RaceInputs.
struct RemoteInputs {
    bool judge_ready   = false;   // level: stays true until toggled off or cleared
    bool start_pressed = false;   // one-shot: true for a single tick
    bool stop_pressed  = false;   // one-shot: judge stops the race clock (Lee's button 2)
    uint64_t t_stop_ns = 0;       // kernel timestamp of that press, handed to Timer::stop()
    bool judge_ack     = false;   // one-shot
    bool abort         = false;   // one-shot
    bool reset_race    = false;   // one-shot, from the GUI: new race, clears false starts too
    std::optional<protocol::RaceSetup> setup;   // one-shot, from the GUI's Setup screen
};

class Remote {
public:
    bool begin();                 // request GPIO lines (+ start the GUI's virtual remote); false if GPIO failed
    RemoteInputs poll();          // non-blocking, call once per loop tick
    void clear_ready();           // call from RaceState::Reset so the next race isn't pre-armed

private:
    struct Button {
        unsigned  line;           // GPIO line offset
        RemoteCmd cmd;            // what this button means
        bool      down      = false;  // currently held?
        uint64_t  t_last_ns = 0;      // last accepted press (for lockout)
    };

    void on_press(Button& b, uint64_t t_ns, RemoteInputs& out);
    void on_release(Button& b, uint64_t t_ns, RemoteInputs& out);
    void apply(RemoteCmd cmd, uint64_t t_ns, RemoteInputs& out);   // what a button does (real or virtual)
    Button* find(unsigned line);

    std::array<Button, 5>              buttons_;   // one per physical button (Ready..Abort)
    std::optional<gpiod::line_request> req_;
    gpiod::edge_event_buffer           buf_{16};
    bool ready_latched_ = false;
    VirtualRemote virtual_;       // GUI buttons, for testing without the receiver
};

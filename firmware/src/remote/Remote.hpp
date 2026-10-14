#pragma once
#include <gpiod.hpp>
#include <array>
#include <cstdint>
#include <optional>

enum class RemoteCmd { None, Ready, Start, Reset, Abort };

// What the remote hands to the state machine each tick.
// poll_inputs() copies these into RaceInputs.
struct RemoteInputs {
    bool judge_ready   = false;   // level: stays true until toggled off or cleared
    bool start_pressed = false;   // one-shot: true for a single tick
    bool judge_ack     = false;   // one-shot
    bool abort         = false;   // one-shot
};

class Remote {
    
public:
    bool begin();                 // request GPIO lines; false on failure
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
    Button* find(unsigned line);

    std::array<Button, 4>              buttons_;
    std::optional<gpiod::line_request> req_;
    gpiod::edge_event_buffer           buf_{16};
    bool ready_latched_ = false;
};

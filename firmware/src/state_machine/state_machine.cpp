#include "light_tree/LightTree.hpp"
#include "remote/Remote.hpp"
#include "sensors/SensorEvents.hpp"
#include "timer/Timer.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;   // same clock (CLOCK_MONOTONIC) gpiod uses for edge timestamps

enum class RaceState {
    Idle,
    Ready,
    Sequence,
    Running,
    FalseStart,
    Finished,
    Reset
};

enum Lane { LEFT = 0, RIGHT = 1 };

// ---------- tuning ----------
constexpr auto FALSE_START_FLASH = 2s;    // red light on time
constexpr auto RACE_TIMEOUT      = 60s;   // TODO: pick a real max race length
constexpr auto FINISHED_HOLD     = 5s;    // how long the winner light stays if judge never acks

// light names per lane (must match LightTree::PIN_MAP)
const char* ALIGN_LIGHT[2] = { "left_align", "right_align" };
const char* RED_LIGHT[2]   = { "left_red",   "right_red"   };
const char* GREEN_LIGHT[2] = { "left_green", "right_green" };

// countdown steps, same order/timing as LightTree::Start() but non-blocking
struct SeqStep { const char* light; std::chrono::milliseconds dur; };
const SeqStep SEQUENCE[] = {
    { "blue",     100ms },
    { "yellow_1", 100ms },
    { "yellow_2", 100ms },
};
constexpr int SEQ_LEN = sizeof(SEQUENCE) / sizeof(SEQUENCE[0]);

// ---------- inputs: everything the state machine reads ----------
// Filled once per loop by poll_inputs() before race_tick().
// TODO: move into the eventFlags structs once those are filled in
struct RaceInputs {
    bool judge_ready      = false;            // from remote: READY latch
    bool start_pressed    = false;            // from remote: START (one tick)
    bool judge_ack        = false;            // from remote: RESET, clears the finished screen (one tick)
    bool abort            = false;            // from remote: judge cancel / e-stop (one tick)
    bool lane_aligned[2]  = { false, false }; // from sensors: ALIGN high = beam reflecting across the lane, lane ready
    bool beam_broken[2]   = { false, false }; // from sensors: FORWARD or BACK broken (false start before green)
    bool lane_done[2]     = { false, false }; // from sensors: valid pass, FORWARD broke before BACK
    Clock::time_point t_done[2]{};            // kernel timestamp of that pass (when FORWARD broke)
};

// ---------- per-race data, wiped in Reset ----------
struct RaceData {
    Clock::time_point t_green{};
    Clock::time_point t_finish[2]{};
    bool finished[2]    = { false, false };
    bool false_start[2] = { false, false };
    int  winner         = -1;               // -1 = no winner (both faulted / timeout)
};

struct RaceMachine {
    RaceState state = RaceState::Reset;     // start in Reset so lights get cleared on boot
    RaceState prev  = RaceState::Idle;      // != state so the first tick runs entry actions
    Clock::time_point entered{};            // when the current state was entered
    Clock::time_point step_started{};       // when the current sequence light came on
    int seq_step = 0;                       // current sequence state
    RaceData data;                          // current race data 
};

// ---------- helpers ----------
static void show_alignment(LightTree& tree, const RaceInputs& in) {
    tree.set(ALIGN_LIGHT[LEFT],  in.lane_aligned[LEFT]);
    tree.set(ALIGN_LIGHT[RIGHT], in.lane_aligned[RIGHT]);
}

static int pick_winner(const RaceData& d) {
    bool ok[2] = { d.finished[LEFT]  && !d.false_start[LEFT],
                   d.finished[RIGHT] && !d.false_start[RIGHT] };
    if (ok[LEFT] && ok[RIGHT])
        return d.t_finish[LEFT] <= d.t_finish[RIGHT] ? LEFT : RIGHT; // TODO: tie rule
    if (ok[LEFT])  return LEFT;
    if (ok[RIGHT]) return RIGHT;
    // one lane faulted and the other never finished -> other lane still wins
    if (d.false_start[LEFT]  && !d.false_start[RIGHT]) return RIGHT;
    if (d.false_start[RIGHT] && !d.false_start[LEFT])  return LEFT;
    return -1;
}

static void write_race_table(const RaceData& d) {
    // TODO: hand this off to the database code (Database.cpp TODO 8):
    //   1. db.save_race(..., &saved)  -> CSV backup on the board, must succeed first
    //   2. sync.enqueue(saved)        -> same rows to Supabase in the background (never blocks)
    auto ms_since_green = [&](int lane) {
        return std::chrono::duration<double, std::milli>(d.t_finish[lane] - d.t_green).count();
    };
    for (int lane : { LEFT, RIGHT }) {
        if (d.false_start[lane])   std::printf("lane %d: FALSE START\n", lane);
        else if (d.finished[lane]) std::printf("lane %d: %.3f ms\n", lane, ms_since_green(lane));
        else                       std::printf("lane %d: DNF\n", lane);
    }
    std::printf("winner: %d\n", d.winner);
}

// ---------- the state machine ----------
void race_tick(RaceMachine& m, const RaceInputs& in, LightTree& tree, Sensors& sensors, Remote& remote, Timer& timer) {
    const auto now = Clock::now();

    // judge abort works from anywhere
    if (in.abort && m.state != RaceState::Idle && m.state != RaceState::Reset)
        m.state = RaceState::Reset;

    // entry detection: run one-time actions the first tick after a transition
    const bool entering = (m.state != m.prev);
    if (entering) {
        m.prev    = m.state;
        m.entered = now;
    }
    const auto in_state = now - m.entered;

    switch (m.state) {
      case RaceState::Idle:
        show_alignment(tree, in);
        if (in.judge_ready && in.lane_aligned[LEFT] && in.lane_aligned[RIGHT])
            m.state = RaceState::Ready;
        break;

      case RaceState::Ready:
        if (entering) tree.on("center_ready");
        show_alignment(tree, in);

        // lane lost alignment or judge un-readied -> back to Idle
        if (!in.judge_ready || !in.lane_aligned[LEFT] || !in.lane_aligned[RIGHT]) {
            tree.off("center_ready");
            m.state = RaceState::Idle;
        } else if (in.start_pressed) {
            m.state = RaceState::Sequence;
        }
        break;

      case RaceState::Sequence:
        if (entering) {
            tree.offGroup({ "left_align", "right_align", "center_ready" });
            sensors.reset();   // drop any breaks/passes from before the countdown (people walking through, last race)
            m.seq_step     = 0;
            m.step_started = now;
            tree.on(SEQUENCE[0].light);
        }

        // leaving before green = false start
        for (int lane : { LEFT, RIGHT })
            if (in.beam_broken[lane]) m.data.false_start[lane] = true;
        if (m.data.false_start[LEFT] || m.data.false_start[RIGHT]) {
            tree.off(SEQUENCE[m.seq_step].light);
            m.state = RaceState::FalseStart;
            break;
        }

        // advance to the next light when this one's time is up
        if (now - m.step_started >= SEQUENCE[m.seq_step].dur) {
            tree.off(SEQUENCE[m.seq_step].light);
            if (++m.seq_step < SEQ_LEN) {
                tree.on(SEQUENCE[m.seq_step].light);
                m.step_started = now;
            } else {
                tree.onGroup({ GREEN_LIGHT[LEFT], GREEN_LIGHT[RIGHT] });
                m.data.t_green = now;   // timer starts here
                // TODO: hand t_green to the Timer code once it exists
                m.state = RaceState::Running;
            }
        }
        break;

      case RaceState::Running:
        for (int lane : { LEFT, RIGHT }) {
            if (in.lane_done[lane] && !m.data.finished[lane]) {
                m.data.finished[lane] = true;
                m.data.t_finish[lane] = in.t_done[lane];   // edge timestamp, not tick time
            }
        }
        // TODO: record per-dog splits here once try_pair() logic is done
        if ((m.data.finished[LEFT] && m.data.finished[RIGHT]) || in_state >= RACE_TIMEOUT)
            m.state = RaceState::Finished;
        break;

      case RaceState::FalseStart:
        if (entering) {
            for (int lane : { LEFT, RIGHT })
                if (m.data.false_start[lane]) tree.on(RED_LIGHT[lane]);
            // TODO: isFault() display of the negative reaction time
        }
        if (in_state >= FALSE_START_FLASH) {
            tree.off(RED_LIGHT[LEFT]);
            tree.off(RED_LIGHT[RIGHT]);
            // false start = forfeit; other lane wins
            // TODO: confirm rule — if it should be a rerun instead, go to Reset here
            m.state = RaceState::Finished;
        }
        break;

      case RaceState::Finished:
        if (entering) {
            m.data.winner = pick_winner(m.data);
            tree.allOff();
            if (m.data.winner >= 0) tree.on(GREEN_LIGHT[m.data.winner]);
            write_race_table(m.data);
        }
        if (in.judge_ack || in_state >= FINISHED_HOLD)
            m.state = RaceState::Reset;
        break;

      case RaceState::Reset:
        tree.allOff();
        remote.clear_ready();   // judge has to press READY again for the next race
        timer.reset();
        m.data = RaceData{};
        m.seq_step = 0;
        m.state = RaceState::Idle;
        break;

      default:
        m.state = RaceState::Reset;
        break;
    }
}

// ---------- input polling ----------
void poll_inputs(Sensors& sensors, Remote& remote, RaceInputs& in) {
    sensors.poll();   // non-blocking, no sensor thread needed
    for (int lane : { LEFT, RIGHT }) {   // lane 1 = LEFT, lane 2 = RIGHT
        const LaneSignals& s = sensors.lane(lane);
        in.lane_aligned[lane] = s.high[ALIGN];
        // rose[] catches a break that started and cleared within one tick
        in.beam_broken[lane]  = s.high[FORWARD] || s.high[BACK] || s.rose[FORWARD] || s.rose[BACK];
        in.lane_done[lane]    = s.passed;
        in.t_done[lane]       = Clock::time_point(std::chrono::nanoseconds(s.t_pass)); // gpiod uses CLOCK_MONOTONIC too
    }

    RemoteInputs r   = remote.poll();   // non-blocking
    in.judge_ready   = r.judge_ready;
    in.start_pressed = r.start_pressed;
    in.judge_ack     = r.judge_ack;
    in.abort         = r.abort;
}

// ---------- main loop ----------
// TODO: call this from main.cpp once LightTree + Sensors + Remote (remote.begin()) are set up there
// TODO: also pass in the Database + SupabaseSync that main.cpp opens/starts
void run_race(LightTree& tree, Sensors& sensors, Remote& remote) {
    RaceMachine m;
    RaceInputs  in;
    for (;;) {
        poll_inputs(sensors, remote, in);
        race_tick(m, in, tree, sensors, remote);
        std::this_thread::sleep_for(1ms);   // 1 kHz tick; timing accuracy comes from timestamps, not this
    }
}

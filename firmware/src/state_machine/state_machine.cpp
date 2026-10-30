#include "state_machine.hpp"

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;   // same clock (CLOCK_MONOTONIC) gpiod uses for edge timestamps

enum class RaceState {
    Idle,
    Ready,
    Sequence,
    Running,
    FalseStart,
    Finished,
    Reset,       // "Reset Heat": clears the clock/times, KEEPS false start counts + red lights
    ResetRace    // "Reset Race": clears everything, including false starts and red lights
};

enum Lane { LEFT = 0, RIGHT = 1 };

// ---------- tuning ----------
constexpr auto FALSE_START_FLASH = 2s;    // heat stays stopped at least this long before it can restart
constexpr auto FINISHED_HOLD     = 5s;    // how long the winner light stays if judge never acks

// light names per lane (must match LightTree::PIN_MAP)
const char* ALIGN_LIGHT[2] = { "left_align", "right_align" };
const char* RED_LIGHT[2]   = { "left_red",   "right_red"   };
const char* GREEN_LIGHT[2] = { "left_green", "right_green" };

// light sequence before green, one light per step (non-blocking)
struct SeqStep { const char* light; std::chrono::milliseconds dur; };
const SeqStep SEQUENCE[] = {
    { "blue",     1000ms },
    { "yellow_1", 1000ms },
    { "yellow_2", 1000ms },
};
constexpr int SEQ_LEN = sizeof(SEQUENCE) / sizeof(SEQUENCE[0]);

// ---------- inputs: everything the state machine reads ----------
// Filled once per loop by poll_inputs() before race_tick().
// TODO: move into the eventFlags structs once those are filled in
struct RaceInputs {
    bool judge_ready      = false;            // from remote: READY latch
    bool start_pressed    = false;            // from remote: START (one tick)
    bool stop_pressed     = false;            // from remote: STOP (one tick), judge ends the race -> timer.stop()
    uint64_t t_stop       = 0;                // ns, when STOP was pressed (kernel timestamp)
    bool judge_ack        = false;            // from remote: RESET, clears the finished screen (one tick)
    bool abort            = false;            // from remote: judge cancel / e-stop (one tick) -> Reset Heat
    bool reset_race       = false;            // "Reset Race" (one tick) -> ResetRace. TODO: wire to the head table GUI
    bool lane_aligned[2]  = { false, false }; // from sensors: ALIGN high = beam reflecting across the lane, lane ready
    bool beam_broken[2]   = { false, false }; // from sensors: FORWARD or BACK broken (false start before green)
    uint64_t t_break[2]   = { 0, 0 };         // ns, when that break happened (0 = unknown) -> timer.start_time()
    bool lane_pass[2]     = { false, false }; // from sensors: a dog made a valid pass THIS tick (FORWARD before BACK)
    uint64_t t_pass[2]    = { 0, 0 };         // ns, kernel timestamp of that pass (when FORWARD broke) -> timer.split()
};

// ---------- per-race data ----------
// Times live in the Timer. Reset Heat wipes everything here except false_starts; Reset Race wipes all of it.
// False start rules: see state_machine.hpp
struct RaceData {
    int  false_starts[2] = { 0, 0 };        // counted false starts, kept across restarts AND Reset Heat
    bool broke_early[2]  = { false, false }; // broke the beam during the current countdown
    int  reruns_owed[2]  = { 0, 0 };        // infractions that need a rerun (TODO: enforce in step 2)
    int  winner          = -1;              // -1 = no winner (timeout / nobody finished)
};

struct RaceMachine {
    RaceState state = RaceState::ResetRace; // start in ResetRace so lights + counts get cleared on boot
    RaceState prev  = RaceState::Idle;      // != state so the first tick runs entry actions
    Clock::time_point entered{};            // when the current state was entered
    Clock::time_point step_started{};       // when the current sequence light came on
    int seq_step = 0;                       // index into SEQUENCE[]
    RaceData data;                          // current race data
};

// ---------- helpers ----------
static void show_alignment(LightTree& tree, const RaceInputs& in) {
    tree.set(ALIGN_LIGHT[LEFT],  in.lane_aligned[LEFT]);
    tree.set(ALIGN_LIGHT[RIGHT], in.lane_aligned[RIGHT]);
}

// lowest final time wins (a false start doesn't forfeit), DNF lanes are skipped
// TODO(step 2): a lane only counts as finished once its reruns_owed are run; only Regular teams can win
static int pick_winner(const Timer& timer) {
    const double t_left  = timer.race_ms(LEFT);
    const double t_right = timer.race_ms(RIGHT);
    if (t_left == RACE_DNF && t_right == RACE_DNF) return -1;   // nobody finished
    if (t_left  == RACE_DNF) return RIGHT;
    if (t_right == RACE_DNF) return LEFT;
    return t_left <= t_right ? LEFT : RIGHT;                    // TODO: tie rule
}

// steady_clock time -> ns, same scale as the gpiod timestamps
static uint64_t to_ns(Clock::time_point t){
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}

// prints the results for now
static void write_race_table(const RaceData& d, const Timer& timer) {
    // TODO: hand this off to the database code (Database.cpp TODO 8):
    //   1. db.save_race(..., &saved)  -> CSV backup on the board, must succeed first
    //   2. sync.enqueue(saved)        -> same rows to Supabase in the background (never blocks)
    for (int lane : { LEFT, RIGHT }) {
        const LaneTime& L  = timer.lane(lane);
        const double    ms = timer.race_ms(lane);          // final time since green, or RACE_DNF
        if (ms != RACE_DNF) std::printf("lane %d: %.3f ms", lane, ms);
        else                std::printf("lane %d: DNF", lane);
        if (L.has_start) std::printf("  start %+.3f ms", L.start_ms);   // negative = left before green
        std::printf("  splits: %d  (false starts: %d, reruns owed: %d)\n",
                    L.n_splits, d.false_starts[lane], d.reruns_owed[lane]);
    }
    std::printf("winner: %d\n", d.winner);
}

// When a beam break happened: the sensor's kernel timestamp if we have one, otherwise "now".
// (t_break is 0 when the dog was already in the beam at sensors.reset(), so there is no edge time.)
static uint64_t break_time(const RaceInputs& in, int lane, Clock::time_point now) {
    return in.t_break[lane] ? in.t_break[lane] : to_ns(now);
}

// ---------- the state machine ----------
void race_tick(RaceMachine& m, const RaceInputs& in, LightTree& tree, Sensors& sensors, Remote& remote, Timer& timer) {
    const auto now = Clock::now();

    // judge abort works from anywhere (= Reset Heat: false starts are kept)
    if (in.abort && m.state != RaceState::Idle && m.state != RaceState::Reset)
        m.state = RaceState::Reset;
    // Reset Race also works from anywhere and wins over abort
    if (in.reset_race)
        m.state = RaceState::ResetRace;

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
                sensors.reset();
                m.seq_step = 0;
                m.step_started = now;
                m.data.broke_early[LEFT] = m.data.broke_early[RIGHT] = false;
                tree.on(SEQUENCE[0].light); // light seq on

                // fresh clock for this start: after a 1st false start we come back here, and the
                // aborted attempt's start time must not carry over into the restart
                timer.reset();
                // tell the Timer when green WILL come on (now + every step's duration),
                // so a dog leaving early gets a negative start time against it
                Clock::duration seq_tot{};
                for(const SeqStep& s : SEQUENCE) seq_tot += s.dur;
                timer.set_expected_green(to_ns(now + seq_tot));
            }
            // leaving before green = false start, checked every tick
            {
                bool any_first = false;
                for (int lane : { LEFT, RIGHT }) {
                    if (in.beam_broken[lane] && !m.data.broke_early[lane]) {
                        m.data.broke_early[lane] = true;   // latch: the dog stays in the beam for many ticks
                        tree.on(RED_LIGHT[lane]);          // infraction marker
                        // record the start dog's trigger: before green, so start_ms comes out negative
                        timer.start_time(lane, break_time(in, lane, now));
                    }
                    if (m.data.broke_early[lane] && m.data.false_starts[lane] == 0) any_first = true;
                }
                if (any_first) {
                    // a 1st false start stops the heat; a 2nd at the same start is superseded and not counted
                    for (int lane : { LEFT, RIGHT })
                        if (m.data.broke_early[lane] && m.data.false_starts[lane] == 0)
                            m.data.false_starts[lane] = 1;
                    tree.off(SEQUENCE[m.seq_step].light);
                    m.state = RaceState::FalseStart;
                    break;
                }
                // only second false starts so far -> keep counting down, they get committed at green
            }

            // advance to the next light when this one's time is up
            if (now - m.step_started >= SEQUENCE[m.seq_step].dur) {
                tree.off(SEQUENCE[m.seq_step].light);
                if (++m.seq_step < SEQ_LEN) {
                    tree.on(SEQUENCE[m.seq_step].light);
                    m.step_started = now;
                } else {
                    tree.onGroup({ GREEN_LIGHT[LEFT], GREEN_LIGHT[RIGHT] });
                    timer.start(to_ns(now));   // green: race clock = 0, splits are measured from here
                    // 2nd false starts: heat continues, the dog reruns at the end of the line-up
                    for (int lane : { LEFT, RIGHT }) {
                        if (m.data.broke_early[lane]) {
                            m.data.false_starts[lane]++;
                            m.data.reruns_owed[lane]++;
                        }
                    }
                    m.state = RaceState::Running;
                }
            }
            break;

        case RaceState::Running:
            for (int lane : { LEFT, RIGHT }) {
                // start dog's FIRST trigger after green (a late start, positive start_ms).
                // Skipped if it already left early: the Timer only keeps the first one anyway.
                if (in.beam_broken[lane] && !timer.lane(lane).has_start)
                    timer.start_time(lane, break_time(in, lane, now));

                // every valid pass = one split (time since green). The Timer itself drops passes from
                // before green and the start dog's own pass (START_LOCKOUT_NS after its start trigger).
                // TODO 4 (Timer.cpp): `early` = incoming dog hit #1 before the returning dog hit #2,
                //                     SensorEvents doesn't report that yet, so it's always false
                if (in.lane_pass[lane])
                    timer.split(lane, in.t_pass[lane], false);
            }
            // the race only ends when the judge presses STOP.
            // Final time per lane = its last split; under MIN_EXCHANGES passes = DNF.
            if (in.stop_pressed) {
                timer.stop(in.t_stop ? in.t_stop : to_ns(now));
                m.state = RaceState::Finished;
            }
            break;

        case RaceState::FalseStart:
            // heat stopped on a 1st false start. Red markers stay on until Reset/Finished.
            // TODO: isFault() display of the negative reaction time (timer.lane(l).start_ms, still
            //       valid here: the Timer is only reset when the sequence restarts)
            // "stopped in error" needs no extra code: a stop only happens on a 1st false start,
            // so both teams already have no more than one when the heat resumes
            if (in_state >= FALSE_START_FLASH) {
                // restart: back to Idle -> Ready (judge is still READY) -> START.
                // Not Reset: that would clear READY and the red markers.
                m.state = RaceState::Idle;
            }
            break;

        case RaceState::Finished:
            if (entering) {
                m.data.winner = pick_winner(timer);          // lowest final time from the Timer
                tree.allOff();
                if (m.data.winner >= 0) tree.on(GREEN_LIGHT[m.data.winner]);
                write_race_table(m.data, timer);
            }
            if (in.judge_ack || in_state >= FINISHED_HOLD)
                m.state = RaceState::Reset;
            break;

        case RaceState::Reset: {
            // "Reset Heat": next heat of the same race. Clock and per-heat data are wiped,
            // but false start counts carry over and their red markers stay lit.
            tree.allOff();
            remote.clear_ready();   // judge has to press READY again for the next heat
            timer.reset();
            RaceData next;                                              // fresh per-heat data...
            for (int lane : { LEFT, RIGHT }) {
                next.false_starts[lane] = m.data.false_starts[lane];   // ...but keep the false starts
                if (next.false_starts[lane] > 0) tree.on(RED_LIGHT[lane]);   // and re-light their markers
            }
            m.data = next;
            m.seq_step = 0;
            m.state = RaceState::Idle;
            break;
        }

        case RaceState::ResetRace:
            // "Reset Race": new race, everything goes, including false starts and red lights
            tree.allOff();
            remote.clear_ready();
            timer.reset();
            m.data = RaceData{};
            m.seq_step = 0;
            m.state = RaceState::Idle;
            break;

        default:
            m.state = RaceState::ResetRace;
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
        // time of that break: FORWARD's rise if FORWARD is the one broken, else BACK's (0 = no edge seen)
        in.t_break[lane]      = (s.high[FORWARD] || s.rose[FORWARD]) ? s.t_rise[FORWARD] : s.t_rise[BACK];
        in.lane_pass[lane]    = s.passed;   // one-shot: true only on the tick the pass happened
        in.t_pass[lane]       = s.t_pass;   // ns, gpiod uses CLOCK_MONOTONIC like Timer::now_ns()
    }

    RemoteInputs r   = remote.poll();   // non-blocking
    in.judge_ready   = r.judge_ready;
    in.start_pressed = r.start_pressed;
    in.stop_pressed  = r.stop_pressed;
    in.t_stop        = r.t_stop_ns;
    in.judge_ack     = r.judge_ack;
    in.abort         = r.abort;
}

// ---------- main loop ----------
// TODO: call from main.cpp (still Hello World)
// TODO: also pass in the Database + SupabaseSync that main.cpp opens/starts
void run_race(LightTree& tree, Sensors& sensors, Remote& remote) {
    RaceMachine m;
    RaceInputs  in;
    Timer       timer;
    for (;;) {
        poll_inputs(sensors, remote, in);
        race_tick(m, in, tree, sensors, remote, timer);
        std::this_thread::sleep_for(1ms);   // 1 kHz tick; timing accuracy comes from timestamps, not this
    }
}

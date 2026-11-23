#include "state_machine.hpp"
#include "gui_link/StatusSender.hpp"
#include <algorithm>
#include <cstring>

using namespace std::chrono_literals;

// ---------- tuning ----------
constexpr auto FALSE_START_FLASH = 2s;    // the heat stays stopped this long before it can restart
constexpr auto FINISHED_HOLD = 5s;        // the winner light stays this long if the judge never acks
constexpr auto STATUS_EVERY = 50ms;

// light names per lane (LightTree::PIN_MAP)
const char* ALIGN_LIGHT[2] = { "left_align", "right_align" };
const char* RED_LIGHT[2] = { "left_red", "right_red" };
const char* GREEN_LIGHT[2] = { "left_green", "right_green" };

// the lights before green, one per step
struct SeqStep { const char* light; std::chrono::milliseconds dur; };
const SeqStep SEQUENCE[] = {
    { "blue",     1000ms },
    { "yellow_1", 1000ms },
    { "yellow_2", 1000ms },
};
constexpr int SEQ_LEN = sizeof(SEQUENCE) / sizeof(SEQUENCE[0]);

// ---------- helpers ----------
static void show_alignment(LightTree& tree, const RaceInputs& in) {
    for (int lane : { LEFT, RIGHT })
        tree.set(ALIGN_LIGHT[lane], in.lane_in_use[lane] && in.lane_aligned[lane]);
}

// every lane in use is aligned; with no lanes in use, never
static bool lanes_aligned(const RaceInputs& in) {
    bool any = false;
    for (int lane : { LEFT, RIGHT }) {
        if (!in.lane_in_use[lane]) continue;
        if (!in.lane_aligned[lane]) return false;
        any = true;
    }
    return any;
}

// lowest final time wins (a false start doesn't forfeit); DNF lanes can't
static int pick_winner(const Timer& timer) {
    const double t_left  = timer.race_ms(LEFT);
    const double t_right = timer.race_ms(RIGHT);
    if (t_left == RACE_DNF && t_right == RACE_DNF) return -1;
    if (t_left == RACE_DNF) return RIGHT;
    if (t_right == RACE_DNF) return LEFT;
    return t_left <= t_right ? LEFT : RIGHT;   // TODO: tie rule
}

static uint64_t to_ns(Clock::time_point t){
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}

constexpr int HEAT_NUMBER = 1;   // TODO: see state_machine.hpp

// results to the log, then the CSV; only a race that's safely on disk gets uploaded
static void write_race_table(const RaceData& d, const Timer& timer, Database& db, SupabaseSync& sync) {
    for (int lane : { LEFT, RIGHT }) {
        const LaneTime& L  = timer.lane(lane);
        const double    ms = timer.race_ms(lane);
        if (ms != RACE_DNF) std::printf("lane %d: %.3f ms", lane, ms);
        else std::printf("lane %d: DNF", lane);
        if (L.has_start) std::printf("  start %+.3f ms", L.start_ms);
        std::printf("  splits: %d  (false starts: %d, reruns owed: %d)\n", L.n_splits, d.false_starts[lane], d.reruns_owed[lane]);
    }
    std::printf("winner: %d\n", d.winner);

    RaceRecord rec;
    rec.heat = HEAT_NUMBER;
    rec.race = db.next_race_number();
    rec.timer = &timer;
    rec.winner = d.winner;
    for (int lane : { LEFT, RIGHT }) rec.false_starts[lane] = d.false_starts[lane];
    std::vector<RaceRow> saved;
    if (db.save_race(rec, &saved)) {
        std::printf("saved as race %d\n", rec.race);
        sync.enqueue(saved);
    } else {
        std::fprintf(stderr, "race %d NOT saved to the CSV (and not uploaded)\n", rec.race);
    }
}

// the sensor's edge time, or "now" if there's none (the dog was already in the beam at sensors.reset())
static uint64_t break_time(const RaceInputs& in, int lane, Clock::time_point now) {
    return in.t_break[lane] ? in.t_break[lane] : to_ns(now);
}

// ---------- the state machine ----------
void race_tick(RaceMachine& m, const RaceInputs& in, LightTree& tree, Sensors& sensors, Remote& remote, Timer& timer,
               Database& db, SupabaseSync& sync, Clock::time_point now) {
    if (in.setup) {
        const bool mid_heat = m.state == RaceState::Sequence || m.state == RaceState::Running
                           || m.state == RaceState::FalseStart;
        if (mid_heat) {
            std::fprintf(stderr, "setup from the GUI ignored: a heat is in progress\n");
        } else {
            m.setup = *in.setup;
            m.state = RaceState::ResetRace;
            std::printf("new race: \"%s\" vs \"%s\", %u heats\n",
                        m.setup.lane_names[LEFT], m.setup.lane_names[RIGHT], m.setup.heats);
        }
    }

    if (in.abort && m.state != RaceState::Idle && m.state != RaceState::Reset)
        m.state = RaceState::Reset;
    if (in.reset_race)   // wins over abort
        m.state = RaceState::ResetRace;

    // entry actions run on the first tick in a state
    const bool entering = (m.state != m.prev);
    if (entering) {
        m.prev = m.state;
        m.entered = now;
    }
    const auto in_state = now - m.entered;

    switch (m.state) {
        case RaceState::Idle:
            show_alignment(tree, in);
            if (in.judge_ready && lanes_aligned(in))
                m.state = RaceState::Ready;
            break;

        case RaceState::Ready:
            if (entering) tree.on("center_ready");
            show_alignment(tree, in);

            if (!in.judge_ready || !lanes_aligned(in)) {
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
                tree.on(SEQUENCE[0].light);

                timer.reset();   // a restart after a false start mustn't keep the aborted start's time
                Clock::duration seq_tot{};
                for(const SeqStep& s : SEQUENCE) seq_tot += s.dur;
                timer.set_expected_green(to_ns(now + seq_tot));
            }
            // leaving before green = false start
            {
                bool any_first = false;
                for (int lane : { LEFT, RIGHT }) {
                    if (in.beam_broken[lane] && !m.data.broke_early[lane]) {
                        m.data.broke_early[lane] = true;   // latched: the dog stays in the beam for many ticks
                        tree.on(RED_LIGHT[lane]);
                        timer.start_time(lane, break_time(in, lane, now));   // negative start_ms
                    }
                    if (m.data.broke_early[lane] && m.data.false_starts[lane] == 0) any_first = true;
                }
                if (any_first) {
                    for (int lane : { LEFT, RIGHT })
                        if (m.data.broke_early[lane] && m.data.false_starts[lane] == 0)
                            m.data.false_starts[lane] = 1;

                    tree.off(SEQUENCE[m.seq_step].light);
                    m.state = RaceState::FalseStart;
                    break;
                }
                // only 2nd false starts so far: keep counting down, they're counted at green
            }

            if (now - m.step_started >= SEQUENCE[m.seq_step].dur) {

                tree.off(SEQUENCE[m.seq_step].light);
                if (++m.seq_step < SEQ_LEN) {
                    tree.on(SEQUENCE[m.seq_step].light);
                    m.step_started = now;
                } else {

                    tree.onGroup({ GREEN_LIGHT[LEFT], GREEN_LIGHT[RIGHT] });
                    timer.start(to_ns(now));

                    // 2nd false starts: the heat goes on, the dog reruns at the end of the line-up
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
                // a late start dog (positive start_ms); the Timer keeps only the first trigger
                if (in.beam_broken[lane] && !timer.lane(lane).has_start)
                    timer.start_time(lane, break_time(in, lane, now));

                // every valid pass = one split; the Timer drops the start dog's own pass
                if (in.lane_pass[lane])
                    timer.split(lane, in.t_pass[lane], false);   // TODO: early passes (Timer.hpp)
            }
            if (in.stop_pressed) {
                timer.stop(in.t_stop ? in.t_stop : to_ns(now));
                m.state = RaceState::Finished;
            }
            break;

        case RaceState::FalseStart:
            // Idle, not Reset: READY and the red markers stay, so the judge just presses START again
            if (in_state >= FALSE_START_FLASH)
                m.state = RaceState::Idle;
            break;

        case RaceState::Finished:
            if (entering) {
                m.data.winner = pick_winner(timer);
                tree.allOff();
                if (m.data.winner >= 0) tree.on(GREEN_LIGHT[m.data.winner]);
                write_race_table(m.data, timer, db, sync);
            }
            if (in.judge_ack || in_state >= FINISHED_HOLD)
                m.state = RaceState::Reset;
            break;

        case RaceState::Reset: {
            tree.allOff();
            remote.clear_ready();
            timer.reset();
            RaceData next;
            for (int lane : { LEFT, RIGHT }) {
                next.false_starts[lane] = m.data.false_starts[lane];
                if (next.false_starts[lane] > 0) tree.on(RED_LIGHT[lane]);
            }
            m.data = next;
            m.seq_step = 0;
            m.state = RaceState::Idle;
            break;
        }

        case RaceState::ResetRace:
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
    sensors.poll();
    for (int lane : { LEFT, RIGHT }) {
        const LaneSignals& s = sensors.lane(lane);
        in.lane_in_use[lane]  = sensors.enabled(lane);
        in.lane_aligned[lane] = s.high[ALIGN];
        in.forward_high[lane] = s.high[FORWARD];
        in.back_high[lane]    = s.high[BACK];
        // rose[] catches a break that started and ended within one tick
        in.beam_broken[lane]  = s.high[FORWARD] || s.high[BACK] || s.rose[FORWARD] || s.rose[BACK];
        in.t_break[lane]      = (s.high[FORWARD] || s.rose[FORWARD]) ? s.t_rise[FORWARD] : s.t_rise[BACK];
        in.lane_pass[lane]    = s.passed;
        in.t_pass[lane]       = s.t_pass;
    }

    RemoteInputs r   = remote.poll();
    in.judge_ready   = r.judge_ready;
    in.start_pressed = r.start_pressed;
    in.stop_pressed  = r.stop_pressed;
    in.t_stop        = r.t_stop_ns;
    in.judge_ack     = r.judge_ack;
    in.abort         = r.abort;
    in.reset_race    = r.reset_race;
    in.setup         = r.setup;
    in.wifi          = r.wifi;
    in.restart       = r.restart;
}

// ---------- GUI status ----------
#ifndef RUFF_VERSION
#define RUFF_VERSION "dev"   // set by the top-level CMakeLists.txt
#endif

template <size_t N>
static void copy_str(char (&dst)[N], const std::string& src) {
    std::memset(dst, 0, N);
    std::memcpy(dst, src.data(), std::min(src.size(), N - 1));
}

protocol::RaceStatus make_status(const RaceMachine& m, const RaceInputs& in, const Timer& timer,
                                 const SensorHealth& health, const SystemControl::Status& sys,
                                 Clock::time_point now) {
    protocol::RaceStatus s;
    s.state       = uint8_t(m.state);
    s.judge_ready = in.judge_ready;
    s.clock_ms    = timer.elapsed_ms(Timer::now_ns());
    s.winner      = int8_t(m.data.winner);

    for (int lane : { LEFT, RIGHT }) {
        const LaneTime& L    = timer.lane(lane);
        s.lane_in_use[lane]  = in.lane_in_use[lane];
        s.aligned[lane]      = in.lane_aligned[lane];
        s.forward[lane]      = in.forward_high[lane];
        s.back[lane]         = in.back_high[lane];
        s.sensor_fault[lane] = uint8_t(in.lane_in_use[lane] ? health.fault(lane, now) : protocol::SensorFault::None);
        s.lane_status[lane]  = uint8_t(L.status);
        s.n_splits[lane]     = uint8_t(L.n_splits);
        s.race_ms[lane]      = timer.race_ms(lane);
        s.has_start[lane]    = L.has_start;
        s.start_ms[lane]     = L.start_ms;
        s.false_starts[lane] = uint8_t(std::min(m.data.false_starts[lane], 255));
    }
    std::memcpy(&s.setup, &m.setup, sizeof s.setup);

    copy_str(s.firmware_version, RUFF_VERSION);
    s.net_op = uint8_t(sys.op);
    copy_str(s.net_message, sys.message);
    copy_str(s.wifi_ssid, sys.ssid);
    return s;
}

// ---------- main loop ----------
void run_race(LightTree& tree, Sensors& sensors, Remote& remote,
              Database& db, SupabaseSync& sync, const std::atomic<bool>& stop) {
    RaceMachine   m;
    RaceInputs    in;
    Timer         timer;
    SensorHealth  health;
    SystemControl sys;
    StatusSender  gui;
    gui.begin();

    RaceState         sent_state = m.state;
    Clock::time_point sent_at{};

    while (!stop) {
        poll_inputs(sensors, remote, in);
        const auto now = Clock::now();
        race_tick(m, in, tree, sensors, remote, timer, db, sync, now);

        for (int lane : { LEFT, RIGHT })
            health.update(lane, in.lane_aligned[lane], in.forward_high[lane], in.back_high[lane], now);
        if (in.wifi)    sys.set_wifi(*in.wifi);
        if (in.restart) sys.restart_software();

        if (m.state != sent_state || now - sent_at >= STATUS_EVERY) {
            gui.send(make_status(m, in, timer, health, sys.status(), now));
            sent_state = m.state;
            sent_at    = now;
        }
        std::this_thread::sleep_for(1ms);
    }
}

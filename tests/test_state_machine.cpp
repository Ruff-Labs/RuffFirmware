// Race state machine (firmware/src/state_machine) on fake hardware with a simulated clock:
// each Rig::step() is one 1 ms tick, so a whole heat runs in a few milliseconds of real time.
#include "test.hpp"
#include "fakes/fake_hardware.hpp"
#include "state_machine/state_machine.hpp"
#include "temp_dir.hpp"
#include <cstring>

using namespace std::chrono_literals;

struct Rig {
    TempDir      dir;
    LightTree    tree;
    Sensors      sensors;
    Remote       remote;
    Timer        timer;
    Database     db;
    SupabaseSync sync;                      // never started: uploads off, like an empty firmware.env
    RaceMachine  m;
    RaceInputs   in;
    Clock::time_point now{ std::chrono::seconds(1000) };

    Rig() {
        fake_reset();
        db.open(dir.file("races.csv"));
        step();                             // boot: ResetRace -> Idle
    }

    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            poll_inputs(sensors, remote, in);
            race_tick(m, in, tree, sensors, remote, timer, db, sync, now);
            now += 1ms;
        }
    }
    uint64_t ns() const { return uint64_t(std::chrono::nanoseconds(now.time_since_epoch()).count()); }
    bool until(RaceState s, int max_ms) {
        for (int i = 0; i < max_ms && m.state != s; ++i) step();
        return m.state == s;
    }
    void advance_to_ms_after_green(int ms) {      // so a pass's split is exactly `ms`
        const uint64_t target = timer.t_green() + uint64_t(ms) * 1'000'000ull;
        while (ns() < target) step();
    }

    // ----- what the judge / dogs do -----
    void align(bool left, bool right) { fake().lanes[LEFT].high[ALIGN] = left; fake().lanes[RIGHT].high[ALIGN] = right; }
    void ready()      { align(true, true); fake().ready_latched = true; CHECK(until(RaceState::Ready, 5)); step(); }
    void start()      { fake().remote.start_pressed = true; step(); CHECK(m.state == RaceState::Sequence); }
    void go_green()   { CHECK(until(RaceState::Running, 3100)); }
    void stop()       { fake().remote.stop_pressed = true; fake().remote.t_stop_ns = ns(); step(); step(); }
    void judge_ack()  { fake().remote.judge_ack = true; step(); }
    void abort_heat() { fake().remote.abort = true; step(); }
    void reset_race() { fake().remote.reset_race = true; step(); }

    // a dog crossing the start line in the right direction (FORWARD then BACK), right now
    void dog(int lane) {
        LaneSignals& s = fake().lanes[lane];
        s.rose[FORWARD]   = true;
        s.t_rise[FORWARD] = ns();
        s.passed          = true;
        s.t_pass          = ns();
        step();
    }
    // a dog sitting in the beam (stays broken until clear())
    void break_beam(int lane) {
        LaneSignals& s = fake().lanes[lane];
        s.high[FORWARD] = true; s.rose[FORWARD] = true; s.t_rise[FORWARD] = ns();
        step();
    }
    void clear_beam(int lane) { fake().lanes[lane].high[FORWARD] = false; step(); }

    // a clean heat: start dogs 200 ms after green, then one dog per lane every few seconds
    void run_heat(std::initializer_list<int> left_ms, std::initializer_list<int> right_ms) {
        ready(); start(); go_green();
        advance_to_ms_after_green(200);
        dog(LEFT); dog(RIGHT);                    // start dogs (inside the 3 s lockout: not splits)
        auto l = left_ms.begin(), r = right_ms.begin();
        while (l != left_ms.end() || r != right_ms.end()) {   // in time order
            const bool left_next = r == right_ms.end() || (l != left_ms.end() && *l <= *r);
            if (left_next) { advance_to_ms_after_green(*l++); dog(LEFT); }
            else           { advance_to_ms_after_green(*r++); dog(RIGHT); }
        }
        advance_to_ms_after_green(30'000);
        stop();
    }
};

static protocol::RaceSetup setup(const char* left, const char* right, int heats) {
    protocol::RaceSetup s;
    s.heats = uint8_t(heats);
    std::strncpy(s.lane_names[0], left, sizeof s.lane_names[0] - 1);
    std::strncpy(s.lane_names[1], right, sizeof s.lane_names[1] - 1);
    return s;
}

// ---------- getting to the start ----------
TEST(sm_boots_to_idle_with_lights_off) {
    Rig r;
    CHECK(r.m.state == RaceState::Idle);
    CHECK(!light("center_ready") && !light("left_red") && !light("left_green"));
}

TEST(sm_ready_needs_both_lanes_aligned_and_judge_ready) {
    Rig r;
    r.align(true, false); fake().ready_latched = true; r.step(5);
    CHECK(r.m.state == RaceState::Idle);       // lane 2 not aligned
    CHECK(light("left_align") && !light("right_align"));
    r.align(true, true); fake().ready_latched = false; r.step(5);
    CHECK(r.m.state == RaceState::Idle);       // judge not ready
    fake().ready_latched = true; r.step(2);
    CHECK(r.m.state == RaceState::Ready);
    CHECK(light("center_ready"));
}

TEST(sm_ready_drops_to_idle_when_a_lane_loses_alignment) {
    Rig r;
    r.ready();
    r.align(true, false); r.step();
    CHECK(r.m.state == RaceState::Idle);
    CHECK(!light("center_ready"));
}

TEST(sm_start_is_ignored_unless_ready) {
    Rig r;
    fake().remote.start_pressed = true; r.step(2);
    CHECK(r.m.state == RaceState::Idle);
}

TEST(sm_light_sequence_one_second_each_then_green) {
    Rig r;
    r.ready(); r.start();
    r.step();                                   // Sequence entry
    CHECK(light("blue") && !light("center_ready") && !light("left_align"));
    r.step(1000);
    CHECK(!light("blue") && light("yellow_1"));
    r.step(1000);
    CHECK(!light("yellow_1") && light("yellow_2"));
    r.step(1000);
    CHECK(r.m.state == RaceState::Running);
    CHECK(!light("yellow_2") && light("left_green") && light("right_green"));
    CHECK(r.timer.running());
}

// ---------- a heat ----------
TEST(sm_clean_heat_lowest_time_wins_and_is_saved) {
    Rig r;
    r.run_heat({ 4000, 8000, 12000, 16000 }, { 4500, 9000, 13500, 18000 });
    CHECK(r.m.state == RaceState::Finished);
    CHECK_NEAR(r.timer.race_ms(LEFT), 16000, 1e-6);
    CHECK_NEAR(r.timer.race_ms(RIGHT), 18000, 1e-6);
    CHECK_NEAR(r.timer.lane(LEFT).start_ms, 200, 1e-6);     // reaction time: start dog 200 ms after green
    CHECK(r.m.data.winner == LEFT);
    CHECK(light("left_green") && !light("right_green"));    // only the winner's green stays on

    std::vector<RaceRow> rows = r.db.rows_after(0);         // written to the CSV on Finished
    CHECK(rows.size() == 2);
    if (rows.size() == 2) {
        CHECK(rows[0].race == 1 && rows[0].lane == "Left" && rows[0].winner);
        CHECK(rows[0].race_time == "16000.000" && rows[1].race_time == "18000.000");
        CHECK(!rows[1].winner);
    }
}

TEST(sm_finished_goes_to_next_heat_after_5s_and_needs_ready_again) {
    Rig r;
    r.run_heat({ 4000, 8000, 12000, 16000 }, { 4500, 9000, 13500, 18000 });
    r.step(4900);
    CHECK(r.m.state == RaceState::Finished);
    r.step(200);
    CHECK(r.m.state == RaceState::Idle);
    CHECK(!fake().ready_latched);              // Reset Heat clears the judge's READY
    CHECK(!light("left_green"));
    CHECK(r.timer.race_ms(LEFT) == RACE_DNF);  // clock wiped for the next heat
}

TEST(sm_judge_ack_leaves_finished_early) {
    Rig r;
    r.run_heat({ 4000, 8000, 12000, 16000 }, { 4500, 9000, 13500, 18000 });
    r.judge_ack(); r.step();
    CHECK(r.m.state == RaceState::Idle);
}

TEST(sm_lane_under_4_passes_is_dnf_and_the_other_wins) {
    Rig r;
    r.run_heat({ 4000, 8000, 12000 }, { 4500, 9000, 13500, 18000 });
    CHECK(r.timer.race_ms(LEFT) == RACE_DNF);
    CHECK(r.m.data.winner == RIGHT);
    CHECK(r.db.rows_after(0).size() == 2 && r.db.rows_after(0)[0].race_time == "DNF");
}

TEST(sm_nobody_finishes_no_winner_no_green) {
    Rig r;
    r.run_heat({}, {});
    CHECK(r.m.data.winner == -1);
    CHECK(!light("left_green") && !light("right_green"));
}

TEST(sm_second_race_gets_the_next_race_number) {
    Rig r;
    r.run_heat({ 4000, 8000, 12000, 16000 }, { 4500, 9000, 13500, 18000 });
    r.judge_ack(); r.step();
    r.run_heat({ 4000, 8000, 12000, 16000 }, { 4500, 9000, 13500, 18000 });
    std::vector<RaceRow> rows = r.db.rows_after(1);
    CHECK(rows.size() == 2 && rows[0].race == 2);
}

// ---------- false starts ----------
TEST(sm_first_false_start_stops_the_heat_then_restarts) {
    Rig r;
    r.ready(); r.start(); r.step();
    r.step(1500);                              // halfway through the lights
    r.break_beam(LEFT);
    CHECK(r.m.state == RaceState::FalseStart);
    CHECK(light("left_red") && !light("right_red"));
    CHECK(r.m.data.false_starts[LEFT] == 1);
    CHECK(r.timer.lane(LEFT).start_ms < -1400 && r.timer.lane(LEFT).start_ms > -1600);   // ~1.5 s early
    r.clear_beam(LEFT);

    r.step(1900);
    CHECK(r.m.state == RaceState::FalseStart);  // red flash holds 2 s
    r.step(200);
    CHECK(r.until(RaceState::Ready, 5));        // judge still READY + aligned: ready to restart
    CHECK(light("left_red"));                   // marker stays on
    r.step(); r.start(); r.go_green();
    CHECK(r.m.data.false_starts[LEFT] == 1);
    CHECK(!r.timer.lane(LEFT).has_start);       // the aborted start's time didn't carry over
}

TEST(sm_second_false_start_lets_the_heat_run_and_owes_a_rerun) {
    Rig r;
    r.ready(); r.start(); r.step(); r.step(1500);
    r.break_beam(LEFT); r.clear_beam(LEFT);     // 1st: stops the heat
    CHECK(r.until(RaceState::Ready, 2500));
    r.step(); r.start(); r.step(); r.step(1000);
    r.break_beam(LEFT); r.clear_beam(LEFT);     // 2nd by the same team
    CHECK(r.m.state == RaceState::Sequence);    // heat keeps going
    r.go_green();
    CHECK(r.m.data.false_starts[LEFT] == 2);
    CHECK(r.m.data.reruns_owed[LEFT] == 1);
    CHECK(r.timer.lane(LEFT).start_ms < 0);     // still timed, against them
}

TEST(sm_other_teams_first_beats_a_simultaneous_second) {
    Rig r;
    r.ready(); r.start(); r.step(); r.step(1500);
    r.break_beam(LEFT); r.clear_beam(LEFT);     // left: 1st false start
    CHECK(r.until(RaceState::Ready, 2500));
    r.step(); r.start(); r.step(); r.step(500);
    LaneSignals& L = fake().lanes[LEFT];        // both lanes break in the same tick
    LaneSignals& R = fake().lanes[RIGHT];
    L.high[FORWARD] = R.high[FORWARD] = true;
    L.rose[FORWARD] = R.rose[FORWARD] = true;
    r.step();
    CHECK(r.m.state == RaceState::FalseStart);  // right's 1st stops the heat...
    CHECK(r.m.data.false_starts[RIGHT] == 1);
    CHECK(r.m.data.false_starts[LEFT] == 1);    // ...and left's 2nd isn't counted
    CHECK(light("left_red") && light("right_red"));
}

// ---------- resets ----------
TEST(sm_abort_is_reset_heat_and_keeps_false_starts) {
    Rig r;
    r.ready(); r.start(); r.step(); r.step(1500);
    r.break_beam(LEFT); r.clear_beam(LEFT);
    CHECK(r.until(RaceState::Ready, 2500));
    r.abort_heat(); r.step();
    CHECK(r.m.state == RaceState::Idle);
    CHECK(r.m.data.false_starts[LEFT] == 1);
    CHECK(light("left_red"));                   // marker re-lit after the reset
    CHECK(!fake().ready_latched);
}

TEST(sm_abort_mid_heat_stops_the_clock) {
    Rig r;
    r.ready(); r.start(); r.go_green(); r.step(500);
    r.abort_heat(); r.step();
    CHECK(r.m.state == RaceState::Idle);
    CHECK(!r.timer.running());
    CHECK(r.db.rows_after(0).empty());          // an aborted heat isn't saved
}

TEST(sm_abort_in_idle_does_nothing) {
    Rig r;
    r.abort_heat();
    CHECK(r.m.state == RaceState::Idle);
}

TEST(sm_reset_race_clears_false_starts_from_anywhere) {
    Rig r;
    r.ready(); r.start(); r.step(); r.step(1500);
    r.break_beam(LEFT); r.clear_beam(LEFT);
    CHECK(r.until(RaceState::Ready, 2500));
    r.step(); r.start(); r.go_green();
    r.reset_race(); r.step();                   // mid-heat
    CHECK(r.m.state == RaceState::Idle);
    CHECK(r.m.data.false_starts[LEFT] == 0);
    CHECK(!light("left_red") && !light("left_green"));
    CHECK(!r.timer.running());
}

// ---------- setup from the GUI ----------
TEST(sm_setup_starts_a_new_race_with_the_names) {
    Rig r;
    r.ready(); r.start(); r.step(); r.step(1500);
    r.break_beam(LEFT); r.clear_beam(LEFT);     // leave a false start behind
    CHECK(r.until(RaceState::Ready, 2500));
    fake().remote.setup = setup("Fast Dogs", "Zoomies", 3);
    r.step(); r.step();
    CHECK(r.m.state == RaceState::Idle);
    CHECK(std::strcmp(r.m.setup.lane_names[LEFT], "Fast Dogs") == 0);
    CHECK(r.m.setup.heats == 3);
    CHECK(r.m.data.false_starts[LEFT] == 0);    // new race: false starts cleared
}

TEST(sm_setup_is_ignored_mid_heat) {
    Rig r;
    r.ready(); r.start(); r.go_green();
    fake().remote.setup = setup("Fast Dogs", "Zoomies", 3);
    r.step();
    CHECK(r.m.state == RaceState::Running);
    CHECK(r.m.setup.lane_names[LEFT][0] == '\0');
}

// ---------- one-lane mode (lane disabled in LANE_PINS) ----------
TEST(sm_one_lane_mode_starts_without_the_disabled_lane) {
    Rig r;
    fake().lane_enabled[RIGHT] = false;
    r.align(true, false); fake().ready_latched = true; r.step(2);
    CHECK(r.m.state == RaceState::Ready);       // lane 2 isn't waited for
    CHECK(light("left_align") && !light("right_align"));
    r.step(); r.start(); r.go_green();
    r.advance_to_ms_after_green(200); r.dog(LEFT);
    for (int ms : { 4000, 8000, 12000, 16000 }) { r.advance_to_ms_after_green(ms); r.dog(LEFT); }
    r.advance_to_ms_after_green(20'000); r.stop();
    CHECK(r.m.data.winner == LEFT);
    CHECK(r.timer.race_ms(RIGHT) == RACE_DNF);
}

TEST(sm_one_lane_mode_still_needs_the_enabled_lane_aligned) {
    Rig r;
    fake().lane_enabled[RIGHT] = false;
    r.align(false, true); fake().ready_latched = true; r.step(5);
    CHECK(r.m.state == RaceState::Idle);
}

TEST(sm_no_lanes_enabled_never_gets_ready) {
    Rig r;
    fake().lane_enabled[LEFT] = fake().lane_enabled[RIGHT] = false;
    r.align(true, true); fake().ready_latched = true; r.step(5);
    CHECK(r.m.state == RaceState::Idle);
}

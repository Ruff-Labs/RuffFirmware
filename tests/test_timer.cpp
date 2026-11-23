// Timer + Countdown (firmware/src/timer). All times are made-up CLOCK_MONOTONIC nanoseconds.
#include "test.hpp"
#include "timer/Timer.hpp"

constexpr uint64_t MS = 1'000'000ull;   // 1 ms in ns
constexpr uint64_t S  = 1000 * MS;
constexpr uint64_t GREEN = 100 * S;     // green light at t = 100 s

// a heat with green at GREEN, lane passes at the given ms after green, then STOP
static Timer heat(std::initializer_list<int> left_ms, std::initializer_list<int> right_ms) {
    Timer t;
    t.set_expected_green(GREEN);
    t.start(GREEN);
    for (int ms : left_ms)  t.split(0, GREEN + ms * MS);
    for (int ms : right_ms) t.split(1, GREEN + ms * MS);
    t.stop(GREEN + 60 * S);
    return t;
}

TEST(timer_final_time_is_last_split) {
    Timer t = heat({ 4000, 8000, 12000, 16500 }, { 4100, 8200, 12300, 16400, 20000 });
    CHECK_NEAR(t.race_ms(0), 16500, 1e-9);
    CHECK_NEAR(t.race_ms(1), 20000, 1e-9);
    CHECK(t.lane(0).status == LaneStatus::Finished);
    CHECK(t.lane(0).n_splits == 4);
}

TEST(timer_fewer_than_min_exchanges_is_dnf) {
    Timer t = heat({ 4000, 8000, 12000 }, {});
    CHECK(t.race_ms(0) == RACE_DNF);
    CHECK(t.race_ms(1) == RACE_DNF);
    CHECK(t.lane(0).status == LaneStatus::DNF);
}

TEST(timer_split_before_green_or_while_stopped_is_ignored) {
    Timer t;
    t.split(0, GREEN + 1 * S);              // not running yet
    t.set_expected_green(GREEN);
    t.start(GREEN);
    t.split(0, GREEN - 5 * MS);             // latched before green
    t.split(0, GREEN);                      // exactly at green: not after it
    CHECK(t.lane(0).n_splits == 0);
    t.stop(GREEN + 10 * S);
    t.split(0, GREEN + 11 * S);             // after STOP
    CHECK(t.lane(0).n_splits == 0);
}

TEST(timer_start_dog_lockout_drops_its_own_pass) {
    Timer t;
    t.set_expected_green(GREEN);
    t.start(GREEN);
    t.start_time(0, GREEN + 200 * MS);              // start dog leaves 200 ms after green
    t.split(0, GREEN + 200 * MS);                   // its own trigger: inside the 3 s lockout
    t.split(0, GREEN + 200 * MS + START_LOCKOUT_NS - 1);
    CHECK(t.lane(0).n_splits == 0);
    t.split(0, GREEN + 200 * MS + START_LOCKOUT_NS); // first dog after the lockout counts
    CHECK(t.lane(0).n_splits == 1);
}

TEST(timer_start_ms_early_is_negative_and_rebased_on_real_green) {
    Timer t;
    t.set_expected_green(GREEN);
    t.start_time(0, GREEN - 150 * MS);      // left 150 ms before the expected green
    CHECK(t.lane(0).has_start);
    CHECK_NEAR(t.lane(0).start_ms, -150, 1e-9);
    t.start(GREEN + 10 * MS);               // green actually came 10 ms late
    CHECK_NEAR(t.lane(0).start_ms, -160, 1e-9);
    t.start_time(0, GREEN + 1 * S);         // only the FIRST trigger counts
    CHECK_NEAR(t.lane(0).start_ms, -160, 1e-9);
}

TEST(timer_start_time_needs_a_sequence) {
    Timer t;
    t.start_time(0, GREEN);                 // nothing to measure against
    CHECK(!t.lane(0).has_start);
}

TEST(timer_early_pass_flag_is_kept_per_split) {
    Timer t;
    t.set_expected_green(GREEN);
    t.start(GREEN);
    t.split(0, GREEN + 4 * S, false);
    t.split(0, GREEN + 8 * S, true);
    CHECK(!t.lane(0).early_pass[0]);
    CHECK(t.lane(0).early_pass[1]);
}

TEST(timer_stops_counting_at_max_dogs) {
    Timer t;
    t.set_expected_green(GREEN);
    t.start(GREEN);
    for (int i = 1; i <= MAX_DOGS + 3; ++i) t.split(0, GREEN + i * S);
    CHECK(t.lane(0).n_splits == MAX_DOGS);
    t.stop(GREEN + 60 * S);
    CHECK_NEAR(t.race_ms(0), MAX_DOGS * 1000.0, 1e-9);
}

TEST(timer_elapsed_counts_down_then_up_then_freezes) {
    Timer t;
    CHECK(t.elapsed_ms(GREEN) == 0.0);                       // idle
    t.set_expected_green(GREEN);
    CHECK_NEAR(t.elapsed_ms(GREEN - 2500 * MS), -2500, 1e-9); // countdown
    t.start(GREEN);
    CHECK_NEAR(t.elapsed_ms(GREEN + 1234 * MS), 1234, 1e-9);  // running
    t.stop(GREEN + 20 * S);
    CHECK_NEAR(t.elapsed_ms(GREEN + 99 * S), 20000, 1e-9);    // frozen at STOP
}

TEST(timer_reset_clears_everything) {
    Timer t = heat({ 4000, 8000, 12000, 16000 }, {});
    t.reset();
    CHECK(!t.running());
    CHECK(t.t_green() == 0);
    CHECK(t.lane(0).n_splits == 0);
    CHECK(t.race_ms(0) == RACE_DNF);
    CHECK(t.elapsed_ms(GREEN) == 0.0);
}

TEST(countdown_presses_add_minutes_and_start_after_3s) {
    Countdown c;
    c.press(0);
    c.press(1 * S);                                  // 2 minutes, start pushed to 1 s + 3 s
    CHECK(!c.counting(3 * S));                       // still taking presses
    CHECK_NEAR(c.remaining_ms(3 * S), 120000, 1e-9);
    CHECK(c.counting(4 * S));
    CHECK_NEAR(c.remaining_ms(4 * S + 30 * S), 90000, 1e-9);
    c.press(10 * S);                                 // press while counting = stop
    CHECK(!c.counting(10 * S));
    CHECK(c.remaining_ms(10 * S) == 0.0);
}

TEST(countdown_runs_out) {
    Countdown c;
    c.start(0, 60 * S);
    CHECK(c.counting(59 * S));
    CHECK(!c.counting(60 * S));
    CHECK(c.remaining_ms(61 * S) == 0.0);
}

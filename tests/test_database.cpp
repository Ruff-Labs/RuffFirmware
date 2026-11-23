// Database (CSV backup) + the Supabase JSON body. Each test gets its own temp folder.
#include "test.hpp"
#include "database/Database.hpp"
#include "database/SupabaseSync.hpp"
#include "temp_dir.hpp"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

constexpr uint64_t MS = 1'000'000ull;
constexpr uint64_t GREEN = 100'000 * MS;

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

// left: start dog 100 ms early, then 5 passes 4 s apart (finishes, the 3rd early);
// right: 2 passes (DNF). Passes are past the 3 s start-dog lockout.
static Timer sample_heat() {
    Timer t;
    t.set_expected_green(GREEN);
    t.start_time(0, GREEN - 100 * MS);   // left left 100 ms early
    t.start(GREEN);
    for (int i = 1; i <= 5; ++i) t.split(0, GREEN + i * 4000 * MS, i == 3);
    for (int i = 1; i <= 2; ++i) t.split(1, GREEN + i * 4100 * MS);
    t.stop(GREEN + 30'000 * MS);
    return t;
}

TEST(db_new_file_gets_header_and_starts_at_race_1) {
    TempDir d;
    Database db;
    CHECK(db.open(d.file("sub/races.csv")));   // creates the folder too
    CHECK(db.next_race_number() == 1);
    const std::string csv = read_file(d.file("sub/races.csv"));
    CHECK(csv.rfind("Heatnumber,Racenumber,Lane,", 0) == 0);
    CHECK(csv.find("RaceTime,ModifiedDate,User,StartMs,EarlyPasses,Winner,FalseStarts\n") != std::string::npos);
}

TEST(db_save_writes_two_rows_and_hands_them_back) {
    TempDir d;
    Database db;
    CHECK(db.open(d.file("races.csv")));
    Timer t = sample_heat();
    RaceRecord rec;
    rec.heat = 2; rec.race = db.next_race_number(); rec.timer = &t;
    rec.winner = 0; rec.false_starts[0] = 1; rec.user = "Lee";
    std::vector<RaceRow> saved;
    CHECK(db.save_race(rec, &saved));
    CHECK(saved.size() == 2);
    CHECK(saved[0].lane == "Left" && saved[1].lane == "Right");
    CHECK(saved[0].race_time == "20000.000");
    CHECK(saved[1].race_time == "DNF");
    CHECK(saved[0].winner && !saved[1].winner);
    CHECK(saved[0].false_starts == 1);
    CHECK(saved[0].start_ms && *saved[0].start_ms == -100.0);
    CHECK(!saved[1].start_ms);
    CHECK(db.next_race_number() == 2);

    const std::string csv = read_file(d.file("races.csv"));
    CHECK(csv.find(",20000.000,") != std::string::npos);
    CHECK(csv.find(",-100.000,Dog2,1,1\n") != std::string::npos);   // StartMs, EarlyPasses, Winner, FalseStarts
    CHECK(csv.find(",DNF,") != std::string::npos);
}

TEST(db_rows_read_back_match_what_was_saved) {
    TempDir d;
    Database db;
    CHECK(db.open(d.file("races.csv")));
    Timer t = sample_heat();
    RaceRecord rec;
    rec.race = 1; rec.timer = &t; rec.winner = 0; rec.user = "Lee, \"Judge\"";   // needs CSV quoting
    std::vector<RaceRow> saved;
    CHECK(db.save_race(rec, &saved));

    std::vector<RaceRow> back = db.rows_after(0);
    CHECK(back.size() == 2);
    if (back.size() != 2) return;
    CHECK(back[0].user == "Lee, \"Judge\"");
    CHECK(back[0].splits_ms.size() == 5 && back[1].splits_ms.size() == 2);
    CHECK(back[0].early_pass == saved[0].early_pass);
    CHECK(back[0].race_time == saved[0].race_time);
    CHECK(back[0].winner == saved[0].winner);
    CHECK(back[0].start_ms && *back[0].start_ms == -100.0);
    CHECK(back[1].race_time == "DNF");
}

TEST(db_reopen_continues_numbering_and_skips_a_half_written_line) {
    TempDir d;
    Timer t = sample_heat();
    {
        Database db;
        CHECK(db.open(d.file("races.csv")));
        RaceRecord rec; rec.timer = &t;
        rec.race = 1; CHECK(db.save_race(rec));
        rec.race = 2; CHECK(db.save_race(rec));
    }
    { std::ofstream f(d.file("races.csv"), std::ios::app); f << "1,99,Left,2026-01-01 00:"; }   // power cut

    Database db;
    CHECK(db.open(d.file("races.csv")));
    CHECK(db.next_race_number() == 3);          // the partial "race 99" doesn't count
    RaceRecord rec; rec.timer = &t; rec.race = 3;
    CHECK(db.save_race(rec));
    CHECK(db.rows_after(0).size() == 6);        // 3 races x 2 lanes, partial line skipped
    CHECK(db.rows_after(2).size() == 2);
    std::vector<RaceRow> last = db.recent(3);
    CHECK(last.size() == 3 && last.back().race == 3);
}

TEST(db_reads_old_17_column_rows) {
    TempDir d;
    {
        std::ofstream f(d.file("races.csv"));
        f << "Heatnumber,Racenumber,Lane,StartTime,FirstPass,Dog1,Dog2,Dog3,Dog4,Dog5,Dog6,Dog7,Dog8,Dog9,"
             "RaceTime,ModifiedDate,User\n"
             "1,7,Right,2025-02-05 10:00:00,1000.000,2000.000,,,,,,,,,2000.000,2025-02-05 10:01:00,Lee\n";
    }
    Database db;
    CHECK(db.open(d.file("races.csv")));
    CHECK(db.next_race_number() == 8);
    std::vector<RaceRow> rows = db.rows_after(0);
    CHECK(rows.size() == 1);
    if (rows.empty()) return;
    CHECK(rows[0].user == "Lee" && rows[0].splits_ms.size() == 2);
    CHECK(!rows[0].start_ms && !rows[0].winner && rows[0].false_starts == 0);
    CHECK(rows[0].early_pass.size() == 2);
}

TEST(db_save_fails_when_not_open) {
    Database db;
    Timer t = sample_heat();
    RaceRecord rec; rec.timer = &t;
    CHECK(!db.save_race(rec));
}

TEST(supabase_json_has_every_column) {
    RaceRow r;
    r.heat = 1; r.race = 7; r.lane = "Left"; r.start_time = "2026-11-22 10:00:00";
    r.splits_ms = { 1250.5, 2501 }; r.early_pass = { false, true }; r.race_time = "2501.000";
    r.modified = "2026-11-22 10:01:00"; r.user = "Lee \"J\""; r.start_ms = -12.5; r.winner = true; r.false_starts = 2;
    RaceRow blank; blank.lane = "Right";
    const std::string j = rows_to_json({ r, blank }, "bench");
    CHECK(j.front() == '[' && j.back() == ']');
    CHECK(j.find("\"board_id\":\"bench\"") != std::string::npos);
    CHECK(j.find("\"splits_ms\":[1250.500,2501.000]") != std::string::npos);
    CHECK(j.find("\"early_pass\":[false,true]") != std::string::npos);
    CHECK(j.find("\"operator\":\"Lee \\\"J\\\"\"") != std::string::npos);   // quotes escaped
    CHECK(j.find("\"start_ms\":-12.500") != std::string::npos);
    CHECK(j.find("\"winner\":true,\"false_starts\":2") != std::string::npos);
    CHECK(j.find("\"start_time\":null") != std::string::npos);              // blank -> null, not ""
    CHECK(j.find("\"start_ms\":null") != std::string::npos);
}

TEST(supabase_disabled_without_env) {
    ::unsetenv("SUPABASE_URL");
    SyncConfig c = SyncConfig::from_env();
    CHECK(!c.enabled());
    SupabaseSync s;
    CHECK(!s.start(c));
    s.enqueue({ RaceRow{} });   // must be a harmless no-op
    ::setenv("SUPABASE_URL", "https://x.supabase.co//", 1);
    ::setenv("SUPABASE_KEY", "k", 1);
    ::setenv("BOARD_ID", "b", 1);
    c = SyncConfig::from_env();
    CHECK(c.enabled() && c.url == "https://x.supabase.co");   // trailing slashes trimmed
    ::unsetenv("SUPABASE_URL"); ::unsetenv("SUPABASE_KEY"); ::unsetenv("BOARD_ID");
}

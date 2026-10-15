// Database.cpp  —  saves race results to a CSV file (same format as the old Python UFLI_races.csv)
//
// Steps:
//   1. pick storage + file location
//   2. open(): create dir/file, write header once
//   3. next_race_number()
//   4. save_race(): format + append rows
//   5. make writes survive power loss
//   6. recent(): read rows back for the GUI
//   7. close()
//   8. wire into state_machine.cpp (replace write_race_table())
#include "Database.hpp"
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

// ---------- settings ----------
// TODO(1): CSV keeps compatibility with the old Python tool and opens in Excel.
//          If you need queries/editing later, swap the inside of this file for sqlite3;
//          the class interface can stay the same.
// TODO(1): pick a path on persistent storage (eMMC/SD), not /tmp
constexpr const char* DEFAULT_DB_PATH = "data/UFLI_races.csv";

static const char* CSV_HEADER =
    "Heatnumber,Racenumber,Lane,StartTime,FirstPass,"
    "Dog1,Dog2,Dog3,Dog4,Dog5,Dog6,Dog7,Dog8,Dog9,"
    "RaceTime,ModifiedDate,User";

static const char* LANE_NAME[NUM_LANES] = { "Left", "Right" };

// ---------- helpers ----------
static std::string format_wall_time(std::chrono::system_clock::time_point tp) {
    // TODO(4): "YYYY-MM-DD HH:MM:SS" like the old CSV
    //   std::time_t t = std::chrono::system_clock::to_time_t(tp);
    //   char buf[32]; std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    //   return buf;
    // TODO(4): the BeagleBone has no RTC battery by default; make sure time is set (NTP) or dates will be 1970
    (void)tp;
    return "";
}

// ---------- 2. open ----------
bool Database::open(const std::string& path) {
    // TODO(2):
    //   path_ = path.empty() ? DEFAULT_DB_PATH : path;
    //   create the parent directory if missing (std::filesystem::create_directories)
    //   if the file doesn't exist or is empty: write CSV_HEADER + '\n'
    //   if it exists: optionally check the first line == CSV_HEADER, warn if not
    //   is_open_ = true; return true;   (false + fprintf(stderr, ...) on any failure)
    (void)path;
    (void)CSV_HEADER;   // TODO: remove once used
    return false;
}

// ---------- 3. race number ----------
int Database::next_race_number() {
    // TODO(3): same as next_race_number() in the old Python
    //   read every line after the header, take column 1 (Racenumber), return max + 1
    //   return 1 if the file is empty
    // TODO(3): or cache the max in a member after open() so you don't re-read the whole file each race
    return 1;
}

// ---------- 4. save ----------
bool Database::save_race(const RaceRecord& rec) {
    if (!is_open_ || !rec.timer) return false;

    // TODO(4): one row per lane
    //   std::ofstream f(path_, std::ios::app);
    //   for (int l = 0; l < NUM_LANES; ++l) {
    //       const LaneTime& L = rec.timer->lane(l);
    //       f << rec.heat << ',' << rec.race << ',' << LANE_NAME[l] << ','
    //         << format_wall_time(rec.timer->wall_start()) << ',';
    //       for (int i = 0; i < MAX_DOGS; ++i)          // FirstPass + Dog1..Dog9; blank if unused
    //           { if (i < L.n_splits) f << fixed 3 decimals L.splits_ms[i]; f << ','; }
    //       f << RaceTime << ',' << format_wall_time(system_clock::now()) << ',' << rec.user << '\n';
    //   }
    // TODO(4): decide what RaceTime says for FalseStart / DNF (e.g. "FS", "DNF", blank)
    // TODO(4): escape commas/quotes in rec.user if it can come from the GUI
    (void)LANE_NAME; (void)format_wall_time;   // TODO: remove once used
    return false;
}

// ---------- 5. durability ----------
// TODO(5): power can be cut at the track. After writing a race:
//   - f.flush(), then fsync() the file (needs the POSIX fd: use open()/write() or fopen()+fileno())
//   - write both lane rows in one write so a race is never half-saved

// ---------- 6. read back ----------
std::vector<RaceRow> Database::recent(int n) {
    std::vector<RaceRow> rows;
    // TODO(6): for the GUI results screen
    //   read the file, skip the header, split each line on ',' into a RaceRow
    //   keep only the last n rows
    //   skip malformed lines instead of crashing
    (void)n;
    return rows;
}

// ---------- 7. close ----------
void Database::close() {
    // TODO(7): nothing to do if each save opens/closes its own ofstream;
    //          if you keep a stream open as a member, flush + close it here
    is_open_ = false;
}

// ---------- 8. wire-up (do this in state_machine.cpp later) ----------
// TODO(8):
//   static Database db;   db.open(DEFAULT_DB_PATH) once at startup (main / run_race)
//   int race_no = db.next_race_number();
//   in write_race_table() (Finished entry):
//       db.save_race({ heat, race_no++, user, &timer });
//   keep the printf output too while testing
// TODO(8): where does heat number come from? (judge remote, GUI SetupScreen, or auto-increment)

#pragma once
#include "timer/Timer.hpp"
#include <optional>
#include <string>
#include <vector>

// One saved race = one row per lane. The first 17 columns are the old Python UFLI_races.csv:
//   Heatnumber, Racenumber, Lane, StartTime, FirstPass, Dog1..Dog9, RaceTime, ModifiedDate, User
// then 4 added after it (so old files and tools still line up):
//   StartMs      reaction time vs green in ms (negative = left early), blank = no start trigger seen
//   EarlyPasses  passes where the incoming dog was early, by column name ("Dog3;Dog7"), blank = none
//   Winner       1 = this lane won the race, 0 = it didn't
//   FalseStarts  false starts counted for this lane in the race
//
// The CSV is the backup and source of truth; SupabaseSync uploads the same rows afterwards, and
// re-sends them from here after a network outage or reboot.
//
// Power can be cut at the track, so a race (both lane rows) goes to disk in ONE write + fsync().
// Worst case after a power cut: a partial last line, which reading skips and open() steps past.

struct RaceRecord {
    int         heat = 1;
    int         race = 1;
    std::string user = "Operator";
    const Timer* timer = nullptr;   // splits, finish times, start time, early passes all come from here
    int         winner = -1;        // lane index, -1 = nobody
    int         false_starts[NUM_LANES] = {};
};

// One CSV line: what SupabaseSync uploads, and what reading the file gives back.
struct RaceRow {
    int         heat = 0;
    int         race = 0;
    std::string lane;
    std::string start_time;
    std::vector<double> splits_ms;  // only the passes that happened
    std::string race_time;          // ms with 3 decimals ("12345.678"), or "DNF" (a false start doesn't forfeit)
    std::string modified;
    std::string user;
    std::optional<double> start_ms; // reaction time vs green, empty = no start trigger seen
    std::vector<bool> early_pass;   // one per split: incoming dog hit #1 before the returning dog hit #2
    bool        winner = false;
    int         false_starts = 0;
};

class Database {
public:
    // Opens (or creates, with its folder and header) the CSV. "" = data/UFLI_races.csv, relative
    // to the working directory: firmware.service runs in the repo clone, on persistent storage.
    // Reads the highest race number, and starts a fresh line if the last one was cut off.
    // Old 17-column files are read and appended to (with a warning). False + log on failure.
    bool open(const std::string& path);

    // max(Racenumber) + 1, like the old Python tool (cached, not re-read each race).
    int  next_race_number();

    // Appends the race's two lane rows in one write + fsync. Only once that succeeded: true, and
    // the same rows are added to *saved (for SupabaseSync). Warns in the log if NTP hasn't set
    // the clock yet, since StartTime / ModifiedDate may then be wrong.
    bool save_race(const RaceRecord& rec, std::vector<RaceRow>* saved = nullptr);

    // Reading back, in file order; malformed lines are skipped (and logged).
    std::vector<RaceRow> recent(int n);               // the last n rows (for a GUI results screen)
    std::vector<RaceRow> rows_after(int race);        // Racenumber > race: the boot-time upload catch-up

    void close();                                     // nothing to flush: every save closes its file

private:
    std::string path_;
    bool        is_open_  = false;
    int         max_race_ = 0;      // highest Racenumber in the file, read once in open()
};

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
// The CSV is the backup / source of truth. The same rows are then uploaded to Supabase
// by SupabaseSync (database/SupabaseSync.hpp).

struct RaceRecord {
    int         heat = 1;
    int         race = 1;
    std::string user = "Operator";
    const Timer* timer = nullptr;   // splits, finish times, start time, early passes all come from here
    int         winner = -1;        // lane that won (LEFT = 0, RIGHT = 1), -1 = nobody
    int         false_starts[NUM_LANES] = {};   // RaceData::false_starts in state_machine.cpp
};

struct RaceRow {                    // one CSV line: read back for the GUI, and what SupabaseSync uploads
    int         heat = 0;
    int         race = 0;
    std::string lane;
    std::string start_time;
    std::vector<double> splits_ms;  // only the passes that happened (blank CSV columns are left out)
    std::string race_time;          // ms with 3 decimals ("12345.678"), or "DNF"
    std::string modified;
    std::string user;
    std::optional<double> start_ms; // reaction time vs green, empty = no start trigger seen
    std::vector<bool> early_pass;   // one per split: incoming dog hit #1 before the returning dog hit #2
    bool        winner = false;
    int         false_starts = 0;
};

class Database {
public:
    bool open(const std::string& path);               // create dir/file + header if missing
    int  next_race_number();                          // max(Racenumber) + 1
    bool save_race(const RaceRecord& rec,             // append 2 rows (left, right)
                   std::vector<RaceRow>* saved = nullptr);  // optional: the rows written, for SupabaseSync
    std::vector<RaceRow> recent(int n);               // last n rows, for the GUI
    std::vector<RaceRow> rows_after(int race);        // rows with Racenumber > race, for sync catch-up
    void close();

private:
    std::string path_;
    bool        is_open_  = false;
    int         max_race_ = 0;      // highest Racenumber in the file, read once in open()
};

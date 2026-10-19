#pragma once
#include "timer/Timer.hpp"
#include <string>
#include <vector>

// One saved race = one row per lane (same layout as the old Python UFLI_races.csv):
// Heatnumber, Racenumber, Lane, StartTime, FirstPass, Dog1..Dog9, RaceTime, ModifiedDate, User
// The CSV is the backup / source of truth. The same rows are then uploaded to Supabase
// by SupabaseSync (database/SupabaseSync.hpp).

struct RaceRecord {
    int         heat = 1;
    int         race = 1;
    std::string user = "Operator";
    const Timer* timer = nullptr;   // splits, finish times, start time all come from here
};

struct RaceRow {                    // one CSV line: read back for the GUI, and what SupabaseSync uploads
    int         heat = 0;
    int         race = 0;
    std::string lane;
    std::string start_time;
    std::vector<double> splits_ms;
    double      race_ms = 0.0;      // TODO: FS / DNF need a representation here too (see Database.cpp TODO 4)
    std::string modified;
    std::string user;
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
    bool        is_open_ = false;
};

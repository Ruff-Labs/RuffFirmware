#pragma once
#include "Timer.hpp"   // build with -I src/timer
#include <string>
#include <vector>

// One saved race = one row per lane (same layout as the old Python UFLI_races.csv):
// Heatnumber, Racenumber, Lane, StartTime, FirstPass, Dog1..Dog9, RaceTime, ModifiedDate, User

struct RaceRecord {
    int         heat = 1;
    int         race = 1;
    std::string user = "Operator";
    const Timer* timer = nullptr;   // splits, finish times, start time all come from here
};

struct RaceRow {                    // one parsed CSV line, for reading results back
    int         heat = 0;
    int         race = 0;
    std::string lane;
    std::string start_time;
    std::vector<double> splits_ms;
    double      race_ms = 0.0;
    std::string modified;
    std::string user;
};

class Database {
public:
    bool open(const std::string& path);               // create dir/file + header if missing
    int  next_race_number();                          // max(Racenumber) + 1
    bool save_race(const RaceRecord& rec);            // append 2 rows (left, right)
    std::vector<RaceRow> recent(int n);               // last n rows, for the GUI
    void close();

private:
    std::string path_;
    bool        is_open_ = false;
};

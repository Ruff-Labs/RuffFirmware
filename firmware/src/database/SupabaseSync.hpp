#pragma once
#include "database/Database.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Uploads race rows to Supabase in the background. The CSV (Database) is the backup:
// rows are only enqueued here AFTER Database::save_race() succeeded, so a failed or
// slow upload never loses a race and never blocks the state machine.
// Table + RLS policies: see docs/supabase-schema.md

struct SyncConfig {
    std::string url;          // SUPABASE_URL, e.g. https://xxxx.supabase.co
    std::string key;          // SUPABASE_KEY, the anon key (never service_role)
    std::string board_id;     // BOARD_ID, which board sent the row
    std::string state_path = "data/last_synced_race";   // highest race number confirmed uploaded

    static SyncConfig from_env();                       // reads the 3 env vars above
    bool enabled() const { return !url.empty() && !key.empty() && !board_id.empty(); }
};

class SupabaseSync {
public:
    bool start(const SyncConfig& cfg);                  // launch the worker thread
    void enqueue(const std::vector<RaceRow>& rows);     // non-blocking, called from the state machine
    void stop();                                        // finish current attempt, join thread

    int  last_synced_race() const;                      // read state_path, 0 if missing

private:
    void worker();
    bool post_rows(const std::vector<RaceRow>& rows);   // one HTTP POST, true on 2xx
    void save_last_synced(int race);

    SyncConfig              cfg_;
    std::thread             thread_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    std::deque<RaceRow>     queue_;
    bool                    running_ = false;
};

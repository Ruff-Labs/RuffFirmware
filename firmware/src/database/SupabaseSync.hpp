#pragma once
#include "database/Database.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Uploads race rows to Supabase in the background. Table + RLS policies: docs/supabase-schema.md
//
// - Rows are only enqueued AFTER Database::save_race() succeeded, so an upload can never lose
//   a race, and enqueue() never touches the network, so it never blocks the race loop.
// - A worker thread POSTs batches. Rows leave the queue only after a 2xx; a failure is retried
//   2 s, 4 s, ... up to 60 s apart. A 4xx (bad column, RLS, wrong key) is logged loudly every
//   try, since retrying won't fix it.
// - The highest uploaded race number is kept in state_path. At boot main.cpp re-sends every CSV
//   row after it; re-sending is harmless (the table's unique key + ignore-duplicates skip them).

struct SyncConfig {
    std::string url;          // SUPABASE_URL, e.g. https://xxxx.supabase.co
    std::string key;          // SUPABASE_KEY, the anon key (never service_role)
    std::string board_id;     // BOARD_ID, which board sent the row
    std::string state_path = "data/last_synced_race";

    // The three env vars above (trailing '/' trimmed from the URL). systemd loads them from
    // /etc/ruff/firmware.env; by hand: `set -a; source /etc/ruff/firmware.env; set +a` first.
    static SyncConfig from_env();
    bool enabled() const { return !url.empty() && !key.empty() && !board_id.empty(); }
};

// The POST body: a JSON array, one object per row, keys = the table's columns, blanks as null.
// Public so tests/ can check it without a network.
std::string rows_to_json(const std::vector<RaceRow>& rows, const std::string& board_id);

class SupabaseSync {
public:
    ~SupabaseSync() { stop(); }   // a still-running std::thread would abort the program

    // Starts the worker. False (and does nothing) if the config isn't complete: CSV only.
    bool start(const SyncConfig& cfg);

    // Queue rows for upload. No-op when sync is off: they're in the CSV and go up at the next boot.
    void enqueue(const std::vector<RaceRow>& rows);

    // Finishes the POST in progress (at most ~10 s) and joins the worker. Whatever is still
    // queued is dropped: it's in the CSV and the boot catch-up resends it. Safe to call twice.
    void stop();

    // Highest race number confirmed uploaded (state_path; 0 if missing). Call after start().
    int  last_synced_race() const;

private:
    void worker();
    bool post_rows(const std::vector<RaceRow>& rows);   // one HTTP POST, true on 2xx
    void save_last_synced(int race);                    // atomic (temp file + rename), never goes down

    SyncConfig              cfg_;
    std::thread             thread_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    std::deque<RaceRow>     queue_;
    bool                    running_ = false;
};

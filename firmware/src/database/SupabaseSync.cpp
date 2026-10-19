// SupabaseSync.cpp  —  background upload of race rows to Supabase (CSV stays the backup)
//
// Steps:
//   1. read config from env (SUPABASE_URL, SUPABASE_KEY, BOARD_ID)
//   2. start(): init libcurl, launch worker thread
//   3. enqueue(): push rows, wake the worker
//   4. worker(): take a batch, POST it, retry with backoff on failure
//   5. post_rows(): build JSON + HTTP POST with libcurl
//   6. last_synced_race() / save_last_synced(): sidecar file so a reboot resumes uploads
//   7. stop()
//   8. wire-up in main.cpp + state_machine.cpp (see Database.cpp TODO 8)
#include "database/SupabaseSync.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <curl/curl.h>
// #include <nlohmann/json.hpp>   // TODO(5): uncomment when building the request body

// ---------- 1. config ----------
SyncConfig SyncConfig::from_env() {
    SyncConfig c;
    // TODO(1): std::getenv returns nullptr when unset, so guard each one
    //   if (const char* v = std::getenv("SUPABASE_URL")) c.url = v;
    //   if (const char* v = std::getenv("SUPABASE_KEY")) c.key = v;
    //   if (const char* v = std::getenv("BOARD_ID"))     c.board_id = v;
    // TODO(1): systemd loads these from /etc/ruff/firmware.env (EnvironmentFile in firmware.service).
    //          For running by hand: `set -a; source /etc/ruff/firmware.env; set +a` first
    return c;
}

// ---------- 2. start ----------
bool SupabaseSync::start(const SyncConfig& cfg) {
    // TODO(2):
    //   if (!cfg.enabled()) { printf("supabase sync disabled (no env), CSV only\n"); return false; }
    //   cfg_ = cfg;
    //   curl_global_init(CURL_GLOBAL_DEFAULT);   // once per program, before any thread uses curl
    //   running_ = true;
    //   thread_ = std::thread(&SupabaseSync::worker, this);
    //   return true;
    (void)cfg;
    return false;
}

// ---------- 3. enqueue ----------
void SupabaseSync::enqueue(const std::vector<RaceRow>& rows) {
    // TODO(3): must never block on the network, it's called from the race loop
    //   { std::lock_guard<std::mutex> lk(mtx_); queue_.insert(queue_.end(), rows.begin(), rows.end()); }
    //   cv_.notify_one();
    // TODO(3): if sync is disabled (start() returned false), just return
    (void)rows;
}

// ---------- 4. worker ----------
void SupabaseSync::worker() {
    // TODO(4):
    //   auto backoff = 2s;
    //   while (true) {
    //       std::vector<RaceRow> batch;
    //       {
    //           std::unique_lock<std::mutex> lk(mtx_);
    //           cv_.wait(lk, [&]{ return !queue_.empty() || !running_; });
    //           if (!running_ && queue_.empty()) return;
    //           copy (don't pop yet) up to ~50 rows from the front into batch
    //       }
    //       if (post_rows(batch)) {
    //           pop those rows from queue_ (under the lock)
    //           save_last_synced(highest batch[i].race);
    //           backoff = 2s;
    //       } else {
    //           wait backoff (use cv_.wait_for so stop() can interrupt it), then backoff = min(backoff * 2, 60s)
    //       }
    //   }
    // TODO(4): only pop rows AFTER a successful POST, so a failed batch is retried, not dropped
    // TODO(4): both lane rows of a race should go in the same batch, so last_synced_race
    //          never points at a half-uploaded race
}

// ---------- 5. HTTP POST ----------
bool SupabaseSync::post_rows(const std::vector<RaceRow>& rows) {
    // TODO(5): build the body, one object per row, columns must match docs/supabase-schema.md
    //   nlohmann::json body = nlohmann::json::array();
    //   for (auto& r : rows) body.push_back({
    //       {"board_id", cfg_.board_id}, {"heat", r.heat}, {"race", r.race}, {"lane", r.lane},
    //       {"start_time", r.start_time}, {"splits_ms", r.splits_ms}, {"race_time", ...},
    //       {"modified_at", r.modified}, {"operator", r.user} });
    //   std::string payload = body.dump();
    //
    // TODO(5): send it
    //   std::string url = cfg_.url + "/rest/v1/race_results?on_conflict=board_id,race,lane";
    //   CURL* c = curl_easy_init();
    //   curl_slist* h = nullptr;
    //   h = curl_slist_append(h, ("apikey: " + cfg_.key).c_str());
    //   h = curl_slist_append(h, ("Authorization: Bearer " + cfg_.key).c_str());
    //   h = curl_slist_append(h, "Content-Type: application/json");
    //   h = curl_slist_append(h, "Prefer: resolution=ignore-duplicates,return=minimal");
    //   curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    //   curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    //   curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload.c_str());
    //   curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);          // don't hang forever on bad Wi-Fi
    //   CURLcode rc = curl_easy_perform(c);
    //   long status = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    //   curl_slist_free_all(h); curl_easy_cleanup(c);
    //   return rc == CURLE_OK && status >= 200 && status < 300;
    //
    // TODO(5): log failures (rc string from curl_easy_strerror, HTTP status) so the handoff team can debug
    // TODO(5): a 4xx (bad column, RLS denied) will never succeed on retry; log loudly instead of looping silently
    // TODO(5): start_time/modified must be ISO-ish ("YYYY-MM-DD HH:MM:SS") or Postgres rejects the timestamptz;
    //          send null instead of "" when blank
    (void)rows;
    return false;
}

// ---------- 6. sync progress ----------
int SupabaseSync::last_synced_race() const {
    // TODO(6): std::ifstream f(cfg_.state_path); int n = 0; f >> n; return n;   (missing file -> 0)
    return 0;
}

void SupabaseSync::save_last_synced(int race) {
    // TODO(6): atomic so a power cut can't leave a half-written number
    //   write race to state_path + ".tmp", flush + fsync, then std::rename(tmp, state_path)
    (void)race;
}

// ---------- 7. stop ----------
void SupabaseSync::stop() {
    // TODO(7):
    //   { std::lock_guard<std::mutex> lk(mtx_); running_ = false; }
    //   cv_.notify_all();
    //   if (thread_.joinable()) thread_.join();
    //   curl_global_cleanup();
    // TODO(7): anything still in queue_ is fine to drop, it's in the CSV and the
    //          boot catch-up (rows_after(last_synced_race())) will resend it
}

// ---------- 8. wire-up ----------
// TODO(8): in main.cpp at startup, after db.open():
//   SupabaseSync sync;
//   if (sync.start(SyncConfig::from_env()))
//       sync.enqueue(db.rows_after(sync.last_synced_race()));   // catch up anything missed while offline
// TODO(8): in state_machine.cpp write_race_table(): after db.save_race() returns true,
//   sync.enqueue(<the 2 rows just saved>)   (see Database.cpp TODO 8)

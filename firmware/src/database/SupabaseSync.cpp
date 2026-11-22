// SupabaseSync.cpp  —  background upload of race rows to Supabase (CSV stays the backup)
//
// enqueue() only adds rows to a queue; a worker thread POSTs them. Rows leave the queue only
// after a 2xx, so a failed upload is retried (2 s, 4 s, ... up to 60 s apart), never dropped.
// The highest uploaded race number goes to state_path, and on boot main.cpp re-sends every
// CSV row after it. Re-sending is harmless: the table's unique key + ignore-duplicates skips them.
#include "database/SupabaseSync.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>

using namespace std::chrono_literals;

constexpr size_t         BATCH_ROWS     = 50;    // rows per POST (a race's two rows are never split)
constexpr auto           BACKOFF_MIN    = 2s;
constexpr auto           BACKOFF_MAX    = 60s;
constexpr long           HTTP_TIMEOUT_S = 10;    // don't hang on bad Wi-Fi

// ---------- config ----------
// systemd loads these from /etc/ruff/firmware.env (EnvironmentFile in firmware.service).
// Running by hand: `set -a; source /etc/ruff/firmware.env; set +a` first.
SyncConfig SyncConfig::from_env() {
    SyncConfig c;
    if (const char* v = std::getenv("SUPABASE_URL")) c.url      = v;
    if (const char* v = std::getenv("SUPABASE_KEY")) c.key      = v;
    if (const char* v = std::getenv("BOARD_ID"))     c.board_id = v;
    while (!c.url.empty() && c.url.back() == '/') c.url.pop_back();   // url + "/rest/v1/..." gets one slash
    return c;
}

// ---------- start ----------
bool SupabaseSync::start(const SyncConfig& cfg) {
    if (thread_.joinable()) return true;   // already running
    if (!cfg.enabled()) {
        std::printf("supabase: sync disabled (SUPABASE_URL / SUPABASE_KEY / BOARD_ID not set), CSV only\n");
        return false;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {   // once, before any thread uses curl
        std::fprintf(stderr, "supabase: curl_global_init failed, CSV only\n");
        return false;
    }
    cfg_     = cfg;
    running_ = true;
    thread_  = std::thread(&SupabaseSync::worker, this);
    std::printf("supabase: uploading as board '%s'\n", cfg_.board_id.c_str());
    return true;
}

// ---------- enqueue ----------
// Called from the race loop: only takes the lock for a copy, never touches the network.
void SupabaseSync::enqueue(const std::vector<RaceRow>& rows) {
    if (rows.empty()) return;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!running_) return;   // sync disabled: the rows are in the CSV, the boot catch-up sends them later
        queue_.insert(queue_.end(), rows.begin(), rows.end());
    }
    cv_.notify_one();
}

// ---------- worker ----------
void SupabaseSync::worker() {
    auto backoff = BACKOFF_MIN;
    for (;;) {
        std::vector<RaceRow> batch;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [&] { return !queue_.empty() || !running_; });
            if (!running_) return;   // stop(): anything left is in the CSV and gets resent at the next boot

            // copy, don't pop: rows only leave the queue after a successful POST.
            // Keep a race's rows together so last_synced_race never points at a half-uploaded race.
            for (const RaceRow& r : queue_) {
                if (batch.size() >= BATCH_ROWS && r.race != batch.back().race) break;
                batch.push_back(r);
            }
        }

        if (post_rows(batch)) {
            int highest = 0;
            for (const RaceRow& r : batch) highest = std::max(highest, r.race);
            {
                std::lock_guard<std::mutex> lk(mtx_);
                queue_.erase(queue_.begin(), queue_.begin() + batch.size());
            }
            save_last_synced(highest);
            backoff = BACKOFF_MIN;
        } else {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, backoff, [&] { return !running_; });   // stop() cuts the wait short
            backoff = std::min(backoff * 2, BACKOFF_MAX);
        }
    }
}

// ---------- JSON ----------
static void json_string(std::string& out, const std::string& s) {
    out += '"';
    for (const unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += char(c);
                }
        }
    }
    out += '"';
}

// blank -> null: Postgres rejects "" for a timestamptz
static void json_string_or_null(std::string& out, const std::string& s) {
    if (s.empty()) out += "null";
    else           json_string(out, s);
}

// one object per row; keys = the columns in docs/supabase-schema.md
static std::string rows_to_json(const std::vector<RaceRow>& rows, const std::string& board_id) {
    std::string body = "[";
    for (size_t i = 0; i < rows.size(); ++i) {
        const RaceRow& r = rows[i];
        if (i) body += ',';
        body += "{\"board_id\":";      json_string(body, board_id);
        body += ",\"heat\":"           + std::to_string(r.heat);
        body += ",\"race\":"           + std::to_string(r.race);
        body += ",\"lane\":";          json_string(body, r.lane);
        body += ",\"start_time\":";    json_string_or_null(body, r.start_time);
        body += ",\"splits_ms\":[";
        for (size_t s = 0; s < r.splits_ms.size(); ++s) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%s%.3f", s ? "," : "", r.splits_ms[s]);
            body += buf;
        }
        body += "],\"race_time\":";    json_string_or_null(body, r.race_time);
        body += ",\"modified_at\":";   json_string_or_null(body, r.modified);
        body += ",\"operator\":";      json_string_or_null(body, r.user);
        if (r.start_ms) {
            char buf[32];
            std::snprintf(buf, sizeof buf, ",\"start_ms\":%.3f", *r.start_ms);
            body += buf;
        } else {
            body += ",\"start_ms\":null";
        }
        body += ",\"early_pass\":[";
        for (size_t e = 0; e < r.early_pass.size(); ++e) {
            if (e) body += ',';
            body += r.early_pass[e] ? "true" : "false";
        }
        body += "],\"winner\":";       body += r.winner ? "true" : "false";
        body += ",\"false_starts\":"   + std::to_string(r.false_starts);
        body += '}';
    }
    return body + ']';
}

// keep the response body for the log instead of letting curl print it to stdout
static size_t collect_body(char* data, size_t size, size_t n, void* user) {
    static_cast<std::string*>(user)->append(data, size * n);
    return size * n;
}

// ---------- HTTP POST ----------
bool SupabaseSync::post_rows(const std::vector<RaceRow>& rows) {
    const std::string body = rows_to_json(rows, cfg_.board_id);
    const std::string url  = cfg_.url + "/rest/v1/race_results?on_conflict=board_id,race,lane";

    CURL* c = curl_easy_init();
    if (!c) {
        std::fprintf(stderr, "supabase: curl_easy_init failed, will retry\n");
        return false;
    }
    curl_slist* h = nullptr;
    h = curl_slist_append(h, ("apikey: " + cfg_.key).c_str());
    h = curl_slist_append(h, ("Authorization: Bearer " + cfg_.key).c_str());
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, "Prefer: resolution=ignore-duplicates,return=minimal");

    std::string response;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, long(body.size()));
    curl_easy_setopt(c, CURLOPT_TIMEOUT, HTTP_TIMEOUT_S);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);              // required when curl runs in a thread
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect_body);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &response);

    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    const int first = rows.front().race, last = rows.back().race;
    if (rc != CURLE_OK) {   // no network, DNS, timeout, TLS...
        std::fprintf(stderr, "supabase: upload of races %d-%d failed: %s (will retry)\n",
                     first, last, curl_easy_strerror(rc));
        return false;
    }
    if (status >= 200 && status < 300) {
        std::printf("supabase: uploaded races %d-%d (%zu rows)\n", first, last, rows.size());
        return true;
    }
    if (status >= 400 && status < 500) {
        // bad column, RLS denied, wrong key: retrying won't fix it, so say so loudly every time
        std::fprintf(stderr,
                     "supabase: ERROR: server rejected races %d-%d (HTTP %ld). This won't fix itself: check the "
                     "table columns (docs/supabase-schema.md), the RLS policies and SUPABASE_KEY. "
                     "Races are still saved in the CSV. Response: %s\n",
                     first, last, status, response.c_str());
    } else {
        std::fprintf(stderr, "supabase: upload of races %d-%d got HTTP %ld (will retry). Response: %s\n",
                     first, last, status, response.c_str());
    }
    return false;
}

// ---------- sync progress ----------
int SupabaseSync::last_synced_race() const {
    std::ifstream f(cfg_.state_path);
    int n = 0;
    f >> n;               // missing or garbage file -> 0, which just means "resend everything"
    return n > 0 ? n : 0;
}

// Atomic: write a temp file, fsync it, then rename over the old one, so a power cut leaves
// either the old number or the new one, never half a number.
void SupabaseSync::save_last_synced(int race) {
    if (race <= last_synced_race()) return;   // only ever moves forward
    const std::string tmp  = cfg_.state_path + ".tmp";
    const std::string data = std::to_string(race) + '\n';

    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    bool ok = fd >= 0
           && ::write(fd, data.data(), data.size()) == ssize_t(data.size())
           && ::fsync(fd) == 0;
    int err = ok ? 0 : errno;
    if (fd >= 0) ::close(fd);
    if (ok && std::rename(tmp.c_str(), cfg_.state_path.c_str()) != 0) { ok = false; err = errno; }

    if (!ok)   // harmless: those races just get re-sent (and skipped as duplicates) at the next boot
        std::fprintf(stderr, "supabase: can't save %s: %s\n", cfg_.state_path.c_str(), std::strerror(err));
}

// ---------- stop ----------
void SupabaseSync::stop() {
    if (!thread_.joinable()) return;   // never started, or already stopped
    {
        std::lock_guard<std::mutex> lk(mtx_);
        running_ = false;
    }
    cv_.notify_all();
    thread_.join();                    // waits for a POST in progress (at most HTTP_TIMEOUT_S)
    curl_global_cleanup();
    // anything still queued is fine to drop: it's in the CSV, and the boot catch-up resends it
}

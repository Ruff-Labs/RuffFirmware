#include "database/Database.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
#ifdef __linux__
#include <sys/timex.h>
#endif

// ---------- settings ----------
constexpr const char* DEFAULT_DB_PATH = "data/UFLI_races.csv";

#define OLD_COLUMNS_HEADER                                  \
    "Heatnumber,Racenumber,Lane,StartTime,FirstPass,"       \
    "Dog1,Dog2,Dog3,Dog4,Dog5,Dog6,Dog7,Dog8,Dog9,"         \
    "RaceTime,ModifiedDate,User"
static const char* OLD_HEADER = OLD_COLUMNS_HEADER;
static const char* CSV_HEADER = OLD_COLUMNS_HEADER ",StartMs,EarlyPasses,Winner,FalseStarts";
#undef OLD_COLUMNS_HEADER

// column positions in CSV_HEADER
constexpr int COL_FIRST_SPLIT   = 4;                      // FirstPass, then Dog1..Dog9
constexpr int COL_RACE_TIME     = COL_FIRST_SPLIT + MAX_DOGS;
constexpr int COL_MODIFIED      = COL_RACE_TIME + 1;
constexpr int COL_USER          = COL_MODIFIED + 1;
constexpr int OLD_COLUMNS       = COL_USER + 1;           // rows from the old Python tool stop here
constexpr int COL_START_MS      = OLD_COLUMNS;
constexpr int COL_EARLY_PASSES  = COL_START_MS + 1;
constexpr int COL_WINNER        = COL_EARLY_PASSES + 1;
constexpr int COL_FALSE_STARTS  = COL_WINNER + 1;
constexpr int CSV_COLUMNS       = COL_FALSE_STARTS + 1;

// split column names, also used in EarlyPasses ("Dog3;Dog7")
static const char* SPLIT_NAME[MAX_DOGS] = {
    "FirstPass", "Dog1", "Dog2", "Dog3", "Dog4", "Dog5", "Dog6", "Dog7", "Dog8", "Dog9",
};

static const char* LANE_NAME[NUM_LANES] = { "Left", "Right" };

// ---------- helpers ----------
// Has NTP set the wall clock? The board has no RTC battery and firmware.service doesn't wait for
// the network, so until then it's whatever was restored at boot (usually stale, not 1970).
// adjtimex() with no modes only reads the sync status.
static bool wall_clock_synced() {
#ifdef __linux__
    timex tx{};
    return adjtimex(&tx) != TIME_ERROR;
#else
    return true;   // laptop builds (unit tests): the OS keeps the clock set
#endif
}

// "YYYY-MM-DD HH:MM:SS" like the old CSV (Postgres also accepts it for timestamptz).
static std::string format_wall_time(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

static std::string format_ms(double ms) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", ms);
    return buf;
}

// Quote a field only if it needs it: a,b -> "a,b", and an embedded " becomes "".
static std::string csv_field(const std::string& s) {
    if (s.find_first_of(",\"") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + '"';
}

// early_pass flags -> "Dog3;Dog7" (blank if none)
static std::string format_early_passes(const std::vector<bool>& early) {
    std::string out;
    for (size_t i = 0; i < early.size() && i < size_t(MAX_DOGS); ++i) {
        if (!early[i]) continue;
        if (!out.empty()) out += ';';
        out += SPLIT_NAME[i];
    }
    return out;
}

// "Dog3;Dog7" -> flags, one per split. False for a name that isn't a split column.
static bool parse_early_passes(const std::string& s, std::vector<bool>& early) {
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(';', start);
        if (end == std::string::npos) end = s.size();
        const std::string name = s.substr(start, end - start);
        int i = 0;
        while (i < MAX_DOGS && name != SPLIT_NAME[i]) ++i;
        if (i == MAX_DOGS) return false;
        if (size_t(i) >= early.size()) early.resize(i + 1, false);
        early[i] = true;
        start = end + 1;
    }
    return true;
}

// One CSV line -> fields, undoing csv_field()'s quoting.
static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out(1);
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c != '"')                                       out.back() += c;
            else if (i + 1 < line.size() && line[i + 1] == '"') { out.back() += '"'; ++i; }
            else                                                quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out.emplace_back();
        } else {
            out.back() += c;
        }
    }
    return out;
}

// false for a malformed line (wrong column count, bad number, half-written by a power cut).
static bool parse_row(const std::string& line, RaceRow& row) {
    const std::vector<std::string> f = split_csv(line);
    if (int(f.size()) != CSV_COLUMNS && int(f.size()) != OLD_COLUMNS) return false;
    try {
        row            = RaceRow{};
        row.heat       = std::stoi(f[0]);
        row.race       = std::stoi(f[1]);
        row.lane       = f[2];
        row.start_time = f[3];
        for (int i = 0; i < MAX_DOGS; ++i) {
            const std::string& s = f[COL_FIRST_SPLIT + i];
            if (s.empty()) break;                    // unused dogs are blank, always at the end
            row.splits_ms.push_back(std::stod(s));
        }
        row.race_time = f[COL_RACE_TIME];
        row.modified  = f[COL_MODIFIED];
        row.user      = f[COL_USER];
        row.early_pass.assign(row.splits_ms.size(), false);
        if (int(f.size()) == CSV_COLUMNS) {
            if (!f[COL_START_MS].empty()) row.start_ms = std::stod(f[COL_START_MS]);
            if (!parse_early_passes(f[COL_EARLY_PASSES], row.early_pass)) return false;
            row.early_pass.resize(row.splits_ms.size(), false);   // never more flags than splits
            row.winner = f[COL_WINNER] == "1";
            if (!f[COL_FALSE_STARTS].empty()) row.false_starts = std::stoi(f[COL_FALSE_STARTS]);
        }
    } catch (const std::exception&) {                // stoi/stod: not a number
        return false;
    }
    return true;
}

// Calls fn(row) for every good row after the header, in file order. Malformed lines are skipped.
template <typename Fn>
static void for_each_row(const std::string& path, Fn fn) {
    std::ifstream in(path);
    std::string line;
    if (!std::getline(in, line)) return;             // header (or empty file)
    RaceRow row;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();   // edited on Windows
        if (line.empty()) continue;
        if (parse_row(line, row)) fn(row);
        else std::fprintf(stderr, "database: skipping malformed line in %s: %s\n", path.c_str(), line.c_str());
    }
}

// Append + fsync in one go: either the whole chunk is on disk when this returns true, or
// (power cut mid-write) at most a partial last line.
static bool append_durable(const std::string& path, const std::string& data) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        std::fprintf(stderr, "database: can't open %s: %s\n", path.c_str(), std::strerror(errno));
        return false;
    }
    size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "database: write to %s failed: %s\n", path.c_str(), std::strerror(errno));
            ::close(fd);
            return false;
        }
        done += size_t(n);
    }
    if (::fsync(fd) != 0) {
        std::fprintf(stderr, "database: fsync of %s failed: %s\n", path.c_str(), std::strerror(errno));
        ::close(fd);
        return false;
    }
    ::close(fd);
    return true;
}

static bool ends_with_newline(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() <= 0) return true;
    in.seekg(-1, std::ios::end);
    char c = 0;
    in.get(c);
    return c == '\n';
}

bool Database::open(const std::string& path) {
    namespace fs = std::filesystem;
    path_     = path.empty() ? DEFAULT_DB_PATH : path;
    is_open_  = false;
    max_race_ = 0;

    std::error_code ec;
    const fs::path parent = fs::path(path_).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    if (ec) {
        std::fprintf(stderr, "database: can't create %s: %s\n", parent.c_str(), ec.message().c_str());
        return false;
    }

    std::string first;
    {
        std::ifstream in(path_);
        if (in) std::getline(in, first);
    }
    if (!first.empty() && first.back() == '\r') first.pop_back();

    if (first.empty()) {
        if (!append_durable(path_, std::string(CSV_HEADER) + '\n')) return false;
    } else {
        if (first == OLD_HEADER)
            std::fprintf(stderr, "database: warning: %s has the old 17-column header; new rows add StartMs, "
                                 "EarlyPasses, Winner, FalseStarts after User. Move the file aside to start a "
                                 "fresh one with the full header.\n", path_.c_str());
        else if (first != CSV_HEADER)
            std::fprintf(stderr, "database: warning: %s has an unexpected header, appending anyway\n", path_.c_str());
        // a power cut mid-write can leave a partial last line
        if (!ends_with_newline(path_) && !append_durable(path_, "\n")) return false;
        for_each_row(path_, [this](const RaceRow& r) { max_race_ = std::max(max_race_, r.race); });
    }

    is_open_ = true;
    std::printf("database: %s (last race %d)\n", path_.c_str(), max_race_);
    return true;
}

int Database::next_race_number() {
    return max_race_ + 1;
}

bool Database::save_race(const RaceRecord& rec, std::vector<RaceRow>* saved) {
    if (!is_open_ || !rec.timer) return false;

    // a newline would split the row; commas/quotes are handled by csv_field()
    std::string user = rec.user;
    std::replace(user.begin(), user.end(), '\n', ' ');
    std::replace(user.begin(), user.end(), '\r', ' ');

    if (!wall_clock_synced())
        std::fprintf(stderr, "database: warning: clock not synced by NTP yet (no network?), race %d's "
                             "StartTime/ModifiedDate may be wrong\n", rec.race);
    const std::string started  = format_wall_time(rec.timer->wall_start());
    const std::string modified = format_wall_time(std::chrono::system_clock::now());

    // both lane rows first, then one write; the RaceRows hold exactly what the CSV gets
    std::vector<RaceRow> rows;
    std::string out;
    for (int l = 0; l < NUM_LANES; ++l) {
        const LaneTime& L = rec.timer->lane(l);
        RaceRow row;
        row.heat       = rec.heat;
        row.race       = rec.race;
        row.lane       = LANE_NAME[l];
        row.start_time = started;
        for (int i = 0; i < L.n_splits; ++i) {
            row.splits_ms.push_back(L.splits_ms[i]);
            row.early_pass.push_back(L.early_pass[i]);
        }
        const double ms = rec.timer->race_ms(l);
        row.race_time = (ms == RACE_DNF) ? "DNF" : format_ms(ms);
        row.modified  = modified;
        row.user      = user;
        if (L.has_start) row.start_ms = L.start_ms;
        row.winner       = (rec.winner == l);
        row.false_starts = rec.false_starts[l];

        out += std::to_string(row.heat) + ',' + std::to_string(row.race) + ',' + row.lane + ','
             + row.start_time + ',';
        for (int i = 0; i < MAX_DOGS; ++i) {         // FirstPass + Dog1..Dog9; blank if unused
            if (i < int(row.splits_ms.size())) out += format_ms(row.splits_ms[i]);
            out += ',';
        }
        out += row.race_time + ',' + row.modified + ',' + csv_field(row.user) + ','
             + (row.start_ms ? format_ms(*row.start_ms) : "") + ','
             + format_early_passes(row.early_pass) + ','
             + (row.winner ? "1" : "0") + ','
             + std::to_string(row.false_starts) + '\n';
        rows.push_back(std::move(row));
    }

    if (!append_durable(path_, out)) return false;

    max_race_ = std::max(max_race_, rec.race);
    if (saved) saved->insert(saved->end(), rows.begin(), rows.end());   // only once it's safely on disk
    return true;
}

std::vector<RaceRow> Database::recent(int n) {
    std::deque<RaceRow> last;
    if (n <= 0) return {};
    for_each_row(path_, [&](const RaceRow& r) {
        last.push_back(r);
        if (int(last.size()) > n) last.pop_front();
    });
    return { last.begin(), last.end() };
}

std::vector<RaceRow> Database::rows_after(int race) {
    std::vector<RaceRow> rows;
    for_each_row(path_, [&](const RaceRow& r) {
        if (r.race > race) rows.push_back(r);
    });
    return rows;
}

void Database::close() {
    is_open_ = false;
}

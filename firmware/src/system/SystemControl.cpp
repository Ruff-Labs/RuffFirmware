#include "system/SystemControl.hpp"
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

using namespace std::chrono_literals;
using protocol::NetOp;

constexpr auto REFRESH_EVERY = 10s;
constexpr int  NOT_STARTED   = -1;    // run(): the program couldn't be started (not installed)

// Runs a program with these arguments (no shell) and waits for it. stdout + stderr go to *out.
// Returns its exit code, or NOT_STARTED.
static int run(const std::vector<std::string>& args, std::string* out = nullptr) {
    int fds[2];
    if (pipe(fds) != 0) return NOT_STARTED;

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);

    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid;
    const int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (rc != 0) { close(fds[0]); return NOT_STARTED; }

    std::string output;
    char buf[512];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof buf)) > 0) output.append(buf, size_t(n));
    close(fds[0]);

    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return NOT_STARTED;
    if (out) *out = output;
    if (!WIFEXITED(st)) return NOT_STARTED;
    const int code = WEXITSTATUS(st);
    return code == 127 ? NOT_STARTED : code;   // 127: the child couldn't exec it
}

static std::string first_line(const std::string& s) {
    std::string line = s.substr(0, s.find('\n'));
    if (line.rfind("Error: ", 0) == 0) line.erase(0, 7);
    return line.size() > size_t(protocol::kMessageLen - 1) ? line.substr(0, protocol::kMessageLen - 4) + "..." : line;
}

// nmcli's error text -> something a person at the track can act on
static std::string explain_nmcli_error(const std::string& output, const std::string& ssid) {
    if (output.find("Secrets were required") != std::string::npos ||
        output.find("802-11-wireless-security") != std::string::npos)
        return "Couldn't join \"" + ssid + "\": wrong password?";
    if (output.find("No network with SSID") != std::string::npos)
        return "Network \"" + ssid + "\" not found (out of range or misspelled?)";
    return first_line(output);
}

static bool valid_ipv4(const std::string& s) {
    in_addr a;
    return inet_pton(AF_INET, s.c_str(), &a) == 1;
}

// "192.168.1.50" -> "192.168.1.50/24"; "" if it isn't a valid address[/prefix]
static std::string with_prefix(std::string addr) {
    const size_t slash = addr.find('/');
    if (slash == std::string::npos) addr += "/24";
    const std::string ip = addr.substr(0, addr.find('/'));
    const std::string pre = addr.substr(addr.find('/') + 1);
    if (!valid_ipv4(ip) || pre.empty() || pre.size() > 2 || pre.find_first_not_of("0123456789") != std::string::npos)
        return "";
    const int p = std::stoi(pre);
    return (p >= 1 && p <= 32) ? addr : "";
}

static std::string field(const char* s, size_t n) {   // a fixed-size char[] that may lack its 0
    return std::string(s, strnlen(s, n));
}

// ---------- lifetime ----------
SystemControl::SystemControl() : thread_(&SystemControl::worker, this) {}

SystemControl::~SystemControl() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        running_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

// ---------- requests (race loop thread) ----------
void SystemControl::set_wifi(const protocol::WifiConfig& cfg) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (wifi_job_ || status_.op == NetOp::Working) {
            status_.message = "Still working on the last change, try again in a moment";
            return;
        }
        wifi_job_  = cfg;
        status_.op = NetOp::Working;
        status_.message = "Connecting to \"" + field(cfg.ssid, sizeof cfg.ssid) + "\"...";
    }
    cv_.notify_one();
}

void SystemControl::restart_software() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        restart_job_ = true;
    }
    cv_.notify_one();
}

SystemControl::Status SystemControl::status() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return status_;
}

void SystemControl::set_status(NetOp op, const std::string& message) {
    std::lock_guard<std::mutex> lk(mtx_);
    status_.op      = op;
    status_.message = message;
}

// ---------- worker ----------
void SystemControl::worker() {
    refresh_ssid();
    for (;;) {
        std::optional<protocol::WifiConfig> wifi;
        bool restart = false;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, REFRESH_EVERY, [this] { return !running_ || wifi_job_ || restart_job_; });
            if (!running_) return;
            wifi.swap(wifi_job_);
            std::swap(restart, restart_job_);
        }
        if (restart) do_restart();
        if (wifi)    do_wifi(*wifi);
        refresh_ssid();
    }
}

void SystemControl::do_restart() {
    std::printf("system: restarting the race software (requested from the GUI)\n");
    // --no-block: systemd does the restart after this returns, so stopping us doesn't cut it short
    if (run({ "systemctl", "--no-block", "restart", "gui.service", "firmware.service" }) != 0)
        std::fprintf(stderr, "system: systemctl restart failed\n");
}

void SystemControl::refresh_ssid() {
    std::string out;
    const int rc = run({ "nmcli", "-t", "-f", "NAME,TYPE", "connection", "show", "--active" }, &out);
    if (rc == NOT_STARTED) {
        if (!warned_no_nmcli_) {
            std::fprintf(stderr, "system: nmcli not found, Wi-Fi setup from the GUI won't work "
                                 "(this board image doesn't use NetworkManager)\n");
            warned_no_nmcli_ = true;
        }
        return;
    }
    // lines are "NAME:TYPE"; a ':' inside NAME is escaped as "\:"
    std::string name, ssid;
    size_t start = 0;
    while (start < out.size()) {
        size_t end = out.find('\n', start);
        if (end == std::string::npos) end = out.size();
        const std::string line = out.substr(start, end - start);
        start = end + 1;
        const size_t colon = line.rfind(':');
        if (colon == std::string::npos || line.compare(colon + 1, std::string::npos, "802-11-wireless") != 0) continue;
        for (size_t i = 0; i < colon; ++i) {
            if (line[i] == '\\' && i + 1 < colon) ++i;
            name += line[i];
        }
        break;
    }
    if (!name.empty()) {
        std::string s;
        if (run({ "nmcli", "-t", "-g", "802-11-wireless.ssid", "connection", "show", name }, &s) == 0)
            ssid = s.substr(0, s.find('\n'));
        if (ssid.empty()) ssid = name;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    wifi_connection_ = name;
    status_.ssid     = ssid.substr(0, 32);
}

void SystemControl::do_wifi(const protocol::WifiConfig& cfg) {
    const std::string ssid     = field(cfg.ssid, sizeof cfg.ssid);
    const std::string password = field(cfg.password, sizeof cfg.password);
    const std::string address  = field(cfg.address, sizeof cfg.address);
    const std::string gateway  = field(cfg.gateway, sizeof cfg.gateway);
    const std::string dns      = field(cfg.dns, sizeof cfg.dns);

    // the GUI checks these too; checked again because anything on the network can send this
    const std::string cidr = address.empty() ? "" : with_prefix(address);
    if (ssid.empty())                              return set_status(NetOp::Failed, "Enter the network name");
    if (!address.empty() && cidr.empty())          return set_status(NetOp::Failed, "IP address isn't valid (e.g. 192.168.1.50)");
    if (!gateway.empty() && !valid_ipv4(gateway))  return set_status(NetOp::Failed, "Gateway isn't a valid IP address");
    if (!dns.empty() && !valid_ipv4(dns))          return set_status(NetOp::Failed, "DNS server isn't a valid IP address");

    std::printf("system: joining Wi-Fi \"%s\" (%s)\n", ssid.c_str(), cidr.empty() ? "DHCP" : cidr.c_str());

    std::vector<std::string> connect = { "nmcli", "--wait", "45", "device", "wifi", "connect", ssid };
    if (!password.empty()) { connect.push_back("password"); connect.push_back(password); }
    std::string out;
    const int rc = run(connect, &out);
    if (rc == NOT_STARTED)
        return set_status(NetOp::Failed, "nmcli not found: this board doesn't use NetworkManager");
    if (rc != 0) {
        std::fprintf(stderr, "system: nmcli connect failed: %s\n", first_line(out).c_str());
        return set_status(NetOp::Failed, explain_nmcli_error(out, ssid));
    }

    // NetworkManager names the connection after the SSID (or "SSID 1" if that name was taken)
    refresh_ssid();
    std::string conn;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        conn = wifi_connection_;
    }
    if (conn.empty()) conn = ssid;

    // static IP, or back to DHCP (clears addresses left over from an earlier static setup)
    std::vector<std::string> modify = { "nmcli", "connection", "modify", conn };
    if (cidr.empty()) {
        modify.insert(modify.end(), { "ipv4.method", "auto", "ipv4.addresses", "", "ipv4.gateway", "" });
    } else {
        modify.insert(modify.end(), { "ipv4.method", "manual", "ipv4.addresses", cidr, "ipv4.gateway", gateway });
    }
    modify.insert(modify.end(), { "ipv4.dns", dns });
    if (run(modify, &out) != 0) {
        std::fprintf(stderr, "system: nmcli modify failed: %s\n", first_line(out).c_str());
        return set_status(NetOp::Failed, "Joined, but couldn't set the IP settings: " + first_line(out));
    }
    if (run({ "nmcli", "--wait", "30", "connection", "up", conn }, &out) != 0) {
        std::fprintf(stderr, "system: nmcli up failed: %s\n", first_line(out).c_str());
        return set_status(NetOp::Failed, "Joined, but reconnecting with the IP settings failed: " + first_line(out));
    }
    std::printf("system: on Wi-Fi \"%s\"\n", ssid.c_str());
    set_status(NetOp::Done, "Connected to \"" + ssid + "\"");
}

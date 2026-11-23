#pragma once
#include <common/protocol.h>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

// Board-level actions the touchscreen asks for, done by the firmware because it runs as root
// (the GUI runs as `debian`). Everything happens on a worker thread, so the race loop never waits.
//
//   Wi-Fi    nmcli (NetworkManager). If the board image doesn't use NetworkManager, the
//            Settings screen says so instead of failing silently.
//   Restart  systemctl --no-block restart gui firmware: the GUI and this program come back
//            in a few seconds.
//
// Programs are started directly with an argument list, never through a shell, so an SSID or
// password can't inject commands. The password is never logged.
class SystemControl {
public:
    struct Status {
        protocol::NetOp op = protocol::NetOp::Idle;
        std::string     message;   // shown under the Wi-Fi form
        std::string     ssid;      // network the board is on now, "" = none / unknown
    };

    SystemControl();
    ~SystemControl();              // waits for a Wi-Fi change in progress (nmcli times out after ~45 s)
    SystemControl(const SystemControl&) = delete;
    SystemControl& operator=(const SystemControl&) = delete;

    void   set_wifi(const protocol::WifiConfig& cfg);   // ignored, with a message, while one is running
    void   restart_software();
    Status status() const;

private:
    void worker();
    void do_wifi(const protocol::WifiConfig& cfg);
    void do_restart();
    void refresh_ssid();           // every REFRESH_EVERY, so the Settings screen shows the current network
    void set_status(protocol::NetOp op, const std::string& message);

    mutable std::mutex      mtx_;
    std::condition_variable cv_;
    bool                    running_ = true;
    std::optional<protocol::WifiConfig> wifi_job_;
    bool                    restart_job_ = false;
    Status                  status_;
    std::string             wifi_connection_;   // NetworkManager connection name of the active Wi-Fi
    bool                    warned_no_nmcli_ = false;
    std::thread             thread_;            // last: starts after everything above is set up
};

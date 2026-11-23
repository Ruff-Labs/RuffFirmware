#pragma once
#include <cstdint>

// Everything the firmware and the GUI both need, in one place, so the two can't drift apart.
//
// Two UDP links, both plain structs sent as raw bytes (every target is little-endian):
//   firmware -> GUI   RaceStatus on kGuiPort, every 50 ms and on every state change
//   GUI -> firmware   commands on kRemotePort: one Command byte, or a SetupPacket / WifiPacket
// Change a packet's layout -> bump kProtocolVersion, so an old GUI ignores the new packets
// instead of showing garbage.
namespace protocol {

constexpr uint16_t kGuiPort    = 5555;   // firmware -> GUI: RaceStatus
constexpr uint16_t kRemotePort = 5556;   // GUI -> firmware: Command, SetupPacket, WifiPacket

constexpr uint32_t kStatusMagic     = 0x46465552;   // "RUFF"
constexpr uint8_t  kProtocolVersion = 2;

constexpr int    kNumLanes    = 2;      // [0] = lane 1 (LEFT), [1] = lane 2 (RIGHT)
constexpr int    kLaneNameLen = 32;     // bytes incl. the terminating 0 (UTF-8)
constexpr int    kVersionLen  = 24;     // "0.1.0+ade38ea"
constexpr int    kMessageLen  = 80;     // network status line for the Settings screen
constexpr double kRaceDnf     = -1.0;   // race time for a lane with no final time

// ---------- shared enums ----------
enum class RaceState : uint8_t {
    Idle,
    Ready,
    Sequence,
    Running,
    FalseStart,
    Finished,
    Reset,       // "Reset Heat": clears the clock/times, KEEPS false start counts + red lights
    ResetRace    // "Reset Race": clears everything, including false starts and red lights
};

inline const char* race_state_name(RaceState s) {
    switch (s) {
        case RaceState::Idle:       return "Idle";
        case RaceState::Ready:      return "Ready";
        case RaceState::Sequence:   return "Sequence";
        case RaceState::Running:    return "Running";
        case RaceState::FalseStart: return "False start";
        case RaceState::Finished:   return "Finished";
        case RaceState::Reset:      return "Reset";
        case RaceState::ResetRace:  return "Reset race";
    }
    return "Unknown";
}

// A false start doesn't take a lane out of the heat, so there's no FalseStart status:
// it shows up as a negative start_ms instead.
enum class LaneStatus : uint8_t { Waiting, Running, Finished, DNF };

// Sensor problems the firmware can tell from the signals alone (SensorHealth.hpp).
enum class SensorFault : uint8_t {
    None,
    Blocked,   // FORWARD or BACK broken for 10 s+: something in the beam, or a stuck sensor
    Wiring,    // ALIGN, FORWARD and BACK all active at once: can't happen with a working sensor
};

// Progress of the last Wi-Fi change from the Settings screen.
enum class NetOp : uint8_t { Idle, Working, Done, Failed };

// ---------- GUI -> firmware ----------
// The first byte of every packet. 1-5 are the judge remote's buttons (the GUI's Remote screen
// stands in for the RF remote); the rest are head-table commands that only the GUI sends.
enum class Command : uint8_t {
    Ready     = 1,
    Start     = 2,
    Stop      = 3,
    Reset     = 4,
    Abort     = 5,
    ResetRace = 6,   // new race: clears false starts and red lights too
    SetupRace = 7,   // SetupPacket: lane names + heats, then Reset Race (ignored mid-heat)
    SetWifi   = 8,   // WifiPacket: join a Wi-Fi network (nmcli, run by the firmware as root)
    Restart   = 9,   // restart the race software (firmware + GUI services)
};

#pragma pack(push, 1)

struct RaceSetup {
    uint8_t heats = 0;                                  // 0 = not set up yet
    char    lane_names[kNumLanes][kLaneNameLen] = {};   // UTF-8, always 0-terminated, "" = not set
};

struct SetupPacket {
    uint8_t   command = uint8_t(Command::SetupRace);
    RaceSetup setup;
};

// All strings 0-terminated. Blank address = DHCP; address may carry a prefix ("192.168.1.50/24").
struct WifiConfig {
    char ssid[33]     = {};
    char password[64] = {};   // blank = open network
    char address[19]  = {};
    char gateway[16]  = {};
    char dns[16]      = {};
};

struct WifiPacket {
    uint8_t    command = uint8_t(Command::SetWifi);
    WifiConfig wifi;
};

// ---------- firmware -> GUI ----------
struct RaceStatus {
    uint32_t magic   = kStatusMagic;
    uint8_t  version = kProtocolVersion;

    uint8_t  state       = 0;              // RaceState
    uint8_t  judge_ready = 0;
    double   clock_ms    = 0.0;            // Timer::elapsed_ms(): negative in countdown, frozen after STOP
    int8_t   winner      = -1;             // -1 = none, else lane index

    uint8_t  lane_in_use[kNumLanes]  = {};  // enabled in LANE_PINS (0 = not wired, one-lane mode)
    uint8_t  aligned[kNumLanes]      = {};  // ALIGN high
    uint8_t  forward[kNumLanes]      = {};  // FORWARD broken right now
    uint8_t  back[kNumLanes]         = {};  // BACK broken right now
    uint8_t  sensor_fault[kNumLanes] = {};  // SensorFault
    uint8_t  lane_status[kNumLanes]  = {};  // LaneStatus
    uint8_t  n_splits[kNumLanes]     = {};
    double   race_ms[kNumLanes]      = {};  // final time; only meaningful when lane_status == Finished
    uint8_t  has_start[kNumLanes]    = {};  // start dog's first trigger seen
    double   start_ms[kNumLanes]     = {};  // reaction time vs green (negative = false start)
    uint8_t  false_starts[kNumLanes] = {};

    RaceSetup setup;                        // what the Setup screen last sent

    char     firmware_version[kVersionLen] = {};
    uint8_t  net_op = 0;                    // NetOp of the last Wi-Fi change
    char     net_message[kMessageLen] = {}; // e.g. "Connected to TrackWiFi", "Wrong password?"
    char     wifi_ssid[33] = {};            // network the board is on now, "" = none
};

#pragma pack(pop)

}  // namespace protocol

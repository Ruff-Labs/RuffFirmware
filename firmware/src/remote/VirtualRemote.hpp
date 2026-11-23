#pragma once
#include <common/protocol.h>
#include <cstdint>

enum class RemoteCmd;   // Remote.hpp

// The GUI -> firmware link: UDP on protocol::kRemotePort, one packet per command (a
// protocol::Command byte, or a SetupPacket / WifiPacket). Remote owns one and feeds its commands
// through the same code as the RF buttons. Bound to any address, so a laptop GUI can drive the
// board too (anyone on the board's network can, so keep the board on the event's own network).
//
// The judge-remote buttons here are for testing until the RF receiver works; then keep them as
// a backup remote or remove them (and the GUI's Remote screen). Head-table commands stay.
class VirtualRemote {
public:
    // What came with a SetupRace / SetWifi command
    struct Payload {
        protocol::RaceSetup  setup;
        protocol::WifiConfig wifi;
    };

    ~VirtualRemote();

    // Opens the non-blocking UDP socket. False (logged) on failure: no GUI commands then.
    bool begin(uint16_t port = protocol::kRemotePort);

    // The next waiting command and its arrival time (CLOCK_MONOTONIC ns, like the sensors), or
    // false if none. Packets of the wrong size are logged and skipped, and every string is
    // 0-terminated whatever the sender did. Non-blocking: call in a loop until it returns false.
    bool next(RemoteCmd& cmd, uint64_t& t_ns, Payload& payload);

private:
    int fd_ = -1;
};

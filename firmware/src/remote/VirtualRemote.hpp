#pragma once
#include <common/protocol.h>
#include <cstdint>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum class RemoteCmd;   // defined in Remote.hpp

// Virtual remote: button presses from the GUI instead of the RF receiver.
// For testing the rest of the firmware before the receiver is wired up.
//
// The GUI sends one UDP packet per press (one byte, protocol::RemoteButton) to
// protocol::kRemotePort. Remote owns one of these and feeds its presses through the
// same code as the real buttons, so the state machine can't tell the difference.
//
// TODO: delete this (and the GUI's Remote screen) once the real receiver works,
//       or keep it as a backup remote for the head table.
class VirtualRemote {
public:
    ~VirtualRemote();
    bool begin(uint16_t port = protocol::kRemotePort);   // open the UDP socket; false on failure

    // Non-blocking. Returns the next waiting press (and when it arrived) or false if none.
    // Call in a loop until it returns false.
    bool next(RemoteCmd& cmd, uint64_t& t_ns);

private:
    int fd_ = -1;
};

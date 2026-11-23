#include "remote/VirtualRemote.hpp"
#include "remote/Remote.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint64_t mono_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1'000'000'000ull + uint64_t(ts.tv_nsec);
}

VirtualRemote::~VirtualRemote() {
    if (fd_ >= 0) close(fd_);
}

bool VirtualRemote::begin(uint16_t port) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        std::fprintf(stderr, "virtual remote: socket failed: %s\n", std::strerror(errno));
        return false;
    }
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "virtual remote: bind to port %u failed: %s\n", port, std::strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }
    std::printf("virtual remote: listening on UDP port %u\n", port);
    return true;
}

// copies a fixed-size packet out of buf; false (logged) if the size is wrong
template <typename Packet>
static bool read_packet(const uint8_t* buf, ssize_t n, Packet& p, const char* what) {
    if (size_t(n) != sizeof(Packet)) {
        std::fprintf(stderr, "virtual remote: %s packet is %zd bytes, expected %zu "
                             "(GUI and firmware built from different versions?), ignored\n",
                     what, n, sizeof(Packet));
        return false;
    }
    std::memcpy(&p, buf, sizeof p);
    return true;
}

template <size_t N>
static void terminate(char (&s)[N]) { s[N - 1] = '\0'; }

bool VirtualRemote::next(RemoteCmd& cmd, uint64_t& t_ns, Payload& payload) {
    if (fd_ < 0) return false;

    uint8_t buf[sizeof(protocol::WifiPacket) + sizeof(protocol::SetupPacket)];   // bigger than any packet
    ssize_t n;
    while ((n = recv(fd_, buf, sizeof buf, 0)) > 0) {   // -1 + EAGAIN: nothing waiting
        t_ns = mono_ns();
        const uint8_t byte = buf[0];
        using protocol::Command;
        switch (static_cast<Command>(byte)) {
            case Command::Ready:     cmd = RemoteCmd::Ready;     break;
            case Command::Start:     cmd = RemoteCmd::Start;     break;
            case Command::Stop:      cmd = RemoteCmd::Stop;      break;
            case Command::Reset:     cmd = RemoteCmd::Reset;     break;
            case Command::Abort:     cmd = RemoteCmd::Abort;     break;
            case Command::ResetRace: cmd = RemoteCmd::ResetRace; break;
            case Command::Restart:   cmd = RemoteCmd::Restart;   break;
            case Command::SetupRace: {
                protocol::SetupPacket p;
                if (!read_packet(buf, n, p, "setup")) continue;
                payload.setup = p.setup;
                for (auto& name : payload.setup.lane_names) terminate(name);
                cmd = RemoteCmd::SetupRace;
                std::printf("virtual remote: setup \"%s\" vs \"%s\", %u heats\n",
                            payload.setup.lane_names[0], payload.setup.lane_names[1], payload.setup.heats);
                return true;
            }
            case Command::SetWifi: {
                protocol::WifiPacket p;
                if (!read_packet(buf, n, p, "wifi")) continue;
                payload.wifi = p.wifi;
                terminate(payload.wifi.ssid);
                terminate(payload.wifi.password);
                terminate(payload.wifi.address);
                terminate(payload.wifi.gateway);
                terminate(payload.wifi.dns);
                cmd = RemoteCmd::SetWifi;
                return true;   // not logged here: SystemControl logs it, without the password
            }
            default:
                std::fprintf(stderr, "virtual remote: unknown command byte %u, ignored\n", byte);
                continue;
        }
        std::printf("virtual remote: command %u\n", byte);
        return true;
    }
    return false;
}

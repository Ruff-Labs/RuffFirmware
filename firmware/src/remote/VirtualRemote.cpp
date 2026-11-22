// VirtualRemote.cpp  —  judge remote buttons from the GUI (UDP), for testing without the RF receiver
#include "remote/VirtualRemote.hpp"
#include "remote/Remote.hpp"


// same clock as gpiod edge timestamps and Timer::now_ns(), so a STOP press time can go to Timer::stop()
static uint64_t mono_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1'000'000'000ull + uint64_t(ts.tv_nsec);
}

VirtualRemote::~VirtualRemote() {
    if (fd_ >= 0) close(fd_);
}

// ---------- setup ----------
bool VirtualRemote::begin(uint16_t port) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        std::fprintf(stderr, "virtual remote: socket failed: %s\n", std::strerror(errno));
        return false;
    }
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);   // poll() must never wait

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // any address, so a GUI on a laptop can drive the board too

    if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "virtual remote: bind to port %u failed: %s\n", port, std::strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }
    std::printf("virtual remote: listening on UDP port %u\n", port);
    return true;
}

// ---------- per-tick read (non-blocking) ----------
bool VirtualRemote::next(RemoteCmd& cmd, uint64_t& t_ns, protocol::RaceSetup& setup) {
    if (fd_ < 0) return false;

    // one datagram per recv(); bigger than any packet so a too-long one is caught, not cut
    uint8_t buf[sizeof(protocol::SetupPacket) + 16];
    ssize_t n;
    while ((n = recv(fd_, buf, sizeof buf, 0)) > 0) {   // -1 with EAGAIN = nothing waiting
        t_ns = mono_ns();                   // arrival time (a few ms after the tap, fine for testing)
        const uint8_t byte = buf[0];
        using protocol::Command;
        switch (static_cast<Command>(byte)) {
            case Command::Ready:     cmd = RemoteCmd::Ready;     break;
            case Command::Start:     cmd = RemoteCmd::Start;     break;
            case Command::Stop:      cmd = RemoteCmd::Stop;      break;
            case Command::Reset:     cmd = RemoteCmd::Reset;     break;
            case Command::Abort:     cmd = RemoteCmd::Abort;     break;
            case Command::ResetRace: cmd = RemoteCmd::ResetRace; break;
            case Command::SetupRace: {
                if (size_t(n) != sizeof(protocol::SetupPacket)) {
                    std::fprintf(stderr, "virtual remote: setup packet is %zd bytes, expected %zu "
                                         "(GUI and firmware built from different versions?), ignored\n",
                                 n, sizeof(protocol::SetupPacket));
                    continue;
                }
                protocol::SetupPacket p;
                std::memcpy(&p, buf, sizeof p);
                setup = p.setup;
                for (auto& name : setup.lane_names) name[protocol::kLaneNameLen - 1] = '\0';   // never trust the 0
                cmd = RemoteCmd::SetupRace;
                std::printf("virtual remote: setup \"%s\" vs \"%s\", %u heats\n",
                            setup.lane_names[0], setup.lane_names[1], setup.heats);
                return true;
            }
            default:
                std::fprintf(stderr, "virtual remote: unknown command byte %u, ignored\n", byte);
                continue;                   // try the next packet
        }
        std::printf("virtual remote: command %u\n", byte);
        return true;
    }
    return false;
}

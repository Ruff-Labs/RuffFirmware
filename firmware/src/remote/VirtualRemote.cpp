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
bool VirtualRemote::next(RemoteCmd& cmd, uint64_t& t_ns) {
    if (fd_ < 0) return false;

    uint8_t byte;
    while (recv(fd_, &byte, 1, 0) == 1) {   // -1 with EAGAIN = nothing waiting
        t_ns = mono_ns();                   // arrival time (a few ms after the tap, fine for testing)
        switch (static_cast<protocol::RemoteButton>(byte)) {
            case protocol::RemoteButton::Ready: cmd = RemoteCmd::Ready; break;
            case protocol::RemoteButton::Start: cmd = RemoteCmd::Start; break;
            case protocol::RemoteButton::Stop:  cmd = RemoteCmd::Stop;  break;
            case protocol::RemoteButton::Reset: cmd = RemoteCmd::Reset; break;
            case protocol::RemoteButton::Abort: cmd = RemoteCmd::Abort; break;
            default:
                std::fprintf(stderr, "virtual remote: unknown button byte %u, ignored\n", byte);
                continue;                   // try the next packet
        }
        std::printf("virtual remote: button %u\n", byte);
        return true;
    }
    return false;
}

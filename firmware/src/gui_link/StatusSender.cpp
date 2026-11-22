// StatusSender.cpp  —  race status to the GUI over UDP (see StatusSender.hpp)
#include "gui_link/StatusSender.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

static bool make_dest(const char* host, sockaddr_in& out) {
    out = {};
    out.sin_family = AF_INET;
    out.sin_port   = htons(protocol::kGuiPort);
    return inet_pton(AF_INET, host, &out.sin_addr) == 1;
}

StatusSender::~StatusSender() {
    if (fd_ >= 0) close(fd_);
}

bool StatusSender::begin() {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        std::fprintf(stderr, "gui link: socket failed: %s (GUI gets no status)\n", std::strerror(errno));
        return false;
    }

    sockaddr_in d;
    make_dest("127.0.0.1", d);
    dest_.push_back(d);

    if (const char* host = std::getenv("GUI_HOST"); host && *host) {
        if (make_dest(host, d)) {
            dest_.push_back(d);
            std::printf("gui link: also sending status to %s:%u\n", host, protocol::kGuiPort);
        } else {
            std::fprintf(stderr, "gui link: GUI_HOST=%s isn't an IPv4 address, ignored\n", host);
        }
    }
    return true;
}

void StatusSender::send(const protocol::RaceStatus& status) {
    if (fd_ < 0) return;
    for (const sockaddr_in& d : dest_) {
        const ssize_t n = sendto(fd_, &status, sizeof status, MSG_DONTWAIT,
                                 reinterpret_cast<const sockaddr*>(&d), sizeof d);
        // ECONNREFUSED: nothing listening (GUI not started yet), the normal case at boot
        if (n < 0 && errno != ECONNREFUSED && errno != EAGAIN && errno != last_errno_) {
            last_errno_ = errno;
            std::fprintf(stderr, "gui link: send failed: %s\n", std::strerror(errno));
        }
    }
}

#include "radio/SerialLink.hpp"
#include <radio_protocol.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

constexpr size_t MAX_QUEUED = 4096;   // frames waiting for take(): only reached if the race loop stalls

uint64_t monotonic_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1'000'000'000ull + uint64_t(ts.tv_nsec);
}

SerialLink::SerialLink(std::string device) : device_(std::move(device)) {
    thread_ = std::thread([this] { run(); });
}

SerialLink::~SerialLink() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<SerialLink::Frame> SerialLink::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Frame> out;
    out.swap(frames_);
    return out;
}

// Raw 8N1 at kSerialBaud, non-blocking. HUPCL off, so closing the port doesn't reset the Nano.
int SerialLink::open_port() const {
    const int fd = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    termios tio{};
    if (tcgetattr(fd, &tio) != 0) { ::close(fd); return -1; }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~HUPCL;
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;
    static_assert(radio::kSerialBaud == 115200, "update the termios speed");
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    if (tcsetattr(fd, TCSANOW, &tio) != 0) { ::close(fd); return -1; }
    tcflush(fd, TCIFLUSH);
    return fd;
}

void SerialLink::run() {
    int          fd = -1;
    FrameDecoder decoder;
    bool         warned = false;

    while (!stop_) {
        if (fd < 0) {
            fd = open_port();
            if (fd < 0) {
                if (!warned) std::fprintf(stderr, "radio: can't open %s (%s), retrying every second\n",
                                          device_.c_str(), std::strerror(errno));
                warned = true;
                for (int i = 0; i < 10 && !stop_; ++i) usleep(100'000);
                continue;
            }
            std::printf("radio: %s open\n", device_.c_str());
            warned = false;
            decoder = FrameDecoder{};
            open_ = true;
        }

        pollfd p{ fd, POLLIN, 0 };
        const int r = ::poll(&p, 1, 100);
        if (r < 0 && errno == EINTR) continue;

        uint8_t buf[256];
        ssize_t n = 0;
        if (r > 0 && (p.revents & POLLIN)) n = ::read(fd, buf, sizeof buf);
        const uint64_t t = monotonic_ns();

        const bool gone = r < 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL))
                       || (r > 0 && n == 0) || (n < 0 && errno != EAGAIN && errno != EINTR);
        if (gone) {
            std::fprintf(stderr, "radio: lost %s, reopening\n", device_.c_str());
            ::close(fd);
            fd = -1;
            open_ = false;
            usleep(500'000);
            continue;
        }
        if (n <= 0) continue;

        const unsigned bad_before = decoder.bad_frames();
        std::vector<Frame> got;
        decoder.push(buf, size_t(n), [&](const uint8_t* body, size_t len) {
            got.push_back({ t, std::vector<uint8_t>(body, body + len) });
        });
        bad_ += decoder.bad_frames() - bad_before;

        if (!got.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& f : got)
                if (frames_.size() < MAX_QUEUED) frames_.push_back(std::move(f));
        }
    }
    if (fd >= 0) ::close(fd);
    open_ = false;
}

#pragma once
#include "radio/FrameDecoder.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The base station's USB serial port, read on its own thread so each frame gets an exact arrival
// time (CLOCK_MONOTONIC, taken the moment read() returns), not "whenever the race loop got to it".
// If the port isn't there or disappears (USB unplugged), it keeps trying to open it every second.
class SerialLink {
public:
    struct Frame {
        uint64_t             t_rx_ns;   // when it arrived
        std::vector<uint8_t> body;      // CRC checked and removed
    };

    explicit SerialLink(std::string device);   // starts the thread
    ~SerialLink();                             // stops it and closes the port
    SerialLink(const SerialLink&) = delete;
    SerialLink& operator=(const SerialLink&) = delete;

    // Every frame since the last call, oldest first. Never blocks for long.
    std::vector<Frame> take();

    bool     is_open() const { return open_; }
    unsigned bad_frames() const { return bad_; }

private:
    void run();
    int  open_port() const;   // -1 if it can't

    const std::string  device_;
    std::atomic<bool>  stop_{ false };
    std::atomic<bool>  open_{ false };
    std::atomic<unsigned> bad_{ 0 };
    std::mutex         mutex_;
    std::vector<Frame> frames_;
    std::thread        thread_;
};

uint64_t monotonic_ns();   // CLOCK_MONOTONIC, the clock the race timer uses

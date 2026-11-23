#pragma once
#include <radio_frame.h>
#include <cstddef>
#include <cstdint>
#include <vector>

// Splits the base station's serial byte stream into frames (radio_frame.h): collects bytes up to
// each 0, undoes COBS and checks the CRC. Bad frames are counted and dropped; the next 0 starts
// over, so a lost or garbled byte costs one frame.
class FrameDecoder {
public:
    static constexpr size_t kMaxEncoded = 80;   // longer = noise, dropped until the next 0

    // Call with each chunk read from the port. on_frame(const uint8_t* body, size_t n) gets every
    // good frame, CRC removed.
    template <class F>
    void push(const uint8_t* data, size_t n, F&& on_frame) {
        for (size_t i = 0; i < n; ++i) {
            const uint8_t b = data[i];
            if (b != 0) {
                if (buf_.size() < kMaxEncoded) buf_.push_back(b);
                else                           overflow_ = true;
                continue;
            }
            if (!buf_.empty()) {
                uint8_t out[kMaxEncoded];
                const size_t len = overflow_ ? 0 : radio::cobs_decode(buf_.data(), buf_.size(), out);
                if (len >= 3 && radio::crc16(out, len - 2) == uint16_t(out[len - 2] | out[len - 1] << 8))
                    on_frame(static_cast<const uint8_t*>(out), len - 2);
                else
                    ++bad_;
            }
            buf_.clear();
            overflow_ = false;
        }
    }

    unsigned bad_frames() const { return bad_; }

private:
    std::vector<uint8_t> buf_;
    bool     overflow_ = false;
    unsigned bad_      = 0;
};

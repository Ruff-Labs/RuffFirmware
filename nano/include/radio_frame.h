#pragma once
#include <stddef.h>
#include <stdint.h>

// Serial framing for the base station -> BeaglePlay link (radio_protocol.h). Header-only and
// free of the standard library so the Nano build can use it too.
//
// A frame is [bytes][crc16 lo][crc16 hi], COBS-encoded so it contains no 0 byte, then a 0 to end
// it. A byte lost on the wire costs one frame (bad CRC); the next 0 resynchronises.
namespace radio {

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
inline uint16_t crc16(const uint8_t* p, size_t n) {
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= uint16_t(*p++) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

// Encodes n bytes into out (room for n + n/254 + 1). Returns the encoded length, without the 0.
inline size_t cobs_encode(const uint8_t* in, size_t n, uint8_t* out) {
    size_t  code_at = 0, o = 1;
    uint8_t code = 1;
    for (size_t i = 0; i < n; ++i) {
        if (in[i] == 0) {
            out[code_at] = code;
            code_at = o++;
            code = 1;
        } else {
            out[o++] = in[i];
            if (++code == 0xFF) {
                out[code_at] = code;
                code_at = o++;
                code = 1;
            }
        }
    }
    out[code_at] = code;
    return o;
}

// Decodes n bytes (no trailing 0) into out (room for n). Returns the decoded length, or 0 if the
// input isn't valid COBS.
inline size_t cobs_decode(const uint8_t* in, size_t n, uint8_t* out) {
    size_t i = 0, o = 0;
    while (i < n) {
        const uint8_t code = in[i++];
        if (code == 0 || i + code - 1 > n) return 0;
        for (uint8_t k = 1; k < code; ++k) out[o++] = in[i++];
        if (code != 0xFF && i < n) out[o++] = 0;
    }
    return o;
}

// Builds a whole frame (CRC, COBS, closing 0) into out, which needs n + 4 bytes (n <= 62).
// Returns the number of bytes to send, 0 if n is too long.
inline size_t make_frame(const uint8_t* body, size_t n, uint8_t* out) {
    uint8_t raw[64];                       // frames here are at most ~40 bytes (radio_protocol.h)
    if (n > sizeof raw - 2) return 0;
    for (size_t i = 0; i < n; ++i) raw[i] = body[i];
    const uint16_t crc = crc16(body, n);
    raw[n]     = uint8_t(crc & 0xFF);
    raw[n + 1] = uint8_t(crc >> 8);
    const size_t len = cobs_encode(raw, n + 2, out);
    out[len] = 0;
    return len + 1;
}

}  // namespace radio

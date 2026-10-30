#pragma once
#include <cstdint>
//TODO: change this is gonna be the message exchange between the firmware and gui
namespace protocol {

enum class MsgType : uint8_t {
    Status      = 0x01,
    SetLed      = 0x02,
    SensorData  = 0x03,
};

struct SensorData {
    uint32_t timestamp_ms;
    float    temperature_c;
    uint16_t adc_raw;
};

constexpr uint16_t kGuiPort  = 5555;   // if they talk over a socket
constexpr int      kMaxLeds  = 8;

// Virtual remote (testing without the RF receiver): the GUI sends ONE byte per button press
// as a UDP packet to the firmware on this port. The byte is a RemoteButton value.
constexpr uint16_t kRemotePort = 5556;

enum class RemoteButton : uint8_t {
    Ready = 1,
    Start = 2,
    Stop  = 3,
    Reset = 4,
    Abort = 5,
};

}  // namespace protocol
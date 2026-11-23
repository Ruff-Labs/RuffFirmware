// SensorHealth (firmware/src/sensors) + what make_status() reports about sensors.
#include "test.hpp"
#include "sensors/SensorHealth.hpp"
#include "state_machine/state_machine.hpp"

using namespace std::chrono_literals;
using protocol::SensorFault;

static const SensorHealth::Clock::time_point T0{ std::chrono::seconds(1000) };

TEST(health_clear_lane_has_no_fault) {
    SensorHealth h;
    h.update(0, true, false, false, T0);
    CHECK(h.fault(0, T0 + 1h) == SensorFault::None);
}

TEST(health_dog_crossing_is_not_blocked) {
    SensorHealth h;
    h.update(0, false, true, false, T0);
    h.update(0, false, false, false, T0 + 400ms);   // dog cleared the beam
    CHECK(h.fault(0, T0 + 30s) == SensorFault::None);
}

TEST(health_beam_broken_10s_is_blocked_and_clears) {
    SensorHealth h;
    h.update(1, false, false, true, T0);
    CHECK(h.fault(1, T0 + 9s) == SensorFault::None);
    CHECK(h.fault(1, T0 + 10s) == SensorFault::Blocked);
    CHECK(h.fault(0, T0 + 10s) == SensorFault::None);   // other lane unaffected
    h.update(1, false, false, false, T0 + 11s);
    CHECK(h.fault(1, T0 + 11s) == SensorFault::None);
}

TEST(health_all_three_active_is_wiring_after_200ms) {
    SensorHealth h;
    h.update(0, true, true, true, T0);
    CHECK(h.fault(0, T0 + 100ms) == SensorFault::None);   // a glitch
    CHECK(h.fault(0, T0 + 200ms) == SensorFault::Wiring);
    CHECK(h.fault(0, T0 + 20s) == SensorFault::Wiring);   // wiring wins over blocked
}

TEST(status_reports_sensors_version_and_network) {
    RaceMachine m; RaceInputs in; Timer t; SensorHealth h;
    in.lane_in_use[1] = false;
    in.lane_aligned[0] = true; in.back_high[0] = true;
    in.back_high[1] = true;                                // unused lane: never a fault
    h.update(0, true, false, true, T0);
    h.update(1, false, false, true, T0);
    SystemControl::Status sys;
    sys.op = protocol::NetOp::Done; sys.message = "Connected to Track"; sys.ssid = "Track";
    const protocol::RaceStatus s = make_status(m, in, t, h, sys, T0 + 11s);
    CHECK(s.lane_in_use[0] == 1 && s.lane_in_use[1] == 0);
    CHECK(s.aligned[0] == 1 && s.back[0] == 1 && s.forward[0] == 0);
    CHECK(s.sensor_fault[0] == uint8_t(SensorFault::Blocked));
    CHECK(s.sensor_fault[1] == uint8_t(SensorFault::None));
    CHECK(s.firmware_version[0] != '\0');
    CHECK(s.net_op == uint8_t(protocol::NetOp::Done));
    CHECK(std::string(s.wifi_ssid) == "Track");
    CHECK(s.version == protocol::kProtocolVersion);
}

TEST(health_offline_wins_over_other_faults) {
    SensorHealth h;
    h.update(0, true, true, true, T0);
    h.set_online(false);
    CHECK(h.fault(0, T0 + 1s) == SensorFault::Offline);
    h.set_online(true);
    CHECK(h.fault(0, T0 + 1s) == SensorFault::Wiring);
}

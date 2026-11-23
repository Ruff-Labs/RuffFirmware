// The sensor radio link (firmware/src/radio): serial framing, the clock fit, and RadioSensors fed
// by a simulated base station + sensor node whose clocks drift, with lost pings, radio retries
// and USB delays. Everything runs on simulated time.
#include "test.hpp"
#include "radio/ClockFit.hpp"
#include "radio/FrameDecoder.hpp"
#include "radio/RadioSensors.hpp"
#include <radio_frame.h>
#include <radio_protocol.h>
#include <cmath>
#include <cstring>
#include <deque>
#include <random>
#include <vector>

using namespace radio;

// ---------- framing ----------
TEST(cobs_round_trip_with_zeros_and_long_runs) {
    std::vector<uint8_t> in(600);
    for (size_t i = 0; i < in.size(); ++i) in[i] = (i % 97 == 0) ? 0 : uint8_t(i * 7 + 1);
    for (size_t i = 100; i < 400; ++i) in[i] = 0x55;     // a run past 254 bytes with no 0
    std::vector<uint8_t> enc(in.size() + in.size() / 254 + 2), dec(enc.size());
    const size_t n = cobs_encode(in.data(), in.size(), enc.data());
    for (size_t i = 0; i < n; ++i) CHECK(enc[i] != 0);
    CHECK(cobs_decode(enc.data(), n, dec.data()) == in.size());
    CHECK(std::memcmp(dec.data(), in.data(), in.size()) == 0);
}

TEST(frames_survive_byte_by_byte_reads_and_a_corrupt_frame) {
    const uint8_t a[] = { 1, 0, 2, 0, 0, 3 }, b[] = { 9, 8, 7 };
    std::vector<uint8_t> stream(64);
    size_t n = make_frame(a, sizeof a, stream.data());
    std::vector<uint8_t> bad(stream.begin(), stream.begin() + n);
    bad[2] ^= 0x10;                                       // flip a bit: CRC must catch it
    n += make_frame(b, sizeof b, stream.data() + n);
    stream.resize(n);
    std::vector<uint8_t> all = { 0x42, 0x13 };            // noise before the first 0
    all.push_back(0);
    all.insert(all.end(), bad.begin(), bad.end());
    all.insert(all.end(), stream.begin(), stream.end());

    FrameDecoder dec;
    std::vector<std::vector<uint8_t>> got;
    for (uint8_t byte : all)
        dec.push(&byte, 1, [&](const uint8_t* p, size_t len) { got.emplace_back(p, p + len); });
    CHECK(got.size() == 2);
    CHECK(dec.bad_frames() == 2);                         // the noise and the flipped frame
    if (got.size() == 2) {
        CHECK(got[0] == std::vector<uint8_t>(a, a + sizeof a));
        CHECK(got[1] == std::vector<uint8_t>(b, b + sizeof b));
    }
}

// ---------- clock fit ----------
TEST(clock_fit_follows_drift_through_jitter) {
    std::mt19937 rng(1);
    std::exponential_distribution<double> delay(1.0 / 2000);   // us, mostly small, sometimes large
    ClockFit fit;
    const double drift = -0.006, offset = 123456.0;            // a Nano running 0.6 % slow
    for (double x = 0; x < 40e6; x += 100e3) {
        const double d = 200 + delay(rng);
        fit.add(x, x * (1 + drift) + offset + d, d);
    }
    CHECK(fit.ready());
    CHECK_NEAR(fit.drift_ppm(), (1 / (1 + drift) - 1) * 1e6, 20);   // the source runs fast vs y
    // half a second past the last sample. The fit follows the quickest samples: the 200 us floor
    // plus the best of ~10 delays per window (~200 us on average).
    const double x = 40.5e6;
    CHECK_NEAR(fit.convert(x), x * (1 + drift) + offset + 400, 150);
}

TEST(unwrap32_crosses_the_rollover_and_accepts_older_values) {
    Unwrap32 u;
    CHECK(u(0xFFFFFF00u) == 0xFFFFFF00ull);
    CHECK(u(0x00000010u) == 0x100000010ull);    // wrapped
    CHECK(u(0xFFFFFFF0u) == 0xFFFFFFF0ull);     // a bit older than the latest: before the wrap
    CHECK(u(0x00000020u) == 0x100000020ull);
}

// ---------- RadioSensors on a simulated link ----------
// Time is "true" time in us (= the BeaglePlay's CLOCK_MONOTONIC). The base station and node clocks
// drift from it; frames reach RadioSensors in the order the base sent them, after a USB delay.
struct Sim {
    RadioSensors rs;
    std::mt19937 rng{ 7 };

    double   base_drift = -0.002, node_drift = 0.004;   // -2000 ppm and +4000 ppm
    double   base0 = 5e6, node0 = 3e6;                  // their clocks at true time 0
    uint8_t  base_boot = 1, node_boot = 1;
    double   ping_loss = 0.1, edge_fail = 0.2;          // per attempt
    bool     node_on = true, duplicate_next = false;

    double   t = 0;                                     // now, us
    uint8_t  levels = 0;
    uint16_t seq = 0;
    uint8_t  ping_id = 0;
    struct Queued { uint16_t seq; uint8_t code; uint32_t t_node; };
    std::deque<Queued> node_queue;
    struct Wire { double t_rx; std::vector<uint8_t> body; };
    std::deque<Wire> wire;
    double last_rx = 0;
    std::vector<std::pair<int, uint64_t>> passes;       // lane, t_pass ns

    uint32_t base_at(double tt) const { return uint32_t(uint64_t(base0 + tt * (1 + base_drift))); }
    uint32_t node_at(double tt) const { return uint32_t(uint64_t(node0 + tt * (1 + node_drift))); }
    double   uniform() { return std::uniform_real_distribution<double>(0, 1)(rng); }

    template <class T> void emit(double at, const T& pkt) { emit_bytes(at, &pkt, sizeof pkt); }
    void emit_bytes(double at, const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        const double usb = 300 + std::min(5000.0, std::exponential_distribution<double>(1.0 / 800)(rng));
        last_rx = std::max(last_rx, at + (n + 4) * 10e6 / kSerialBaud + usb);
        wire.push_back({ last_rx, std::vector<uint8_t>(b, b + n) });
    }
    // a radio packet from the node, as the base relays it
    void relay(double at, const void* pkt, size_t n) {
        std::vector<uint8_t> body = { S_RADIO, kSensorNodeId };
        body.insert(body.end(), static_cast<const uint8_t*>(pkt), static_cast<const uint8_t*>(pkt) + n);
        emit_bytes(at, body.data(), body.size());
    }

    // the node sees an input change at true time `at` (must be >= t)
    void input(double at, int lane, Signal sig, bool on) {
        const uint8_t i = uint8_t(lane * NUM_SIGNALS + sig);
        levels = on ? (levels | 1 << i) : (levels & ~(1 << i));
        node_queue.push_back({ seq++, uint8_t(i | (on ? kEdgeRising : 0)), node_at(at) });
    }

    void step_1ms() {
        t += 1000;
        const long ms = long(std::lround(t / 1000));
        if (ms % 100 == 0) {
            SerialHello h; h.boot = base_boot; h.t_us = base_at(t);
            emit(t, h);
        }
        if (ms % 100 == 50 && node_on) {
            const double rx  = t + 350 + 1500 * int(uniform() * 3);   // 0-2 retries
            const double ack = rx + 450;
            SerialPingSent s; s.node = kSensorNodeId; s.id = ++ping_id;
            s.acked = uniform() >= ping_loss;
            s.t_tx_us = base_at(t); s.t_ack_us = base_at(ack);
            emit(ack, s);
            if (s.acked) {
                RadioSyncReply r; r.boot = node_boot; r.id = s.id; r.t_rx_us = node_at(rx);
                relay(ack + 3000, &r, sizeof r);
            }
        }
        if (node_on && !node_queue.empty() && uniform() >= edge_fail) {
            RadioEdges e; e.boot = node_boot; e.first_seq = node_queue.front().seq; e.count = 0;
            size_t k = 0;
            while (k < node_queue.size() && e.count < kMaxEdgesPerPacket) {
                e.edges[e.count].code = node_queue[k].code;
                e.edges[e.count].t_us = node_queue[k].t_node;
                ++e.count; ++k;
            }
            const size_t n = sizeof e - sizeof e.edges + e.count * sizeof(Edge);
            relay(t + 1500, &e, n);
            if (duplicate_next) { relay(t + 4000, &e, n); duplicate_next = false; }   // ack lost, resent
            node_queue.erase(node_queue.begin(), node_queue.begin() + long(k));
        } else if (node_on && node_queue.empty() && ms % 200 == 0) {
            RadioHeartbeat hb; hb.boot = node_boot; hb.levels = levels; hb.next_seq = seq;
            hb.dropped = 0; hb.t_us = node_at(t);
            relay(t + 1500, &hb, sizeof hb);
        }

        while (!wire.empty() && wire.front().t_rx <= t) {
            rs.feed(uint64_t(wire.front().t_rx * 1e3), wire.front().body.data(), wire.front().body.size());
            wire.pop_front();
        }
        rs.poll_at(uint64_t(t * 1e3));
        for (int l = 0; l < NUM_LANES; ++l)
            if (rs.lane(l).passed) passes.push_back({ l, rs.lane(l).t_pass });
    }
    void run_ms(int n) { for (int i = 0; i < n; ++i) step_1ms(); }

    // a dog through lane 0 at true time `at`: FORWARD breaks, then BACK, both clear 30 ms later
    void dog(double at) {
        input(at,           0, FORWARD, true);
        input(at + 15000,   0, BACK,    true);
        input(at + 30000,   0, FORWARD, false);
        input(at + 45000,   0, BACK,    false);
    }
};

TEST(radio_sensors_come_online_once_both_clocks_are_fitted) {
    Sim s;
    s.input(0, 0, ALIGN, true);
    s.run_ms(1500);
    CHECK(!s.rs.online());                     // ~1.5 windows of sync so far
    s.run_ms(3000);
    CHECK(s.rs.online());
    CHECK(s.rs.lane(0).high[ALIGN]);
    CHECK_NEAR(s.rs.node_clock().drift_ppm(), (1.004 / 0.998 - 1) * 1e6, 300);
}

TEST(radio_pass_times_are_within_1ms_despite_drift_and_jitter) {
    Sim s;
    s.run_ms(5000);
    const double dogs[] = { 10e6, 31.2345e6, 59.9e6 };
    size_t next = 0;
    while (s.t < 61e6) {
        if (next < 3 && s.t + 1000 >= dogs[next]) s.dog(dogs[next++]);
        s.step_1ms();
    }
    CHECK(s.passes.size() == 3);
    for (size_t i = 0; i < s.passes.size() && i < 3; ++i) {
        CHECK(s.passes[i].first == 0);
        CHECK_NEAR(s.passes[i].second / 1e3, dogs[i], 1000);   // us
    }
    CHECK(s.rs.stats().lost == 0);
}

TEST(radio_works_across_the_node_micros_rollover) {
    Sim s;
    s.node0 = 4294967296.0 - 20e6;             // the node's 32-bit micros() wraps 20 s in
    s.run_ms(5000);
    s.dog(30e6);
    s.run_ms(30000);
    CHECK(s.passes.size() == 1);
    if (!s.passes.empty()) CHECK_NEAR(s.passes[0].second / 1e3, 30e6, 1000);
}

TEST(radio_duplicate_packets_count_once) {
    Sim s;
    s.edge_fail = 0;
    s.run_ms(5000);
    s.duplicate_next = true;
    s.dog(s.t + 1000);
    s.run_ms(500);
    CHECK(s.passes.size() == 1);
    CHECK(s.rs.stats().duplicates > 0);
}

TEST(radio_goes_offline_without_heartbeats_and_catches_up_after) {
    Sim s;
    s.run_ms(5000);
    CHECK(s.rs.online());
    s.node_on = false;                         // out of range: the node keeps its edges
    s.dog(s.t + 2000);
    s.run_ms(1500);
    CHECK(!s.rs.online());
    CHECK(s.passes.empty());
    s.node_on = true;
    s.run_ms(1000);
    CHECK(s.rs.online());
    CHECK(s.passes.size() == 1);               // late, with its real time
    if (!s.passes.empty()) CHECK_NEAR(s.passes[0].second / 1e3, 5.002e6, 1000);
}

TEST(radio_backlog_gives_one_pass_per_poll) {
    Sim s;
    s.run_ms(5000);
    s.node_on = false;
    for (int i = 0; i < 3; ++i) s.dog(s.t + 1000 + i * 100000);
    s.run_ms(400);
    s.node_on = true;
    s.edge_fail = 0;
    s.run_ms(300);
    CHECK(s.passes.size() == 3);
    for (size_t i = 1; i < s.passes.size(); ++i)
        CHECK(s.passes[i].second - s.passes[i - 1].second > 99'000'000ull);   // 100 ms apart, in order
}

TEST(radio_heartbeat_detects_lost_edges_and_fixes_levels) {
    Sim s;
    s.run_ms(5000);
    s.input(s.t + 1000, 0, ALIGN, true);
    s.node_queue.clear();                      // that edge never reaches the BeaglePlay
    s.run_ms(500);
    CHECK(s.rs.stats().lost == 1);
    CHECK(s.rs.lane(0).high[ALIGN]);           // the heartbeat's levels put it right
}

TEST(radio_node_restart_resyncs) {
    Sim s;
    s.run_ms(5000);
    s.node_boot = 2;                           // power cycled: new clock, edge numbers from 0
    s.node0 = 77e6;
    s.seq = 0;
    s.run_ms(4000);
    CHECK(s.rs.stats().node_reboots == 1);
    CHECK(s.rs.online());
    s.dog(s.t + 1000);
    s.run_ms(200);
    CHECK(s.passes.size() == 1);
    if (!s.passes.empty()) CHECK_NEAR(s.passes[0].second / 1e3, 9.001e6, 1000);
}

TEST(radio_ignores_unwired_lanes) {
    Sim s;
    s.run_ms(5000);
    s.input(s.t + 1000, 1, ALIGN, true);       // lane 2 is off in LANE_ENABLED: its pins float
    s.run_ms(500);
    if (!LANE_ENABLED[1]) CHECK(!s.rs.lane(1).high[ALIGN]);
}

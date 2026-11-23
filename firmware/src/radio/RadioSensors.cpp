#include "radio/RadioSensors.hpp"
#include <radio_protocol.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace radio;

constexpr size_t   MAX_PENDING  = 2000;           // edges waiting for the clocks (~200 dogs): then the oldest go
constexpr uint64_t MAX_RTT_US   = 50'000;         // slower pings say nothing useful about the node's clock
constexpr double   SERIAL_US_PER_BYTE = 10e6 / kSerialBaud;   // 8N1 = 10 bits per byte

template <class T>
static bool read_as(const uint8_t* p, size_t n, T& out) {
    if (n < sizeof(T)) return false;
    std::memcpy(&out, p, sizeof(T));
    return true;
}

RadioSensors::RadioSensors(const std::string& device, double offset_us)
    : link_(std::make_unique<SerialLink>(device)), offset_us_(offset_us) {
    for (int l = 0; l < NUM_LANES; ++l)
        if (!enabled(l))
            std::printf("sensors: lane %d is disabled in LANE_ENABLED: one-lane mode, races start without it\n", l + 1);
}

RadioSensors::RadioSensors() = default;

// ---------- frames from the base station ----------
void RadioSensors::feed(uint64_t t_rx_ns, const uint8_t* p, size_t n) {
    if (n == 0) return;
    switch (p[0]) {
        case S_HELLO:     on_hello(t_rx_ns, n + 4, p, n); break;   // + CRC, COBS byte, closing 0
        case S_PING_SENT: on_ping_sent(p, n); break;
        case S_RADIO:     on_radio(t_rx_ns, p, n); break;
        default: break;
    }
}

void RadioSensors::on_hello(uint64_t t_rx_ns, size_t frame_len, const uint8_t* p, size_t n) {
    SerialHello h;
    if (!read_as(p, n, h)) return;
    if (h.version != kRadioVersion) {
        static bool warned = false;
        if (!warned) std::fprintf(stderr, "radio: base station speaks radio protocol v%u, this firmware v%u: reflash the Nanos from this commit\n",
                                  h.version, kRadioVersion);
        warned = true;
        return;
    }
    if (heard_base_ && h.boot != base_boot_) {
        std::printf("radio: base station restarted\n");
        ++stats_.base_reboots;
        base_time_.reset();
        base_fit_.clear();
        node_fit_.clear();   // it maps to the old base clock
        for (Ping& pg : pings_) pg = {};
    }
    heard_base_    = true;
    base_boot_     = h.boot;
    last_hello_ns_ = t_rx_ns;

    // The hello left the base at t_us and its last byte arrived at t_rx; the time on the wire is
    // known, the USB delay isn't (the window's lowest is kept).
    const double x = double(base_time_(h.t_us));
    const double y = t_rx_ns / 1e3 - frame_len * SERIAL_US_PER_BYTE;
    base_fit_.add(x, y, y - x);
}

void RadioSensors::on_ping_sent(const uint8_t* p, size_t n) {
    SerialPingSent s;
    if (!heard_base_ || !read_as(p, n, s) || s.node != kSensorNodeId || !s.acked) return;
    pings_[s.id % 8] = { s.id, false, base_time_(s.t_tx_us), base_time_(s.t_ack_us) };
}

void RadioSensors::node_boot(uint8_t boot) {
    if (heard_node_ && boot == node_boot_) return;
    if (heard_node_) {
        std::printf("radio: sensor node restarted\n");
        ++stats_.node_reboots;
        node_time_.reset();
        node_fit_.clear();
        pending_.clear();       // their times are on the old clock
        have_seq_    = false;
        have_levels_ = false;
    }
    heard_node_ = true;
    node_boot_  = boot;
}

void RadioSensors::on_radio(uint64_t t_rx_ns, const uint8_t* p, size_t n) {
    if (n < sizeof(SerialRadio) + 1 || p[1] != kSensorNodeId) return;
    p += sizeof(SerialRadio);
    n -= sizeof(SerialRadio);

    switch (p[0]) {
        case R_EDGES: {
            constexpr size_t header = sizeof(RadioEdges) - sizeof(RadioEdges::edges);
            RadioEdges e;
            if (n < header) return;
            std::memcpy(&e, p, std::min(n, sizeof e));
            if (e.count > kMaxEdgesPerPacket || n < header + e.count * sizeof(Edge)) return;
            node_boot(e.boot);
            for (uint8_t k = 0; k < e.count; ++k) {
                if (pending_.size() >= MAX_PENDING) { pending_.pop_front(); ++stats_.lost; }
                pending_.push_back({ false, e.boot, uint16_t(e.first_seq + k), e.edges[k].code,
                                     node_time_(e.edges[k].t_us), 0 });
            }
            break;
        }
        case R_HEARTBEAT: {
            RadioHeartbeat hb;
            if (!read_as(p, n, hb) || hb.version != kRadioVersion) return;
            node_boot(hb.boot);
            last_heartbeat_ns_ = t_rx_ns;
            pending_.push_back({ true, hb.boot, hb.next_seq, hb.levels, node_time_(hb.t_us), hb.dropped });
            break;
        }
        case R_SYNC_REPLY: {
            RadioSyncReply r;
            if (!read_as(p, n, r)) return;
            node_boot(r.boot);
            Ping& pg = pings_[r.id % 8];
            if (pg.id != r.id || pg.used || pg.t_ack < pg.t_tx) return;   // its ping-sent frame was lost
            pg.used = true;
            const uint64_t rtt = pg.t_ack - pg.t_tx;
            if (rtt > MAX_RTT_US) return;
            // the node got the ping somewhere between t_tx and t_ack: call it the middle
            node_fit_.add(double(node_time_(r.t_rx_us)), (pg.t_tx + pg.t_ack) / 2.0, double(rtt));
            break;
        }
        default: break;
    }
}

// ---------- applying edges ----------
uint64_t RadioSensors::to_monotonic_ns(uint64_t t_node_us) const {
    const double mono_us = base_fit_.convert(node_fit_.convert(double(t_node_us))) + offset_us_;
    return mono_us > 0 ? uint64_t(mono_us * 1e3) : 0;
}

bool RadioSensors::apply(const Item& it) {
    if (!have_seq_) {
        expected_seq_ = it.seq;
        have_seq_     = true;
    }
    const int16_t ahead = int16_t(it.seq - expected_seq_);

    if (it.heartbeat) {
        if (ahead < 0) return true;   // older than edges already applied
        if (ahead > 0) {              // edges sent before this heartbeat never got here
            std::fprintf(stderr, "radio: %d sensor edge(s) lost on the serial link\n", int(ahead));
            stats_.lost += unsigned(ahead);
            expected_seq_ = it.seq;
        }
        // every edge up to now is applied, so these levels are exactly what the node sees
        for (int i = 0; i < kInputs; ++i) {
            const int  lane = i / NUM_SIGNALS;
            const auto sig  = Signal(i % NUM_SIGNALS);
            const bool lv   = (it.code >> i) & 1;
            if (!enabled(lane)) continue;
            if (have_levels_ && lanes_[lane].high[sig] != lv) ++stats_.corrected;
            set_level(lane, sig, lv);
        }
        have_levels_ = true;
        if (it.dropped != stats_.node_dropped) {
            std::fprintf(stderr, "radio: sensor node buffer overflowed (%u edges dropped in total)\n", it.dropped);
            stats_.node_dropped = it.dropped;
        }
        return true;
    }

    if (ahead < 0) { ++stats_.duplicates; return true; }
    if (ahead > 0) {
        std::fprintf(stderr, "radio: %d sensor edge(s) lost on the serial link\n", int(ahead));
        stats_.lost += unsigned(ahead);
    }
    expected_seq_ = uint16_t(it.seq + 1);

    const int input = it.code & 0x0F;
    if (input >= kInputs) return true;
    const int lane = input / NUM_SIGNALS;
    if (!enabled(lane)) return true;   // an unwired lane's inputs float: ignore them
    edge(lane, Signal(input % NUM_SIGNALS), it.code & kEdgeRising, to_monotonic_ns(it.t_node_us));
    return !lanes_[lane].passed;
}

// ---------- poll ----------
void RadioSensors::poll() {
    if (link_) {
        for (const SerialLink::Frame& f : link_->take()) feed(f.t_rx_ns, f.body.data(), f.body.size());
        stats_.bad_frames = link_->bad_frames();
    }
    poll_at(monotonic_ns());   // "now" after the frames, so none is newer than it
}

void RadioSensors::poll_at(uint64_t now_ns) {
    begin_poll();
    // At most one pass per poll: after a dropout, buffered edges can hold several, and the state
    // machine takes one split per tick. The rest wait for the next tick, times intact.
    if (base_fit_.ready() && node_fit_.ready()) {
        while (!pending_.empty()) {
            const Item it = pending_.front();
            pending_.pop_front();
            if (!apply(it)) break;
        }
    }
    update_online(now_ns);
}

void RadioSensors::update_online(uint64_t now_ns) {
    const auto recent  = [now_ns](uint64_t t) { return int64_t(now_ns - t) < int64_t(kOfflineAfterNs); };
    const bool base_ok = heard_base_ && recent(last_hello_ns_);
    const bool node_ok = heard_node_ && recent(last_heartbeat_ns_);
    const bool synced  = base_fit_.ready() && node_fit_.ready() && have_levels_;
    const bool now_online = base_ok && node_ok && synced;

    if (now_online != online_) {
        if (now_online)
            std::printf("radio: sensors online (node clock drift %+.0f ppm)\n", node_fit_.drift_ppm());
        else
            std::fprintf(stderr, "radio: sensors OFFLINE: %s\n",
                         !base_ok ? "no base station on USB" : !node_ok ? "no word from the sensor node (power? range?)"
                                                             : "clocks not synced yet");
    }
    online_ = now_online;
}

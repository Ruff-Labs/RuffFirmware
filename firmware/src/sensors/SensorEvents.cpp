// gates.cpp  —  8 sensors → 2 gates → paired crossing times
#include <gpiod.hpp>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unordered_map>

// ---------- settings ----------
constexpr const char* CHIP_PATH  = "/dev/gpiochip2"; // find with `gpioinfo`
constexpr bool        ACTIVE_LOW = true;             // NPN sensors with pull-ups
constexpr uint64_t    REARM_NS   = 50'000'000;       // 50 ms: re-trips this soon after clearing = same crossing

// direction aka right or left

// (in)L* -- *R(out)

enum Dir {
    INCOMING = 0,
    OUTGOING = 1
};

struct Sensor {
    unsigned line;   // GPIO line offset on CHIP_PATH
    Dir      dir;    // which gate it belongs to
    int      bit;    // its position (0-3) inside that gate
};

// run gpioinfo on beagle will then get something like the following

// gpiochip1 - 32 lines:
//         line  26:      "P8_14"       unused   input  active-high
//         line  27:      "P8_17"       unused   input  active-high
//         line  28:      "P9_12"       unused   input  active-high


//TODO: change this later on
const std::array<Sensor, 8> SENSORS = {{
    {  7, INCOMING, 0 },
    {  8, INCOMING, 1 },
    {  9, INCOMING, 2 },
    { 10, INCOMING, 3 },

    { 11, OUTGOING, 0 },
    { 12, OUTGOING, 1 },
    { 13, OUTGOING, 2 },
    { 14, OUTGOING, 3 },
}};

// ---------- one raw sensor change ----------
struct SensorEvent {
    uint64_t t_ns;     // kernel timestamp
    int      sensor;   // index into SENSORS
    bool     blocked;  // true = beam blocked
};

// ---------- thread-safe queue (same as before) ----------
class EventQueue {
public:
    void push(const SensorEvent& e) {
        { std::lock_guard<std::mutex> lk(m_); q_.push(e); }
        cv_.notify_one();
    }
    SensorEvent pop() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return !q_.empty(); });
        SensorEvent e = q_.front(); q_.pop();
        return e;
    }
private:
    std::queue<SensorEvent> q_; // line of events first in first out
    std::mutex m_;              // lock for the the thread
    std::condition_variable cv_;// causes the thread to sleep until its told something changed
};

// ---------- worker thread: watches all 8 pins ----------
void sensor_worker(EventQueue& out) {
    gpiod::line_settings settings;
    settings.set_direction(gpiod::line::direction::INPUT)
            .set_edge_detection(gpiod::line::edge::BOTH)
            .set_bias(ACTIVE_LOW ? gpiod::line::bias::PULL_UP
                                 : gpiod::line::bias::AS_IS);

    gpiod::line::offsets offsets;
    std::unordered_map<unsigned, int> line_to_sensor;   // pin number -> SENSORS index
    for (int i = 0; i < (int)SENSORS.size(); ++i) {
        offsets.push_back(SENSORS[i].line);
        line_to_sensor[SENSORS[i].line] = i;
    }

    auto request = gpiod::chip(CHIP_PATH)
        .prepare_request()
        .set_consumer("qm-gates")
        .add_line_settings(offsets, settings)   // all 8 pins in ONE request
        .do_request();

    gpiod::edge_event_buffer buffer(64);
    for (;;) { // condition loops forever
        request.read_edge_events(buffer);       // sleep until any pin changes
        for (const auto& ev : buffer) {
            int  s    = line_to_sensor.at(ev.line_offset());   // which sensor?
            bool rose = ev.type() == gpiod::edge_event::event_type::RISING_EDGE; // the rising edge
            bool on   = ACTIVE_LOW ? !rose : rose;             // dark-on: on = blocked
            out.push({ ev.timestamp_ns().ns(), s, on });
        }
    }
}

// ---------- gate logic ----------
struct Gate {
    const char* name;
    uint8_t  mask    = 0;   // bit set = that sensor is blocked
    uint64_t t_enter = 0;   // when the gate was first broken
    uint64_t t_exit  = 0;   // when the gate fully cleared
};

std::array<Gate, 2> gates = {{ { "INCOMING" }, { "OUTGOING" } }};
std::optional<uint64_t> pending_in, pending_out;   // entries waiting to be paired

double ms(int64_t ns) { return ns / 1e6; } // converts from ns to ms

// waits until there is one unpaired incoming entry and one unpaired outgoing entry then prints the t_out-t_in
// it will reset both
void try_pair() {
    if (pending_in && pending_out) {
        int64_t delta = (int64_t)*pending_out - (int64_t)*pending_in;
        // std::printf("  >> PAIR: outgoing - incoming = %+.3f ms %s\n",
        //             ms(delta), delta < 0 ? "(outgoing crossed FIRST)" : "");

        //TODO: figure out the logic for this 
        pending_in.reset();
        pending_out.reset();
    }
}


// The difference between the countdown timer and the triggered event to display
// an early or false start. Signified by a negative number and indicates a red LED
// light for 2 seconds.
void isFault(){
    // displays the negative value and indicates the red led light for 2 seconds
}


void handle(const SensorEvent& e) {
    const Sensor& s = SENSORS[e.sensor];
    Gate& g = gates[s.dir];

    uint8_t before = g.mask;
    if (e.blocked) g.mask |=  (1u << s.bit);    // set this sensor bit
    else           g.mask &= ~(1u << s.bit);    // clear this sensor bit

    if (before == 0 && g.mask != 0) {           // gate just got broken
        bool same_crossing = g.t_exit && (e.t_ns - g.t_exit < REARM_NS);
        if (!same_crossing) {
            g.t_enter = e.t_ns;
            std::printf("%s entered @ %.3f ms (sensor %d)\n",
                        g.name, ms(g.t_enter), s.bit + 1);
            if (s.dir == INCOMING) pending_in  = g.t_enter;
            else                   pending_out = g.t_enter;
            try_pair();
        }
    } else if (before != 0 && g.mask == 0) {    // all 4 beams clear again
        g.t_exit = e.t_ns;
        std::printf("%s cleared @ %.3f ms (blocked %.1f ms)\n",
                    g.name, ms(g.t_exit), ms(g.t_exit - g.t_enter));
    }
}

int main() {
    EventQueue events;
    std::thread([&] {
        try { sensor_worker(events); }
        catch (const std::exception& ex) {
            std::fprintf(stderr, "GPIO error: %s\n", ex.what());
            std::exit(1);
        }
    }).detach();

    for (;;) handle(events.pop());
}
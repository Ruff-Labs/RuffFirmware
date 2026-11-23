// radio_monitor: watch the sensor radio link without running the race.
//   sudo systemctl stop firmware          (only one program can have the port)
//   ./build/firmware/radio_monitor [/dev/ruff-radio]
// Prints every sensor change and pass as the BeaglePlay sees it (times in ms since start, on
// the BeaglePlay's clock), and a status line every 2 s: link state, clock drift, lost edges.
#include "radio/RadioSensors.hpp"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <thread>

static std::atomic<bool> g_stop{ false };

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::signal(SIGINT,  [](int) { g_stop = true; });
    std::signal(SIGTERM, [](int) { g_stop = true; });

    const char* dev = argc > 1 ? argv[1] : "/dev/ruff-radio";
    RadioSensors sensors(dev);
    const uint64_t t0 = monotonic_ns();
    const auto ms = [t0](uint64_t t) { return t ? (double(t) - double(t0)) / 1e6 : 0.0; };
    const char* names[NUM_SIGNALS] = { "BACK", "FORWARD", "ALIGN" };

    LaneSignals before[NUM_LANES]{};
    uint64_t    last_status = 0;
    std::printf("radio_monitor: listening on %s, Ctrl+C to quit\n", dev);

    while (!g_stop) {
        sensors.poll();
        for (int l = 0; l < NUM_LANES; ++l) {
            if (!sensors.enabled(l)) continue;
            const LaneSignals& s = sensors.lane(l);
            for (int sig = 0; sig < NUM_SIGNALS; ++sig) {
                if (s.high[sig] != before[l].high[sig] || s.rose[sig])
                    std::printf("%10.3f  lane %d %-7s %s\n", ms(s.high[sig] ? s.t_rise[sig] : s.t_fall[sig]),
                                l + 1, names[sig], s.high[sig] ? "on" : "off");
            }
            if (s.passed) std::printf("%10.3f  lane %d PASS\n", ms(s.t_pass), l + 1);
            before[l] = s;
        }

        const uint64_t now = monotonic_ns();
        if (now - last_status >= 2'000'000'000ull) {
            last_status = now;
            const auto& st = sensors.stats();
            std::printf("--- %s | base %s | node %s | node drift %+.0f ppm (%d s fitted) | lost %u, dup %u, fixed %u, node dropped %u, bad frames %u\n",
                        sensors.online() ? "ONLINE" : "offline",
                        sensors.base_heard() ? "heard" : "silent", sensors.node_heard() ? "heard" : "silent",
                        sensors.node_clock().drift_ppm(), sensors.node_clock().windows(),
                        st.lost, st.duplicates, st.corrected, st.node_dropped, st.bad_frames);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}

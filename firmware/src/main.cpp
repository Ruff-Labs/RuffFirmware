#include "state_machine/state_machine.hpp"
#include "database/Database.hpp"
#include "database/SupabaseSync.hpp"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <exception>

// set by SIGTERM (systemctl stop/restart) or Ctrl+C; run_race() checks it every tick
static std::atomic<bool> g_stop{ false };
static void on_signal(int) { g_stop = true; }

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // print each line right away, so journalctl shows it live
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT,  on_signal);

    try {
        LightTree tree;     // first, so its destructor turns the lights off if anything below throws
        Sensors   sensors;
        Remote    remote;

        if (!remote.begin())   // fails until the receiver pins are filled in (Remote.cpp TODO 1)
            std::fprintf(stderr, "firmware: RF remote not set up, GUI remote only\n");

        // CSV backup on the board; races are still printed to the log if it can't be opened
        Database db;
        if (!db.open(""))
            std::fprintf(stderr, "firmware: race database not available, results are only printed\n");

        // Supabase upload in the background (off when /etc/ruff/firmware.env is empty).
        // Catch up first: races saved while offline or before a power cut.
        SupabaseSync sync;
        if (sync.start(SyncConfig::from_env()))
            sync.enqueue(db.rows_after(sync.last_synced_race()));

        run_race(tree, sensors, remote, db, sync, g_stop);   // returns on SIGTERM / Ctrl+C

        std::printf("firmware: shutting down\n");
        sync.stop();   // waits for an upload in progress (at most ~10 s)
        db.close();
    } catch (const std::exception& e) {
        // wrong chip, busy lines or bad pin numbers; systemd restarts us (firmware.service)
        std::fprintf(stderr, "firmware: hardware setup failed: %s\n", e.what());
        return 1;
    }

    return 0;
}

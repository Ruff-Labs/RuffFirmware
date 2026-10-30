#include "state_machine/state_machine.hpp"
#include <cstdio>
#include <exception>

// TODO: still to add:
//   - Database + SupabaseSync (commented out below), and pass them into run_race()
//   - on shutdown (SIGTERM from systemd): sync.stop(); db.close();
//     run_race() never returns yet, so nothing after it runs
int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // print each line right away, so journalctl shows it live

    try {
        LightTree tree;     // first, so its destructor turns the lights off if anything below throws
        Sensors   sensors;
        Remote    remote;

        if (!remote.begin())   // fails until the receiver pins are filled in (Remote.cpp TODO 1)
            std::fprintf(stderr, "firmware: RF remote not set up, GUI remote only\n");

        // Database db; db.open("");
        // SupabaseSync sync;
        // if (sync.start(SyncConfig::from_env())){
        //     sync.enqueue(db.rows_after(sync.last_synced_race()));
        // }

        run_race(tree, sensors, remote);
    } catch (const std::exception& e) {
        // wrong chip, busy lines or bad pin numbers; systemd restarts us (firmware.service)
        std::fprintf(stderr, "firmware: hardware setup failed: %s\n", e.what());
        return 1;
    }

    return 0;
}

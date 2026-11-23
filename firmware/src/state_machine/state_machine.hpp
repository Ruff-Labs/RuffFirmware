#pragma once
#include "database/Database.hpp"
#include "database/SupabaseSync.hpp"
#include "light_tree/LightTree.hpp"
#include "remote/Remote.hpp"
#include "sensors/SensorEvents.hpp"
#include "sensors/SensorHealth.hpp"
#include "system/SystemControl.hpp"
#include <common/protocol.h>
#include "timer/Timer.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <optional>
#include <thread>

// Race state machine. One heat:
//   Idle        every wired lane aligned + judge READY   -> Ready (center light on)
//               (one-lane mode: a lane disabled in LANE_PINS isn't waited for; it ends up DNF)
//   Ready       START                                    -> Sequence
//   Sequence    blue, yellow, yellow (1 s each), green   -> Running (race clock = 0 at green)
//               a dog breaks the beam early (1st false start) -> FalseStart
//   FalseStart  after 2 s, back to Idle to restart the sequence (READY and red lights kept)
//   Running     judge presses STOP                       -> Finished
//   Finished    winner's green light, results printed + saved (CSV, then Supabase); RESET or 5 s -> Reset
//   Reset       "Reset Heat": clears times, keeps false starts + their red lights -> Idle
//   ResetRace   "Reset Race": clears everything (also runs at boot)              -> Idle
// From anywhere: ABORT (remote) -> Reset; Reset Race (GUI) -> ResetRace. The Setup screen's
// "Start Race" sets lane names + heats and does a Reset Race, except mid-heat (ignored, so a stray
// tap at the head table can't wipe a heat).
//
// False start rules:
//   1st (either team)     stop the heat, red light on, restart the sequence
//   2nd by the same team  red light on, the heat continues, that dog owes a rerun
//   2nd at the same start as the other team's 1st: the 1st wins (stop + restart), the 2nd isn't counted
//
// Winner = lowest final time (a lane's last split). Under MIN_EXCHANGES passes at STOP = DNF.
// TODO: a tie goes to lane 1; reruns owed aren't enforced; "only Regular teams can win" isn't
//       checked; the negative reaction time isn't shown after a false start.
// TODO: the heat number: every race is saved as heat 1 until it's decided where it comes from.

using protocol::RaceState;
using Clock = std::chrono::steady_clock;   // CLOCK_MONOTONIC, like gpiod's edge timestamps

enum Lane { LEFT = 0, RIGHT = 1 };

// Everything the state machine reads, filled by poll_inputs() each tick. One-shots (presses,
// passes, GUI commands) are true / set for exactly one tick. Times are CLOCK_MONOTONIC ns.
struct RaceInputs {
    // judge remote / GUI
    bool     judge_ready   = false;   // READY latch
    bool     start_pressed = false;
    bool     stop_pressed  = false;
    uint64_t t_stop        = 0;       // when STOP was pressed
    bool     judge_ack     = false;   // RESET: leave Finished early
    bool     abort         = false;   // -> Reset Heat
    bool     reset_race    = false;   // GUI -> ResetRace
    std::optional<protocol::RaceSetup>  setup;   // GUI Setup screen -> new race
    std::optional<protocol::WifiConfig> wifi;    // GUI Settings -> SystemControl (not the race)
    bool     restart       = false;              // GUI Settings -> SystemControl

    // sensors, per lane
    bool     lane_in_use[2]  = { true, true };   // enabled in LANE_PINS
    bool     lane_aligned[2] = {};               // ALIGN high
    bool     beam_broken[2]  = {};               // FORWARD or BACK broken now or during the tick
    bool     forward_high[2] = {};               // levels, for the GUI and SensorHealth
    bool     back_high[2]    = {};
    uint64_t t_break[2]      = {};               // when that break started (0 = no edge seen)
    bool     lane_pass[2]    = {};               // a valid pass (FORWARD then BACK) this tick
    uint64_t t_pass[2]       = {};               // when FORWARD broke for it
};

// Per-race data; the times live in the Timer. Reset Heat keeps false_starts, Reset Race wipes all.
struct RaceData {
    int  false_starts[2] = {};       // counted, kept across restarts and Reset Heat
    bool broke_early[2]  = {};       // broke the beam during the current countdown
    int  reruns_owed[2]  = {};       // 2nd false starts (TODO: enforce)
    int  winner          = -1;       // -1 = nobody finished
};

struct RaceMachine {
    RaceState state = RaceState::ResetRace;   // boot clears the lights + counts
    RaceState prev  = RaceState::Idle;        // != state, so the first tick runs entry actions
    Clock::time_point entered{};              // when the current state was entered
    Clock::time_point step_started{};         // when the current sequence light came on
    int seq_step = 0;                         // index into SEQUENCE[]
    RaceData data;
    protocol::RaceSetup setup;                // from the Setup screen, kept across resets
};

// ---------- one tick ----------
// run_race() calls these every 1 ms. Public so tests/ can drive the state machine with fake
// hardware and a simulated clock (`now`).
void poll_inputs(Sensors& sensors, Remote& remote, RaceInputs& in);   // never blocks
void race_tick(RaceMachine& m, const RaceInputs& in, LightTree& tree, Sensors& sensors, Remote& remote,
               Timer& timer, Database& db, SupabaseSync& sync, Clock::time_point now);

// Everything the GUI screens show, from the current tick.
protocol::RaceStatus make_status(const RaceMachine& m, const RaceInputs& in, const Timer& timer,
                                 const SensorHealth& health, const SystemControl::Status& sys,
                                 Clock::time_point now);

// main.cpp sets up the hardware, the database and the Supabase sync, then calls this. Every 1 ms:
// read the sensors + remote, step the race, update SensorHealth, hand GUI Wi-Fi / restart
// requests to SystemControl, and send the GUI its status (every 50 ms and on every state change).
// Returns once `stop` is set (SIGTERM from systemd).
void run_race(LightTree& tree, Sensors& sensors, Remote& remote,
              Database& db, SupabaseSync& sync, const std::atomic<bool>& stop);

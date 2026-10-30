#pragma once
#include "light_tree/LightTree.hpp"
#include "remote/Remote.hpp"
#include "sensors/SensorEvents.hpp"

#include "timer/Timer.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

// Race state machine (state_machine.cpp).
//
// One heat:
//   Idle        both lanes aligned + judge READY        -> Ready (center light on)
//   Ready       START                                   -> Sequence
//   Sequence    blue, yellow, yellow (1 s each), green  -> Running (race clock = 0 at green)
//               a dog breaks the beam early (1st false start) -> FalseStart
//   FalseStart  after 2 s, back to Idle to restart the sequence
//   Running     judge presses STOP                      -> Finished
//   Finished    winner's green light + results printed; RESET or 5 s -> Reset
//   Reset       "Reset Heat": clears times, keeps false starts + their red lights -> Idle
//   ResetRace   "Reset Race": clears everything (also runs on boot)               -> Idle
//   ABORT on the remote jumps to Reset from any state except Idle.
//
// False start rules:
//   1st (either team)     -> stop the heat, red light on, restart the sequence
//   2nd by the same team  -> red light on, heat continues, that dog owes a rerun
//   2nd at the same start as the other team's 1st -> the 1st wins: stop + restart, the 2nd isn't counted
//
// Winner = lowest final time (a lane's last split). A lane with fewer than MIN_EXCHANGES
// passes when STOP is pressed is DNF.

// main.cpp sets up the hardware, then calls this. Never returns: every 1 ms it reads the
// sensors + remote, steps the race and drives the light tree.
void run_race(LightTree& tree, Sensors& sensors, Remote& remote);

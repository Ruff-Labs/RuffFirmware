//#include "LightTree.hpp"

enum class RaceState {
    Idle,
    Ready,
    Sequence,
    Running,
    FalseStart,
    Finished,
    Reset
};

// mainly sudo code
void race_tick(RaceState& s) {
    switch (s) {
      case RaceState::Idle:
        // dummy function name -> update_ready_lights();          // pins 8/9 show alignment [1]
        if (judge_ready_flag && all_lanes_aligned())
            s = RaceState::Ready;
        break;

      case RaceState::Ready:
        set_pin(PIN_READY_1, true);
        if (start_pressed) s = RaceState::Sequence;
        break;

      case RaceState::Sequence:
        // step through blue_right -> yellow_1 -> yellow_2 -> green
        if (sequence_done) s = RaceState::Running;
        break;

      case RaceState::Running:
        // read sensors, record splits
        if (false_start) s = RaceState::FalseStart;
        else if (race_over) s = RaceState::Finished;
        break;

      case RaceState::FalseStart:
        flash_false_start_light(red_pin); // 2s red flash [1]
        s = RaceState::Running;           // or back to Ready, your call
        break;

      case RaceState::Finished:
        // winner light on, then write_race_table() [1]
        s = RaceState::Reset;
        break;

      case RaceState::Reset:
        // clear all lights: pins 7/8/9 + all color pins false [1]
        s = RaceState::Idle;
        break;
    }
}


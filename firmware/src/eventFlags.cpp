// TODO: event flags shared between modules. Nothing uses these yet (the state machine reads
//       RaceInputs in state_machine.cpp), so fill them in or delete this file.

// light tree: red light on for 2 s after a false start or early start
struct TreeLightFlag {
    bool green_on;
    bool false_start;
};

// sensors: false start / early start seen
struct SensorEventFlag {
};

// timer: false start
struct TimerFlag {
};

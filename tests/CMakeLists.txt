# Unit tests: Timer, state machine, database and the sensor radio link, runnable on a laptop (no BeaglePlay, no GPIO).
# The hardware classes (LightTree, Sensors, Remote) are swapped for fakes/fake_hardware.cpp,
# and fakes/gpiod.hpp stands in for libgpiod so the real headers still compile.
#   cmake --build build --target unit_tests && ctest --test-dir build --output-on-failure
if(WIN32)
    message(STATUS "unit tests need POSIX (macOS/Linux), skipped on Windows")
    return()
endif()

find_package(CURL)                  # SupabaseSync links it (no network is used by the tests)
find_package(Threads REQUIRED)
if(NOT CURL_FOUND)
    message(WARNING "libcurl not found, skipping unit tests")
    return()
endif()

set(FW ${PROJECT_SOURCE_DIR}/firmware/src)

add_executable(unit_tests
    test_main.cpp
    test_timer.cpp
    test_database.cpp
    test_state_machine.cpp
    test_sensor_health.cpp
    test_radio.cpp
    fakes/fake_hardware.cpp
    # the real firmware code under test
    ${FW}/timer/Timer.cpp
    ${FW}/database/Database.cpp
    ${FW}/database/SupabaseSync.cpp
    ${FW}/state_machine/state_machine.cpp
    ${FW}/remote/VirtualRemote.cpp
    ${FW}/gui_link/StatusSender.cpp
    ${FW}/sensors/SensorHealth.cpp
    ${FW}/sensors/SensorEvents.cpp
    ${FW}/radio/RadioSensors.cpp
    ${FW}/radio/SerialLink.cpp
    ${FW}/radio/ClockFit.cpp
    ${FW}/system/SystemControl.cpp
)
# BEFORE: fakes/gpiod.hpp must win over a real libgpiod on the board
target_include_directories(unit_tests BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fakes)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FW} ${PROJECT_SOURCE_DIR}/nano/include)
target_link_libraries(unit_tests PRIVATE CURL::libcurl Threads::Threads common)

add_test(NAME unit_tests COMMAND unit_tests)

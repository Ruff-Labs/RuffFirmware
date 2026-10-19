# libgpiod v2 C++ bindings (apt: libgpiod-dev, Debian 13+)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED IMPORTED_TARGET libgpiodcxx>=2.0)

# Supabase upload (apt: libcurl4-openssl-dev)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_executable(firmware
    src/main.cpp
    src/eventFlags.cpp
    src/database/Database.cpp
    src/database/SupabaseSync.cpp
    src/light_tree/LightTree.cpp
    src/remote/Remote.cpp
    src/sensors/SensorEvents.cpp
    src/state_machine/state_machine.cpp
    src/timer/Timer.cpp
)

target_include_directories(firmware PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(firmware PRIVATE PkgConfig::GPIOD CURL::libcurl Threads::Threads common)

# Separate test program for the light tree
add_executable(tree_test
    src/light_tree/TreeTest.cpp
    src/light_tree/LightTree.cpp
)
target_include_directories(tree_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tree_test PRIVATE PkgConfig::GPIOD)

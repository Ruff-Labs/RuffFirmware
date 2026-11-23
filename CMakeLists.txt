cmake_minimum_required(VERSION 3.16)
project(board VERSION 0.1.0 LANGUAGES CXX)

# Version shown in the GUI (Settings + sidebar) and sent by the firmware: 0.1.0+<git commit>.
# Re-read on every configure (scripts/build.sh configures each time).
execute_process(COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                OUTPUT_VARIABLE RUFF_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(RUFF_VERSION "${PROJECT_VERSION}")
if(RUFF_GIT_COMMIT)
    string(APPEND RUFF_VERSION "+${RUFF_GIT_COMMIT}")
endif()
add_compile_definitions(RUFF_VERSION="${RUFF_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RUFF_BUILD_TESTS "Build the unit tests (tests/)" ON)

add_subdirectory(common)
add_subdirectory(firmware)
add_subdirectory(gui)

if(RUFF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
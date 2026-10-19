cmake_minimum_required(VERSION 3.16)
project(board CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(common)
add_subdirectory(firmware)
add_subdirectory(gui)
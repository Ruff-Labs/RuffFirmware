add_library(common INTERFACE)   # header-only; use STATIC if it has .cpp files
target_include_directories(common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
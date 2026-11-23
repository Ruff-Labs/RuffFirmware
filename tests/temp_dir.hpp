#pragma once
#include <filesystem>
#include <string>
#include <unistd.h>

// A fresh, empty folder under the system temp dir, deleted when the test ends.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        static int n = 0;
        path = std::filesystem::temp_directory_path()
             / ("ruff_test_" + std::to_string(::getpid()) + "_" + std::to_string(n++));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
    std::string file(const char* name) const { return (path / name).string(); }
};

#!/usr/bin/env bash
# Configure + build firmware and gui into build/
set -euo pipefail
cd "$(dirname "$0")/.."
# Unit tests are for laptops / CI; skip them here so a deploy builds only what runs
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRUFF_BUILD_TESTS=OFF
# GUI first, so a firmware compile error still leaves a fresh GUI binary
cmake --build build --target gui --parallel
cmake --build build --parallel

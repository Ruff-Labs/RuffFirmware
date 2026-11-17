#!/usr/bin/env bash
# Configure + build firmware and gui into build/
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -B build -DCMAKE_BUILD_TYPE=Release
# GUI first, so a firmware compile error still leaves a fresh GUI binary
cmake --build build --target gui --parallel
cmake --build build --parallel

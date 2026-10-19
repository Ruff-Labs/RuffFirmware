#!/usr/bin/env bash
# Configure + build firmware and gui into build/
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

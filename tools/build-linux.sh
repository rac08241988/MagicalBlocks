#!/usr/bin/env bash
# Native Linux build (clang). Usage: tools/build-linux.sh [extra cmake args...]
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
B=$APP/build-linux
cmake -S "$APP" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "$@"
cmake --build "$B" --target MagicalBlocks -j"$(nproc)"

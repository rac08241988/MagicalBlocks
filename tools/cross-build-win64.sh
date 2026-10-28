#!/usr/bin/env bash
# Cross-compile MagicalBlocks.exe for Windows x64 from Linux with llvm-mingw (see /workspace/n64recomp/SETUP.md).
# Usage: tools/cross-build-win64.sh [llvm-mingw-version] [extra cmake args...]   (default 20250709 = LLVM 20.1.8)
set -euo pipefail
ROOT=/workspace/n64recomp
APP=$(cd "$(dirname "$0")/.." && pwd)
V=${1:-20250709}; shift || true
TC=$ROOT/llvm-mingw-$V-ucrt-ubuntu-22.04-x86_64
B=$APP/build-win64
[ -x "$ROOT/host-tools/file_to_c" ] || { echo "missing $ROOT/host-tools/file_to_c (see SETUP.md)"; exit 1; }
cmake -S "$APP" -B "$B" -G Ninja -C "$ROOT/zelda-mingw-flags.cmake" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/toolchain-llvm-mingw-x64.cmake" -DLLVM_MINGW_ROOT="$TC" \
  -DCMAKE_BUILD_TYPE=Release \
  -DFILE_TO_C_HOST_EXECUTABLE="$ROOT/host-tools/file_to_c" \
  -DFREETYPE_MINGW_ROOT="$ROOT/win-deps/llvm-mingw-$V/freetype" "$@"
cmake --build "$B" --target MagicalBlocks -j"$(nproc)"

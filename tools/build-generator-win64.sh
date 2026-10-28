#!/usr/bin/env bash
# Cross-builds the code generators shipped with option (b) (OPTB.md): N64Recomp.exe + RSPRecomp.exe (MIT, static,
# no extra DLLs) from an N64Recomp checkout at the commit pinned in deps.lock (ffb39cd). Out-of-tree build.
# Usage: tools/build-generator-win64.sh   (N64RECOMP_SRC, default /workspace/n64recomp/N64Recomp)
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
SRC=${N64RECOMP_SRC:-/workspace/n64recomp/N64Recomp}
TC=${LLVM_MINGW:-/workspace/n64recomp/llvm-mingw-20250709-ucrt-ubuntu-22.04-x86_64}
B=$APP/build-generator-win64
want=ffb39cd
have=$(git -C "$SRC" rev-parse --short=7 HEAD)
[ "$have" = "$want" ] || echo "warning: $SRC is at $have, deps.lock pins $want" >&2
cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER="$TC/bin/x86_64-w64-mingw32-clang" -DCMAKE_CXX_COMPILER="$TC/bin/x86_64-w64-mingw32-clang++" \
  -DCMAKE_EXE_LINKER_FLAGS="-static" >/dev/null
cmake --build "$B" --target N64RecompCLI RSPRecomp -j"$(nproc)"
ls -la "$B/N64Recomp.exe" "$B/RSPRecomp.exe"

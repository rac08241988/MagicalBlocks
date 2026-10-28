#!/usr/bin/env bash
# Generates the ROM-derived sources for MagicalBlocks from the user's own ROM:
#   RecompiledFuncs/  (N64Recomp, CPU code)      rsp/n_aspMain.cpp  (RSPRecomp, audio microcode)
# Nothing generated here may be committed or redistributed (see .gitignore).
#
# Usage: tools/generate_code.sh [path/to/rom]   (default: ../rom/mtc_usa.z64)
# Requires: the splat disassembly project in ../disasm (python venv with splat64, binutils-mips-linux-gnu)
#           and N64Recomp/RSPRecomp binaries (N64RECOMP_BIN_DIR, default /workspace/n64recomp/N64Recomp/build).
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
MTC=$(cd "$APP/.." && pwd)
ROM=${1:-$MTC/rom/mtc_usa.z64}
BIN=${N64RECOMP_BIN_DIR:-/workspace/n64recomp/N64Recomp/build}
PY=${SPLAT_PYTHON:-/workspace/n64recomp/.venv-re/bin/python}
want=ca3fbd17406031a88e04bb79959d851550b641d0
have=$(sha1sum "$ROM" | cut -d' ' -f1)
if [ "$have" != "$want" ]; then
  echo "ROM SHA1 mismatch: $have (expected $want). Convert .v64/.n64 to .z64 first or use the USA dump." >&2
  exit 1
fi
mkdir -p "$MTC/rom"
[ "$(readlink -f "$ROM")" = "$(readlink -f "$MTC/rom/mtc_usa.z64")" ] || cp "$ROM" "$MTC/rom/mtc_usa.z64"
# 1) disassemble + rebuild the ELF (byte-matching check included)
( cd "$MTC/disasm" && rm -rf asm build && "$PY" -m splat split magicaltetris.yaml > split.log && ./build.sh )
# 2) CPU code
rm -rf "$APP/RecompiledFuncs" && mkdir -p "$APP/RecompiledFuncs"
"$BIN/N64Recomp" "$APP/recomp-config/mtc.us.toml"
# 3) audio microcode
mkdir -p "$APP/rsp"
"$BIN/RSPRecomp" "$APP/recomp-config/n_aspMain.us.toml"
echo "Generated: $APP/RecompiledFuncs ($(ls "$APP/RecompiledFuncs" | wc -l) files), $APP/rsp/n_aspMain.cpp"

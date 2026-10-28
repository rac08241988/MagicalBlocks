#!/usr/bin/env bash
# gamecode-scan.sh <mtc_game.dll> <file>...: proves that no file contains the recompiled game code (OPTB.md).
# Reference = the .text section of a game-code module generated from the ROM on this machine (by the shipped
# generator kit, e.g. under wine). Every file is searched for any 64-byte run of that native code (rom_scan.c,
# same low-entropy filters). A build that links the game code (MTC_STATIC_GAMECODE / 0.1.x) lights up here.
# Exit 0 clean, 1 hits.
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
DLL=$1; shift
OBJCOPY=${OBJCOPY:-/workspace/n64recomp/llvm-mingw-20250709-ucrt-ubuntu-22.04-x86_64/bin/llvm-objcopy}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
"$OBJCOPY" --dump-section .text="$W/text.bin" "$DLL" "$W/dll.tmp"
cc -O2 -o "$W/rom_scan" "$APP/tools/rom_scan.c"
END=$(printf '%X' "$(stat -c %s "$W/text.bin")")
echo "game-code scan: reference .text of $(basename "$DLL") = $(stat -c %s "$W/text.bin") bytes, 64-byte windows"
"$W/rom_scan" "$W/text.bin" 0 "$END" 64 "$@"

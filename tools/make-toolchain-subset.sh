#!/usr/bin/env bash
# Builds the minimal Windows compiler bundle shipped with option (b) (OPTB.md): clang + lld (+ their DLLs) and only the
# headers the generated code needs (tools/toolchain-files.txt), taken from the official llvm-mingw release zip.
# Usage: tools/make-toolchain-subset.sh [out_dir]   (default build-toolchain-win64/toolchain)
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd); MT=$(cd "$APP/.." && pwd)
REL=20250709; NAME=llvm-mingw-$REL-ucrt-x86_64
OUTD=${1:-$APP/build-toolchain-win64/toolchain}
CACHE=$MT/dist/cache; mkdir -p "$CACHE"
ZIP=$CACHE/$NAME.zip
if [ ! -f "$ZIP" ]; then
  curl -fsSL -o "$ZIP.part" "https://github.com/mstorsjo/llvm-mingw/releases/download/$REL/$NAME.zip" && mv "$ZIP.part" "$ZIP"
fi
X=$CACHE/$NAME
[ -d "$X" ] || ( cd "$CACHE" && unzip -q "$ZIP" )
rm -rf "$OUTD"; mkdir -p "$OUTD"
grep -v '^#' "$APP/tools/toolchain-files.txt" | while read -r f; do
  [ -n "$f" ] || continue
  mkdir -p "$OUTD/$(dirname "$f")"; cp "$X/$f" "$OUTD/$f"
done
echo "toolchain: $(du -sh "$OUTD" | cut -f1) in $(find "$OUTD" -type f | wc -l) files -> $OUTD"

#!/usr/bin/env bash
# Release packaging (LOCAL ONLY - see ../RELEASE.md before publishing anything):
#   ../dist/release/MagicalBlocks-$V-win64-setup.exe     NSIS installer (program + shaders, then ROM step)
#   ../dist/release/MagicalBlocks-$V-win64-portable.zip  same files + portable.txt, ROM picked in-app
# Option (b) (see ../RELEASE.md / ../OPTB.md): the package holds NO recompiled game code. It ships the generator
# (tools/N64Recomp.exe, tools/RSPRecomp.exe), a clang/lld subset (toolchain/) and gamecode/ (symbol/config TOML,
# glue sources, runtime headers); the game code is generated from the player's ROM on their PC.
# Usage: tools/package-release.sh [version]
#   needs build-win64 (non-static), build-generator-win64 (tools/build-generator-win64.sh),
#   build-toolchain-win64 (tools/make-toolchain-subset.sh), makensis (NSIS 3), ../shader-audit/slang-shaders,
#   and a local ROM for the ROM-byte scan: MTC_SCAN_ROM (default ../rom/mtc_usa.z64; it is never staged).
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd); MT=$(cd "$APP/.." && pwd)
V=${1:-0.1.4}; OUT=$MT/dist/release; ST=$MT/dist/release-stage-$V; S=$ST/MagicalBlocks
command -v makensis >/dev/null || { echo "makensis not found (apt install nsis)"; exit 1; }
ROM=${MTC_SCAN_ROM:-$MT/rom/mtc_usa.z64}
[ -f "$ROM" ] || { echo "ROM-byte scan needs a local ROM: set MTC_SCAN_ROM (it is only read, never staged)"; exit 1; }
GEN=$APP/build-generator-win64; TCW=$APP/build-toolchain-win64/toolchain
[ -f "$GEN/N64Recomp.exe" ] && [ -f "$GEN/RSPRecomp.exe" ] || { echo "missing $GEN (run tools/build-generator-win64.sh)"; exit 1; }
[ -f "$TCW/bin/clang-20.exe" ] || { echo "missing $TCW (run tools/make-toolchain-subset.sh)"; exit 1; }
[ -f "$APP/build-win64/gamecode/mtc.syms.toml" ] || { echo "build-win64/gamecode missing (non-static build needed)"; exit 1; }
[ -f "$APP/build-win64/libRecompiledFuncs.a" ] && grep -q "MTC_STATIC_GAMECODE:BOOL=ON" "$APP/build-win64/CMakeCache.txt" && { echo "build-win64 is configured with MTC_STATIC_GAMECODE=ON"; exit 1; }
strings "$APP/build-win64/MagicalBlocks.exe" | grep -x "$V" >/dev/null || { echo "build-win64 exe is not version $V"; exit 1; }
rm -rf "$ST"; mkdir -p "$S" "$OUT"
cp "$APP"/build-win64/{MagicalBlocks.exe,SDL2.dll,dxcompiler.dll,dxil.dll} "$APP/recompcontrollerdb.txt" "$S/"
tar -C "$APP" --exclude=assets/scss -cf - assets | tar -C "$S" -xf -
# game-code generator kit (no game code): tools, compiler subset, symbol/config data + glue + runtime headers
mkdir -p "$S/tools" && cp "$GEN/N64Recomp.exe" "$GEN/RSPRecomp.exe" "$S/tools/"
cp -a "$TCW" "$S/toolchain"
cp -a "$APP/build-win64/gamecode" "$S/gamecode"
rm -rf "$S/gamecode-cache"
cp "$APP/LICENSE" "$S/LICENSE.txt"
cp "$APP/installer/README.txt" "$S/README.txt"
mkdir -p "$S/licenses" && cp "$APP"/licenses/*.txt "$S/licenses/"
python3 "$MT/shaders-release/package-external-shaders.py" "$MT/shader-audit/slang-shaders" "$S/shaders"
python3 "$APP/tools/make_notices.py" "$S/THIRD-PARTY-NOTICES.txt"
# Never ship a ROM (or anything that looks like one) or ROM-derived images.
find "$S" \( -iname '*.z64' -o -iname '*.v64' -o -iname '*.n64' -o -iname '*.rom' \) -print -delete
# ROM-byte gate: no 64-byte window of the ROM's code region (0x1000-0x7A140) may appear in any shipped file
# (all three ROM byte orders are checked). Then an informational whole-ROM scan.
SCAN=$ST/rom_scan; cc -O2 -o "$SCAN" "$APP/tools/rom_scan.c"
mapfile -d '' FILES < <(find "$S" -type f -print0)
echo "ROM-byte scan of ${#FILES[@]} staged files (code region 0x1000-0x7A140, 64-byte windows)..."
if ! "$SCAN" "$ROM" 1000 7A140 64 "${FILES[@]}" > "$ST/rom-scan-code.txt"; then
  cat "$ST/rom-scan-code.txt"; echo "FAIL: ROM code bytes found in the package"; exit 1; fi
tail -1 "$ST/rom-scan-code.txt"
"$SCAN" "$ROM" 0 $(printf '%X' "$(stat -c %s "$ROM")") 64 "${FILES[@]}" > "$ST/rom-scan-full.txt" || true
echo "informational whole-ROM scan: $(tail -1 "$ST/rom-scan-full.txt")  (details: $ST/rom-scan-full.txt)"
# Game-code gate: no 64-byte run of the native game code (the .text of a module generated from the ROM on this
# machine, MTC_SCAN_MODULE, default ../tmp/optb-ref/mtc_game.dll) may appear in any shipped file.
MOD=${MTC_SCAN_MODULE:-$MT/tmp/optb-ref/mtc_game.dll}
if [ -f "$MOD" ]; then
  if ! "$APP/tools/gamecode-scan.sh" "$MOD" "${FILES[@]}" > "$ST/gamecode-scan.txt"; then
    cat "$ST/gamecode-scan.txt"; echo "FAIL: recompiled game code found in the package"; exit 1; fi
  echo "$(head -1 "$ST/gamecode-scan.txt"): $(tail -1 "$ST/gamecode-scan.txt")"
else
  echo "WARNING: no reference module ($MOD): game-code gate skipped (generate one, see OPTB.md)"
fi
# CRLF for the text files people open in Notepad.
for f in README.txt LICENSE.txt THIRD-PARTY-NOTICES.txt licenses/*.txt; do sed -i 's/\r\?$/\r/' "$S/$f"; done

# 1) installer
SETUP=$OUT/MagicalBlocks-$V-win64-setup.exe
( cd "$APP/installer" && makensis -V2 -DVERSION="$V" -DVERSION_NUM="${V%%-*}" -DSTAGE="$S" -DOUTFILE="$SETUP" -DICON="$APP/icons/app.ico" MagicalBlocks.nsi )
# 2) portable zip (folder MagicalBlocks-$V/ with portable.txt -> config/ROM/saves stay in the folder)
ZIP=$OUT/MagicalBlocks-$V-win64-portable.zip
rm -f "$ZIP"; P=$ST/MagicalBlocks-$V; cp -a "$S" "$P"; : > "$P/portable.txt"
( cd "$ST" && python3 - "$ZIP" "MagicalBlocks-$V" <<'P'
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(sys.argv[2]):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f); z.write(p, p)
P
)
# the final archives too (the installer is LZMA-compressed, so this mainly guards the zip's stored files)
"$SCAN" "$ROM" 1000 7A140 64 "$SETUP" "$ZIP" > "$ST/rom-scan-archives.txt" || { cat "$ST/rom-scan-archives.txt"; echo "FAIL: ROM bytes in archives"; exit 1; }
# local-only symbols for tools/symbolize_crash.py (NOT part of the release; keep out of uploads)
mkdir -p "$MT/dist/symbols" && cp "$APP/build-win64/MagicalBlocks.map" "$MT/dist/symbols/MagicalBlocks-$V.map" 2>/dev/null || echo "note: no .map in build-win64"
( cd "$OUT" && sha256sum "$(basename "$SETUP")" "$(basename "$ZIP")" > "SHA256SUMS-$V.txt" && cat "SHA256SUMS-$V.txt"
  # also merge into the shared SHA256SUMS.txt (other versions' lines are kept)
  touch SHA256SUMS.txt && { grep -v -- "-$V-win64-" SHA256SUMS.txt || true; cat "SHA256SUMS-$V.txt"; } > SHA256SUMS.txt.new && mv SHA256SUMS.txt.new SHA256SUMS.txt )
ls -la "$OUT"

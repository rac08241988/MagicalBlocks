#!/usr/bin/env bash
# Fetch the pinned dependencies listed in deps.lock into lib/ (and rt64 into ../rt64 if it is missing).
set -euo pipefail
APP=$(cd "$(dirname "$0")/.." && pwd)
clone() { # dir url commit
  [ -d "$1" ] && { echo "exists: $1"; return; }
  git clone --recursive "$2" "$1"
  git -C "$1" checkout "$3"
  git -C "$1" submodule update --init --recursive
}
grep -v '^#' "$APP/deps.lock" | while read -r name url commit; do
  [ -z "${name:-}" ] && continue
  case "$name" in
    rt64)
      RT=${MTC_RT64_DIR:-$APP/../rt64-widescreen}
      if [ ! -d "$RT" ]; then
        clone "$RT" "$url" "$commit"
        git -C "$RT" checkout -b s2dex-objects
        git -C "$RT" apply "$APP/patches-deps/rt64-mingw-cross.diff"
        git -C "$RT" add -A && git -C "$RT" -c user.name=mtc -c user.email=mtc@localhost commit -qm "mingw cross-compile fixes [n64recomp-cross]"
        git -C "$RT" -c user.name=mtc -c user.email=mtc@localhost am "$APP/patches-deps/rt64-s2dex-objects.patch"
        git -C "$RT" checkout -b shaders
        git -C "$RT" -c user.name=mtc -c user.email=mtc@localhost am "$APP/patches-deps/rt64-shaders.patch"
        git -C "$RT" checkout -b widescreen
        git -C "$RT" -c user.name=mtc -c user.email=mtc@localhost am "$APP/patches-deps/rt64-widescreen.patch"
        git -C "$RT" checkout -b hifps
        git -C "$RT" -c user.name=mtc -c user.email=mtc@localhost am "$APP/patches-deps/rt64-hifps.patch"
        git -C "$RT" submodule update --init --recursive
      fi ;;
    *) clone "$APP/lib/$name" "$url" "$commit" ;;
  esac
done
echo "deps ready"

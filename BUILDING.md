# Building from source

You need your own ROM of Magical Tetris Challenge (USA, SHA1 `ca3fbd17406031a88e04bb79959d851550b641d0`).
The game code is generated from it on your machine; nothing generated may be committed or shared.

Requirements: Linux (or WSL), CMake 3.20+, Ninja, clang, python3 with splat64, binutils-mips-linux-gnu,
N64Recomp + RSPRecomp, and for Windows builds llvm-mingw 20250709 (LLVM 20).

```
tools/fetch_deps.sh                 # pinned dependencies (deps.lock) + RT64 with the patches in patches-deps/
tools/generate_code.sh path/to/rom  # ROM check -> disassembly/ELF -> RecompiledFuncs/ + rsp/ (git-ignored)
tools/build-linux.sh                # native Linux build  -> build-linux/MagicalBlocks
tools/cross-build-win64.sh          # Windows x64 build    -> build-win64/MagicalBlocks.exe
tools/package-release.sh 0.1.3      # setup.exe (NSIS 3, makensis) + portable zip -> ../dist/release/
```

The disassembly project (`../disasm`, splat config) and the widescreen border module (`../widescreen`) live next
to this repository.

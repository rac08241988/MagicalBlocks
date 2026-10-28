#!/usr/bin/env python3
"""Writes THIRD-PARTY-NOTICES.txt for the dist from the license files of the linked components."""
import os, sys
APP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MT = os.path.dirname(APP); R = os.environ.get('MTC_RT64_DIR', os.path.join(MT, 'rt64-widescreen')); C = os.path.join(R, 'src/contrib')
NR = '/workspace/n64recomp'
items = [
 ("N64ModernRuntime (librecomp / ultramodern)", f"{NR}/N64ModernRuntime/COPYING"),
 ("N64Recomp (linked parts, and tools/N64Recomp.exe + tools/RSPRecomp.exe)", f"{NR}/N64Recomp/LICENSE"),
 ("rabbitizer (in tools/*.exe)", f"{NR}/N64Recomp/lib/rabbitizer/LICENSE"),
 ("fmt (in tools/*.exe)", f"{NR}/N64Recomp/lib/fmt/LICENSE"),
 ("toml++ (in tools/*.exe)", f"{NR}/N64Recomp/lib/tomlplusplus/LICENSE"),
 ("ELFIO (in tools/*.exe)", f"{NR}/N64Recomp/lib/ELFIO/LICENSE.txt"),
 ("LLVM / clang / lld / libc++ / libunwind (toolchain/, from llvm-mingw 20250709)", f"{APP}/build-toolchain-win64/toolchain/LICENSE.TXT"),
 ("mingw-w64 headers (toolchain/include)", f"{APP}/build-toolchain-win64/toolchain/x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64.txt"),
 ("mingw-w64 runtime headers", f"{APP}/build-toolchain-win64/toolchain/x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt"),
 ("RT64", f"{R}/LICENSE"),
 ("Built-in post-processing shaders (embedded in the exe)", f"{R}/src/postfx/builtin/NOTICES.md"),
 ("glslang", f"{C}/glslang/LICENSE.txt"), ("SPIRV-Cross", f"{C}/spirv-cross/LICENSE"),
 ("re-spirv", f"{C}/re-spirv/LICENSE"), ("stb", f"{C}/stb/LICENSE"), ("xxHash", f"{C}/xxHash/LICENSE"),
 ("zstd", f"{C}/zstd/LICENSE"), ("D3D12MemoryAllocator", f"{C}/D3D12MemoryAllocator/LICENSE.txt"),
 ("VulkanMemoryAllocator", f"{C}/VulkanMemoryAllocator/LICENSE.txt"), ("volk", f"{C}/volk/LICENSE.md"),
 ("Vulkan-Headers", f"{C}/Vulkan-Headers/LICENSE.md"), ("hlslpp", f"{C}/hlslpp/LICENSE"),
 ("ddspp", f"{C}/ddspp/LICENSE"), ("nativefiledialog-extended", f"{C}/nativefiledialog-extended/LICENSE"),
 ("Dear ImGui", f"{C}/imgui/LICENSE.txt"), ("im3d", f"{C}/im3d/LICENSE"), ("implot", f"{C}/implot/LICENSE"),
 ("RmlUi", f"{APP}/lib/RmlUi/LICENSE.txt"), ("lunasvg", f"{APP}/lib/lunasvg/LICENSE"),
 ("SDL2 (SDL2.dll)", f"{APP}/build-win64/_deps/sdl2-src/COPYING.txt"),
 ("FreeType (FreeType License)", f"{NR}/deps-src/freetype-2.13.2/docs/FTL.TXT"),
 ("Lato fonts (assets/LatoLatin-*.ttf) - SIL Open Font License 1.1", f"{APP}/licenses/OFL-Lato.txt"),
 ("Noto Emoji (assets/NotoEmoji-Regular.ttf) - SIL Open Font License 1.1", f"{APP}/licenses/OFL-NotoEmoji.txt"),
 ("PromptFont (assets/promptfont/promptfont.ttf) - SIL Open Font License 1.1", f"{APP}/licenses/OFL-PromptFont.txt"),
]
out = ["Magical Blocks - third-party notices", "=" * 60, "",
 "This program is not affiliated with or endorsed by Nintendo, Capcom, Disney, or The Tetris Company.",
 "No game assets (graphics, music, sound, text) are included; they are read at runtime from the user's own ROM.",
 "This package contains no game code either. On first start (or in the setup's ROM step) the game code is generated",
 "on your PC from your own ROM, with the included N64Recomp/RSPRecomp tools and clang/lld compiler, and kept in",
 "%LOCALAPPDATA%\\MagicalBlocks\\gamecode-cache (portable: next to the program). The gamecode folder holds only",
 "function addresses/names and build settings. See RELEASE.md of the source repository.", "",
 "This program (derived from Zelda64Recomp) is GPL-3.0: see COPYING.txt.",
 "DirectX Shader Compiler (dxcompiler.dll, dxil.dll): Microsoft, University of Illinois/NCSA Open Source License (LLVM).",
 "  https://github.com/microsoft/DirectXShaderCompiler/blob/main/LICENSE.TXT",
 "Fonts (Lato, Noto Emoji, PromptFont): SIL Open Font License 1.1 - full texts below and in licenses/.",
 "Optional shaders in the shaders folder keep their own licenses: see shaders/README.txt and shaders/LICENSES/.", ""]
for name, path in items:
    if not os.path.isfile(path):
        print("warning: missing", path, file=sys.stderr); continue
    out += ["-" * 60, name, "-" * 60, open(path, errors='replace').read().strip(), ""]
open(sys.argv[1], 'w').write("\n".join(out) + "\n")

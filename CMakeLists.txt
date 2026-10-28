cmake_minimum_required(VERSION 3.20)

# MagicalBlocks - N64Recomp-based PC port (needs the USA ROM, NMTE).
# Structure follows Zelda64Recomp (b65c482) on the latest N64ModernRuntime (cdf5abb).
#
# ROM-derived code is NOT part of the repo: RecompiledFuncs/ (N64Recomp) and rsp/n_aspMain.cpp
# (RSPRecomp) are generated locally from the user's own ROM with tools/generate_code.sh.

project(MagicalBlocks LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(MTC_EXE_NAME MagicalBlocks)

# ---- RT64 location ---------------------------------------------------------------------------------
# Default: the S2DEX-enabled RT64 working copy next to this project (../rt64, branch s2dex-objects =
# upstream 23cab60 + mingw cross fixes + S2DEX object commands). Point it at any other RT64 checkout
# (e.g. a stock 23cab60 + patches-deps/rt64-mingw-cross.diff) with -DMTC_RT64_DIR=/path/to/rt64.
set(MTC_RT64_DIR "${CMAKE_SOURCE_DIR}/../rt64-widescreen" CACHE PATH "RT64 source directory")
get_filename_component(MTC_RT64_DIR "${MTC_RT64_DIR}" ABSOLUTE)
if (NOT EXISTS "${MTC_RT64_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "RT64 not found at MTC_RT64_DIR=${MTC_RT64_DIR}")
endif()
message(STATUS "Using RT64 from ${MTC_RT64_DIR}")

# ---- ROM-derived inputs (generated locally) --------------------------------------------------------
set(MTC_RECOMPILED_DIR "${CMAKE_SOURCE_DIR}/RecompiledFuncs" CACHE PATH "N64Recomp output (generated from your ROM)")
set(MTC_RSP_DIR "${CMAKE_SOURCE_DIR}/rsp" CACHE PATH "RSPRecomp output (generated from your ROM)")
# Option (b) (OPTB.md): by default the exe contains NO game code. The game code is generated from the player's
# ROM and compiled on their PC at first launch (src/main/gamecode.cpp). MTC_STATIC_GAMECODE=ON links a locally
# generated RecompiledFuncs/ + rsp/ into the exe instead (developer builds, performance comparisons; never ship).
option(MTC_STATIC_GAMECODE "Link the locally generated game code into the exe (developer builds only)" OFF)
if (MTC_STATIC_GAMECODE AND (NOT EXISTS "${MTC_RECOMPILED_DIR}/recomp_overlays.inl" OR NOT EXISTS "${MTC_RSP_DIR}/n_aspMain.cpp"))
    message(FATAL_ERROR "Generated code missing. Run tools/generate_code.sh with your own Magical Tetris Challenge (USA) ROM first.")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything /W4")
endif()

if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
endif()

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (WIN32)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/lib/")
endif()

set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
add_compile_definitions(HLSL_CPU)

add_subdirectory(${MTC_RT64_DIR} ${CMAKE_BINARY_DIR}/rt64)

set(BUILD_SHARED_LIBS OFF)
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/lunasvg)
set(RMLUI_SVG_PLUGIN ON CACHE BOOL "" FORCE)
set(RMLUI_TESTS_ENABLED OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/RmlUi)
target_compile_definitions(rmlui_core PRIVATE LUNASVG_BUILD_STATIC)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)

target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/rt64/src)

# ---- RecompiledFuncs (ROM-derived, generated; MTC_STATIC_GAMECODE only) -------------------------------
if (MTC_STATIC_GAMECODE)
add_library(RecompiledFuncs STATIC)
target_compile_options(RecompiledFuncs PRIVATE
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-implicit-function-declaration
)
target_include_directories(RecompiledFuncs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)
file(GLOB FUNC_C_SOURCES ${MTC_RECOMPILED_DIR}/*.c)
target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES} ${MTC_RECOMPILED_DIR}/lookup.cpp)
# Diagnostics: build against a trace_mode RecompiledFuncs (tools/trace/README.md).
option(MTC_TRACE "RecompiledFuncs were generated with trace_mode = true" OFF)
if (MTC_TRACE)
    target_include_directories(RecompiledFuncs PRIVATE ${CMAKE_SOURCE_DIR}/tools/trace)
    target_sources(RecompiledFuncs PRIVATE ${CMAKE_SOURCE_DIR}/tools/trace/trace.cpp)
endif()
endif()

# ---- Main executable ------------------------------------------------------------------------------
add_executable(${MTC_EXE_NAME})

set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/main/main.cpp
    ${CMAKE_SOURCE_DIR}/src/main/crash_report.cpp
    ${CMAKE_SOURCE_DIR}/src/main/support.cpp
    ${CMAKE_SOURCE_DIR}/src/main/gamecode.cpp
    ${CMAKE_SOURCE_DIR}/src/main/rt64_render_context.cpp

    ${CMAKE_SOURCE_DIR}/src/game/input.cpp
    ${CMAKE_SOURCE_DIR}/src/game/controls.cpp
    ${CMAKE_SOURCE_DIR}/src/game/config.cpp
    ${CMAKE_SOURCE_DIR}/src/game/graphics_presets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/rom_check.cpp
    ${CMAKE_SOURCE_DIR}/src/game/tlb.cpp
    ${CMAKE_SOURCE_DIR}/src/game/widescreen.cpp
    ${CMAKE_SOURCE_DIR}/src/game/hifps.cpp

    ${CMAKE_SOURCE_DIR}/src/ui/ui_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_state.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_scroll_nav.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_launcher.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_config.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_prompt.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_config_sub_menu.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_color_hack.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_rml_hacks.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_elements.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_mod_details_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_mod_installer.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_mod_menu.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_api.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_api_events.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_api_images.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/util/hsv.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/core/ui_context.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_button.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_clickable.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_container.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_element.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_image.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_label.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_radio.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_scroll_container.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_slider.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_span.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_style.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_text_input.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/elements/ui_toggle.cpp


    ${CMAKE_SOURCE_DIR}/lib/RmlUi/Backends/RmlUi_Platform_SDL.cpp
)

target_include_directories(${MTC_EXE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    ${CMAKE_SOURCE_DIR}/lib/concurrentqueue
    ${CMAKE_SOURCE_DIR}/lib/GamepadMotionHelpers
    ${CMAKE_SOURCE_DIR}/lib/RmlUi/Include
    ${CMAKE_SOURCE_DIR}/lib/RmlUi/Backends
    ${MTC_RT64_DIR}/src/contrib
    ${MTC_RT64_DIR}/src/contrib/hlslpp/include
    ${MTC_RT64_DIR}/src/contrib/dxc/inc
    ${MTC_RT64_DIR}/src
    ${MTC_RT64_DIR}/src/rhi
    ${MTC_RT64_DIR}/src/render
    ${MTC_RT64_DIR}/src/contrib/nativefiledialog-extended/src/include
    ${CMAKE_SOURCE_DIR}/lib/SlotMap
    ${CMAKE_BINARY_DIR}/ui-shaders
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_definitions(${MTC_EXE_NAME} PRIVATE MTC_RT64_DIR_NAME="${MTC_RT64_DIR}")

if (CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_options(${MTC_EXE_NAME} PRIVATE -march=nehalem -fno-strict-aliasing -fms-extensions)
else()
    target_compile_options(${MTC_EXE_NAME} PRIVATE -fno-strict-aliasing -fms-extensions)
endif()

if (MSVC)
    # Identical code folding breaks mod function patching.
    target_link_options(${MTC_EXE_NAME} PRIVATE /OPT:NOICF)
endif()

if (WIN32)
    include(FetchContent)
    if (DEFINED ENV{SDL2_VERSION})
        set(SDL2_VERSION $ENV{SDL2_VERSION})
    else()
        set(SDL2_VERSION "2.30.3")
    endif()

    if (MINGW)
        # [n64recomp-cross] mingw SDL2 devel package (headers + libSDL2.dll.a + SDL2.dll)
        FetchContent_Declare(sdl2
            URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-mingw.tar.gz)
        FetchContent_MakeAvailable(sdl2)
        set(SDL2_MINGW_ROOT ${sdl2_SOURCE_DIR}/x86_64-w64-mingw32)
        target_include_directories(${MTC_EXE_NAME} PRIVATE ${SDL2_MINGW_ROOT}/include ${SDL2_MINGW_ROOT}/include/SDL2)
        target_link_directories(${MTC_EXE_NAME} PRIVATE ${SDL2_MINGW_ROOT}/lib)
        set(SDL2_DLL_PATH "${SDL2_MINGW_ROOT}/bin/SDL2.dll")
        # GNU-style subsystem flags; static libc++/libunwind so no libc++.dll is needed (SDL2 stays a DLL).
        set_target_properties(${MTC_EXE_NAME} PROPERTIES
            LINK_FLAGS_RELEASE "-mwindows"
            LINK_FLAGS_RELWITHDEBINFO "-mwindows"
            LINK_FLAGS_MINSIZEREL "-mwindows")
        target_link_options(${MTC_EXE_NAME} PRIVATE -static-libstdc++ -static-libgcc)
        target_link_libraries(${MTC_EXE_NAME} PRIVATE SDL2 winmm shell32)
    else()
        FetchContent_Declare(sdl2
            URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip)
        FetchContent_MakeAvailable(sdl2)
        target_include_directories(${MTC_EXE_NAME} PRIVATE ${sdl2_SOURCE_DIR}/include)
        target_link_directories(${MTC_EXE_NAME} PRIVATE ${sdl2_SOURCE_DIR}/lib/x64)
        set(SDL2_DLL_PATH "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll")
        set_target_properties(${MTC_EXE_NAME} PROPERTIES
            LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
            LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
        target_link_libraries(${MTC_EXE_NAME} PRIVATE SDL2 Winmm.lib Shell32.lib)
    endif()

    add_custom_command(TARGET ${MTC_EXE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL2_DLL_PATH}"
            "${MTC_RT64_DIR}/src/contrib/dxc/bin/x64/dxil.dll"
            "${MTC_RT64_DIR}/src/contrib/dxc/bin/x64/dxcompiler.dll"
            $<TARGET_FILE_DIR:${MTC_EXE_NAME}>)

    target_sources(${MTC_EXE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/icons/app.rc)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SDL2 REQUIRED)
    add_compile_definitions("RT64_SDL_WINDOW_VULKAN")

    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
        COMMAND file_to_c ${CMAKE_SOURCE_DIR}/icons/512.png icon_bytes ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
        DEPENDS ${CMAKE_SOURCE_DIR}/icons/512.png)
    target_sources(${MTC_EXE_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c)

    target_include_directories(${MTC_EXE_NAME} PRIVATE ${SDL2_INCLUDE_DIRS})
    find_package(Freetype REQUIRED)
    target_link_libraries(${MTC_EXE_NAME} PRIVATE ${FREETYPE_LIBRARIES} SDL2::SDL2)

    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)
    target_link_libraries(${MTC_EXE_NAME} PRIVATE "-latomic -static-libstdc++" ${CMAKE_DL_LIBS} Threads::Threads)
endif()

if (MTC_STATIC_GAMECODE)
    target_sources(${MTC_EXE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/main/register_overlays.cpp ${MTC_RSP_DIR}/n_aspMain.cpp)
    target_compile_definitions(${MTC_EXE_NAME} PRIVATE MTC_STATIC_GAMECODE)
    target_link_libraries(${MTC_EXE_NAME} PRIVATE RecompiledFuncs)
endif()

# Shipped game-code inputs (no ROM data): configs, symbol file, module glue, and the runtime headers the
# generated code is compiled against. Copied next to the exe as gamecode/.
set(MTC_GAMECODE_HEADERS
    "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include/recomp.h|recomp.h"
    "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include/librecomp/rsp.hpp|librecomp/rsp.hpp"
    "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include/librecomp/rsp_vu.hpp|librecomp/rsp_vu.hpp"
    "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include/librecomp/rsp_vu_impl.hpp|librecomp/rsp_vu_impl.hpp"
    "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include/librecomp/sections.h|librecomp/sections.h"
    "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include/ultramodern/ultra64.h|ultramodern/ultra64.h"
)
set(MTC_GAMECODE_COPY_CMDS)
foreach(entry ${MTC_GAMECODE_HEADERS})
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 src)
    list(GET parts 1 dst)
    list(APPEND MTC_GAMECODE_COPY_CMDS COMMAND ${CMAKE_COMMAND} -E copy_if_different "${src}" "$<TARGET_FILE_DIR:${MTC_EXE_NAME}>/gamecode/include/${dst}")
endforeach()
add_custom_command(TARGET ${MTC_EXE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${MTC_EXE_NAME}>/gamecode/glue"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/gamecode/mtc.us.toml" "${CMAKE_SOURCE_DIR}/gamecode/n_aspMain.us.toml" "${CMAKE_SOURCE_DIR}/gamecode/mtc.syms.toml"
        "$<TARGET_FILE_DIR:${MTC_EXE_NAME}>/gamecode"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/gamecode/glue/mtc_game_abi.h" "${CMAKE_SOURCE_DIR}/gamecode/glue/mtc_game_glue.cpp" "${CMAKE_SOURCE_DIR}/gamecode/glue/mtc_game_tables.cpp"
        "$<TARGET_FILE_DIR:${MTC_EXE_NAME}>/gamecode/glue"
    ${MTC_GAMECODE_COPY_CMDS})

target_link_libraries(${MTC_EXE_NAME} PRIVATE
    librecomp
    ultramodern
    rt64
    RmlUi::Core
    RmlUi::Debugger
    nfd
    lunasvg
)

# ---- Widescreen borders (MIT module in ../widescreen; art is decoded from the user's ROM at runtime) --
set(MTC_WIDESCREEN_DIR "${CMAKE_SOURCE_DIR}/../widescreen" CACHE PATH "Widescreen border module directory")
get_filename_component(MTC_WIDESCREEN_DIR "${MTC_WIDESCREEN_DIR}" ABSOLUTE)
set(MTC_WS_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(${MTC_WIDESCREEN_DIR} ${CMAKE_BINARY_DIR}/mtc_widescreen)
# Embed the data-only manifest (no ROM data in it) so the exe needs no extra file.
set(MTC_WS_MANIFEST "${MTC_WIDESCREEN_DIR}/data/borders.manifest")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MTC_WS_MANIFEST}")
file(READ "${MTC_WS_MANIFEST}" MTC_WS_MANIFEST_TEXT)
file(WRITE "${CMAKE_BINARY_DIR}/mtc_ws_gen/mtc_ws_manifest.inc" "R\"MTCWS(${MTC_WS_MANIFEST_TEXT})MTCWS\"\n")
target_include_directories(${MTC_EXE_NAME} PRIVATE "${CMAKE_BINARY_DIR}/mtc_ws_gen")
target_link_libraries(${MTC_EXE_NAME} PRIVATE mtc_widescreen)

# DXC for the UI shaders (mirrors RT64's own setup; see Zelda64Recomp).
set(DXC_COMMON_OPTS "-I${MTC_RT64_DIR}/src")
set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_0" "-D DYNAMIC_RENDER_PARAMS")
set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_0" "-D DYNAMIC_RENDER_PARAMS" "-fvk-invert-y")
set(DXC_CS_OPTS "${DXC_COMMON_OPTS}" "-E" "CSMain" "-T cs_6_0")
set(DXC_GS_OPTS "${DXC_COMMON_OPTS}" "-E" "GSMain" "-T gs_6_0")

if (WIN32 AND NOT CMAKE_HOST_WIN32)
    # [n64recomp-cross] Linux host -> Windows target: use the Linux DXC binary
    set(DXC "LD_LIBRARY_PATH=${MTC_RT64_DIR}/src/contrib/dxc/lib/x64" "${MTC_RT64_DIR}/src/contrib/dxc/bin/x64/dxc-linux")
    add_compile_definitions(NOMINMAX)
elseif (WIN32)
    set(DXC "${MTC_RT64_DIR}/src/contrib/dxc/bin/x64/dxc.exe")
    add_compile_definitions(NOMINMAX)
else()
    if (CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set(DXC "LD_LIBRARY_PATH=${MTC_RT64_DIR}/src/contrib/dxc/lib/x64" "${MTC_RT64_DIR}/src/contrib/dxc/bin/x64/dxc-linux")
    else()
        set(DXC "LD_LIBRARY_PATH=${MTC_RT64_DIR}/src/contrib/dxc/lib/arm64" "${MTC_RT64_DIR}/src/contrib/dxc/bin/arm64/dxc-linux")
    endif()
endif()

# Compiled UI shaders go to ui-shaders/ so the build dir's shaders/ can hold RT64 post-processing presets.
build_vertex_shader(${MTC_EXE_NAME} "shaders/InterfaceVS.hlsl" "ui-shaders/InterfaceVS.hlsl")
build_pixel_shader(${MTC_EXE_NAME} "shaders/InterfacePS.hlsl" "ui-shaders/InterfacePS.hlsl")

target_sources(${MTC_EXE_NAME} PRIVATE ${SOURCES})

# Crash reports (src/main/crash_report.cpp): DbgHelp stack walks + minidumps on Windows; a link map next to the
# exe (local only, not packaged) resolves the module+offset frames in a report (tools/symbolize_crash.py).
if (WIN32)
    target_link_libraries(${MTC_EXE_NAME} PRIVATE dbghelp)
    if (MINGW)
        target_link_options(${MTC_EXE_NAME} PRIVATE "-Wl,-Map=${CMAKE_BINARY_DIR}/${MTC_EXE_NAME}.map")
    endif()
endif()
set_property(TARGET ${MTC_EXE_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

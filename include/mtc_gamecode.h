#ifndef __MTC_GAMECODE_H__
#define __MTC_GAMECODE_H__

// Option (b) (OPTB.md): the game code is not shipped. It is generated on the player's PC from their own ROM
// (N64Recomp + RSPRecomp), compiled with the bundled toolchain into a module (mtc_game.dll /
// libmtc_game.so) cached in the app data folder, and loaded at runtime.
// With MTC_STATIC_GAMECODE (developer builds) the code is linked into the exe instead and all of this is a no-op.

#include <cstdint>
#include <functional>
#include <string>

namespace mtc::gamecode {
    enum class State : int {
        NoRom = 0,      // no (valid) ROM stored yet
        NeedsBuild = 1, // ROM present, module missing or stale
        Building = 2,
        Ready = 3,      // module built (and loaded once load() succeeded)
        Failed = 4,
    };

    bool is_static();

    // Cached module present and built from the stored ROM by this app version.
    bool module_is_current();

    // Blocking. progress(fraction 0..1, message). Returns false and sets `error` (user-facing) on failure.
    bool generate(const std::function<void(float, const std::string&)>& progress, std::string& error);

    // Loads the cached module and registers its function tables with librecomp. Idempotent.
    bool load(std::string& error);
    bool is_loaded();

    // Launcher helpers: background build with progress, polled from the UI thread.
    void refresh_state();          // re-evaluate NoRom / NeedsBuild / Ready
    void start_async_build();      // no-op unless NeedsBuild or Failed
    State state();
    float progress();
    std::string message();
    uint32_t change_counter();     // increments on every state/progress change
}

#endif

#pragma once
// Widescreen borders glue: composes the border canvas for the current screen from the user's ROM
// (widescreen/ module, data-only manifest) and hands it to RT64 (rt64_widescreen_* API).
#include <cstdint>
#include <string>
#include <vector>

extern "C" void mtc_ws_on_bg_load(uint8_t* rdram, uint32_t rom_offset); // called by the func_80053078 hook

namespace mtc::widescreen {
    void attach(void* rt64_app);   // renderer created (RT64::Application*)
    void detach(void* rt64_app);   // renderer shutting down
    void refresh();                // style / shader scope changed in the config
    std::vector<std::pair<std::string, std::string>> styles(); // (id, label), for a menu
    std::string current_screen();
    std::string default_style();   // manifest default style id (what "default" in the config means), "" if unavailable
}

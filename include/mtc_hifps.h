#pragma once
#include <cstdint>
// High frame rate glue: hands the frame-interpolation and limiter settings to RT64 (rt64_hifps_* API).
// The game itself always ticks at 60 Hz; RT64 renders the extra frames in between (see APP.md, "High fps").

namespace mtc::hifps {
    void attach(void* rt64_app); // renderer created (RT64::Application*)
    void detach(void* rt64_app); // renderer shutting down
    void refresh();              // limiter / interpolation option changed

    // Optional explicit object ids ("tag hooks"). A RecompiledFuncs generated with tools/hifps/gen_tag_hooks.py
    // calls mtc_hifps_fn_enter/mtc_hifps_fn_exit around the game's 2D draw functions; the display list commands
    // each call writes are tagged with an id made of (function, a0 object pointer, caller). Called from send_dl
    // with the task's display list address, right before RT64 processes it, to hand that list's ranges over.
    void before_display_list(uint32_t dl_address);
}

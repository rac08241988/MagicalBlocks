// Graphics presets, framerate limiter and resolution options for MagicalBlocks.
// These sit on top of ultramodern::renderer::GraphicsConfig (which RT64 consumes) - see APP.md.
#include "mtc_config.h"
#include "mtc_widescreen_glue.h"
#include "mtc_hifps.h"
#include "mtc_support.h"
#include "SDL.h"

#include <algorithm>
#include <mutex>

using ultramodern::renderer::AspectRatio;
using ultramodern::renderer::RefreshRate;
using ultramodern::renderer::Resolution;

namespace {
    std::mutex extra_mutex;
    mtc::ExtraGraphicsConfig extra_config{};

    // RT64 treats a Manual target as a frame-pacing target; "Uncapped" asks for more than any display does.
    constexpr int uncapped_target = 1000;

    int framerate_value(mtc::FramerateMode mode, int custom) {
        switch (mode) {
            case mtc::FramerateMode::Fps60:   return 60;
            case mtc::FramerateMode::Fps90:   return 90;
            case mtc::FramerateMode::Fps120:  return 120;
            case mtc::FramerateMode::Fps144:  return 144;
            case mtc::FramerateMode::Fps238:  return 238;
            case mtc::FramerateMode::Uncapped: return uncapped_target;
            case mtc::FramerateMode::Custom:  return std::clamp(custom, 20, 1000);
            default: return 60;
        }
    }
}

const std::vector<mtc::GraphicsPreset>& mtc::get_graphics_presets() {
    static const std::vector<GraphicsPreset> presets = {
        // Vanilla: what the cartridge does - 4:3, native 320x240, 60 Hz, no borders.
        { "vanilla",          "Vanilla (4:3, native res, 60 fps)", AspectRatio::Original, ResolutionMode::Native, FramerateMode::Fps60,  false },
        // Recomp: 16:9 output with widescreen borders around the original 4:3 game, window resolution, 60 fps.
        { "recomp",           "Recomp (16:9 borders, 60 fps)",     AspectRatio::Original, ResolutionMode::Window, FramerateMode::Fps60,  true },
        { "recomp_1080p_60",  "Recomp 1080p 60",                   AspectRatio::Original, ResolutionMode::R1080p, FramerateMode::Fps60,  true },
        { "recomp_1080p_120", "Recomp 1080p 120",                  AspectRatio::Original, ResolutionMode::R1080p, FramerateMode::Fps120, true },
        { "recomp_1080p_144", "Recomp 1080p 144",                  AspectRatio::Original, ResolutionMode::R1080p, FramerateMode::Fps144, true },
        { "recomp_1440p_144", "Recomp 1440p 144",                  AspectRatio::Original, ResolutionMode::R1440p, FramerateMode::Fps144, true },
        { "recomp_1440p_238", "Recomp 1440p 238",                  AspectRatio::Original, ResolutionMode::R1440p, FramerateMode::Fps238, true },
        { "recomp_4k_60",     "Recomp 4K 60",                      AspectRatio::Original, ResolutionMode::R2160p, FramerateMode::Fps60,  true },
        { "recomp_4k_120",    "Recomp 4K 120",                     AspectRatio::Original, ResolutionMode::R2160p, FramerateMode::Fps120, true },
    };
    return presets;
}

const mtc::ExtraGraphicsConfig& mtc::get_extra_graphics_config() {
    std::lock_guard lock{ extra_mutex };
    return extra_config;
}

void mtc::set_extra_graphics_config(const ExtraGraphicsConfig& cfg) {
    {
        std::lock_guard lock{ extra_mutex };
        extra_config = cfg;
    }
    mtc::widescreen::refresh();
    mtc::hifps::refresh();
}

bool mtc::borders_enabled(const ExtraGraphicsConfig& extra) {
    return !extra.widescreen.empty() && extra.widescreen != "off";
}

void mtc::resolve_extra_options(ultramodern::renderer::GraphicsConfig& gfx, const ExtraGraphicsConfig& extra) {
    // Borders live in the pillarbox, so they need the game at its Original aspect (Expand leaves no side space).
    if (borders_enabled(extra)) {
        gfx.ar_option = AspectRatio::Original;
    }
    switch (extra.framerate) {
        case FramerateMode::Fps60:
            gfx.rr_option = RefreshRate::Original;
            gfx.rr_manual_value = 60;
            break;
        case FramerateMode::VSync:
            gfx.rr_option = RefreshRate::Display;
            break;
        default:
            gfx.rr_option = RefreshRate::Manual;
            gfx.rr_manual_value = framerate_value(extra.framerate, extra.framerate_custom);
            break;
    }

    switch (extra.resolution) {
        case ResolutionMode::Native:   gfx.res_option = Resolution::Original; break;
        case ResolutionMode::Native2x: gfx.res_option = Resolution::Original2x; break;
        default:                       gfx.res_option = Resolution::Auto; gfx.ds_option = 1; break;
    }
}

bool mtc::apply_preset(const std::string& preset_id, ultramodern::renderer::GraphicsConfig& gfx, ExtraGraphicsConfig& extra) {
    for (const auto& p : get_graphics_presets()) {
        if (p.id == preset_id) {
            gfx.ar_option = p.aspect;
            if (p.borders) {
                if (!borders_enabled(extra)) extra.widescreen = "default";
            } else {
                extra.widescreen = "off";
            }
            extra.resolution = p.resolution;
            extra.framerate = p.framerate;
            extra.preset = p.id;
            if (p.resolution == ResolutionMode::Native) gfx.ds_option = 1;
            resolve_extra_options(gfx, extra);
            return true;
        }
    }
    return false;
}

std::string mtc::detect_preset(const ultramodern::renderer::GraphicsConfig& gfx, const ExtraGraphicsConfig& extra) {
    for (const auto& p : get_graphics_presets()) {
        if (gfx.ar_option == p.aspect && extra.resolution == p.resolution && extra.framerate == p.framerate &&
            borders_enabled(extra) == p.borders) {
            return p.id;
        }
    }
    return "custom";
}

void mtc::get_requested_window_size(const ExtraGraphicsConfig& extra, int& width, int& height) {
    switch (extra.resolution) {
        case ResolutionMode::R720p:  width = 1280; height = 720;  return;
        case ResolutionMode::R1080p: width = 1920; height = 1080; return;
        case ResolutionMode::R1440p: width = 2560; height = 1440; return;
        case ResolutionMode::R2160p: width = 3840; height = 2160; return;
        default: width = 0; height = 0; return;
    }
}

void mtc::apply_window_size_request(SDL_Window* window) {
    if (window == nullptr) return;
    if (ultramodern::renderer::get_graphics_config().wm_option == ultramodern::renderer::WindowMode::Fullscreen) {
        return; // fullscreen renders at display resolution
    }
    int w = 0, h = 0;
    const ExtraGraphicsConfig extra = get_extra_graphics_config();
    get_requested_window_size(extra, w, h);
    if (w == 0) {
        // "Window" resolution: keep the user's window, but borders need a 16:9 output - widen a 4:3 window.
        if (!borders_enabled(extra)) return;
        SDL_GetWindowSize(window, &w, &h);
        if (h <= 0 || w * 9 >= h * 16 - 9) return;
        w = (h * 16 + 8) / 9;
    }
    // Don't create a window larger than the usable desktop area; RT64 renders at window resolution.
    SDL_Rect usable{};
    int display = SDL_GetWindowDisplayIndex(window);
    if (display >= 0 && SDL_GetDisplayUsableBounds(display, &usable) == 0 && usable.w > 0 && usable.h > 0) {
        float scale = std::min(1.0f, std::min(float(usable.w) / w, float(usable.h) / h));
        w = int(w * scale); h = int(h * scale);
    }
    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

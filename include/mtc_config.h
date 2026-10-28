#ifndef __MTC_CONFIG_H__
#define __MTC_CONFIG_H__

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include "ultramodern/config.hpp"
#include "recomp_input.h"

struct SDL_Window;

namespace mtc {
    constexpr std::u8string_view program_id = u8"MagicalBlocks";
    constexpr std::string_view program_name = "Magical Blocks";

    void load_config();
    void save_config();

    void reset_input_bindings();
    void reset_cont_input_bindings();
    void reset_kb_input_bindings();
    void reset_single_input_binding(recomp::InputDevice device, recomp::GameInput input);

    std::filesystem::path get_app_folder_path(); // cached; falls back from an unwritable portable folder
    std::filesystem::path detect_app_folder_path();
    std::string app_folder_note(); // non-empty when the portable folder wasn't writable

    bool get_debug_mode_enabled();
    void set_debug_mode_enabled(bool enabled);

    void open_quit_game_prompt();

    // ---------------------------------------------------------------------------------------------
    // Project-level graphics options layered on top of ultramodern::renderer::GraphicsConfig.
    // See APP.md "Options menu" for how each one maps onto RT64 / ultramodern.
    // ---------------------------------------------------------------------------------------------

    // Framerate limiter. Game logic always runs at the original 60 Hz VI rate (one tick per retrace);
    // these only change the presentation rate (RT64 frame interpolation / pacing).
    enum class FramerateMode {
        Fps60,      // Original (RefreshRate::Original)
        Fps90,
        Fps120,
        Fps144,
        Fps238,     // e.g. 240 Hz VRR monitors capped just under the refresh rate
        Uncapped,   // Manual with a very high target
        Custom,     // Manual with framerate_custom
        VSync,      // RefreshRate::Display
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(mtc::FramerateMode, {
        {mtc::FramerateMode::Fps60, "60"},
        {mtc::FramerateMode::Fps90, "90"},
        {mtc::FramerateMode::Fps120, "120"},
        {mtc::FramerateMode::Fps144, "144"},
        {mtc::FramerateMode::Fps238, "238"},
        {mtc::FramerateMode::Uncapped, "Uncapped"},
        {mtc::FramerateMode::Custom, "Custom"},
        {mtc::FramerateMode::VSync, "VSync"},
    });

    // Output resolution. Native/2x use RT64's fixed internal resolutions; the others render at
    // window resolution (Resolution::Auto) and size the window to that resolution when windowed.
    enum class ResolutionMode {
        Native,     // 320x240 (Resolution::Original)
        Native2x,   // 640x480 (Resolution::Original2x)
        R720p,
        R1080p,
        R1440p,
        R2160p,
        Window,     // whatever the window/display is (Resolution::Auto)
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(mtc::ResolutionMode, {
        {mtc::ResolutionMode::Native, "Native"},
        {mtc::ResolutionMode::Native2x, "Native2x"},
        {mtc::ResolutionMode::R720p, "720p"},
        {mtc::ResolutionMode::R1080p, "1080p"},
        {mtc::ResolutionMode::R1440p, "1440p"},
        {mtc::ResolutionMode::R2160p, "2160p"},
        {mtc::ResolutionMode::Window, "Window"},
    });

    struct GraphicsPreset {
        std::string id;          // stable id stored in graphics.json
        std::string label;       // shown in the menu
        ultramodern::renderer::AspectRatio aspect;
        ResolutionMode resolution;
        FramerateMode framerate;
        bool borders;            // widescreen borders on (forces the Original 4:3 game aspect inside a 16:9 output)
    };

    // "Vanilla", "Recomp", "Recomp 1080p 120", ... plus "Custom" (id "custom") when the
    // individual options no longer match any preset.
    const std::vector<GraphicsPreset>& get_graphics_presets();

    struct ExtraGraphicsConfig {
        std::string preset = "recomp";
        FramerateMode framerate = FramerateMode::Fps60;
        int framerate_custom = 60;
        ResolutionMode resolution = ResolutionMode::Window;
        std::string shader = "";       // RT64 post-processing preset id ("" = none), applied live from the menu
        std::string widescreen = "default"; // border style id from the widescreen manifest, "default" (= manifest default, Bricks for now) or "off"
        bool widescreen_shaders_on_borders = false; // post-processing on game + borders (false = game only)
        bool frame_interpolation = true; // above 60 fps: interpolate 2D draws (false = repeat frames)

        auto operator<=>(const ExtraGraphicsConfig&) const = default;
    };

    const ExtraGraphicsConfig& get_extra_graphics_config();
    void set_extra_graphics_config(const ExtraGraphicsConfig& cfg);
    // Widescreen borders are on unless the style is "off"/empty. When on, the game is drawn at its Original
    // 4:3 aspect and the borders fill the rest of a 16:9 (or wider) output.
    bool borders_enabled(const ExtraGraphicsConfig& extra);

    // Applies a preset id to both config structs (does not apply them to the renderer).
    bool apply_preset(const std::string& preset_id,
                      ultramodern::renderer::GraphicsConfig& gfx, ExtraGraphicsConfig& extra);
    // Returns the id of the preset matching the given options, or "custom".
    std::string detect_preset(const ultramodern::renderer::GraphicsConfig& gfx, const ExtraGraphicsConfig& extra);
    // Translates the extra options into GraphicsConfig fields (rr_option / rr_manual_value / res_option).
    void resolve_extra_options(ultramodern::renderer::GraphicsConfig& gfx, const ExtraGraphicsConfig& extra);
    // Requested window size for the current resolution mode (0,0 = don't care).
    void get_requested_window_size(const ExtraGraphicsConfig& extra, int& width, int& height);

    // Resizes/centres the SDL window for the selected resolution mode (windowed only).
    void apply_window_size_request(::SDL_Window* window);

};

#endif

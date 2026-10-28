// Widescreen borders glue (see widescreen/WIDESCREEN.md).
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "mtc_widescreen/borders.h"
#include "render/rt64_widescreen_api.h"
#include "librecomp/game.hpp"
#include "mtc_game.h"
#include "mtc_config.h"
#include "mtc_widescreen_glue.h"

namespace {
    const char builtin_manifest[] =
#include "mtc_ws_manifest.inc"
        ;

    std::mutex mtx;
    void* app = nullptr;
    bool tried = false, ready = false;
    mtcws::Borders borders;
    mtcws::Rom rom;
    std::string screen = "other";
    std::string pushed_key;
    std::map<std::string, mtcws::Image> cache; // "style|screen"

    // Reads a file through std::filesystem::path (wide APIs on Windows): path::string() converts to the ANSI code
    // page and throws for profile folders with characters outside it, which used to escape as std::terminate.
    bool read_file(const std::filesystem::path& path, std::string& out) {
        std::ifstream f{ path, std::ios::binary };
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    bool load_locked_impl();

    bool load_locked() {
        if (tried) return ready;
        try {
            return load_locked_impl();
        }
        catch (const std::exception& e) {
            fprintf(stderr, "[widescreen] load failed: %s\n", e.what());
            ready = false;
            return false;
        }
    }

    bool load_locked_impl() {
        tried = true;
        std::string err;
        const std::filesystem::path cfg = recomp::get_config_path();
        // A manifest next to the config (e.g. ~/.config/MagicalBlocks/widescreen.manifest)
        // replaces the built-in one, so border art can be changed without rebuilding.
        const std::filesystem::path override_path = cfg / "widescreen.manifest";
        static bool manifest_loaded = false;
        if (!manifest_loaded) {
            bool ok;
            std::string text;
            if (std::filesystem::exists(override_path) && read_file(override_path, text)) ok = borders.loadManifest(text, err);
            else ok = borders.loadManifest(builtin_manifest, err);
            if (!ok) { fprintf(stderr, "[widescreen] manifest: %s\n", err.c_str()); return false; }
            manifest_loaded = true;
        }
        const std::filesystem::path rom_path = cfg / (std::u8string(mtc::game_id) + u8".z64");
        if (!std::filesystem::exists(rom_path)) {
            tried = false; // no ROM stored yet (first run): try again once the user has selected one
            return false;
        }
        std::string bytes;
        if (!read_file(rom_path, bytes)) {
            fprintf(stderr, "[widescreen] rom: could not read the stored ROM\n");
            return false;
        }
        if (!rom.assign(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), err) || !borders.checkRom(rom, err)) {
            fprintf(stderr, "[widescreen] rom: %s\n", err.c_str());
            return false;
        }
        ready = true;
        return true;
    }

    std::string wanted_style_locked() {
        std::string s = mtc::get_extra_graphics_config().widescreen;
        if (const char* env = std::getenv("MTC_WS_STYLE")) s = env;
        if (s.empty() || s == "off") return "";
        if (s == "default") return borders.defaultStyle();
        return s;
    }

    int wanted_scope() {
        bool all = mtc::get_extra_graphics_config().widescreen_shaders_on_borders;
        if (const char* env = std::getenv("MTC_WS_SHADERS")) all = std::string(env) == "all";
        return all ? RT64_WIDESCREEN_SHADERS_GAME_AND_BORDERS : RT64_WIDESCREEN_SHADERS_GAME_ONLY;
    }

    void push_locked(bool force) {
        if (app == nullptr) return;
        rt64_widescreen_set_shader_scope(app, wanted_scope());
        const std::string style = load_locked() ? wanted_style_locked() : std::string();
        if (style.empty()) {
            rt64_widescreen_set_enabled(app, 0);
            pushed_key.clear();
            return;
        }
        const std::string key = style + "|" + screen;
        if (!force && key == pushed_key) return;
        auto it = cache.find(key);
        if (it == cache.end()) {
            mtcws::Image img;
            std::string err;
            if (!borders.compose(rom, style, screen, borders.defaultSideWidth(), img, err)) {
                fprintf(stderr, "[widescreen] compose %s: %s\n", key.c_str(), err.c_str());
                rt64_widescreen_set_enabled(app, 0);
                return;
            }
            it = cache.emplace(key, std::move(img)).first;
        }
        rt64_widescreen_set_canvas(app, it->second.rgba.data(), it->second.width, it->second.height, borders.gameWidth());
        rt64_widescreen_set_enabled(app, 1);
        pushed_key = key;
    }
}

extern "C" void mtc_ws_on_bg_load(uint8_t* rdram, uint32_t rom_offset) {
    (void)rdram;
    // Runs on the game thread inside recompiled code: nothing may escape from here.
    try {
        std::lock_guard lock{ mtx };
        if (!load_locked()) return;
        std::string key = borders.screenForBgLoad(rom_offset);
        if (key.empty()) key = "other";
        if (key == screen) return;
        screen = key;
        push_locked(false);
    }
    catch (const std::exception& e) {
        fprintf(stderr, "[widescreen] bg load hook: %s\n", e.what());
    }
}

void mtc::widescreen::attach(void* rt64_app) {
    if (!rt64_widescreen_supported()) return;
    std::lock_guard lock{ mtx };
    app = rt64_app;
    push_locked(true);
}

void mtc::widescreen::detach(void* rt64_app) {
    std::lock_guard lock{ mtx };
    if (app == rt64_app) { app = nullptr; pushed_key.clear(); }
}

void mtc::widescreen::refresh() {
    std::lock_guard lock{ mtx };
    push_locked(false);
}

std::vector<std::pair<std::string, std::string>> mtc::widescreen::styles() {
    std::lock_guard lock{ mtx };
    std::vector<std::pair<std::string, std::string>> out;
    if (!load_locked()) return out;
    for (const std::string& s : borders.styles()) out.emplace_back(s, borders.styleLabel(s));
    return out;
}

std::string mtc::widescreen::default_style() {
    std::lock_guard lock{ mtx };
    if (!load_locked()) return {};
    return borders.defaultStyle();
}

std::string mtc::widescreen::current_screen() {
    std::lock_guard lock{ mtx };
    return screen;
}

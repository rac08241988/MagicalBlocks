#include "recomp_ui.h"
#include "mtc_config.h"
#include "mtc_support.h"
#include "mtc_game.h"
#include "mtc_gamecode.h"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"
#include "RmlUi/Core.h"
#include "nfd.h"
#include <filesystem>

static std::string version_string;

Rml::DataModelHandle model_handle;
bool mtc_rom_valid = false;
// Option (b) game-code preparation status (mtc::gamecode::State), shown by launcher.rml.
int mtc_code_state = 0;
int mtc_code_percent = 0;
std::string mtc_code_message;
static uint32_t mtc_code_seen = 0xFFFFFFFF;

static void start_preparing_game() {
    mtc::gamecode::refresh_state();
    mtc::gamecode::start_async_build();
}

// Called on the UI thread every frame (ui_state.cpp) to mirror the background build's progress.
void mtc_launcher_ui_tick() {
    uint32_t c = mtc::gamecode::change_counter();
    if (c == mtc_code_seen || !model_handle) return;
    mtc_code_seen = c;
    mtc_code_state = (int)mtc::gamecode::state();
    mtc_code_percent = int(mtc::gamecode::progress() * 100.0f + 0.5f);
    mtc_code_message = mtc::gamecode::message();
    model_handle.DirtyVariable("mtc_code_state");
    model_handle.DirtyVariable("mtc_code_percent");
    model_handle.DirtyVariable("mtc_code_message");
}

extern std::vector<recomp::GameEntry> supported_games;

// ROM picker: the user selects their own dump (.z64/.v64/.n64). We normalize the byte order and check the
// SHA1 against the USA (NMTE) dump ourselves so we can give a friendly message, then hand it to librecomp,
// which stores the normalized copy in the app folder (<app folder>/mtc.n64.us.z64) so later launches
// don't ask again.
void select_rom() {
    mtc::open_file_dialog([](bool success, const std::filesystem::path& path) {
        if (!success) {
            return;
        }
        mtc::RomCheck check = mtc::check_rom_file(path);
        if (check.result != mtc::RomCheckResult::Good) {
            recompui::message_box(mtc::rom_check_message(check).c_str());
            return;
        }
        recomp::RomValidationError rom_error = recomp::select_rom(path, mtc::game_id);
        switch (rom_error) {
            case recomp::RomValidationError::Good:
                mtc_rom_valid = true;
                model_handle.DirtyVariable("mtc_rom_valid");
                start_preparing_game();
                break;
            case recomp::RomValidationError::FailedToOpen:
                recompui::message_box("Failed to open ROM file.");
                break;
            default:
                // Should not happen: the SHA1 already matched. Most likely the app folder isn't writable.
                recompui::message_box("The ROM was verified, but it could not be saved to the app data folder. "
                                      "Please check that the folder is writable and try again.");
                break;
        }
    });
}

recompui::ContextId launcher_context;

recompui::ContextId recompui::get_launcher_context_id() {
	return launcher_context;
}

class LauncherMenu : public recompui::MenuController {
public:
    LauncherMenu() {
        mtc_rom_valid = recomp::is_rom_valid(mtc::game_id);
        start_preparing_game();
    }
    ~LauncherMenu() override {

    }
    void load_document() override {
		launcher_context = recompui::create_context(mtc::get_asset_path("launcher.rml"));
    }
    void register_events(recompui::UiEventListenerInstancer& listener) override {
        recompui::register_event(listener, "select_rom",
            [](const std::string& param, Rml::Event& event) {
                select_rom();
            }
        );
        recompui::register_event(listener, "rom_selected",
            [](const std::string& param, Rml::Event& event) {
                mtc_rom_valid = true;
                model_handle.DirtyVariable("mtc_rom_valid");
                start_preparing_game();
            }
        );
        recompui::register_event(listener, "retry_prepare",
            [](const std::string& param, Rml::Event& event) {
                start_preparing_game();
            }
        );
        recompui::register_event(listener, "start_game",
            [](const std::string& param, Rml::Event& event) {
                if (mtc::gamecode::state() != mtc::gamecode::State::Ready) {
                    return;
                }
                std::string err;
                if (!mtc::gamecode::load(err)) {
                    recompui::message_box(("The game could not be started: " + err).c_str());
                    return;
                }
                recomp::start_game(mtc::game_id, "");
                recompui::hide_all_contexts();
            }
        );
        recompui::register_event(listener, "open_controls",
            [](const std::string& param, Rml::Event& event) {
                recompui::set_config_tab(recompui::ConfigTab::Controls);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "open_settings",
            [](const std::string& param, Rml::Event& event) {
                recompui::set_config_tab(recompui::ConfigTab::General);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "open_mods",
            [](const std::string &param, Rml::Event &event) {
                recompui::set_config_tab(recompui::ConfigTab::Mods);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "exit_game",
            [](const std::string& param, Rml::Event& event) {
                ultramodern::quit();
            }
        );
    }
    void make_bindings(Rml::Context* context) override {
        Rml::DataModelConstructor constructor = context->CreateDataModel("launcher_model");

        constructor.Bind("mtc_rom_valid", &mtc_rom_valid);
        constructor.Bind("mtc_code_state", &mtc_code_state);
        constructor.Bind("mtc_code_percent", &mtc_code_percent);
        constructor.Bind("mtc_code_message", &mtc_code_message);

        version_string = recomp::get_project_version().to_string();
        constructor.Bind("version_number", &version_string);

        model_handle = constructor.GetModelHandle();
    }
};

std::unique_ptr<recompui::MenuController> recompui::create_launcher_menu() {
    return std::make_unique<LauncherMenu>();
}

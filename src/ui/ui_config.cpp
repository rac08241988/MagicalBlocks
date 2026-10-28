#include "recomp_ui.h"
#include "recomp_input.h"
#include "mtc_sound.h"
#include "mtc_config.h"
#include "mtc_widescreen_glue.h"
#include "mtc_render.h"
#include "mtc_ui_nav.h"
#include "mtc_support.h"
#include "promptfont.h"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"
#include "RmlUi/Core.h"
#include <algorithm>

#include "core/ui_context.h"

ultramodern::renderer::GraphicsConfig new_options;
mtc::ExtraGraphicsConfig new_extra_options;
Rml::DataModelHandle nav_help_model_handle;
Rml::DataModelHandle general_model_handle;
Rml::DataModelHandle controls_model_handle;
Rml::DataModelHandle graphics_model_handle;
Rml::DataModelHandle sound_options_model_handle;

// True if controller config menu is open, false if keyboard config menu is open, undefined otherwise
bool configuring_controller = false;

int recompui::config_tab_to_index(recompui::ConfigTab tab) {
    switch (tab) {
    case recompui::ConfigTab::General:
        return 0;
    case recompui::ConfigTab::Controls:
        return 1;
    case recompui::ConfigTab::Graphics:
        return 2;
    case recompui::ConfigTab::Sound:
        return 3;
    case recompui::ConfigTab::Mods:
        return 4;
    case recompui::ConfigTab::Debug:
        return 5;
    default:
        assert(false && "Unknown config tab.");
        return 0;
    }
}

template <typename T>
void get_option(const T& input, Rml::Variant& output) {
    std::string value = "";
    to_json(value, input);

    if (value.empty()) {
        throw std::runtime_error("Invalid value :" + std::to_string(int(input)));
    }

    output = value;
}

template <typename T>
void set_option(T& output, const Rml::Variant& input) {
    T value = T::OptionCount;
    from_json(input.Get<std::string>(), value);

    if (value == T::OptionCount) {
        throw std::runtime_error("Invalid value :" + input.Get<std::string>());
    }

    output = value;
}

template <typename T>
void bind_option(Rml::DataModelConstructor& constructor, const std::string& name, T* option) {
    constructor.BindFunc(name,
        [option](Rml::Variant& out) { get_option(*option, out); },
        [option](const Rml::Variant& in) {
            set_option(*option, in);
            graphics_model_handle.DirtyVariable("options_changed");
            graphics_model_handle.DirtyVariable("ds_info");
        graphics_model_handle.DirtyVariable("border_style");
        graphics_model_handle.DirtyVariable("borders_on");
        graphics_model_handle.DirtyVariable("shader_scope");
        }
    );
};

template <typename T>
void bind_atomic(Rml::DataModelConstructor& constructor, Rml::DataModelHandle handle, const char* name, std::atomic<T>* atomic_val) {
    constructor.BindFunc(name,
        [atomic_val](Rml::Variant& out) {
            out = atomic_val->load();
        },
        [atomic_val, handle, name](const Rml::Variant& in) mutable {
            atomic_val->store(in.Get<T>());
            handle.DirtyVariable(name);
        }
    );
}

static int scanned_binding_index = -1;
static int scanned_input_index = -1;
static int focused_input_index = -1;
static int focused_config_option_index = -1;

static bool msaa2x_supported = false;
static bool msaa4x_supported = false;
static bool msaa8x_supported = false;
static bool sample_positions_supported = false;

static bool cont_active = true;

static recomp::InputDevice cur_device = recomp::InputDevice::Controller;

int recomp::get_scanned_input_index() {
    return scanned_input_index;
}

void recomp::finish_scanning_input(recomp::InputField scanned_field) {
    recomp::set_input_binding(static_cast<recomp::GameInput>(scanned_input_index), scanned_binding_index, cur_device, scanned_field);
    scanned_input_index = -1;
    scanned_binding_index = -1;
    controls_model_handle.DirtyVariable("inputs");
    controls_model_handle.DirtyVariable("active_binding_input");
    controls_model_handle.DirtyVariable("active_binding_slot");
    nav_help_model_handle.DirtyVariable("nav_help__accept");
    nav_help_model_handle.DirtyVariable("nav_help__exit");
    graphics_model_handle.DirtyVariable("gfx_help__apply");
}

void recomp::cancel_scanning_input() {
    recomp::stop_scanning_input();
    scanned_input_index = -1;
    scanned_binding_index = -1;
    controls_model_handle.DirtyVariable("inputs");
    controls_model_handle.DirtyVariable("active_binding_input");
    controls_model_handle.DirtyVariable("active_binding_slot");
    nav_help_model_handle.DirtyVariable("nav_help__accept");
    nav_help_model_handle.DirtyVariable("nav_help__exit");
    graphics_model_handle.DirtyVariable("gfx_help__apply");
}

void recomp::config_menu_set_cont_or_kb(bool cont_interacted) {
    if (cont_active != cont_interacted) {
        cont_active = cont_interacted;

        if (nav_help_model_handle) {
            nav_help_model_handle.DirtyVariable("nav_help__navigate");
            nav_help_model_handle.DirtyVariable("nav_help__accept");
            nav_help_model_handle.DirtyVariable("nav_help__exit");
            nav_help_model_handle.DirtyVariable("nav_help__tabs");
            nav_help_model_handle.DirtyVariable("nav_help__tabs_visible");
        }

        if (graphics_model_handle) {
            graphics_model_handle.DirtyVariable("gfx_help__apply");
        }
    }
}

void close_config_menu_impl() {
    mtc::save_config();

    recompui::ContextId config_context = recompui::get_config_context_id();
    recompui::ContextId sub_menu_context = recompui::get_config_sub_menu_context_id();

    if (recompui::is_context_shown(sub_menu_context)) {
    	recompui::hide_context(sub_menu_context);
    }
    else {
    	recompui::hide_context(config_context);
    }

    if (!ultramodern::is_game_started()) {
        recompui::show_context(recompui::get_launcher_context_id(), "");
    }
}

extern SDL_Window* window;

void apply_graphics_config(void) {
    mtc::resolve_extra_options(new_options, new_extra_options);
    new_extra_options.preset = mtc::detect_preset(new_options, new_extra_options);
    mtc::set_extra_graphics_config(new_extra_options);
    ultramodern::renderer::set_graphics_config(new_options);
#if defined(__linux__) // TODO: Remove once RT64 gets native fullscreen support on Linux
    if (new_options.wm_option == ultramodern::renderer::WindowMode::Fullscreen) {
        SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(window,0);
    }
#endif
    mtc::apply_window_size_request(window);
}

static bool graphics_options_changed() {
    return ultramodern::renderer::get_graphics_config() != new_options || mtc::get_extra_graphics_config() != new_extra_options;
}

void close_config_menu() {
    if (graphics_options_changed()) {
        recompui::open_choice_prompt(
            "Graphics options have changed",
            "Would you like to apply or discard the changes?",
            "Apply",
            "Discard",
            []() {
                apply_graphics_config();
                graphics_model_handle.DirtyAllVariables();
                close_config_menu_impl();
            },
            []() {
                new_options = ultramodern::renderer::get_graphics_config();
                new_extra_options = mtc::get_extra_graphics_config();
                graphics_model_handle.DirtyAllVariables();
                close_config_menu_impl();
            },
            recompui::ButtonVariant::Success,
            recompui::ButtonVariant::Error,
            true,
            "config__close-menu-button"
        );
        return;
    }

    close_config_menu_impl();
}

void mtc::open_quit_game_prompt() {
    recompui::open_choice_prompt(
        "Are you sure you want to quit?",
        "The current game will be closed.",
        "Quit",
        "Cancel",
        []() {
            ultramodern::quit();
        },
        []() {},
        recompui::ButtonVariant::Error,
        recompui::ButtonVariant::Tertiary,
        true,
        "config__quit-game-button"
    );
}

// These defaults values don't matter, as the config file handling overrides them.
struct ControlOptionsContext {
    int rumble_strength; // 0 to 100
    int gyro_sensitivity; // 0 to 100
    int mouse_sensitivity; // 0 to 100
    int joystick_deadzone; // 0 to 100
    recomp::BackgroundInputMode background_input_mode;
};

ControlOptionsContext control_options_context;

int recomp::get_rumble_strength() {
    return control_options_context.rumble_strength;
}

void recomp::set_rumble_strength(int strength) {
    control_options_context.rumble_strength = strength;
    if (general_model_handle) {
        general_model_handle.DirtyVariable("rumble_strength");
    }
}

int recomp::get_gyro_sensitivity() {
    return control_options_context.gyro_sensitivity;
}

int recomp::get_mouse_sensitivity() {
    return control_options_context.mouse_sensitivity;
}

int recomp::get_joystick_deadzone() {
    return control_options_context.joystick_deadzone;
}

void recomp::set_gyro_sensitivity(int sensitivity) {
    control_options_context.gyro_sensitivity = sensitivity;
    if (general_model_handle) {
        general_model_handle.DirtyVariable("gyro_sensitivity");
    }
}

void recomp::set_mouse_sensitivity(int sensitivity) {
    control_options_context.mouse_sensitivity = sensitivity;
    if (general_model_handle) {
        general_model_handle.DirtyVariable("mouse_sensitivity");
    }
}

void recomp::set_joystick_deadzone(int deadzone) {
    control_options_context.joystick_deadzone = deadzone;
    if (general_model_handle) {
        general_model_handle.DirtyVariable("joystick_deadzone");
    }
}

recomp::BackgroundInputMode recomp::get_background_input_mode() {
    return control_options_context.background_input_mode;
}

void recomp::set_background_input_mode(recomp::BackgroundInputMode mode) {
    control_options_context.background_input_mode = mode;
    if (general_model_handle) {
        general_model_handle.DirtyVariable("background_input_mode");
    }
    SDL_SetHint(
        SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
        mode == recomp::BackgroundInputMode::On
            ? "1"
            : "0"
    );
}

struct SoundOptionsContext {
    std::atomic<int> main_volume; // Option to control the volume of all sound
    void reset() {
        main_volume = 100;
    }
    SoundOptionsContext() {
        reset();
    }
};

SoundOptionsContext sound_options_context;

void mtc::reset_sound_settings() {
    sound_options_context.reset();
    if (sound_options_model_handle) {
        sound_options_model_handle.DirtyAllVariables();
    }
}

void mtc::set_main_volume(int volume) {
    sound_options_context.main_volume.store(volume);
    if (sound_options_model_handle) {
        sound_options_model_handle.DirtyVariable("main_volume");
    }
}

int mtc::get_main_volume() {
    return sound_options_context.main_volume.load();
}

struct DebugContext {
    Rml::DataModelHandle model_handle;
    bool debug_enabled = false;
};

DebugContext debug_context;

recompui::ContextId config_context;

recompui::ContextId recompui::get_config_context_id() {
	return config_context;
}

// Helper copied from RmlUi to get a named child.
Rml::Element* recompui::get_child_by_tag(Rml::Element* parent, const std::string& tag)
{
	// Look for the existing child
	for (int i = 0; i < parent->GetNumChildren(); i++)
	{
        Rml::Element* child = parent->GetChild(i);
		if (child->GetTagName() == tag)
			return child;
	}

    return nullptr;
}

// ---- post-processing shader list (RT64 postfx) ----
static std::vector<std::string> shader_ids;
static std::vector<std::string> shader_labels;
static std::vector<std::string> shader_licenses;
static std::string shader_error;
static bool show_shader_notices = false;
// RmlUi can re-submit a select's value when its option list changes (e.g. the list is rebuilt once the
// renderer is up), so only accept shader changes while the user is actually interacting with the select.
static bool shader_select_has_focus = false;

static std::string rml_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '{': out += "&#123;"; break;
            case '}': out += "&#125;"; break;
            case '\n': out += "<br/>"; break;
            default: out += c;
        }
    }
    return out;
}

static void dirty_shader_vars() {
    if (!graphics_model_handle) return;
    graphics_model_handle.DirtyVariable("shader_ids");
    graphics_model_handle.DirtyVariable("shader_labels");
    graphics_model_handle.DirtyVariable("shader");
    graphics_model_handle.DirtyVariable("shader_error");
    graphics_model_handle.DirtyVariable("shader_license");
    graphics_model_handle.DirtyVariable("shader_folder");
}

// Rebuilds the dropdown from RT64 (built-ins first, then <exe>/shaders via manifest.txt).
static void refresh_shader_list(bool dirty) {
    shader_ids.clear(); shader_labels.clear(); shader_licenses.clear();
    for (const auto& p : mtc::renderer::list_shader_presets()) {
        shader_ids.push_back(p.id);
        std::string label = p.name;
        if (!p.license.empty()) label += "  [" + p.license + "]";
        if (!p.id.empty() && !p.builtin) label += "  (shaders folder)";
        shader_labels.push_back(label);
        shader_licenses.push_back(p.license);
    }
    // Keep a saved-but-unlisted selection visible instead of showing an empty select.
    std::string active = mtc::renderer::active_shader_preset();
    if (std::find(shader_ids.begin(), shader_ids.end(), active) == shader_ids.end()) {
        shader_ids.push_back(active);
        shader_labels.push_back(active + " (not found)");
        shader_licenses.push_back("");
    }
    if (dirty) dirty_shader_vars();
}

// Widescreen border styles for the menu: "off" plus the manifest's styles (fallback list before a ROM is stored).
static std::vector<std::string> border_ids;
static std::vector<std::string> border_labels;
static std::string border_default_id = "bricks";
static void refresh_border_list() {
    border_ids = { "off" };
    border_labels = { "Off" };
    auto styles = mtc::widescreen::styles();
    if (styles.empty()) {
        styles = { {"bricks", "Bricks"}, {"stage", "Stage"}, {"planks", "Planks"}, {"characters", "Characters"} };
    }
    // Short menu names for the known styles; the manifest's longer description is the fallback.
    static const std::vector<std::pair<std::string, std::string>> short_names = {
        {"bricks", "Bricks"}, {"stage", "Stage"}, {"planks", "Planks"}, {"characters", "Characters"} };
    for (auto& [id, label] : styles) {
        std::string name = label;
        for (auto& [sid, sname] : short_names) if (sid == id) name = sname;
        border_ids.push_back(id);
        border_labels.push_back(name);
    }
    border_default_id = mtc::widescreen::default_style();
    if (border_default_id.empty()) border_default_id = border_ids.size() > 1 ? border_ids[1] : "off";
}

class ConfigTabsetListener : public Rml::EventListener {
    void ProcessEvent(Rml::Event& event) override {
        if (event.GetId() == Rml::EventId::Tabchange) {
            int tab_index = event.GetParameter<int>("tab_index", 0);
            bool in_mod_tab = (tab_index == recompui::config_tab_to_index(recompui::ConfigTab::Mods));
            if (in_mod_tab) {
                recompui::set_config_tabset_mod_nav();
            }
            else {
                Rml::ElementTabSet* tabset = recompui::get_config_tabset();
                Rml::Element* tabs = recompui::get_child_by_tag(tabset, "tabs");
                if (tabs != nullptr) {
                    size_t num_children = tabs->GetNumChildren();
                    for (size_t i = 0; i < num_children; i++) {
                        tabs->GetChild(i)->SetProperty(Rml::PropertyId::NavDown, Rml::Style::Nav::Auto);
                    }
                }
            }
        }
    }
};

class ConfigMenu : public recompui::MenuController {
private:
    ConfigTabsetListener config_tabset_listener;
public:
    ConfigMenu() {

    }
    ~ConfigMenu() override {

    }
    void load_document() override {
		config_context = recompui::create_context(mtc::get_asset_path("config_menu.rml"));
        recompui::update_mod_list(false);
        recompui::get_config_tabset()->AddEventListener(Rml::EventId::Tabchange, &config_tabset_listener);
    }
    void register_events(recompui::UiEventListenerInstancer& listener) override {
        recompui::register_event(listener, "apply_options",
            [](const std::string& param, Rml::Event& event) {
                graphics_model_handle.DirtyVariable("options_changed");
                apply_graphics_config();
            });
        recompui::register_event(listener, "config_keydown",
            [](const std::string& param, Rml::Event& event) {
                if (!recompui::is_prompt_open() && event.GetId() == Rml::EventId::Keydown) {
                    // Arrow keys/d-pad into and out of the scrollable option lists (RmlUi won't cross
                    // scroll-container boundaries on its own).
                    if (mtc::ui::navigate_across_scroll_containers(event)) {
                        return;
                    }
                    auto key = event.GetParameter<Rml::Input::KeyIdentifier>("key_identifier", Rml::Input::KeyIdentifier::KI_UNKNOWN);
                    switch (key) {
                        case Rml::Input::KeyIdentifier::KI_ESCAPE:
                            close_config_menu();
                            break;
                        case Rml::Input::KeyIdentifier::KI_F:
                            graphics_model_handle.DirtyVariable("options_changed");
                            apply_graphics_config();
                            break;
                    }
                }
            });
        // This needs to be separate from `close_config_menu` so it ensures that the event is only on the target
        recompui::register_event(listener, "close_config_menu_backdrop",
            [](const std::string& param, Rml::Event& event) {
                if (event.GetPhase() == Rml::EventPhase::Target) {
                    close_config_menu();
                }
            });
        recompui::register_event(listener, "close_config_menu",
            [](const std::string& param, Rml::Event& event) {
                close_config_menu();
            });

        recompui::register_event(listener, "open_quit_game_prompt",
            [](const std::string& param, Rml::Event& event) {
                mtc::open_quit_game_prompt();
            });

        recompui::register_event(listener, "toggle_input_device",
            [](const std::string& param, Rml::Event& event) {
                cur_device = cur_device == recomp::InputDevice::Controller
                    ? recomp::InputDevice::Keyboard
                    : recomp::InputDevice::Controller;
                controls_model_handle.DirtyVariable("input_device_is_keyboard");
                controls_model_handle.DirtyVariable("inputs");
            });
            
    }

    void bind_config_list_events(Rml::DataModelConstructor &constructor) {
        constructor.BindEventCallback("set_cur_config_index",
            [](Rml::DataModelHandle model_handle, Rml::Event& event, const Rml::VariantList& inputs) {
                int option_index = inputs.at(0).Get<size_t>();
                // watch for mouseout being overzealous during event bubbling, only clear if the event's attached element matches the current
                if (option_index == -1 && event.GetType() == "mouseout" && event.GetCurrentElement() != event.GetTargetElement()) {
                    return;
                }
                focused_config_option_index = option_index;
                model_handle.DirtyVariable("cur_config_index");
            });

        constructor.Bind("cur_config_index", &focused_config_option_index);
    }

    // Presets / framerate limiter / resolution / shader (MagicalBlocks additions).
    static void dirty_mtc_graphics() {
        graphics_model_handle.DirtyVariable("options_changed");
        graphics_model_handle.DirtyVariable("preset");
        graphics_model_handle.DirtyVariable("framerate");
        graphics_model_handle.DirtyVariable("framerate_custom");
        graphics_model_handle.DirtyVariable("resolution");
        graphics_model_handle.DirtyVariable("ar_option");
        graphics_model_handle.DirtyVariable("rr_option");
        graphics_model_handle.DirtyVariable("res_option");
        graphics_model_handle.DirtyVariable("ds_info");
    }

    void make_mtc_graphics_bindings(Rml::DataModelConstructor& constructor) {
        static std::vector<std::string> preset_ids;
        static std::vector<std::string> preset_labels;
        preset_ids.clear(); preset_labels.clear();
        for (const auto& p : mtc::get_graphics_presets()) {
            preset_ids.push_back(p.id);
            preset_labels.push_back(p.label);
        }
        preset_ids.push_back("custom");
        preset_labels.push_back("Custom");
        refresh_shader_list(false);

        constructor.RegisterArray<std::vector<std::string>>();
        constructor.Bind("preset_ids", &preset_ids);
        constructor.Bind("preset_labels", &preset_labels);
        constructor.Bind("shader_ids", &shader_ids);
        refresh_border_list();
        constructor.Bind("border_ids", &border_ids);
        constructor.Bind("border_labels", &border_labels);
        constructor.Bind("shader_labels", &shader_labels);

        constructor.BindFunc("preset",
            [](Rml::Variant& out) { out = mtc::detect_preset(new_options, new_extra_options); },
            [](const Rml::Variant& in) {
                std::string id = in.Get<std::string>();
                if (id != "custom") {
                    mtc::apply_preset(id, new_options, new_extra_options);
                }
                dirty_mtc_graphics();
            });
        constructor.BindFunc("framerate",
            [](Rml::Variant& out) { get_option(new_extra_options.framerate, out); },
            [](const Rml::Variant& in) {
                set_option(new_extra_options.framerate, in);
                mtc::resolve_extra_options(new_options, new_extra_options);
                dirty_mtc_graphics();
            });
        constructor.BindFunc("framerate_custom",
            [](Rml::Variant& out) { out = new_extra_options.framerate_custom; },
            [](const Rml::Variant& in) {
                new_extra_options.framerate_custom = in.Get<int>();
                mtc::resolve_extra_options(new_options, new_extra_options);
                dirty_mtc_graphics();
            });
        constructor.BindFunc("resolution",
            [](Rml::Variant& out) { get_option(new_extra_options.resolution, out); },
            [](const Rml::Variant& in) {
                set_option(new_extra_options.resolution, in);
                mtc::resolve_extra_options(new_options, new_extra_options);
                dirty_mtc_graphics();
            });
        // Widescreen borders: part of the normal Apply flow. "default" shows as the manifest's default style.
        constructor.BindFunc("border_style",
            [](Rml::Variant& out) {
                std::string s = new_extra_options.widescreen;
                if (s.empty()) s = "off";
                if (s == "default") s = border_default_id;
                out = s;
            },
            [](const Rml::Variant& in) {
                std::string id = in.Get<std::string>();
                std::string cur = new_extra_options.widescreen == "default" ? border_default_id : new_extra_options.widescreen;
                if (id == cur) return; // re-submitted on load / list change
                new_extra_options.widescreen = id;
                mtc::resolve_extra_options(new_options, new_extra_options);
                dirty_mtc_graphics();
            });
        constructor.BindFunc("borders_on",
            [](Rml::Variant& out) { out = mtc::borders_enabled(new_extra_options); });
        constructor.BindFunc("shader_scope",
            [](Rml::Variant& out) { out = std::string{new_extra_options.widescreen_shaders_on_borders ? "all" : "game"}; },
            [](const Rml::Variant& in) {
                new_extra_options.widescreen_shaders_on_borders = in.Get<std::string>() == "all";
                dirty_mtc_graphics();
            });
        constructor.BindFunc("frame_interp",
            [](Rml::Variant& out) { out = std::string{new_extra_options.frame_interpolation ? "on" : "off"}; },
            [](const Rml::Variant& in) {
                new_extra_options.frame_interpolation = in.Get<std::string>() == "on";
                dirty_mtc_graphics();
            });
        // Shader: applied live through RT64's postfx API and saved immediately (not part of "Apply").
        constructor.BindFunc("shader",
            [](Rml::Variant& out) { out = mtc::renderer::active_shader_preset(); },
            [](const Rml::Variant& in) {
                std::string id = in.Get<std::string>();
                if (id == mtc::renderer::active_shader_preset()) {
                    return; // RmlUi re-submits the current value when the document loads
                }
                if (!shader_select_has_focus) {
                    graphics_model_handle.DirtyVariable("shader"); // not a user action: show the active preset again
                    return;
                }
                if (!mtc::renderer::shaders_ready()) {
                    // Renderer not up yet: remember the choice; RT64Context applies it on creation.
                    mtc::ExtraGraphicsConfig saved = mtc::get_extra_graphics_config();
                    saved.shader = id;
                    mtc::set_extra_graphics_config(saved);
                    new_extra_options.shader = id;
                    dirty_shader_vars();
                    return;
                }
                std::string err;
                if (mtc::renderer::set_shader_preset(id, &err)) {
                    shader_error.clear();
                    mtc::ExtraGraphicsConfig saved = mtc::get_extra_graphics_config();
                    saved.shader = id;
                    mtc::set_extra_graphics_config(saved);
                    new_extra_options.shader = id;
                    mtc::save_config();
                } else {
                    shader_error = "Could not load this shader: " + err;
                }
                dirty_shader_vars();
            });
        constructor.Bind("shader_error", &shader_error);
        constructor.BindFunc("shader_license",
            [](Rml::Variant& out) {
                std::string active = mtc::renderer::active_shader_preset();
                for (size_t i = 0; i < shader_ids.size(); i++) {
                    if (shader_ids[i] == active) { out = shader_licenses[i].empty() ? std::string{"n/a"} : shader_licenses[i]; return; }
                }
                out = std::string{"n/a"};
            });
        constructor.BindFunc("shaders_supported",
            [](Rml::Variant& out) { out = mtc::renderer::shaders_supported(); });
        constructor.BindFunc("shader_folder",
            [](Rml::Variant& out) { out = mtc::renderer::shader_folder(); });
        constructor.BindFunc("shader_notices",
            [](Rml::Variant& out) { out = rml_escape(mtc::renderer::shader_notices()); });
        constructor.Bind("show_shader_notices", &show_shader_notices);
        constructor.BindEventCallback("shader_focus",
            [](Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& inputs) {
                shader_select_has_focus = inputs.at(0).Get<int>() != 0;
                focused_config_option_index = shader_select_has_focus ? 6 : -1;
                handle.DirtyVariable("cur_config_index");
            });
        constructor.BindEventCallback("shader_rescan",
            [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { refresh_shader_list(true); });
        constructor.BindEventCallback("toggle_shader_notices",
            [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList&) {
                show_shader_notices = !show_shader_notices;
                handle.DirtyVariable("show_shader_notices");
            });
    }

    void make_graphics_bindings(Rml::Context* context) {
        Rml::DataModelConstructor constructor = context->CreateDataModel("graphics_model");
        if (!constructor) {
            throw std::runtime_error("Failed to make RmlUi data model for the graphics config menu");
        }

        ultramodern::sleep_milliseconds(50);
        new_options = ultramodern::renderer::get_graphics_config();
        new_extra_options = mtc::get_extra_graphics_config();
        bind_config_list_events(constructor);
        make_mtc_graphics_bindings(constructor);

        constructor.BindFunc("res_option",
            [](Rml::Variant& out) { get_option(new_options.res_option, out); },
            [](const Rml::Variant& in) {
                set_option(new_options.res_option, in);
                graphics_model_handle.DirtyVariable("options_changed");
                graphics_model_handle.DirtyVariable("ds_info");
                graphics_model_handle.DirtyVariable("ds_option");
            }
        );
        bind_option(constructor, "wm_option", &new_options.wm_option);
        constructor.BindFunc("ar_option",
            [](Rml::Variant& out) { get_option(new_options.ar_option, out); },
            [](const Rml::Variant& in) { set_option(new_options.ar_option, in); dirty_mtc_graphics(); });
        bind_option(constructor, "hr_option", &new_options.hr_option);
        bind_option(constructor, "msaa_option", &new_options.msaa_option);
        bind_option(constructor, "api_option", &new_options.api_option);
        constructor.BindFunc("api_active", [](Rml::Variant& out) {
            const std::string name = mtc::renderer::active_api_name();
            out = name.empty() ? std::string("not started") : name;
        });
        constructor.BindFunc("d3d12_available", [](Rml::Variant& out) {
#ifdef _WIN32
            out = true;
#else
            out = false;
#endif
        });
        bind_option(constructor, "rr_option", &new_options.rr_option);
        constructor.BindFunc("rr_manual_value",
            [](Rml::Variant& out) {
                out = new_options.rr_manual_value;
            },
            [](const Rml::Variant& in) {
                new_options.rr_manual_value = in.Get<int>();
                graphics_model_handle.DirtyVariable("options_changed");
            });
        constructor.BindFunc("ds_option",
            [](Rml::Variant& out) {
                if (new_options.res_option == ultramodern::renderer::Resolution::Auto) {
                    out = 1;
                } else {
                    out = new_options.ds_option;
                }
            },
            [](const Rml::Variant& in) {
                new_options.ds_option = in.Get<int>();
                graphics_model_handle.DirtyVariable("options_changed");
                graphics_model_handle.DirtyVariable("ds_info");
            });

        constructor.BindFunc("display_refresh_rate",
            [](Rml::Variant& out) {
                out = ultramodern::get_display_refresh_rate();
            });

        constructor.BindFunc("options_changed",
            [](Rml::Variant& out) {
                out = graphics_options_changed();
            });
        constructor.BindFunc("ds_info",
            [](Rml::Variant& out) {
                switch (new_options.res_option) {
                    default:
                    case ultramodern::renderer::Resolution::Auto:
                        out = "Downsampling is not available at auto resolution";
                        return;
                    case ultramodern::renderer::Resolution::Original:
                        if (new_options.ds_option == 2) {
                            out = "Rendered in 480p and scaled to 240p";
                        } else if (new_options.ds_option == 4) {
                            out = "Rendered in 960p and scaled to 240p";
                        }
                        return;
                    case ultramodern::renderer::Resolution::Original2x:
                        if (new_options.ds_option == 2) {
                            out = "Rendered in 960p and scaled to 480p";
                        } else if (new_options.ds_option == 4) {
                            out = "Rendered in 4K and scaled to 480p";
                        }
                        return;
                }
                out = "";
            });
        
        constructor.BindFunc("gfx_help__apply", [](Rml::Variant& out) {
            if (cont_active) {
                out = \
                    (recomp::get_input_binding(recomp::GameInput::APPLY_MENU, 0, recomp::InputDevice::Controller).to_string() != "" ?
                        " " + recomp::get_input_binding(recomp::GameInput::APPLY_MENU, 0, recomp::InputDevice::Controller).to_string() :
                        ""
                    ) + \
                    (recomp::get_input_binding(recomp::GameInput::APPLY_MENU, 1, recomp::InputDevice::Controller).to_string() != "" ?
                        " " + recomp::get_input_binding(recomp::GameInput::APPLY_MENU, 1, recomp::InputDevice::Controller).to_string() :
                        ""
                    );
            } else {
                out = " " PF_KEYBOARD_F;
            }
        });

        constructor.Bind("msaa2x_supported", &msaa2x_supported);
        constructor.Bind("msaa4x_supported", &msaa4x_supported);
        constructor.Bind("msaa8x_supported", &msaa8x_supported);
        constructor.Bind("sample_positions_supported", &sample_positions_supported);

        graphics_model_handle = constructor.GetModelHandle();
    }

    void make_controls_bindings(Rml::Context* context) {
        Rml::DataModelConstructor constructor = context->CreateDataModel("controls_model");
        if (!constructor) {
            throw std::runtime_error("Failed to make RmlUi data model for the controls config menu");
        }

        constructor.BindFunc("input_count", [](Rml::Variant& out) { out = static_cast<uint64_t>(recomp::get_num_inputs()); } );
        constructor.BindFunc("input_device_is_keyboard", [](Rml::Variant& out) { out = cur_device == recomp::InputDevice::Keyboard; } );

        constructor.RegisterTransformFunc("get_input_name", [](const Rml::VariantList& inputs) {
            return Rml::Variant{recomp::get_input_name(static_cast<recomp::GameInput>(inputs.at(0).Get<size_t>()))};
        });

        constructor.RegisterTransformFunc("get_input_enum_name", [](const Rml::VariantList& inputs) {
            return Rml::Variant{recomp::get_input_enum_name(static_cast<recomp::GameInput>(inputs.at(0).Get<size_t>()))};
        });

        constructor.BindEventCallback("set_input_binding",
            [](Rml::DataModelHandle model_handle, Rml::Event& event, const Rml::VariantList& inputs) {
                scanned_input_index = inputs.at(0).Get<size_t>();
                scanned_binding_index = inputs.at(1).Get<size_t>();
                recomp::start_scanning_input(cur_device);
                model_handle.DirtyVariable("active_binding_input");
                model_handle.DirtyVariable("active_binding_slot");
            });

        constructor.BindEventCallback("reset_input_bindings_to_defaults",
            [](Rml::DataModelHandle model_handle, Rml::Event& event, const Rml::VariantList& inputs) {
                if (cur_device == recomp::InputDevice::Controller) {
                    mtc::reset_cont_input_bindings();
                } else {
                    mtc::reset_kb_input_bindings();
                }
                model_handle.DirtyAllVariables();
                nav_help_model_handle.DirtyVariable("nav_help__accept");
                nav_help_model_handle.DirtyVariable("nav_help__exit");
                graphics_model_handle.DirtyVariable("gfx_help__apply");
            });

        constructor.BindEventCallback("clear_input_bindings",
            [](Rml::DataModelHandle model_handle, Rml::Event& event, const Rml::VariantList& inputs) {
                recomp::GameInput input = static_cast<recomp::GameInput>(inputs.at(0).Get<size_t>());
                for (size_t binding_index = 0; binding_index < recomp::bindings_per_input; binding_index++) {
                    recomp::set_input_binding(input, binding_index, cur_device, recomp::InputField{});
                }
                model_handle.DirtyVariable("inputs");
                graphics_model_handle.DirtyVariable("gfx_help__apply");
            });

        constructor.BindEventCallback("reset_single_input_binding_to_default",
            [](Rml::DataModelHandle model_handle, Rml::Event& event, const Rml::VariantList& inputs) {
                recomp::GameInput input = static_cast<recomp::GameInput>(inputs.at(0).Get<size_t>());
                mtc::reset_single_input_binding(cur_device, input);
                model_handle.DirtyVariable("inputs");
                nav_help_model_handle.DirtyVariable("nav_help__accept");
                nav_help_model_handle.DirtyVariable("nav_help__exit");
            });

        constructor.BindEventCallback("set_input_row_focus",
            [](Rml::DataModelHandle model_handle, Rml::Event& event, const Rml::VariantList& inputs) {
                int input_index = inputs.at(0).Get<size_t>();
                // watch for mouseout being overzealous during event bubbling, only clear if the event's attached element matches the current
                if (input_index == -1 && event.GetType() == "mouseout" && event.GetCurrentElement() != event.GetTargetElement()) {
                    return;
                }
                focused_input_index = input_index;
                model_handle.DirtyVariable("cur_input_row");
            });

        // Rml variable definition for an individual InputField.
        struct InputFieldVariableDefinition : public Rml::VariableDefinition {
            InputFieldVariableDefinition() : Rml::VariableDefinition(Rml::DataVariableType::Scalar) {}

            virtual bool Get(void* ptr, Rml::Variant& variant) override { variant = reinterpret_cast<recomp::InputField*>(ptr)->to_string(); return true; }
            virtual bool Set(void* ptr, const Rml::Variant& variant) override { return false; }
        };
        // Static instance of the InputField variable definition to have a pointer to return to RmlUi.
        static InputFieldVariableDefinition input_field_definition_instance{};

        // Rml variable definition for an array of InputField values (e.g. all the bindings for a single input).
        struct BindingContainerVariableDefinition : public Rml::VariableDefinition {
            BindingContainerVariableDefinition() : Rml::VariableDefinition(Rml::DataVariableType::Array) {}

            virtual bool Get(void* ptr, Rml::Variant& variant) override { return false; }
            virtual bool Set(void* ptr, const Rml::Variant& variant) override { return false; }

            virtual int Size(void* ptr) override { return recomp::bindings_per_input; }
            virtual Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override {
                recomp::GameInput input = static_cast<recomp::GameInput>((uintptr_t)ptr);
                return Rml::DataVariable{&input_field_definition_instance, &recomp::get_input_binding(input, address.index, cur_device)};
            }
        };
        // Static instance of the InputField array variable definition to have a fixed pointer to return to RmlUi.
        static BindingContainerVariableDefinition binding_container_var_instance{};

        // Rml variable definition for an array of an array of InputField values (e.g. all the bindings for all inputs).
        struct BindingArrayContainerVariableDefinition : public Rml::VariableDefinition {
            BindingArrayContainerVariableDefinition() : Rml::VariableDefinition(Rml::DataVariableType::Array) {}

            virtual bool Get(void* ptr, Rml::Variant& variant) override { return false; }
            virtual bool Set(void* ptr, const Rml::Variant& variant) override { return false; }

            virtual int Size(void* ptr) override { return recomp::get_num_inputs(); }
            virtual Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override {
                // Encode the input index as the pointer to avoid needing to do any allocations.
                return Rml::DataVariable(&binding_container_var_instance, (void*)(uintptr_t)address.index);
            }
        };

        // Static instance of the BindingArrayContainerVariableDefinition variable definition to have a fixed pointer to return to RmlUi.
        static BindingArrayContainerVariableDefinition binding_array_var_instance{};

        struct InputContainerVariableDefinition : public Rml::VariableDefinition {
            InputContainerVariableDefinition() : Rml::VariableDefinition(Rml::DataVariableType::Struct) {}

            virtual bool Get(void* ptr, Rml::Variant& variant) override { return true; }
            virtual bool Set(void* ptr, const Rml::Variant& variant) override { return false; }

            virtual int Size(void* ptr) override { return recomp::get_num_inputs(); }
            virtual Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override {
                if (address.name == "array") {
                    return Rml::DataVariable(&binding_array_var_instance, nullptr);
                }
                else {
                    recomp::GameInput input = recomp::get_input_from_enum_name(address.name);
                    if (input != recomp::GameInput::COUNT) {
                        return Rml::DataVariable(&binding_container_var_instance, (void*)(uintptr_t)input);
                    }
                }
                return Rml::DataVariable{};
            }
        };

        // Dummy type to associate with the variable definition.
        struct InputContainer {};
        constructor.RegisterCustomDataVariableDefinition<InputContainer>(Rml::MakeUnique<InputContainerVariableDefinition>());

        // Dummy instance of the dummy type to bind to the variable.
        static InputContainer dummy_container;
        constructor.Bind("inputs", &dummy_container);

        constructor.BindFunc("cur_input_row", [](Rml::Variant& out) {
            if (focused_input_index == -1) {
                out = "NONE";
            }
            else {
                out = recomp::get_input_enum_name(static_cast<recomp::GameInput>(focused_input_index));
            }
        });

        constructor.BindFunc("active_binding_input", [](Rml::Variant& out) {
            if (scanned_input_index == -1) {
                out = "NONE";
            }
            else {
                out = recomp::get_input_enum_name(static_cast<recomp::GameInput>(scanned_input_index));
            }
        });

        constructor.Bind<int>("active_binding_slot", &scanned_binding_index);

        controls_model_handle = constructor.GetModelHandle();
    }

    void make_nav_help_bindings(Rml::Context* context) {
        Rml::DataModelConstructor constructor = context->CreateDataModel("nav_help_model");
        if (!constructor) {
            throw std::runtime_error("Failed to make RmlUi data model for nav help");
        }

        constructor.BindFunc("nav_help__navigate", [](Rml::Variant& out) {
            if (cont_active) {
                out = PF_DPAD;
            } else {
                out = PF_KEYBOARD_ARROWS PF_KEYBOARD_TAB;
            }
        });

        constructor.BindFunc("nav_help__accept", [](Rml::Variant& out) {
            if (cont_active) {
                out = \
                    recomp::get_input_binding(recomp::GameInput::ACCEPT_MENU, 0, recomp::InputDevice::Controller).to_string() + \
                    recomp::get_input_binding(recomp::GameInput::ACCEPT_MENU, 1, recomp::InputDevice::Controller).to_string();
            } else {
                out = PF_KEYBOARD_ENTER;
            }
        });

        constructor.BindFunc("nav_help__exit", [](Rml::Variant& out) {
            if (cont_active) {
                out = \
                    recomp::get_input_binding(recomp::GameInput::TOGGLE_MENU, 0, recomp::InputDevice::Controller).to_string() + \
                    recomp::get_input_binding(recomp::GameInput::TOGGLE_MENU, 1, recomp::InputDevice::Controller).to_string();
            } else {
                out = PF_KEYBOARD_ESCAPE;
            }
        });

        // Magical Blocks 0.1.4: LB/RB switch tabs (controller only).
        constructor.BindFunc("nav_help__tabs", [](Rml::Variant& out) {
            out = PF_XBOX_LEFT_SHOULDER PF_XBOX_RIGHT_SHOULDER;
        });
        constructor.BindFunc("nav_help__tabs_visible", [](Rml::Variant& out) {
            out = cont_active;
        });

        nav_help_model_handle = constructor.GetModelHandle();
    }

    void make_general_bindings(Rml::Context* context) {
        Rml::DataModelConstructor constructor = context->CreateDataModel("general_model");
        if (!constructor) {
            throw std::runtime_error("Failed to make RmlUi data model for the control options menu");
        }

        bind_config_list_events(constructor);
        
        constructor.Bind("rumble_strength", &control_options_context.rumble_strength);
        constructor.Bind("gyro_sensitivity", &control_options_context.gyro_sensitivity);
        constructor.Bind("mouse_sensitivity", &control_options_context.mouse_sensitivity);
        constructor.Bind("joystick_deadzone", &control_options_context.joystick_deadzone);
        bind_option(constructor, "background_input_mode", &control_options_context.background_input_mode);

        general_model_handle = constructor.GetModelHandle();
    }
    
    void make_sound_options_bindings(Rml::Context* context) {
        Rml::DataModelConstructor constructor = context->CreateDataModel("sound_options_model");
        if (!constructor) {
            throw std::runtime_error("Failed to make RmlUi data model for the sound options menu");
        }

        bind_config_list_events(constructor);
        
        sound_options_model_handle = constructor.GetModelHandle();

        bind_atomic(constructor, sound_options_model_handle, "main_volume", &sound_options_context.main_volume);
    }

    void make_debug_bindings(Rml::Context* context) {
        Rml::DataModelConstructor constructor = context->CreateDataModel("debug_model");
        if (!constructor) {
            throw std::runtime_error("Failed to make RmlUi data model for the debug menu");
        }

        bind_config_list_events(constructor);

        // Bind the debug mode enabled flag.
        constructor.Bind("debug_enabled", &debug_context.debug_enabled);
        
        debug_context.model_handle = constructor.GetModelHandle();
    }

    void make_bindings(Rml::Context* context) override {
        // initially set cont state for ui help
        //recomp::config_menu_set_cont_or_kb(recompui::get_cont_active());
        make_nav_help_bindings(context);
        make_general_bindings(context);
        make_controls_bindings(context);
        make_graphics_bindings(context);
        make_sound_options_bindings(context);
        make_debug_bindings(context);
    }
};

std::unique_ptr<recompui::MenuController> recompui::create_config_menu() {
    return std::make_unique<ConfigMenu>();
}

bool mtc::get_debug_mode_enabled() {
    return debug_context.debug_enabled;
}

void mtc::set_debug_mode_enabled(bool enabled) {
    debug_context.debug_enabled = enabled;
    if (debug_context.model_handle) {
        debug_context.model_handle.DirtyVariable("debug_enabled");
    }
}

void recompui::update_supported_options() {
    msaa2x_supported = mtc::renderer::RT64MaxMSAA() >= RT64::UserConfiguration::Antialiasing::MSAA2X;
    msaa4x_supported = mtc::renderer::RT64MaxMSAA() >= RT64::UserConfiguration::Antialiasing::MSAA4X;
    msaa8x_supported = mtc::renderer::RT64MaxMSAA() >= RT64::UserConfiguration::Antialiasing::MSAA8X;
    sample_positions_supported = mtc::renderer::RT64SamplePositionsSupported();
    
    new_options = ultramodern::renderer::get_graphics_config();
    new_extra_options = mtc::get_extra_graphics_config();
    refresh_shader_list(false);
    refresh_border_list();

    graphics_model_handle.DirtyAllVariables();
}

void recompui::toggle_fullscreen() {
    new_options.wm_option = (new_options.wm_option == ultramodern::renderer::WindowMode::Windowed) ? ultramodern::renderer::WindowMode::Fullscreen : ultramodern::renderer::WindowMode::Windowed;
    apply_graphics_config();
    graphics_model_handle.DirtyVariable("wm_option");
}

void recompui::set_config_tab(ConfigTab tab) {
    get_config_tabset()->SetActiveTab(config_tab_to_index(tab));
}

namespace {
    Rml::Element* first_focusable(Rml::Element* e) {
        for (int i = 0; i < e->GetNumChildren(); i++) {
            Rml::Element* c = e->GetChild(i);
            const auto& cv = c->GetComputedValues();
            if (cv.display() == Rml::Style::Display::None || cv.visibility() != Rml::Style::Visibility::Visible) continue;
            if (cv.focus() == Rml::Style::Focus::None) continue;
            if (cv.tab_index() == Rml::Style::TabIndex::Auto) return c;
            if (Rml::Element* r = first_focusable(c)) return r;
        }
        return nullptr;
    }

    void scroll_all_to_top(Rml::Element* e) {
        e->SetScrollTop(0.0f);
        for (int i = 0; i < e->GetNumChildren(); i++) scroll_all_to_top(e->GetChild(i));
    }
}

// LB / RB in the settings menu: previous / next tab, wrapping over General..Mods. Ignored while the Controls tab is
// waiting for a binding (the bumper must then be bound, not switch tabs).
bool recompui::config_cycle_tab(int direction) {
    if (recomp::get_scanned_input_index() != -1) return false;
    Rml::ElementTabSet* tabset = get_config_tabset();
    if (tabset == nullptr) return false;
    constexpr int first = 0;
    const int count = config_tab_to_index(ConfigTab::Mods) + 1;
    int cur = tabset->GetActiveTab();
    if (cur < first || cur >= count) cur = 0;
    const int next = ((cur - first + direction) % count + count) % count + first;
    tabset->SetActiveTab(next);
    if (Rml::ElementDocument* doc = tabset->GetOwnerDocument()) doc->UpdateDocument();
    Rml::Element* panels = get_child_by_tag(tabset, "panels");
    Rml::Element* panel = panels != nullptr && next < panels->GetNumChildren() ? panels->GetChild(next) : nullptr;
    if (panel != nullptr) {
        scroll_all_to_top(panel);
        if (Rml::Element* f = first_focusable(panel)) {
            f->Focus();
            scroll_all_to_top(panel);
        }
    }
    else {
        Rml::Element* tabs = get_child_by_tag(tabset, "tabs");
        if (tabs != nullptr && next < tabs->GetNumChildren()) tabs->GetChild(next)->Focus();
    }
    return true;
}

Rml::ElementTabSet* recompui::get_config_tabset() {
    ContextId config_context = recompui::get_config_context_id();

    ContextId old_context = recompui::try_close_current_context();

    Rml::ElementDocument *doc = config_context.get_document();
    assert(doc != nullptr);

    Rml::Element *tabset_el = doc->GetElementById("config_tabset");
    assert(tabset_el != nullptr);

    Rml::ElementTabSet *tabset = rmlui_dynamic_cast<Rml::ElementTabSet *>(tabset_el);
    assert(tabset != nullptr);

    if (old_context != ContextId::null()) {
        old_context.open();
    }

    return tabset;
}

Rml::Element* recompui::get_mod_tab() {
    ContextId config_context = recompui::get_config_context_id();

    ContextId old_context = recompui::try_close_current_context();

    Rml::ElementDocument* doc = config_context.get_document();
    assert(doc != nullptr);

    Rml::Element* tab_el = doc->GetElementById("tab_mods");
    assert(tab_el != nullptr);

    if (old_context != ContextId::null()) {
        old_context.open();
    }

    return tab_el;
}

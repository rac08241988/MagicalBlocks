#include <string>
#include <vector>
#include <optional>
#include <cstdlib>
#include <memory>
#include <cstring>
#include <variant>
#include <algorithm>

#define HLSL_CPU
#include "hle/rt64_application.h"
#include "rt64_render_hooks.h"
#include "overloaded.h"

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"

#include "mtc_render.h"
#include "recomp_ui.h"
#include "concurrentqueue.h"
#include "mtc_config.h"
#include "mtc_widescreen_glue.h"
#include "mtc_hifps.h"
#include <atomic>
#include <mutex>
#if defined(RT64_POSTPROCESS) && RT64_POSTPROCESS
#include "postfx/rt64_postprocess_api.h"
#define MTC_HAS_POSTFX 1
#else
#define MTC_HAS_POSTFX 0
#endif

static RT64::UserConfiguration::Antialiasing device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
static bool sample_positions_supported = false;
static bool high_precision_fb_enabled = false;

static uint8_t DMEM[0x1000];
static uint8_t IMEM[0x1000];

struct TexturePackEnableAction {
    std::string mod_id;
};

struct TexturePackDisableAction {
    std::string mod_id;
};

struct TexturePackSecondaryEnableAction {
    std::string mod_id;
};

struct TexturePackSecondaryDisableAction {
    std::string mod_id;
};

struct TexturePackUpdateAction {
};

using TexturePackAction = std::variant<TexturePackEnableAction, TexturePackDisableAction, TexturePackSecondaryEnableAction, TexturePackSecondaryDisableAction, TexturePackUpdateAction>;

static moodycamel::ConcurrentQueue<TexturePackAction> texture_pack_action_queue;

unsigned int MI_INTR_REG = 0;

unsigned int DPC_START_REG = 0;
unsigned int DPC_END_REG = 0;
unsigned int DPC_CURRENT_REG = 0;
unsigned int DPC_STATUS_REG = 0;
unsigned int DPC_CLOCK_REG = 0;
unsigned int DPC_BUFBUSY_REG = 0;
unsigned int DPC_PIPEBUSY_REG = 0;
unsigned int DPC_TMEM_REG = 0;

void dummy_check_interrupts() {}

RT64::UserConfiguration::Antialiasing compute_max_supported_aa(RT64::RenderSampleCounts bits) {
    if (bits & RT64::RenderSampleCount::Bits::COUNT_2) {
        if (bits & RT64::RenderSampleCount::Bits::COUNT_4) {
            if (bits & RT64::RenderSampleCount::Bits::COUNT_8) {
                return RT64::UserConfiguration::Antialiasing::MSAA8X;
            }
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        }
        return RT64::UserConfiguration::Antialiasing::MSAA2X;
    };
    return RT64::UserConfiguration::Antialiasing::None;
}

RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio option) {
    switch (option) {
        case ultramodern::renderer::AspectRatio::Original:
            return RT64::UserConfiguration::AspectRatio::Original;
        case ultramodern::renderer::AspectRatio::Expand:
            return RT64::UserConfiguration::AspectRatio::Expand;
        case ultramodern::renderer::AspectRatio::Manual:
            return RT64::UserConfiguration::AspectRatio::Manual;
        case ultramodern::renderer::AspectRatio::OptionCount:
            return RT64::UserConfiguration::AspectRatio::OptionCount;
    }
}

RT64::UserConfiguration::Antialiasing to_rt64(ultramodern::renderer::Antialiasing option) {
    switch (option) {
        case ultramodern::renderer::Antialiasing::None:
            return RT64::UserConfiguration::Antialiasing::None;
        case ultramodern::renderer::Antialiasing::MSAA2X:
            return RT64::UserConfiguration::Antialiasing::MSAA2X;
        case ultramodern::renderer::Antialiasing::MSAA4X:
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        case ultramodern::renderer::Antialiasing::MSAA8X:
            return RT64::UserConfiguration::Antialiasing::MSAA8X;
        case ultramodern::renderer::Antialiasing::OptionCount:
            return RT64::UserConfiguration::Antialiasing::OptionCount;
    }
}

RT64::UserConfiguration::RefreshRate to_rt64(ultramodern::renderer::RefreshRate option) {
    switch (option) {
        case ultramodern::renderer::RefreshRate::Original:
            return RT64::UserConfiguration::RefreshRate::Original;
        case ultramodern::renderer::RefreshRate::Display:
            return RT64::UserConfiguration::RefreshRate::Display;
        case ultramodern::renderer::RefreshRate::Manual:
            return RT64::UserConfiguration::RefreshRate::Manual;
        case ultramodern::renderer::RefreshRate::OptionCount:
            return RT64::UserConfiguration::RefreshRate::OptionCount;
    }
}

RT64::UserConfiguration::InternalColorFormat to_rt64(ultramodern::renderer::HighPrecisionFramebuffer option) {
    switch (option) {
        case ultramodern::renderer::HighPrecisionFramebuffer::Off:
            return RT64::UserConfiguration::InternalColorFormat::Standard;
        case ultramodern::renderer::HighPrecisionFramebuffer::On:
            return RT64::UserConfiguration::InternalColorFormat::High;
        case ultramodern::renderer::HighPrecisionFramebuffer::Auto:
            return RT64::UserConfiguration::InternalColorFormat::Automatic;
        case ultramodern::renderer::HighPrecisionFramebuffer::OptionCount:
            return RT64::UserConfiguration::InternalColorFormat::OptionCount;
    }
}

void set_application_user_config(RT64::Application* application, const ultramodern::renderer::GraphicsConfig& config) {
    switch (config.res_option) {
        default:
        case ultramodern::renderer::Resolution::Auto:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
            application->userConfig.downsampleMultiplier = 1;
            break;
        case ultramodern::renderer::Resolution::Original:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
        case ultramodern::renderer::Resolution::Original2x:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = 2.0 * std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
    }

    switch (config.hr_option) {
        default:
        case ultramodern::renderer::HUDRatioMode::Original:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
            break;
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
            application->userConfig.extAspectTarget = 16.0/9.0;
            break;
        case ultramodern::renderer::HUDRatioMode::Full:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
            break;
    }

    application->userConfig.aspectRatio = to_rt64(config.ar_option);
    application->userConfig.antialiasing = to_rt64(config.msaa_option);
    application->userConfig.refreshRate = to_rt64(config.rr_option);
    application->userConfig.refreshRateTarget = config.rr_manual_value;
    application->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
    application->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
}

ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult rt64_result) {
    switch (rt64_result) {
        case RT64::Application::SetupResult::Success:
            return ultramodern::renderer::SetupResult::Success;
        case RT64::Application::SetupResult::DynamicLibrariesNotFound:
            return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        case RT64::Application::SetupResult::InvalidGraphicsAPI:
            return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
        case RT64::Application::SetupResult::GraphicsAPINotFound:
            return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        case RT64::Application::SetupResult::GraphicsDeviceNotFound:
            return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }

    fprintf(stderr, "Unhandled `RT64::Application::SetupResult` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

ultramodern::renderer::GraphicsApi map_graphics_api(RT64::UserConfiguration::GraphicsAPI api) {
    switch (api) {
        case RT64::UserConfiguration::GraphicsAPI::D3D12:
            return ultramodern::renderer::GraphicsApi::D3D12;
        case RT64::UserConfiguration::GraphicsAPI::Vulkan:
            return ultramodern::renderer::GraphicsApi::Vulkan;
        case RT64::UserConfiguration::GraphicsAPI::Metal:
            return ultramodern::renderer::GraphicsApi::Metal;
        case RT64::UserConfiguration::GraphicsAPI::Automatic:
            return ultramodern::renderer::GraphicsApi::Auto;
    }

    fprintf(stderr, "Unhandled `RT64::UserConfiguration::GraphicsAPI` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

// ---- Post-processing shaders ---------------------------------------------------------------------------
// The RT64 application currently presenting, for the options menu (UI thread). RT64's postfx C API is
// thread-safe; the mutex only serializes our own calls so the returned-string lifetimes hold.
static std::atomic<RT64::Application*> postfx_app{nullptr};
static std::mutex postfx_mutex;

static std::string normalize_shader_id(const std::string& id) {
    return (id == "none" || id == "off") ? std::string{} : id;
}

static void restore_saved_shader_preset() {
#if MTC_HAS_POSTFX
    std::string saved = normalize_shader_id(mtc::get_extra_graphics_config().shader);
    if (saved.empty()) return;
    std::string err;
    if (!mtc::renderer::set_shader_preset(saved, &err)) {
        // e.g. the shaders folder was removed: fall back to no shader silently, but log why.
        fprintf(stderr, "[shaders] could not restore preset '%s': %s\n", saved.c_str(), err.c_str());
        mtc::ExtraGraphicsConfig extra = mtc::get_extra_graphics_config();
        extra.shader = "";
        mtc::set_extra_graphics_config(extra);
    }
#endif
}

bool mtc::renderer::shaders_supported() {
#if MTC_HAS_POSTFX
    return rt64_postprocess_supported() != 0;
#else
    return false;
#endif
}

bool mtc::renderer::shaders_ready() {
    return shaders_supported() && postfx_app.load() != nullptr;
}

std::vector<mtc::renderer::ShaderPreset> mtc::renderer::list_shader_presets() {
    std::vector<ShaderPreset> out{ ShaderPreset{ "", "None", "", true } };
#if MTC_HAS_POSTFX
    std::lock_guard lock{ postfx_mutex };
    RT64::Application* a = postfx_app.load();
    if (a == nullptr) return out;
    int n = rt64_postprocess_refresh_presets(a);
    for (int i = 0; i < n; i++) {
        const char* id = rt64_postprocess_preset_id(a, i);
        const char* name = rt64_postprocess_preset_name(a, i);
        const char* lic = rt64_postprocess_preset_license(a, i);
        out.push_back(ShaderPreset{ id ? id : "", name ? name : (id ? id : "?"), lic ? lic : "",
            rt64_postprocess_preset_is_builtin(a, i) != 0 });
    }
#endif
    return out;
}

bool mtc::renderer::set_shader_preset(const std::string& id_in, std::string* error) {
    std::string id = normalize_shader_id(id_in);
#if MTC_HAS_POSTFX
    std::lock_guard lock{ postfx_mutex };
    RT64::Application* a = postfx_app.load();
    if (a == nullptr) {
        if (error) *error = "The renderer is not running yet.";
        return false;
    }
    if (rt64_postprocess_set_preset(a, id.c_str()) != 0) {
        return true;
    }
    if (error) {
        const char* e = rt64_postprocess_last_error(a);
        *error = (e && *e) ? e : "Unknown error while loading the shader preset.";
    }
    return false;
#else
    if (error) *error = "This build was compiled without post-processing support.";
    return id.empty();
#endif
}

std::string mtc::renderer::active_shader_preset() {
#if MTC_HAS_POSTFX
    std::lock_guard lock{ postfx_mutex };
    RT64::Application* a = postfx_app.load();
    if (a != nullptr) {
        const char* id = rt64_postprocess_active_preset(a);
        return id ? id : "";
    }
#endif
    return normalize_shader_id(mtc::get_extra_graphics_config().shader);
}

std::string mtc::renderer::shader_folder() {
#if MTC_HAS_POSTFX
    std::lock_guard lock{ postfx_mutex };
    RT64::Application* a = postfx_app.load();
    if (a != nullptr) {
        const char* d = rt64_postprocess_get_search_directory(a);
        if (d) return d;
    }
#endif
    return "shaders";
}

std::string mtc::renderer::shader_notices() {
#if MTC_HAS_POSTFX
    const char* n = rt64_postprocess_builtin_notices();
    return n ? n : "";
#else
    return "";
#endif
}

namespace {
    std::optional<ultramodern::renderer::GraphicsApi> g_api_override;
    std::mutex g_active_api_mutex;
    std::string g_active_api_name;
}

void mtc::renderer::set_api_override(std::optional<ultramodern::renderer::GraphicsApi> api) { g_api_override = api; }
std::optional<ultramodern::renderer::GraphicsApi> mtc::renderer::api_override() { return g_api_override; }
void mtc::renderer::set_active_api_name(const std::string& name) { std::lock_guard lock{ g_active_api_mutex }; g_active_api_name = name; }
std::string mtc::renderer::active_api_name() { std::lock_guard lock{ g_active_api_mutex }; return g_active_api_name; }
std::string mtc::renderer::graphics_api_name(ultramodern::renderer::GraphicsApi api) {
    switch (api) {
        case ultramodern::renderer::GraphicsApi::D3D12: return "D3D12";
        case ultramodern::renderer::GraphicsApi::Vulkan: return "Vulkan";
        case ultramodern::renderer::GraphicsApi::Metal: return "Metal";
        default: return "Auto";
    }
}

mtc::renderer::RT64Context::RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool debug) {
    static unsigned char dummy_rom_header[0x40];
    recompui::set_render_hooks();

    // Set up the RT64 application core fields.
    RT64::Application::Core appCore{};
#if defined(_WIN32)
    appCore.window = window_handle.window;
#elif defined(__linux__) || defined(__ANDROID__)
    appCore.window = window_handle;
#elif defined(__APPLE__)
    appCore.window.window = window_handle.window;
    appCore.window.view = window_handle.view;
#endif

    appCore.checkInterrupts = dummy_check_interrupts;

    appCore.HEADER = dummy_rom_header;
    appCore.RDRAM = rdram;
    appCore.DMEM = DMEM;
    appCore.IMEM = IMEM;

    appCore.MI_INTR_REG = &MI_INTR_REG;

    appCore.DPC_START_REG = &DPC_START_REG;
    appCore.DPC_END_REG = &DPC_END_REG;
    appCore.DPC_CURRENT_REG = &DPC_CURRENT_REG;
    appCore.DPC_STATUS_REG = &DPC_STATUS_REG;
    appCore.DPC_CLOCK_REG = &DPC_CLOCK_REG;
    appCore.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
    appCore.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
    appCore.DPC_TMEM_REG = &DPC_TMEM_REG;

    ultramodern::renderer::ViRegs* vi_regs = ultramodern::renderer::get_vi_regs();

    appCore.VI_STATUS_REG = &vi_regs->VI_STATUS_REG;
    appCore.VI_ORIGIN_REG = &vi_regs->VI_ORIGIN_REG;
    appCore.VI_WIDTH_REG = &vi_regs->VI_WIDTH_REG;
    appCore.VI_INTR_REG = &vi_regs->VI_INTR_REG;
    appCore.VI_V_CURRENT_LINE_REG = &vi_regs->VI_V_CURRENT_LINE_REG;
    appCore.VI_TIMING_REG = &vi_regs->VI_TIMING_REG;
    appCore.VI_V_SYNC_REG = &vi_regs->VI_V_SYNC_REG;
    appCore.VI_H_SYNC_REG = &vi_regs->VI_H_SYNC_REG;
    appCore.VI_LEAP_REG = &vi_regs->VI_LEAP_REG;
    appCore.VI_H_START_REG = &vi_regs->VI_H_START_REG;
    appCore.VI_V_START_REG = &vi_regs->VI_V_START_REG;
    appCore.VI_V_BURST_REG = &vi_regs->VI_V_BURST_REG;
    appCore.VI_X_SCALE_REG = &vi_regs->VI_X_SCALE_REG;
    appCore.VI_Y_SCALE_REG = &vi_regs->VI_Y_SCALE_REG;

    // Set up the RT64 application configuration fields.
    RT64::ApplicationConfiguration appConfig;
    appConfig.useConfigurationFile = false;

    // Pick the graphics API: --graphics-api overrides the setting. "Auto" means Vulkan first on Windows since 0.1.4
    // (D3D12 is the fallback when Vulkan can't start): the D3D12 path crashed at game start on a user's NVIDIA
    // system in 0.1.3 and isn't testable here, while Vulkan is (lavapipe, wine). D3D12 stays selectable.
    auto& cur_config = ultramodern::renderer::get_graphics_config();
    ultramodern::renderer::GraphicsApi requested = cur_config.api_option;
    if (mtc::renderer::api_override().has_value()) requested = *mtc::renderer::api_override();
    std::vector<RT64::UserConfiguration::GraphicsAPI> attempts;
    switch (requested) {
        case ultramodern::renderer::GraphicsApi::D3D12: attempts = { RT64::UserConfiguration::GraphicsAPI::D3D12 }; break;
        case ultramodern::renderer::GraphicsApi::Vulkan: attempts = { RT64::UserConfiguration::GraphicsAPI::Vulkan }; break;
        case ultramodern::renderer::GraphicsApi::Metal: attempts = { RT64::UserConfiguration::GraphicsAPI::Metal }; break;
        case ultramodern::renderer::GraphicsApi::Auto:
#ifdef _WIN32
            attempts = { RT64::UserConfiguration::GraphicsAPI::Vulkan, RT64::UserConfiguration::GraphicsAPI::D3D12 };
#else
            attempts = { RT64::UserConfiguration::GraphicsAPI::Automatic };
#endif
            break;
    }

    uint32_t thread_id = 0;
#ifdef _WIN32
    thread_id = window_handle.thread_id;
#endif
    for (size_t attempt = 0; attempt < attempts.size(); attempt++) {
        // Create the RT64 application.
        app = std::make_unique<RT64::Application>(appCore, appConfig);

        // Set initial user config settings based on the current settings.
        set_application_user_config(app.get(), cur_config);
        app->userConfig.developerMode = debug;
        // Force gbi depth branches to prevent LODs from kicking in.
        app->enhancementConfig.f3dex.forceBranch = true;
        // Scale LODs based on the output resolution.
        app->enhancementConfig.textureLOD.scale = true;
        app->userConfig.graphicsAPI = attempts[attempt];

        // Set up the RT64 application.
        setup_result = map_setup_result(app->setup(thread_id));
        // Get the API that RT64 chose.
        chosen_api = map_graphics_api(app->chosenGraphicsAPI);
        if (setup_result == ultramodern::renderer::SetupResult::Success) {
            break;
        }
        fprintf(stderr, "[renderer] %s setup failed (result %d)%s\n", mtc::renderer::graphics_api_name(chosen_api).c_str(),
            int(setup_result), (attempt + 1 < attempts.size()) ? ", trying the next graphics API" : "");
        app = nullptr;
    }
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        app = nullptr;
        return;
    }
    {
        const RT64::RenderDeviceDescription& desc = app->device->getDescription();
        fprintf(stdout, "[renderer] graphics API: %s (requested %s), device: %s, driver 0x%llX, VRAM %llu MB\n",
            mtc::renderer::graphics_api_name(chosen_api).c_str(), mtc::renderer::graphics_api_name(requested).c_str(),
            desc.name.c_str(), (unsigned long long)desc.driverVersion, (unsigned long long)(desc.dedicatedVideoMemory >> 20));
        mtc::renderer::set_active_api_name(mtc::renderer::graphics_api_name(chosen_api) + " - " + desc.name);
    }

    // Set the application's fullscreen state.
    app->setFullScreen(cur_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);

    // Check if the selected device actually supports MSAA sample positions and MSAA for for the formats that will be used
    // and downgrade the configuration accordingly.
    // MTC_FORCE_MSAA=1 (testing): offer MSAA even without programmable sample positions (e.g. lavapipe), to
    // exercise the MSAA render paths that real GPUs take by default.
    const bool force_msaa = std::getenv("MTC_FORCE_MSAA") != nullptr;
    if (app->device->getCapabilities().sampleLocations || force_msaa) {
        RT64::RenderSampleCounts color_sample_counts = app->device->getSampleCountsSupported(RT64::RenderFormat::R8G8B8A8_UNORM);
        RT64::RenderSampleCounts depth_sample_counts = app->device->getSampleCountsSupported(RT64::RenderFormat::D32_FLOAT);
        RT64::RenderSampleCounts common_sample_counts = color_sample_counts & depth_sample_counts;
        device_max_msaa = compute_max_supported_aa(common_sample_counts);
        sample_positions_supported = true;
    }
    else {
        device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
        sample_positions_supported = false;
    }

    high_precision_fb_enabled = app->shaderLibrary->usesHDR;

    // Expose the application to the shader menu and restore the saved post-processing preset.
    postfx_app.store(app.get());
    restore_saved_shader_preset();
    mtc::widescreen::attach(app.get());
    mtc::hifps::attach(app.get());
}

mtc::renderer::RT64Context::~RT64Context() {
    mtc::widescreen::detach(app.get());
    mtc::hifps::detach(app.get());
    if (postfx_app.load() == app.get()) {
        postfx_app.store(nullptr);
    }
}

void mtc::renderer::RT64Context::send_dl(const OSTask* task) {
    check_texture_pack_actions();
    app->state->rsp->reset();
    app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
    mtc::hifps::before_display_list(task->t.data_ptr & 0x3FFFFFF);
    app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
}

// Newer ultramodern (589bbf0+) asks the renderer for a "dummy workload" on every VI while no game is
// running (launcher/menus). RT64 23cab60 (+ S2DEX branch) has no API for it and presents the UI fine from
// update_screen() with the dummy VI state, which is what the older runtime relied on, so this is a no-op.
void mtc::renderer::RT64Context::send_dummy_workload(uint32_t fb_address) {
    (void)fb_address;
}

void mtc::renderer::RT64Context::update_screen() {
    app->updateScreen();
}

void mtc::renderer::RT64Context::shutdown() {
    mtc::widescreen::detach(app.get());
    postfx_app.store(nullptr);
    if (app != nullptr) {
        app->end();
    }
}

bool mtc::renderer::RT64Context::update_config(const ultramodern::renderer::GraphicsConfig& old_config, const ultramodern::renderer::GraphicsConfig& new_config) {
    if (old_config == new_config) {
        return false;
    }

    if (new_config.wm_option != old_config.wm_option) {
        app->setFullScreen(new_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
    }

    set_application_user_config(app.get(), new_config);

    app->updateUserConfig(true);

    if (new_config.msaa_option != old_config.msaa_option) {
        app->updateMultisampling();
    }
    return true;
}

void mtc::renderer::RT64Context::enable_instant_present() {
    // Enable the present early presentation mode for minimal latency.
    app->enhancementConfig.presentation.mode = RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;

    app->updateEnhancementConfig();
}

uint32_t mtc::renderer::RT64Context::get_display_framerate() const {
    return app->presentQueue->ext.sharedResources->swapChainRate;
}

float mtc::renderer::RT64Context::get_resolution_scale() const {
    constexpr int ReferenceHeight = 240;
    switch (app->userConfig.resolution) {
        case RT64::UserConfiguration::Resolution::WindowIntegerScale:
            if (app->sharedQueueResources->swapChainHeight > 0) {
                return std::max(float((app->sharedQueueResources->swapChainHeight + ReferenceHeight - 1) / ReferenceHeight), 1.0f);
            }
            else {
                return 1.0f;
            }
        case RT64::UserConfiguration::Resolution::Manual:
            return float(app->userConfig.resolutionMultiplier);
        case RT64::UserConfiguration::Resolution::Original:
        default:
            return 1.0f;
    }
}

void mtc::renderer::RT64Context::check_texture_pack_actions() {
    bool packs_changed = false;
    TexturePackAction cur_action;
    while (texture_pack_action_queue.try_dequeue(cur_action)) {
        std::visit(overloaded{
            [&](TexturePackDisableAction &to_disable) {
                enabled_texture_packs.erase(to_disable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackEnableAction &to_enable) {
                enabled_texture_packs.insert(to_enable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackSecondaryDisableAction &to_override_disable) {
                secondary_disabled_texture_packs.insert(to_override_disable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackSecondaryEnableAction &to_override_enable) {
                secondary_disabled_texture_packs.erase(to_override_enable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackUpdateAction &) {
                packs_changed = true;
            }
        }, cur_action);
    }

    // If any packs were disabled, unload all packs and load all the active ones.
    if (packs_changed) {
        // Sort the enabled texture packs in reverse order so that earlier ones override later ones.
        std::vector<std::string> sorted_texture_packs{};
        sorted_texture_packs.reserve(enabled_texture_packs.size());
        for (const std::string& mod : enabled_texture_packs) {
            if (!secondary_disabled_texture_packs.contains(mod)) {
                sorted_texture_packs.emplace_back(mod);
            }
        }

        std::sort(sorted_texture_packs.begin(), sorted_texture_packs.end(),
            [](const std::string& lhs, const std::string& rhs) {
                return recomp::mods::get_mod_order_index(lhs) > recomp::mods::get_mod_order_index(rhs);
            }
        );

        // Build the path list from the sorted mod list.
        std::vector<RT64::ReplacementDirectory> replacement_directories;
        replacement_directories.reserve(enabled_texture_packs.size());
        for (const std::string &mod_id : sorted_texture_packs) {
            replacement_directories.emplace_back(RT64::ReplacementDirectory(recomp::mods::get_mod_filename(mod_id)));
        }

        if (!replacement_directories.empty()) {
            app->textureCache->loadReplacementDirectories(replacement_directories);
        }
        else {
            app->textureCache->clearReplacementDirectories();
        }
    }
}

RT64::UserConfiguration::Antialiasing mtc::renderer::RT64MaxMSAA() {
    return device_max_msaa;
}

std::unique_ptr<ultramodern::renderer::RendererContext> mtc::renderer::create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
    return std::make_unique<mtc::renderer::RT64Context>(rdram, window_handle, developer_mode);
}

bool mtc::renderer::RT64SamplePositionsSupported() {
    return sample_positions_supported;
}

bool mtc::renderer::RT64HighPrecisionFBEnabled() {
    return high_precision_fb_enabled;
}

void mtc::renderer::trigger_texture_pack_update() {
    texture_pack_action_queue.enqueue(TexturePackUpdateAction{});
}

void mtc::renderer::enable_texture_pack(const recomp::mods::ModContext& context, const recomp::mods::ModHandle& mod) {
    texture_pack_action_queue.enqueue(TexturePackEnableAction{mod.manifest.mod_id});

    // Check for the texture pack enabled config option.
    const recomp::config::ConfigSchema& config_schema = context.get_mod_config_schema(mod.manifest.mod_id);
    auto find_it = config_schema.options_by_id.find(mtc::renderer::special_option_texture_pack_enabled);
    if (find_it != config_schema.options_by_id.end()) {
        const recomp::config::ConfigOption& config_option = config_schema.options[find_it->second];

        if (is_texture_pack_enable_config_option(config_option, false)) {
            recomp::config::ConfigValueVariant value_variant = context.get_mod_config_value(mod.manifest.mod_id, config_option.id);
            uint32_t value;
            if (uint32_t* value_ptr = std::get_if<uint32_t>(&value_variant)) {
                value = *value_ptr;
            }
            else {
                value = 0;
            }

            if (value) {
                mtc::renderer::secondary_enable_texture_pack(mod.manifest.mod_id);
            }
            else {
                mtc::renderer::secondary_disable_texture_pack(mod.manifest.mod_id);
            }
        }
    }
}

void mtc::renderer::disable_texture_pack(const recomp::mods::ModHandle& mod) {
    texture_pack_action_queue.enqueue(TexturePackDisableAction{mod.manifest.mod_id});
}

void mtc::renderer::secondary_enable_texture_pack(const std::string& mod_id) {
    texture_pack_action_queue.enqueue(TexturePackSecondaryEnableAction{mod_id});
}

void mtc::renderer::secondary_disable_texture_pack(const std::string& mod_id) {
    texture_pack_action_queue.enqueue(TexturePackSecondaryDisableAction{mod_id});
}


// HD texture enable option. Must be an enum with two options.
// The first option is treated as disabled and the second option is treated as enabled.
bool mtc::renderer::is_texture_pack_enable_config_option(const recomp::config::ConfigOption& option, bool show_errors) {
    if (option.id == mtc::renderer::special_option_texture_pack_enabled) {
        if (option.type != recomp::config::ConfigOptionType::Enum) {
            if (show_errors) {
                recompui::message_box(("Mod has the special config option id for enabling an HD texture pack (\"" + mtc::renderer::special_option_texture_pack_enabled + "\"), but the config option is not an enum.").c_str());
            }
            return false;
        }

        const recomp::config::ConfigOptionEnum &option_enum = std::get<recomp::config::ConfigOptionEnum>(option.variant);
        if (option_enum.options.size() != 2) {
            if (show_errors) {
                recompui::message_box(("Mod has the special config option id for enabling an HD texture pack (\"" + mtc::renderer::special_option_texture_pack_enabled + "\"), but the config option doesn't have exactly 2 values.").c_str());
            }
            return false;
        }

        return true;
    }
    return false;
}

#ifndef __MTC_RENDER_H__
#include <string>
#include <vector>
#define __MTC_RENDER_H__

#include <optional>
#include <unordered_set>
#include <filesystem>

#include "common/rt64_user_configuration.h"
#include "ultramodern/renderer_context.hpp"
#include "librecomp/mods.hpp"

namespace RT64 {
    struct Application;
}

namespace mtc {
    namespace renderer {
        inline const std::string special_option_texture_pack_enabled = "_recomp_texture_pack_enabled";

        class RT64Context final : public ultramodern::renderer::RendererContext {
        public:
            ~RT64Context() override;
            RT64Context(uint8_t *rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode);

            bool valid() override { return static_cast<bool>(app); }

            bool update_config(const ultramodern::renderer::GraphicsConfig &old_config, const ultramodern::renderer::GraphicsConfig &new_config) override;

            void enable_instant_present() override;
            void send_dl(const OSTask *task) override;
            void send_dummy_workload(uint32_t fb_address) override;
            void update_screen() override;
            void shutdown() override;
            uint32_t get_display_framerate() const override;
            float get_resolution_scale() const override;

        private:
            std::unique_ptr<RT64::Application> app;
            std::unordered_set<std::string> enabled_texture_packs;
            std::unordered_set<std::string> secondary_disabled_texture_packs;

            void check_texture_pack_actions();
        };

        std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(uint8_t *rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode);

        // Graphics API selection (0.1.4): --graphics-api auto|vulkan|d3d12 overrides graphics.json's api_option for
        // the session. Auto = Vulkan first on Windows, D3D12 as the fallback.
        void set_api_override(std::optional<ultramodern::renderer::GraphicsApi> api);
        std::optional<ultramodern::renderer::GraphicsApi> api_override();
        std::string graphics_api_name(ultramodern::renderer::GraphicsApi api);
        void set_active_api_name(const std::string& name);
        std::string active_api_name(); // "Vulkan - NVIDIA GeForce ..." once the renderer is up

        RT64::UserConfiguration::Antialiasing RT64MaxMSAA();
        bool RT64SamplePositionsSupported();
        bool RT64HighPrecisionFBEnabled();

        void trigger_texture_pack_update();
        void enable_texture_pack(const recomp::mods::ModContext& context, const recomp::mods::ModHandle& mod);
        void disable_texture_pack(const recomp::mods::ModHandle& mod);
        void secondary_enable_texture_pack(const std::string& mod_id);
        void secondary_disable_texture_pack(const std::string& mod_id);

        // Texture pack enable option. Must be an enum with two options.
        // The first option is treated as disabled and the second option is treated as enabled.
        bool is_texture_pack_enable_config_option(const recomp::config::ConfigOption& option, bool show_errors);

        // Post-processing shaders (RT64 branch `shaders`, src/postfx/rt64_postprocess_api.h).
        // Ids: "" = none, "builtin:<name>" = embedded preset, "file:<path>" = preset from the shaders folder.
        struct ShaderPreset {
            std::string id;
            std::string name;
            std::string license;
            bool builtin = false;
        };
        bool shaders_supported();                       // RT64 was built with post-processing
        bool shaders_ready();                           // ...and a renderer is running
        std::vector<ShaderPreset> list_shader_presets(); // rescans; "None" first, then built-ins, then shaders/
        // Applies live (compiles synchronously, switches next frame). On failure the previous preset stays and
        // `error` receives RT64's message.
        bool set_shader_preset(const std::string& id, std::string* error);
        std::string active_shader_preset();
        std::string shader_folder();
        std::string shader_notices();                   // Markdown notices of the embedded shaders
    }
}

#endif

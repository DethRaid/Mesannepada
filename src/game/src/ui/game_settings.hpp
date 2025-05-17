#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>
#include <EASTL/string.h>

#include "scalability/settings_controller.hpp"
#include "ui/ui_screen.hpp"

namespace ui {
    class SettingsScreen final : public Screen {
    public:
        explicit SettingsScreen(Controller& controller_in);

        ~SettingsScreen() override;

        bool on_escape() override;

    private:
        Rml::DataModelHandle graphics_model_handle;

        bool graphics_dirty = false;

        uint32_t antialiasing = 0;

        uint32_t dlss_mode = 3;
        bool dlss_ray_reconstruction = true;

        uint32_t fsr3_mode = 1;

        std::string shadow_fidelity = "Ultra";
        std::string gi_fidelity = "Ultra";

        bool audio_dirty = false;

        float master_volume = 100;
        float music_volume = 100;
        float sfx_volume = 100;

        std::string master_volume_text;
        std::string music_volume_text;
        std::string sfx_volume_text;

        Rml::DataModelHandle audio_model_handle;

        bool show_confirmation = false;

        Rml::DataModelHandle screen_model_handle;

        void on_overall_fidelity(Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params);

        void on_antialiasing(Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params);

        void on_submit_graphics_options(Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params);

        void set_dlss_options(SettingsController& settings);

        void set_fsr3_options(SettingsController& settings);

        void on_submit_game_options(Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params);

        void on_submit_audio_options(Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params);

        void on_exit_screen(Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params);

        void update_cached_fidelity(const SettingsController& settings);
    };
}

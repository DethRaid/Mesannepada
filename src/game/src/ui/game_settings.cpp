#include "game_settings.hpp"

#if SAH_USE_STREAMLINE
#include <sl.h>
#include <sl_dlss.h>
#endif
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>

#if SAH_USE_FFX
#include <ffx_api/ffx_upscale.h>
#endif

#include "console/cvars.hpp"
#include "core/engine.hpp"
#include "core/fidelity_level.hpp"
#include "core/system_interface.hpp"
#include "core/toml_config.hpp"
#include "i18n/cuneiform.hpp"
#include "ui/ui_controller.hpp"

static std::shared_ptr<spdlog::logger> logger;

namespace ui {
    SettingsScreen::SettingsScreen(Controller& controller_in) : Screen{controller_in} {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("GameSettings");
        }

        is_blocking_screen = true;

        auto& context = controller.get_context();

        {
            auto graphics_data_model = context.CreateDataModel("graphics_settings");

            graphics_data_model.Bind("dirty", &graphics_dirty);

            graphics_data_model.Bind("antialiasing", &antialiasing);

            graphics_data_model.Bind("dlss_mode", &dlss_mode);
            graphics_data_model.Bind("dlss_ray_reconstruction", &dlss_ray_reconstruction);

            graphics_data_model.Bind("fsr3_mode", &fsr3_mode);

            graphics_data_model.Bind("shadow_fidelity", &shadow_fidelity);
            graphics_data_model.Bind("gi_fidelity", &gi_fidelity);

            graphics_data_model.BindEventCallback("on_overall_fidelity", &SettingsScreen::on_overall_fidelity, this);
            graphics_data_model.BindEventCallback("on_antialiasing", &SettingsScreen::on_antialiasing, this);
            graphics_data_model.BindEventCallback(
                "on_submit_graphics_options",
                &SettingsScreen::on_submit_graphics_options,
                this);

            graphics_model_handle = graphics_data_model.GetModelHandle();
        }

        {
            auto audio_data_model = context.CreateDataModel("audio_settings");

            audio_data_model.Bind("dirty", &audio_dirty);

            audio_data_model.Bind("master_volume", &master_volume);
            audio_data_model.Bind("music_volume", &music_volume);
            audio_data_model.Bind("sfx_volume", &sfx_volume);

            audio_data_model.Bind("master_volume_text", &master_volume_text);
            audio_data_model.Bind("music_volume_text", &music_volume_text);
            audio_data_model.Bind("sfx_volume_text", &sfx_volume_text);

            audio_data_model.BindEventCallback(
                "on_submit_audio_options",
                &SettingsScreen::on_submit_audio_options,
                this);

            audio_model_handle = audio_data_model.GetModelHandle();
        }

        {
            auto screen_data_model = context.CreateDataModel("settings_screen");

            screen_data_model.Bind("show_confirmation", &show_confirmation);

            screen_data_model.BindEventCallback("on_exit_screen", &SettingsScreen::on_exit_screen, this);

            screen_model_handle = screen_data_model.GetModelHandle();
        }

        //graphics_data_model.BindEventCallback("on_submit_game_options", &GameSettings::on_submit_game_options, this);


        load_document(SystemInterface::get().get_data_folder() / "ui/settings/settings.rml");

        update_cached_fidelity(Engine::get().get_settings_controller());
    }

    SettingsScreen::~SettingsScreen() {
        auto& context = controller.get_context();

        context.RemoveDataModel("graphics_settings");
        context.RemoveDataModel("audio_settings");
        context.RemoveDataModel("settings_screen");
    }

    bool SettingsScreen::on_escape() {
        controller.pop_screen();

        return true;
    }

    // ReSharper disable once CppMemberFunctionMayBeConst
    void SettingsScreen::on_submit_game_options(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        // TODO
    }

    void SettingsScreen::on_overall_fidelity(
        const Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params
    ) {
        assert(params.size() == 1);

        const auto& parameter = params[0];
        const auto graphics_fidelity = parameter.Get<FidelityLevel>();

        logger->debug("Selected overall graphics fidelity {}", to_string(graphics_fidelity));

        auto& scalability = Engine::get().get_settings_controller();
        scalability.set_overall_graphics_fidelity(graphics_fidelity);

        update_cached_fidelity(scalability);
    }

    void SettingsScreen::on_antialiasing(
        const Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params
    ) {
        assert(params.size() == 1);

        const auto& parameter = params[0];
        const auto aa = parameter.Get<uint32_t>();

        antialiasing = aa;

        auto& settings = Engine::get().get_settings_controller();

        switch(aa) {
        case 0:
            settings.set_antialiasing(render::AntiAliasingType::None);
            break;

#if SAH_USE_STREAMLINE
        case 1:
            settings.set_antialiasing(render::AntiAliasingType::DLSS);
            break;
#endif

        case 2:
            settings.set_antialiasing(render::AntiAliasingType::FSR3);
            break;

        default:
            settings.set_antialiasing(render::AntiAliasingType::None);
        }

        graphics_model_handle.DirtyVariable("antialiasing");

        settings.apply_graphics_settings();
    }

    // ReSharper disable once CppMemberFunctionMayBeConst
    void SettingsScreen::on_submit_graphics_options(
        Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params
    ) {
        auto& settings = Engine::get().get_settings_controller();

        settings.set_shadow_fidelity(from_string(shadow_fidelity.c_str()));
        settings.set_global_illumination_fidelity(from_string(gi_fidelity.c_str()));

        if(antialiasing == 0) {
            settings.set_antialiasing(render::AntiAliasingType::None);

        } else if(antialiasing == 1) {
            settings.set_antialiasing(render::AntiAliasingType::DLSS);
            set_dlss_options(settings);

        } else if(antialiasing == 2) {
            settings.set_antialiasing(render::AntiAliasingType::FSR3);
            set_fsr3_options(settings);
        }

        settings.apply_graphics_settings();

        const auto exit = params[0].Get<bool>();
        if(exit) {
            controller.pop_screen();
        }
    }

    void SettingsScreen::set_dlss_options(SettingsController& settings) {
#if SAH_USE_STREAMLINE
        settings.set_dlss_mode(static_cast<sl::DLSSMode>(dlss_mode));
        settings.set_use_ray_reconstruction(dlss_ray_reconstruction);
#endif
    }

    void SettingsScreen::set_fsr3_options(SettingsController& settings) {
#if SAH_USE_FFX
        settings.set_fsr3_mode(static_cast<FfxApiUpscaleQualityMode>(fsr3_mode));
#endif
    }

    // ReSharper disable once CppMemberFunctionMayBeConst
    void SettingsScreen::on_submit_audio_options(
        Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params
    ) {
        auto& settings = Engine::get().get_settings_controller();
        settings.set_master_volume(master_volume);
        settings.set_music_volume(music_volume);
        settings.set_sfx_volume(sfx_volume);
        settings.apply_audio_settings();

        master_volume_text = i18n::to_cuneiform(static_cast<uint32_t>(master_volume)).c_str();
        music_volume_text = i18n::to_cuneiform(static_cast<uint32_t>(music_volume)).c_str();
        sfx_volume_text = i18n::to_cuneiform(static_cast<uint32_t>(sfx_volume)).c_str();

        audio_model_handle.DirtyAllVariables();
        audio_dirty = false;

        const auto exit = params[0].Get<bool>();
        if(exit) {
            controller.pop_screen();
        }
    }

    void SettingsScreen::on_exit_screen(
        Rml::DataModelHandle handle, Rml::Event& event, const Rml::VariantList& params
    ) {
        const auto save = params[0].Get<bool>();
        if(save && graphics_dirty) {
            on_submit_graphics_options(handle, event, params);
        }
        if(save && audio_dirty) {
            on_submit_audio_options(handle, event, params);
        }

        controller.pop_screen();
    }

    void SettingsScreen::update_cached_fidelity(const SettingsController& settings) {
        const auto aa = settings.get_antialiasing();
        if(aa == render::AntiAliasingType::None) {
            antialiasing = 0;

#if SAH_USE_STREAMLINE
        } else if(aa == render::AntiAliasingType::DLSS) {
            antialiasing = 1;
#endif
        } else if(aa == render::AntiAliasingType::FSR3) {
            antialiasing = 2;
        }

#if SAH_USE_STREAMLINE
        dlss_mode = static_cast<uint32_t>(settings.get_dlss_mode());
        dlss_ray_reconstruction = settings.get_ray_reconstruction();
#endif

#if SAH_USE_FFX
        fsr3_mode = static_cast<uint32_t>(settings.get_fsr3_mode());
#endif

        shadow_fidelity = to_string(settings.get_shadow_fidelity());
        gi_fidelity = to_string(settings.get_global_illumination_fidelity());

        graphics_model_handle.DirtyAllVariables();
    }
}

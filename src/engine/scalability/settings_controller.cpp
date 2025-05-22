#include "settings_controller.hpp"

#include "core/system_interface.hpp"
#include <toml11/parser.hpp>

#include "console/cvars.hpp"
#include "core/issue_breakpoint.hpp"
#include "core/toml_config.hpp"

static constexpr auto SCALABILITY_FILE_NAME = "data/config/scalability.toml";

static std::shared_ptr<spdlog::logger> logger;

SettingsController::SettingsController() {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("SettingsController");
    }
    try {
        auto* scalability_file = SystemInterface::get().open_file(SCALABILITY_FILE_NAME);
        scalability_data = toml::parse(scalability_file, SCALABILITY_FILE_NAME);
    } catch(const std::exception& e) {
        logger->error(e.what());
        SAH_BREAKPOINT;
    }

    load_settings_file();
}

SettingsController::~SettingsController() {
    save_graphics_settings_file();
    save_audio_settings_file();
}

void SettingsController::set_antialiasing(const render::AntiAliasingType antialiasing) {
    anti_aliasing = antialiasing;
}

#if SAH_USE_STREAMLINE
void SettingsController::set_dlss_mode(const sl::DLSSMode dlss_mode) {
    this->dlss_mode = dlss_mode;
}
#endif

void SettingsController::set_use_ray_reconstruction(const bool use_ray_reconstruction_in) {
    use_ray_reconstruction = use_ray_reconstruction_in;
}

void SettingsController::set_fsr3_mode(FfxApiUpscaleQualityMode fsr3_mode_in) {
    fsr3_mode = fsr3_mode_in;
}

void SettingsController::set_overall_graphics_fidelity(const FidelityLevel fidelity) {
    graphics_fidelity = fidelity;

    logger->info("Setting graphics fidelity to {}", to_string(fidelity));

    const auto& graphics_table = scalability_data.at("graphics");
    const auto& selected_graphics = graphics_table.at(to_string(fidelity));

    const auto& texture_fidelity = selected_graphics.at("textures").as_string();
    set_texture_fidelity(from_string(texture_fidelity.c_str()));

    const auto& shadow_fidelity = selected_graphics.at("shadows").as_string();
    set_shadow_fidelity(from_string(shadow_fidelity.c_str()));

    const auto& gi_fidelity = selected_graphics.at("global_illumination").as_string();
    set_global_illumination_fidelity(from_string(gi_fidelity.c_str()));
}

void SettingsController::set_texture_fidelity(const FidelityLevel fidelity) {
    logger->info("Setting texture fidelity to {}", to_string(fidelity));
    texture_fidelity = fidelity;
}

void SettingsController::set_shadow_fidelity(const FidelityLevel fidelity) {
    logger->info("Setting shadow fidelity to {}", to_string(fidelity));
    shadow_fidelity = fidelity;

    const auto& shadows_table = scalability_data.at("shadows");
    const auto& selected_shadows = shadows_table.at(to_string(fidelity)).as_table();

    CVarSystem::Get()->set_from_toml(selected_shadows);
}

void SettingsController::set_global_illumination_fidelity(const FidelityLevel fidelity) {
    logger->info("Setting global illumination fidelity to {}", to_string(fidelity));
    gi_fidelity = fidelity;

    const auto& gi_table = scalability_data.at("global_illumination");
    const auto& selected_gi = gi_table.at(to_string(fidelity)).as_table();

    CVarSystem::Get()->set_from_toml(selected_gi);
}

render::AntiAliasingType SettingsController::get_antialiasing() const {
    return anti_aliasing;
}

#if SAH_USE_STREAMLINE
sl::DLSSMode SettingsController::get_dlss_mode() const {
    return dlss_mode;
}
#endif

bool SettingsController::get_ray_reconstruction() const {
    return use_ray_reconstruction;
}

FfxApiUpscaleQualityMode SettingsController::get_fsr3_mode() const {
    return fsr3_mode;
}

FidelityLevel SettingsController::get_overall_fidelity() const {
    return graphics_fidelity;
}

FidelityLevel SettingsController::get_shadow_fidelity() const {
    return shadow_fidelity;
}

FidelityLevel SettingsController::get_global_illumination_fidelity() const {
    return gi_fidelity;
}

void SettingsController::apply_graphics_settings() {
    auto* cvars = CVarSystem::Get();

    cvars->SetEnumCVar("r.AntiAliasing", anti_aliasing);
#if SAH_USE_STREAMLINE
    if(anti_aliasing == render::AntiAliasingType::DLSS) {
        cvars->SetEnumCVar("r.DLSS.Mode", dlss_mode);
        cvars->SetIntCVar("r.DLSS-RR.Enabled", use_ray_reconstruction);
    } else
#endif
    if(anti_aliasing == render::AntiAliasingType::FSR3) {
        cvars->SetIntCVar("r.FSR3.Quality", fsr3_mode);
    }

    set_texture_fidelity(texture_fidelity);
    set_shadow_fidelity(shadow_fidelity);
    set_global_illumination_fidelity(gi_fidelity);

    save_graphics_settings_file();
}

void SettingsController::save_graphics_settings_file() const {
    auto data_file_path = SystemInterface::get().get_write_folder();
    if(!std::filesystem::exists(data_file_path)) {
        std::filesystem::create_directories(data_file_path);
    }

    data_file_path /= "settings.toml";

    auto data_file = toml::parse<TomlTypeConfig>(data_file_path);

    data_file["graphics"]["antialiasing"] = static_cast<uint32_t>(anti_aliasing);
#if SAH_USE_STREAMLINE
    data_file["graphics"]["dlss_mode"] = static_cast<uint32_t>(dlss_mode);
#endif
    data_file["graphics"]["dlss_ray_reconstruction"] = use_ray_reconstruction;
    data_file["graphics"]["fsr3_mode"] = static_cast<uint32_t>(fsr3_mode);
    data_file["graphics"]["shadow_fidelity"] = to_string(shadow_fidelity);
    data_file["graphics"]["gi_fidelity"] = to_string(gi_fidelity);

    SystemInterface::get().write_file(data_file_path, toml::format(data_file));
}

void SettingsController::set_master_volume(const float master_volume_in) {
    master_volume = master_volume_in;
}

void SettingsController::set_music_volume(const float music_volume_in) {
    music_volume = music_volume_in;
}

void SettingsController::set_sfx_volume(const float sfx_volume_in) {
    sfx_volume = sfx_volume_in;
}

void SettingsController::apply_audio_settings() const {
    auto* cvars = CVarSystem::Get();

    cvars->SetFloatCVar("a.MasterVolume", master_volume);
    cvars->SetFloatCVar("a.MusicVolume", music_volume);
    cvars->SetFloatCVar("a.SFXVolume", sfx_volume);
}

void SettingsController::save_audio_settings_file() const {
    auto data_file_path = SystemInterface::get().get_write_folder();
    if(!std::filesystem::exists(data_file_path)) {
        std::filesystem::create_directories(data_file_path);
    }

    data_file_path /= "settings.toml";

    auto data_file = toml::parse<TomlTypeConfig>(data_file_path);

    data_file["audio"]["master_volume"] = master_volume;
    data_file["audio"]["music_volume"] = music_volume;
    data_file["audio"]["sfx_volume"] = sfx_volume;

    SystemInterface::get().write_file(data_file_path, toml::format(data_file));
}

void SettingsController::load_settings_file() {
    const auto data_file_path = SystemInterface::get().get_write_folder() / "settings.toml";
    if(!std::filesystem::exists(data_file_path)) {
        return;
    }

    const auto data_file = toml::parse<TomlTypeConfig>(data_file_path);

    if(data_file.contains("graphics")) {
        const auto& graphics_settings = data_file.at("graphics").as_table();

        if(const auto& itr = graphics_settings.find("antialiasing"); itr != graphics_settings.end()) {
            anti_aliasing = static_cast<render::AntiAliasingType>(itr->second.as_integer());
        }
#if SAH_USE_STREAMLINE
        if (const auto itr = graphics_settings.find("dlss_mode"); itr != graphics_settings.end()) {
            dlss_mode = static_cast<sl::DLSSMode>(itr->second.as_integer());
        }
#endif
        if(const auto itr = graphics_settings.find("dlss_ray_reconstruction"); itr != graphics_settings.end()) {
            use_ray_reconstruction = itr->second.as_boolean();
        }
        if(const auto itr = graphics_settings.find("fsr3_mode"); itr != graphics_settings.end()) {
            fsr3_mode = static_cast<FfxApiUpscaleQualityMode>(itr->second.as_integer());
        }
        if(const auto itr = graphics_settings.find("shadow_fidelity"); itr != graphics_settings.end()) {
            shadow_fidelity = from_string(itr->second.as_string().c_str());
        }
        if(const auto itr = graphics_settings.find("gi_fidelity"); itr != graphics_settings.end()) {
            gi_fidelity = from_string(itr->second.as_string().c_str());
        }

        apply_graphics_settings();
    }

    if(data_file.contains("audio")) {
        const auto& audio_settings = data_file.at("audio").as_table();

        if (const auto itr = audio_settings.find("master_volume"); itr != audio_settings.end()) {
            master_volume = itr->second.as_floating();
        }
        if(const auto itr = audio_settings.find("music_volume"); itr != audio_settings.end()) {
            music_volume = itr->second.as_floating();
        }
        if(const auto itr = audio_settings.find("sfx_volume"); itr != audio_settings.end()) {
            sfx_volume = itr->second.as_floating();
        }

        apply_audio_settings();
    }
}

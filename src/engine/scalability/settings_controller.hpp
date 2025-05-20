#pragma once

#include <sl.h>
#include <sl_dlss.h>
#include <ffx_api/ffx_upscale.h>
#include <toml11/types.hpp>

#include "core/fidelity_level.hpp"
#include "render/antialiasing_type.hpp"

class SettingsController {
public:
    SettingsController();

    ~SettingsController();

    void set_antialiasing(render::AntiAliasingType antialiasing);

    void set_dlss_mode(sl::DLSSMode dlss_mode);

    void set_use_ray_reconstruction(bool use_ray_reconstruction_in);

    void set_fsr3_mode(FfxApiUpscaleQualityMode fsr3_mode_in);

    void set_overall_graphics_fidelity(FidelityLevel fidelity);

    void set_texture_fidelity(FidelityLevel fidelity);

    void set_shadow_fidelity(FidelityLevel fidelity);

    void set_global_illumination_fidelity(FidelityLevel fidelity);

    render::AntiAliasingType get_antialiasing() const;

    sl::DLSSMode get_dlss_mode() const;

    bool get_ray_reconstruction() const;

    FfxApiUpscaleQualityMode get_fsr3_mode() const;

    FidelityLevel get_overall_fidelity() const;

    FidelityLevel get_shadow_fidelity() const;

    FidelityLevel get_global_illumination_fidelity() const;

    void apply_graphics_settings();

    /**
     * Saves the current settings to the data file
     */
    void save_graphics_settings_file() const;

    void set_master_volume(float master_volume_in);

    void set_music_volume(float music_volume_in);

    void set_sfx_volume(float sfx_volume_in);

    void apply_audio_settings() const;

    void save_audio_settings_file() const;

private:
    toml::value scalability_data;

    render::AntiAliasingType anti_aliasing = render::AntiAliasingType::DLSS;

    sl::DLSSMode dlss_mode = sl::DLSSMode::eMaxQuality;

    bool use_ray_reconstruction = false;

    FfxApiUpscaleQualityMode fsr3_mode = FFX_UPSCALE_QUALITY_MODE_QUALITY;

    FidelityLevel graphics_fidelity = FidelityLevel::Ultra;
    FidelityLevel texture_fidelity = FidelityLevel::Ultra;
    FidelityLevel shadow_fidelity = FidelityLevel::Ultra;
    FidelityLevel gi_fidelity = FidelityLevel::Ultra;

    float master_volume = 100;
    float music_volume = 100;
    float sfx_volume = 100;

    void load_settings_file();
};

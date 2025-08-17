#pragma once

#if SAH_USE_STREAMLINE
#include <sl.h>
#include <sl_dlss.h>
#endif
#if SAH_USE_XESS
#include <xess/xess.h>
#endif
#if SAH_USE_FFX
#include <ffx_api/ffx_upscale.h>
#endif
#include <toml11/types.hpp>

#include "core/fidelity_level.hpp"
#include "render/antialiasing_type.hpp"

class SettingsController {
public:
    SettingsController();

    ~SettingsController();

    void set_antialiasing(render::AntiAliasingType antialiasing);

#if SAH_USE_STREAMLINE
    void set_dlss_mode(sl::DLSSMode dlss_mode);
#endif

    void set_use_ray_reconstruction(bool use_ray_reconstruction_in);

#if SAH_USE_XESS
    void set_xess_mode(xess_quality_settings_t xess_quality_setting);
#endif

#if SAH_USE_FFX
    void set_fsr3_mode(FfxApiUpscaleQualityMode fsr3_mode_in);
#endif

    void set_overall_graphics_fidelity(FidelityLevel fidelity);

    void set_texture_fidelity(FidelityLevel fidelity);

    void set_shadow_fidelity(FidelityLevel fidelity);

    void set_global_illumination_fidelity(FidelityLevel fidelity);

    render::AntiAliasingType get_antialiasing() const;

#if SAH_USE_STREAMLINE
    sl::DLSSMode get_dlss_mode() const;
#endif

    bool get_ray_reconstruction() const;

#if SAH_USE_XESS
    xess_quality_settings_t get_xess_mode() const;
#endif

#if SAH_USE_FFX
    FfxApiUpscaleQualityMode get_fsr3_mode() const;
#endif

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

#if SAH_USE_STREAMLINE
    sl::DLSSMode dlss_mode = sl::DLSSMode::eMaxQuality;
#endif

    bool use_ray_reconstruction = false;

#if SAH_USE_XESS
    xess_quality_settings_t xess_quality_setting = XESS_QUALITY_SETTING_BALANCED;
#endif

#if SAH_USE_FFX
    FfxApiUpscaleQualityMode fsr3_mode = FFX_UPSCALE_QUALITY_MODE_QUALITY;
#endif

    FidelityLevel graphics_fidelity = FidelityLevel::Ultra;
    FidelityLevel texture_fidelity = FidelityLevel::Ultra;
    FidelityLevel shadow_fidelity = FidelityLevel::Ultra;
    FidelityLevel gi_fidelity = FidelityLevel::Ultra;

    float master_volume = 100;
    float music_volume = 100;
    float sfx_volume = 100;

    void load_settings_file();
};

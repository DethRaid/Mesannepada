#include "nvidia_realtime_denoiser.hpp"

#include <EASTL/array.h>
#include <EASTL/string.h>

#include <NRI.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRIWrapperVK.h>

#include <Extensions/NRIHelper.h>
#ifndef NRI_HELPER
#define NRI_HELPER 1
#endif
#include <NRDIntegration.hpp>

#include "console/cvars.hpp"
#include "core/system_interface.hpp"
#include "render/gbuffer.hpp"
#include "render/backend/render_graph.hpp"

namespace render {
    static auto cvar_nrd_split_screen = AutoCVar_Int{"r.NRD.SplitScreen",
                                                     "Whether to show the split noisy/denoised view", 0};
    static auto cvar_nrd_validation = AutoCVar_Int{"r.NRD.Validation",
                                                   "Whether to enable NRD validation", 1};

    static auto cvar_reblur_antilag_sigma = AutoCVar_Float{
        "r.NRD.ReBLUR.AntiLag.LuminanceSigmaScale",
        "[1; 5] - delta is reduced by local variance multiplied by this value",
        4.0
    };

    static auto cvar_reblur_antilag_sensitivity = AutoCVar_Float{
        "r.NRD.ReBLUR.LuminanceSigmaSensitivity",
        "[1; 5] - antilag sensitivity (smaller values increase sensitivity)",
        3.0
    };

    static auto cvar_reblur_max_accumulated_frames = AutoCVar_Int{
        "r.NRD.ReBLUR.MaxAccumulatedFrameNum",
        "[0; 63] - maximum number of linearly accumulated frames",
        30
    };

    static auto cvar_reblur_max_fast_frames = AutoCVar_Int{
        "r.NRD.ReBLUR.MaxFastAccumulatedFrameNum",
        "[0; maxAccumulatedFrameNum) - maximum number of linearly accumulated frames for fast history\nValues \">= maxAccumulatedFrameNum\" disable fast history\nUsually 5x times shorter than the main history",
        6
    };

    static auto cvar_reblur_max_stabilized_frames = AutoCVar_Int{
        "r.NRD.ReBLUR.MaxStabilizedFrameNum",
        "[0; maxAccumulatedFrameNum] - maximum number of linearly accumulated frames for stabilized radiance\n\"0\" disables the stabilization pass\nValues \">= maxAccumulatedFrameNum\"  get clamped to \"maxAccumulatedFrameNum\"",
        nrd::REBLUR_MAX_HISTORY_FRAME_NUM
    };

    static auto cvar_reblur_history_fix_frame_num = AutoCVar_Int{
        "r.NRD.ReBLUR.HistoryFrameFixNum",
        "[0; 3] - number of reconstructed frames after history reset (less than \"maxFastAccumulatedFrameNum\")",
        3
    };

    static auto cvar_reblur_history_fix_pixel_stride = AutoCVar_Int{
        "r.NRD.ReBLUR.HistoryFixBasePixelStride",
        "(> 0) - base stride between pixels in 5x5 history reconstruction kernel (gets reduced over time)",
        14
    };

    static auto cvar_reblur_diffuse_prepass_blur_radius = AutoCVar_Float{
        "r.NRD.ReBLUR.DiffusePrepassBlurRadius",
        "(pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, must be used in case of badly defined signals and probabilistic sampling)",
        30.f
    };

    static auto cvar_reblur_specular_prepass_blur_radius = AutoCVar_Float{
        "r.NRD.ReBLUR.SpecularPrepassBlurRadius",
        "(pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, must be used in case of badly defined signals and probabilistic sampling)",
        50.f
    };

    static auto cvar_reblur_min_hit_dist_weight = AutoCVar_Float{
        "r.NRD.ReBLUR.MinHitDistanceWeight",
        "(0; 0.2] - bigger values reduce sensitivity to shadows in spatial passes, smaller values are recommended for signals with relatively clean hit distance (like RTXDI/RESTIR)",
        0.1f
    };

    static auto cvar_reblur_min_blur_radius = AutoCVar_Float{
        "r.NRD.ReBLUR.MinBlurRadius",
        "(pixels) - min denoising radius (for converged state)",
        1.f
    };

    static auto cvar_reblur_max_blur_radius = AutoCVar_Float{
        "r.NRD.ReBLUR.MaxBlurRadius",
        "(pixels) - base (max) denoising radius (gets reduced over time)",
        30.f
    };

    static auto cvar_reblur_lobe_angle_fraction = AutoCVar_Float{
        "r.NRD.ReBLUR.LobeAngleFraction",
        "(normalized %) - base fraction of diffuse or specular lobe angle used to drive normal based rejection",
        0.15f
    };

    static auto cvar_reblur_roughness_fraction = AutoCVar_Float{
        "r.NRD.ReBLUR.RoughnessFraction",
        "(normalized %) - base fraction of center roughness used to drive roughness based rejection",
        0.15f
    };

    static auto cvar_reblur_accumulation_roughness_threshold = AutoCVar_Float{
        "r.NRD.ReBLUR.ResponsiveAccumulationRoughnessThreshold",
        "[0; 1] - if roughness < this, temporal accumulation becomes responsive and driven by roughness (useful for animated water)",
        0.f
    };

    static auto cvar_reblur_plane_dist_sensitivity = AutoCVar_Float{
        "r.NRD.ReBLUR.PlaneDistanceSensitivity",
        "(normalized %) - represents maximum allowed deviation from the local tangent plane",
        0.02f
    };

    static auto cvar_reblur_specular_threshold_mv_x = AutoCVar_Float{
        "r.NRD.SpecularProbabilityThresholdsForMvModification.x",
        "IN_MV = lerp(IN_MV, specularMotion, smoothstep(this[0], this[1], specularProbability))",
        0.5f
    };

    static auto cvar_reblur_specular_threshold_mv_y = AutoCVar_Float{
        "r.NRD.SpecularProbabilityThresholdsForMvModification.y",
        "IN_MV = lerp(IN_MV, specularMotion, smoothstep(this[0], this[1], specularProbability))",
        0.9f
    };

    static auto cvar_reblur_firefly_suppressor_min_scale = AutoCVar_Float{
        "r.NRD.ReBLUR.FireflySuppressorMinRelativeScale",
        "[1; 3] - undesired sporadic outliers suppression to keep output stable (smaller values maximize suppression in exchange of bias)",
        2.f
    };

    static auto cvar_reblur_checkerboard_mode = AutoCVar_Enum<nrd::CheckerboardMode>{
        "r.NRD.ReBLUR.CheckerboardMode",
        "If not OFF and used for DIFFUSE_SPECULAR, defines diffuse orientation, specular orientation is the opposite",
        nrd::CheckerboardMode::OFF
    };

    static auto cvar_reblur_hit_dist_reconstruction = AutoCVar_Enum<nrd::HitDistanceReconstructionMode>{
        "r.NRD.ReBLUR.HitDistanceReconstructionMode",
        "Must be used only in case of probabilistic sampling (not checkerboarding), when a pixel can be skipped and have \"0\" (invalid) hit distance",
        nrd::HitDistanceReconstructionMode::OFF
    };

    static auto cvar_reblur_enable_anti_firefly = AutoCVar_Int{
        "r.NRD.ReBLUR.EnableAntiFirefly",
        "Adds bias in case of badly defined signals, but tries to fight with fireflies",
        false
    };

    static auto cvar_reblur_enable_performance_mode = AutoCVar_Int{
        "r.NRD.ReBLUR.EnablePerformanceMode",
        "Boosts performance by sacrificing IQ",
        false
    };

    static auto cvar_reblur_min_diff_material = AutoCVar_Float{
        "r.NRD.ReBLUR.MinMaterialForDiffuse",
        "(Optional) material ID comparison: max(m0, minMaterial) == max(m1, minMaterial) (requires \"NormalEncoding::R10_G10_B10_A2_UNORM\")",
        4.f
    };

    static auto cvar_reblur_min_spec_material = AutoCVar_Float{
        "r.NRD.ReBLUR.MinMaterialForSpecular",
        "(Optional) material ID comparison: max(m0, minMaterial) == max(m1, minMaterial) (requires \"NormalEncoding::R10_G10_B10_A2_UNORM\")",
        4.f
    };

    static auto cvar_reblur_prepass_for_spec_motion_estimation = AutoCVar_Int{
        "r.NRD.ReBLUR.UsePrepassOnlyForSpecularMotionEstimation",
        "In rare cases, when bright samples are so sparse that any other bright neighbor can't be reached, pre-pass transforms a standalone bright pixel into a standalone bright blob, worsening the situation. Despite that it's a problem of sampling, the denoiser needs to handle it somehow on its side too. Diffuse pre-pass can be just disabled, but for specular it's still needed to find optimal hit distance for tracking. This boolean allow to use specular pre-pass for tracking purposes only (use with care)",
        false
    };

    static std::shared_ptr<spdlog::logger> logger;

    static nrd::Resource get_nrd_resource(TextureHandle texture, bool is_output = false);

    NvidiaRealtimeDenoiser::NvidiaRealtimeDenoiser() :
        instance{eastl::make_unique<nrd::Integration>()} {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("NvidiaRealtimeDenoiser");
        }
    }

    NvidiaRealtimeDenoiser::~NvidiaRealtimeDenoiser() {
        instance->Destroy();
    }

    void NvidiaRealtimeDenoiser::set_constants(const SceneView& scene_view, DenoiserType type,
                                               const uint2 render_resolution
        ) {
        if(cached_resolution != render_resolution || cached_denoiser_type != type) {
            cached_resolution = render_resolution;
            cached_denoiser_type = type;
            recreate_instance();
        }

        instance->NewFrame();

        const auto& view_data = scene_view.get_gpu_data();

        const auto smol_render_resolution = glm::u16vec2{cached_resolution};

        auto common_settings = nrd::CommonSettings{
            .motionVectorScale = {1.f / view_data.render_resolution.x, 1.f / view_data.render_resolution.y, 0},
            .cameraJitter = {view_data.jitter.x, view_data.jitter.y},
            .cameraJitterPrev = {view_data.previous_jitter.x, view_data.previous_jitter.y},
            .resourceSize = {smol_render_resolution.x, smol_render_resolution.y},
            .resourceSizePrev = {smol_render_resolution.x, smol_render_resolution.y},
            .rectSize = {smol_render_resolution.x, smol_render_resolution.y},
            .rectSizePrev = {smol_render_resolution.x, smol_render_resolution.y},
            .denoisingRange = 10000.f,
            .splitScreen = static_cast<float>(cvar_nrd_split_screen.get()),
            .frameIndex = scene_view.get_frame_count(),
            .isBaseColorMetalnessAvailable = false,
            .enableValidation = cvar_nrd_validation.get() != 0
        };

        const auto view_to_clip = scene_view.get_projection();
        memcpy(common_settings.viewToClipMatrix, &view_to_clip[0][0], sizeof(float4x4));

        const auto view_to_clip_prev = scene_view.get_last_frame_projection();
        memcpy(common_settings.viewToClipMatrixPrev, &view_to_clip_prev[0][0], sizeof(float4x4));

        const auto world_to_view = scene_view.get_view();
        memcpy(common_settings.worldToViewMatrix, &world_to_view[0][0], sizeof(float4x4));

        const auto world_to_view_prev = scene_view.get_last_frame_view();
        memcpy(common_settings.worldToViewMatrixPrev, &world_to_view_prev[0][0], sizeof(float4x4));

        instance->SetCommonSettings(common_settings);

        if(cached_denoiser_type == DenoiserType::ReBLUR) {
            auto reblur_settings = nrd::ReblurSettings{
                .antilagSettings = {
                    .luminanceSigmaScale = cvar_reblur_antilag_sigma.get(),
                    .luminanceSensitivity = cvar_reblur_antilag_sensitivity.get()
                },
                .maxAccumulatedFrameNum = cvar_reblur_max_accumulated_frames.get_uint(),
                .maxFastAccumulatedFrameNum = cvar_reblur_max_fast_frames.get_uint(),
                .maxStabilizedFrameNum = cvar_reblur_max_stabilized_frames.get_uint(),
                .historyFixFrameNum = cvar_reblur_history_fix_frame_num.get_uint(),
                .historyFixBasePixelStride = cvar_reblur_history_fix_pixel_stride.get_uint(),
                .diffusePrepassBlurRadius = cvar_reblur_diffuse_prepass_blur_radius.get(),
                .specularPrepassBlurRadius = cvar_reblur_specular_prepass_blur_radius.get(),
                .minHitDistanceWeight = cvar_reblur_min_hit_dist_weight.get(),
                .minBlurRadius = cvar_reblur_min_blur_radius.get(),
                .maxBlurRadius = cvar_reblur_max_blur_radius.get(),
                .lobeAngleFraction = cvar_reblur_lobe_angle_fraction.get(),
                .roughnessFraction = cvar_reblur_roughness_fraction.get(),
                .responsiveAccumulationRoughnessThreshold = cvar_reblur_accumulation_roughness_threshold.get(),
                .planeDistanceSensitivity = cvar_reblur_plane_dist_sensitivity.get(),
                .specularProbabilityThresholdsForMvModification = {cvar_reblur_specular_threshold_mv_x.get(), cvar_reblur_specular_threshold_mv_y.get()},
                .fireflySuppressorMinRelativeScale = cvar_reblur_firefly_suppressor_min_scale.get(),
                .checkerboardMode = cvar_reblur_checkerboard_mode.get(),
                .hitDistanceReconstructionMode = cvar_reblur_hit_dist_reconstruction.get(),
                .enableAntiFirefly = cvar_reblur_enable_anti_firefly.get() != 0,
                .enablePerformanceMode = cvar_reblur_enable_performance_mode.get() != 0,
                .minMaterialForDiffuse = cvar_reblur_min_diff_material.get(),
                .minMaterialForSpecular = cvar_reblur_min_spec_material.get(),
                .usePrepassOnlyForSpecularMotionEstimation = cvar_reblur_prepass_for_spec_motion_estimation.get() != 0
            };

            instance->SetDenoiserSettings(static_cast<nrd::Identifier>(nrd::Denoiser::REBLUR_DIFFUSE),
                                          &reblur_settings);

        } else if(cached_denoiser_type == DenoiserType::ReLAX) {
            auto relax_settings = nrd::RelaxSettings{};

            instance->SetDenoiserSettings(static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE), &relax_settings);
        }
    }

    void NvidiaRealtimeDenoiser::do_denoising(
        RenderGraph& graph, const BufferHandle view_buffer, const TextureHandle gbuffer_depth,
        const TextureHandle motion_vectors, const TextureHandle noisy_diffuse,
        const TextureHandle packed_normals_roughness, const TextureHandle linear_depth_texture,
        const TextureHandle denoised_diffuse
        ) {
        auto& allocator = RenderBackend::get().get_global_allocator();
        if(cvar_nrd_validation.get() != 0 && validation_texture == nullptr) {
            validation_texture = allocator.create_texture("nrd_validation",
                                                          {
                                                              .format = VK_FORMAT_R8G8B8A8_UNORM,
                                                              .resolution = denoised_diffuse->get_resolution(),
                                                              .usage = TextureUsage::StorageImage,
                                                          });
        } else if(cvar_nrd_validation.get() == 0) {
            allocator.destroy_texture(validation_texture);
            validation_texture = nullptr;
        }

        auto texture_usages = TextureUsageList{
            {
                .texture = motion_vectors,
                .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .access = VK_ACCESS_2_SHADER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            {
                .texture = packed_normals_roughness,
                .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .access = VK_ACCESS_2_SHADER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            {
                .texture = linear_depth_texture,
                .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .access = VK_ACCESS_2_SHADER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            {
                .texture = noisy_diffuse,
                .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .access = VK_ACCESS_2_SHADER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            {
                .texture = denoised_diffuse,
                .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                .layout = VK_IMAGE_LAYOUT_GENERAL,
            },
        };
        if(cvar_nrd_validation.get() != 0) {
            texture_usages.emplace_back(validation_texture,
                                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                                        VK_IMAGE_LAYOUT_GENERAL);
        }
        graph.add_pass({
            .name = "evaluate_nrd",
            .textures = texture_usages,
            .execute = [&](const CommandBuffer& commands) {
                // Fill resource snapshot
                nrd::ResourceSnapshot resource_snapshot = {};
                // Common
                resource_snapshot.SetResource(nrd::ResourceType::IN_MV, get_nrd_resource(motion_vectors));
                resource_snapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS,
                                              get_nrd_resource(packed_normals_roughness));
                resource_snapshot.SetResource(nrd::ResourceType::IN_VIEWZ, get_nrd_resource(linear_depth_texture));

                // Denoiser specific
                resource_snapshot.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST,
                                              get_nrd_resource(noisy_diffuse));
                resource_snapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST,
                                              get_nrd_resource(denoised_diffuse, true));
                if(cvar_nrd_validation.get() != 0) {
                    resource_snapshot.SetResource(nrd::ResourceType::OUT_VALIDATION,
                                                  get_nrd_resource(validation_texture, true));
                }

                // Denoise
                const auto command_buffer_desc = nri::CommandBufferVKDesc{
                    .vkCommandBuffer = commands.get_vk_commands(),
                    .queueType = nri::QueueType::GRAPHICS,
                };

                const auto identifier = static_cast<nrd::Identifier>(denoiser);

                instance->DenoiseVK(&identifier, 1, command_buffer_desc, resource_snapshot);
            }
        });
    }

    TextureHandle NvidiaRealtimeDenoiser::get_validation_texture() const {
        return validation_texture;
    }

    void NvidiaRealtimeDenoiser::draw_validation_overlay(RenderGraph& graph, const TextureHandle lit_scene_texture
        ) const {
        if(validation_texture == nullptr) {
            return;
        }

        if(validation_overlay_shader == nullptr) {
            validation_overlay_shader = RenderBackend::get()
                                        .get_pipeline_cache()
                                        .create_pipeline("shader://gi/rtgi/nrd_validation_overlay.comp.spv"_res);
        }

        const auto set = validation_overlay_shader->begin_building_set(0)
                                                  .bind(validation_texture)
                                                  .bind(lit_scene_texture)
                                                  .build();

        const auto resolution = lit_scene_texture->get_resolution();
        graph.add_compute_dispatch(ComputeDispatch<uint2>{
            .name = "nrd_validation_overlay",
            .descriptor_sets = {set},
            .push_constants = resolution,
            .num_workgroups = uint3{resolution + uint2{7} / uint2{8}, 1},
            .compute_shader = validation_overlay_shader
        });
    }

    void NvidiaRealtimeDenoiser::recreate_instance() {
        const auto& backend = RenderBackend::get();
        const auto vk_queue_desc = nri::QueueFamilyVKDesc{
            .queueNum = 0,
            .queueType = nri::QueueType::GRAPHICS,
            .familyIndex = backend.get_graphics_queue_family_index()
        };

        const auto device_desc = nri::DeviceCreationVKDesc{
            .vkInstance = backend.get_instance(),
            .vkDevice = backend.get_device(),
            .vkPhysicalDevice = backend.get_physical_device(),
            .queueFamilies = &vk_queue_desc,
            .queueFamilyNum = 1,
            .minorVersion = 3,
            .enableNRIValidation = true
        };

        if(cached_denoiser_type == DenoiserType::ReBLUR) {
            denoiser = nrd::Denoiser::REBLUR_DIFFUSE;
        } else if(cached_denoiser_type == DenoiserType::ReLAX) {
            denoiser = nrd::Denoiser::RELAX_DIFFUSE;
        }

        const auto denoiser_desc = nrd::DenoiserDesc{
            .identifier = static_cast<nrd::Identifier>(denoiser),
            .denoiser = denoiser
        };
        const auto instance_create_desc = nrd::InstanceCreationDesc{
            .denoisers = &denoiser_desc,
            .denoisersNum = 1
        };

        const auto integration_desc = nrd::IntegrationCreationDesc{
            .name = "NRD",
            .resourceWidth = static_cast<uint16_t>(cached_resolution.x),
            .resourceHeight = static_cast<uint16_t>(cached_resolution.y),
            .queuedFrameNum = 2,
            .enableWholeLifetimeDescriptorCaching = false,
            .autoWaitForIdle = false,
        };

        const auto result = instance->RecreateVK(integration_desc, instance_create_desc, device_desc);
        if(result != nrd::Result::SUCCESS) {
            throw std::runtime_error{"Could not create NRD!"};
        }
    }

    nrd::Resource get_nrd_resource(const TextureHandle texture, const bool is_output) {
        return nrd::Resource{
            .vk = {
                .image = reinterpret_cast<VKNonDispatchableHandle>(texture->image),
                .format = texture->create_info.format
            },
            .state = nri::AccessLayoutStage{
                .access = nri::AccessBits::SHADER_RESOURCE,
                .layout = is_output ? nri::Layout::SHADER_RESOURCE_STORAGE : nri::Layout::SHADER_RESOURCE,
                .stages = nri::StageBits::COMPUTE_SHADER},
            .userArg = texture,
        };
    }
} // render

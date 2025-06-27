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
                                                   "Whether to enable NRD validation", 0};

    static std::shared_ptr<spdlog::logger> logger;

    static nrd::Resource get_nrd_resource(TextureHandle texture, bool is_output = false);

    NvidiaRealtimeDenoiser::NvidiaRealtimeDenoiser(const uint2 resolution) :
        instance{eastl::make_unique<nrd::Integration>()} {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("NvidiaRealtimeDenoiser");
        }

        recreate_instance(resolution);
    }

    NvidiaRealtimeDenoiser::~NvidiaRealtimeDenoiser() {
        instance->Destroy();
    }

    void NvidiaRealtimeDenoiser::set_constants(const SceneView& scene_view, const uint2 render_resolution) {
        if(cached_resolution != render_resolution) {
            recreate_instance(render_resolution);
        }

        instance->NewFrame();

        const auto& view_data = scene_view.get_gpu_data();
        cached_resolution = render_resolution;

        const auto smol_render_resolution = glm::u16vec2{render_resolution};

        auto common_settings = nrd::CommonSettings{
            .cameraJitter = {view_data.jitter.x, view_data.jitter.y},
            .cameraJitterPrev = {view_data.previous_jitter.x, view_data.previous_jitter.y},
            .resourceSize = {smol_render_resolution.x, smol_render_resolution.y},
            .resourceSizePrev = {smol_render_resolution.x, smol_render_resolution.y},
            .rectSize = {smol_render_resolution.x, smol_render_resolution.y},
            .rectSizePrev = {smol_render_resolution.x, smol_render_resolution.y},
            .splitScreen = static_cast<float>(cvar_nrd_split_screen.Get()),
            .frameIndex = scene_view.get_frame_count(),
            .isBaseColorMetalnessAvailable = true,
            .enableValidation = cvar_nrd_validation.Get() != 0
        };

        const auto view_to_clip = scene_view.get_projection();
        memcpy(common_settings.viewToClipMatrix, &view_to_clip, sizeof(float4x4));

        const auto view_to_clip_prev = scene_view.get_last_frame_projection();
        memcpy(common_settings.viewToClipMatrixPrev, &view_to_clip_prev, sizeof(float4x4));

        const auto world_to_view = scene_view.get_view();
        memcpy(common_settings.worldToViewMatrix, &world_to_view, sizeof(float4x4));

        const auto world_to_view_prev = scene_view.get_last_frame_view();
        memcpy(common_settings.worldToViewMatrixPrev, &world_to_view_prev, sizeof(float4x4));

        instance->SetCommonSettings(common_settings);

        // TODO: Play with the settings?
        auto relax_settings = nrd::RelaxSettings{};

        instance->SetDenoiserSettings(static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE), &relax_settings);
    }

    void NvidiaRealtimeDenoiser::do_denoising(
        RenderGraph& graph, const TextureHandle gbuffer_depth, const TextureHandle motion_vectors,
        const TextureHandle noisy_diffuse, const TextureHandle packed_normals_roughness,
        const TextureHandle denoised_diffuse
        ) {
        graph.add_pass({
            .name = "evaluate_nrd",
            .textures = {
                {
                    .texture = motion_vectors,
                    .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                },
                {
                    .texture = packed_normals_roughness,
                    .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                },
                {
                    .texture = gbuffer_depth,
                    .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                },
                {
                    .texture = noisy_diffuse,
                    .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                },
                {
                    .texture = denoised_diffuse,
                    .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                    .layout = VK_IMAGE_LAYOUT_GENERAL,
                },
            },
            .execute = [&](const CommandBuffer& commands) {
                // Fill resource snapshot
                nrd::ResourceSnapshot resource_snapshot = {};
                // Common
                resource_snapshot.SetResource(nrd::ResourceType::IN_MV, get_nrd_resource(motion_vectors));
                resource_snapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS,
                                              get_nrd_resource(packed_normals_roughness));
                resource_snapshot.SetResource(nrd::ResourceType::IN_VIEWZ, get_nrd_resource(gbuffer_depth));

                // Denoiser specific
                resource_snapshot.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST,
                                              get_nrd_resource(noisy_diffuse));
                resource_snapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST,
                                              get_nrd_resource(denoised_diffuse, true));

                // Denoise
                const auto command_buffer_desc = nri::CommandBufferVKDesc{
                    .vkCommandBuffer = commands.get_vk_commands(),
                    .queueType = nri::QueueType::GRAPHICS,
                };

                instance->DenoiseVK(&denoiser_descs[0].identifier, 1, command_buffer_desc, resource_snapshot);
            }
        });
    }

    void NvidiaRealtimeDenoiser::recreate_instance(const uint2 resolution) {
        const auto& backend = RenderBackend::get();
        const auto vk_queue_desc = nri::QueueFamilyVKDesc{
            .queueNum = 0,
            .queueType = nri::QueueType::GRAPHICS,
            .familyIndex = backend.get_graphics_queue_family_index()
        };

        const auto device_extensions = eastl::array{
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
        };

        const auto device_desc = nri::DeviceCreationVKDesc{
            .vkExtensions = {
                .deviceExtensions = device_extensions.data(),
                .deviceExtensionNum = static_cast<uint32_t>(device_extensions.size())
            },
            .vkInstance = backend.get_instance(),
            .vkDevice = backend.get_device(),
            .vkPhysicalDevice = backend.get_physical_device(),
            .queueFamilies = &vk_queue_desc,
            .queueFamilyNum = 1,
            .minorVersion = 3,
            .enableNRIValidation = true
        };

        const auto instance_create_desc = nrd::InstanceCreationDesc{
            .denoisers = denoiser_descs.data(),
            .denoisersNum = static_cast<uint32_t>(denoiser_descs.size())
        };

        const auto integration_desc = nrd::IntegrationCreationDesc{
            .name = "NRD",
            .resourceWidth = static_cast<uint16_t>(resolution.x),
            .resourceHeight = static_cast<uint16_t>(resolution.y),
            .queuedFrameNum = 2,
            .enableWholeLifetimeDescriptorCaching = false,
            .autoWaitForIdle = false,
        };

        const auto result = instance->RecreateVK(integration_desc, instance_create_desc, device_desc);
        if(result != nrd::Result::SUCCESS) {
            throw std::runtime_error{"Could not create NRD!"};
        }

        cached_resolution = resolution;
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

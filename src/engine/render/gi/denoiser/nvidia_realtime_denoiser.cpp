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
            .cameraJitter = {view_data.jitter.x, view_data.jitter.y},
            .cameraJitterPrev = {view_data.previous_jitter.x, view_data.previous_jitter.y},
            .resourceSize = {smol_render_resolution.x, smol_render_resolution.y},
            .resourceSizePrev = {smol_render_resolution.x, smol_render_resolution.y},
            .rectSize = {smol_render_resolution.x, smol_render_resolution.y},
            .rectSizePrev = {smol_render_resolution.x, smol_render_resolution.y},
            .splitScreen = static_cast<float>(cvar_nrd_split_screen.get()),
            .frameIndex = scene_view.get_frame_count(),
            .isBaseColorMetalnessAvailable = false,
            .enableValidation = cvar_nrd_validation.get() != 0
        };

        const auto view_to_clip = transpose(scene_view.get_projection());
        memcpy(common_settings.viewToClipMatrix, &view_to_clip, sizeof(float4x4));

        const auto view_to_clip_prev = transpose(scene_view.get_last_frame_projection());
        memcpy(common_settings.viewToClipMatrixPrev, &view_to_clip_prev, sizeof(float4x4));

        const auto world_to_view = transpose(scene_view.get_view());
        memcpy(common_settings.worldToViewMatrix, &world_to_view, sizeof(float4x4));

        const auto world_to_view_prev = transpose(scene_view.get_last_frame_view());
        memcpy(common_settings.worldToViewMatrixPrev, &world_to_view_prev, sizeof(float4x4));

        instance->SetCommonSettings(common_settings);

        if(cached_denoiser_type == DenoiserType::ReBLUR) {
            auto reblur_settings = nrd::ReblurSettings{};

            instance->SetDenoiserSettings(static_cast<nrd::Identifier>(nrd::Denoiser::REBLUR_DIFFUSE),
                                          &reblur_settings);

        } else if(cached_denoiser_type == DenoiserType::ReLAX) {
            // TODO: Play with the settings?
            auto relax_settings = nrd::RelaxSettings{};

            instance->SetDenoiserSettings(static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE), &relax_settings);
        }
    }

    void NvidiaRealtimeDenoiser::do_denoising(
        RenderGraph& graph, const TextureHandle gbuffer_depth, const TextureHandle motion_vectors,
        const TextureHandle noisy_diffuse, const TextureHandle packed_normals_roughness,
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
                .texture = gbuffer_depth,
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
                resource_snapshot.SetResource(nrd::ResourceType::IN_VIEWZ, get_nrd_resource(gbuffer_depth));

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

    void NvidiaRealtimeDenoiser::recreate_instance() {
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

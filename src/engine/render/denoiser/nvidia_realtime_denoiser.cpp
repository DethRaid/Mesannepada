#include "nvidia_realtime_denoiser.hpp"

#include <EASTL/array.h>
#include <EASTL/span.h>
#include <EASTL/string.h>

#include "console/cvars.hpp"
#include "core/system_interface.hpp"
#include "render/backend/render_graph.hpp"

namespace render {
    static auto cvar_nrd_split_screen = AutoCVar_Int{"r.NRD.SplitScreen",
                                                     "Whether to show the split noisy/denoised view", 0};
    static auto cvar_nrd_validation = AutoCVar_Int{"r.NRD.Validation",
                                                   "Whether to enable NRD validation", 1};

    static std::shared_ptr<spdlog::logger> logger;

    NvidiaRealtimeDenoiser::NvidiaRealtimeDenoiser() {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("NvidiaRealtimeDenoiser");
        }
        constexpr auto denoiser_descs = eastl::array{
            // ReLAX
            nrd::DenoiserDesc{
                .identifier = static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE),
                .denoiser = nrd::Denoiser::RELAX_DIFFUSE
            },
            // TODO: Reflections denoiser - combine with RELAX_DIFFUSE?
        };

        const auto instance_create_descs = nrd::InstanceCreationDesc{
            .denoisers = denoiser_descs.data(),
            .denoisersNum = static_cast<uint32_t>(denoiser_descs.size())
        };

        const auto result = nrd::CreateInstance(instance_create_descs, instance);
        if(result != nrd::Result::SUCCESS) {
            logger->error("Could not create NRD: {}", static_cast<uint32_t>(result));
            instance = nullptr;
        }

        create_nrd_resources();
    }

    NvidiaRealtimeDenoiser::~NvidiaRealtimeDenoiser() {
        if(instance) {
            nrd::DestroyInstance(*instance);
        }
    }

    void NvidiaRealtimeDenoiser::set_constants(const SceneView& scene_view, const uint2 render_resolution) {
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

        nrd::SetCommonSettings(*instance, common_settings);

        // TODO: Play with the settings?
        auto relax_settings = nrd::RelaxSettings{};

        nrd::SetDenoiserSettings(*instance,
                                 static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE),
                                 &relax_settings);
    }

    void NvidiaRealtimeDenoiser::do_denoising(
        RenderGraph& graph, const GBuffer& gbuffer, TextureHandle noisy_diffuse, TextureHandle denoised_diffuse
        ) {
        /*
         * NRD gives us a list of dispatches. We need to... dispatch them
         *
         * It also gives us some shader blobs, we can like hash them and recompile compute shaders if needed? But I suspect they're not needed
         */

        constexpr auto nrd_ids = eastl::array{static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE)};
        const nrd::DispatchDesc* dispatches_ptr;
        uint32_t num_dispatches;
        nrd::GetComputeDispatches(*instance,
                                  nrd_ids.data(),
                                  static_cast<uint32_t>(nrd_ids.size()),
                                  dispatches_ptr,
                                  num_dispatches);

        const auto dispatches = eastl::span{dispatches_ptr, num_dispatches};
        auto cur_dispatch_idx = 0;
        for(const auto& dispatch : dispatches) {
            auto name = eastl::string{dispatch.name};
            if(name.empty()) {
                name = eastl::string{eastl::string::CtorSprintf{}, "NRD Dispatch %d", cur_dispatch_idx};
            }

            graph.add_pass({
                .name = name.c_str(),
                .textures = {},
                .buffers = {},
                .descriptor_sets = {},
                .execute = {}
            });

            cur_dispatch_idx++;
        }
    }

    void NvidiaRealtimeDenoiser::create_nrd_resources() {
        const auto instance_desc = nrd::GetInstanceDesc(*instance);

        // Create the resources that the instance gave us
        const auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        constant_buffer = allocator.create_buffer("NRD Constant Buffer",
                                                  instance_desc.constantBufferMaxDataSize,
                                                  BufferUsage::UniformBuffer);
        if(constant_buffer) {
            logger->debug("Created NRD constant buffer");
        }

        const auto nrd_samplers = eastl::span{instance_desc.samplers, instance_desc.samplersNum};
        samplers.reserve(nrd_samplers.size());
        for(const auto nrd_sampler : nrd_samplers) {
            auto filter_mode = VK_FILTER_NEAREST;
            constexpr auto wrap_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            if(nrd_sampler == nrd::Sampler::LINEAR_CLAMP) {
                filter_mode = VK_FILTER_LINEAR;
            }
            samplers.emplace_back(allocator.get_sampler({
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = filter_mode,
                .minFilter = filter_mode,
                .addressModeU = wrap_mode,
                .addressModeV = wrap_mode,
                .addressModeW = wrap_mode,
                .maxLod = VK_LOD_CLAMP_NONE,
            }));
        }
        if(samplers.size() == nrd_samplers.size()) {
            logger->debug("Created NRD samplers");
        }

        const auto nrd_pipelines = eastl::span{instance_desc.pipelines, instance_desc.pipelinesNum};
        pipelines.reserve(nrd_pipelines.size());
        for(const auto& nrd_pipeline : nrd_pipelines) {
            const auto pipeline = backend.get_pipeline_cache().create_pipeline(
                nrd_pipeline.shaderFileName,
                eastl::span{static_cast<const std::byte*>(nrd_pipeline.computeShaderSPIRV.bytecode),
                            nrd_pipeline.computeShaderSPIRV.size});

            if(!pipeline) {
                logger->error("Could not create NRD pipeline {}!", nrd_pipeline.shaderFileName);
            } else {
                pipelines.emplace_back(pipeline);
            }
        }
        if(pipelines.size() == nrd_pipelines.size()) {
            logger->debug("Created NRD pipelines");
        }
    }
} // render

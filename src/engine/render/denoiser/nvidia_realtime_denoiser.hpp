#pragma once

#include <NRD.h>
#include <EASTL/vector.h>
#include <vulkan/vulkan_core.h>

#include "render/scene_view.hpp"
#include "shared/prelude.h"

namespace render {
    struct GBuffer;
    class RenderGraph;
    /**
     * Wraps Nvidia's Realtime Denoisers
     *
     * We're using the shadow and diffuse denoisers for now, we can use the specular denoiser when we get RT reflections
     */
    class NvidiaRealtimeDenoiser {
    public:

        NvidiaRealtimeDenoiser();

        ~NvidiaRealtimeDenoiser();

        void set_constants(const SceneView& scene_view, uint2 render_resolution);

        void do_denoising(
            RenderGraph& graph, const GBuffer& gbuffer, TextureHandle noisy_diffuse, TextureHandle denoised_diffuse
            );

    private:
        uint2 cached_resolution = uint2{0};

        nrd::Instance* instance = nullptr;

        BufferHandle constant_buffer = nullptr;

        eastl::vector<VkSampler> samplers;

        eastl::vector<ComputePipelineHandle> pipelines;

        void create_nrd_resources();
    };
} // render

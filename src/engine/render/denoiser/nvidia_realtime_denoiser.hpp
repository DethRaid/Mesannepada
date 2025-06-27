#pragma once

#include <NRD.h>
#include <EASTL/array.h>
#include <EASTL/unique_ptr.h>

#include "render/scene_view.hpp"
#include "shared/prelude.h"

namespace nrd {
    struct Integration;
}

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
        explicit NvidiaRealtimeDenoiser(uint2 resolution);

        ~NvidiaRealtimeDenoiser();

        void set_constants(const SceneView& scene_view, uint2 render_resolution);

        void do_denoising(
            RenderGraph& graph, TextureHandle gbuffer_depth, TextureHandle motion_vectors, TextureHandle noisy_diffuse,
            TextureHandle packed_normals_roughness, TextureHandle denoised_diffuse
            );

    private:
        uint2 cached_resolution = uint2{0};

        static inline auto denoiser_descs = eastl::array{
            // ReLAX
            nrd::DenoiserDesc{
                .identifier = static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE),
                .denoiser = nrd::Denoiser::RELAX_DIFFUSE
            },
            // TODO: Reflections denoiser - combine with RELAX_DIFFUSE?
        };

        eastl::unique_ptr<nrd::Integration> instance;

        static inline ComputePipelineHandle pack_nrd_inputs_pipeline = nullptr;

        void recreate_instance(uint2 resolution);
    };
} // render

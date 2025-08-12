#pragma once

#include <NRD.h>
#include <EASTL/array.h>
#include <EASTL/unique_ptr.h>

#include "render/scene_view.hpp"
#include "render/gi/denoiser/denoiser_type.hpp"
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
        explicit NvidiaRealtimeDenoiser();

        ~NvidiaRealtimeDenoiser();

        void set_constants(const SceneView& scene_view, DenoiserType type, uint2 render_resolution);

        void do_denoising(
            RenderGraph& graph, BufferHandle view_buffer, TextureHandle gbuffer_depth, TextureHandle motion_vectors,
            TextureHandle noisy_diffuse, TextureHandle packed_normals_roughness, TextureHandle linear_depth_texture,
            TextureHandle denoised_diffuse
            );

        TextureHandle get_validation_texture() const;

        /**
         * Overlay the validation texture for debugging purposes
         */
        void draw_validation_overlay(RenderGraph& graph, TextureHandle lit_scene_texture) const;

    private:
        uint2 cached_resolution = uint2{0};
        DenoiserType cached_denoiser_type = DenoiserType::None;
        nrd::Denoiser denoiser{};

        TextureHandle validation_texture = nullptr;

        eastl::unique_ptr<nrd::Integration> instance;

        static inline ComputePipelineHandle validation_overlay_shader = nullptr;

        void recreate_instance();
    };
} // render

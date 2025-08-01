#pragma once

#include <EASTL/memory.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include "render/backend/buffer_usage_token.hpp"
#include "render/backend/handles.hpp"
#include "render/backend/texture_usage_token.hpp"
#include "render/gi/denoiser/nvidia_realtime_denoiser.hpp"
#include "render/gi/global_illuminator.hpp"
#include "render/gi/irradiance_cache.hpp"

namespace render {
    class ProceduralSky;
    class RenderWorld;
    class CommandBuffer;
    struct DescriptorSet;
    class SceneView;
    class RenderGraph;
    struct GBuffer;

    /**
     * Uses ray tracing to calculate global illumination
     */
    class RayTracedGlobalIllumination final : public IGlobalIlluminator {
    public:
        static bool should_render();

        RayTracedGlobalIllumination();

        ~RayTracedGlobalIllumination() override;

        void pre_render(
            RenderGraph& graph, const SceneView& view, const RenderWorld& world, TextureHandle noise_tex
        ) override;

        void post_render(
            RenderGraph& graph, const SceneView& view, const RenderWorld& world, const GBuffer& gbuffer,
             TextureHandle motion_vectors, TextureHandle noise_tex
        ) override;

        /**
         * Retrieves resource usages for applying this effect to the lit scene
         */
        void get_lighting_resource_usages(
            TextureUsageList& textures, BufferUsageList& buffers
        ) const override;

        /**
         * Applies the RTGI to the scene image with an additive blending pixel shader
         *
         * Optionally can sample rays around the current pixel, decreasing GI noise
         */
        void render_to_lit_scene(
            CommandBuffer& commands, BufferHandle view_buffer, BufferHandle sun_buffer, TextureHandle ao_tex,
            TextureHandle noise_texture
        ) const override;

        void render_volumetrics(
            RenderGraph& render_graph, const SceneView& player_view, const RenderWorld& world, const GBuffer& gbuffer,
            TextureHandle lit_scene_handle
        ) override;

        void draw_debug_overlays(
            RenderGraph& graph, const SceneView& view, const GBuffer& gbuffer, TextureHandle lit_scene_texture
        ) override;

    private:
        /**
         * Stores ray start parameters, such as direction and... well, it's just direction
         */
        TextureHandle ray_texture = nullptr;

        /**
         * Per-pixel irradiance, calculated by ray tracing
         *
         * RGB = irradiance, A = ray distance
         */
        TextureHandle ray_irradiance = nullptr;

        /**
         * Irradiance but denoised!
         */
        TextureHandle denoised_irradiance = nullptr;

#if SAH_USE_IRRADIANCE_CACHE
        eastl::unique_ptr<IrradianceCache> irradiance_cache = nullptr;
#endif

        eastl::unique_ptr<NvidiaRealtimeDenoiser> denoiser;
        TextureHandle denoiser_data = nullptr;

        static inline RayTracingPipelineHandle rtgi_pipeline = nullptr;

        static inline GraphicsPipelineHandle overlay_pso = nullptr;

        static inline GraphicsPipelineHandle filter_pso = nullptr;
    };
}

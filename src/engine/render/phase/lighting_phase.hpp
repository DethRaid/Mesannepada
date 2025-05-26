#pragma once

#include <EASTL/optional.h>

#include "render/gbuffer.hpp"
#include "render/backend/graphics_pipeline.hpp"
#include "render/backend/handles.hpp"

namespace render {
    class IGlobalIlluminator;
    class RayTracedGlobalIllumination;
    struct NoiseTexture;
    class ProceduralSky;
    struct DescriptorSet;
    class RenderGraph;
    class CommandBuffer;
    class RenderScene;
    class RenderBackend;
    class SceneView;
    class LightPropagationVolume;

    /**
     * Computes the lighting form the gbuffers
     *
     * This pass adds in lighting from a variety of sources: the sun, the sky, indirect lighting, area
     * lights, etc
     */
    class LightingPhase {
    public:
        explicit LightingPhase();

        void render(
            RenderGraph& render_graph,
            const RenderScene& scene,
            const GBuffer& gbuffer,
            TextureHandle lit_scene_texture,
            TextureHandle ao_texture,
            const IGlobalIlluminator* gi,
            eastl::optional<TextureHandle> vrsaa_shading_rate_image,
            const NoiseTexture& noise,
            TextureHandle noise_2d
        );

    private:
        GraphicsPipelineHandle point_lights_pipeline = nullptr;

        GraphicsPipelineHandle emission_pipeline;

        TextureHandle sky_occlusion_map = nullptr;

        void rasterize_sky_shadow(RenderGraph& render_graph, const SceneView& view);

        void apply_point_lights(CommandBuffer& commands, const DescriptorSet& set, uint32_t num_point_lights) const;

        void add_emissive_lighting(CommandBuffer& commands) const;
    };
}

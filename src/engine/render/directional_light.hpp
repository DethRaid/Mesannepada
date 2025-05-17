#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "backend/buffer_usage_token.hpp"
#include "render/backend/handles.hpp"
#include "backend/graphics_pipeline.hpp"
#include "backend/texture_usage_token.hpp"
#include "shared/sun_light_constants.hpp"

namespace render {
    struct NoiseTexture;
    struct GBuffer;
    class ResourceUploadQueue;
    class RenderScene;
    class RenderGraph;
    struct DescriptorSet;
    class ResourceAllocator;

    class CommandBuffer;
    class SceneView;
    class RenderBackend;

    enum class SunShadowMode {
        Off,
        CascadedShadowMaps,
        RayTracing
    };

    /**
     * Represents a directional light
     */
    class DirectionalLight {
    public:
        static SunShadowMode get_shadow_mode();

        explicit DirectionalLight();

        void update_shadow_cascades(const SceneView& view);

        void set_direction(const glm::vec3& direction);

        void set_color(glm::vec4 color);

        void update_buffer(ResourceUploadQueue& queue);

        BufferHandle get_constant_buffer() const;

        GraphicsPipelineHandle get_pipeline() const;

        glm::vec3 get_direction() const;

        void render_shadows(RenderGraph& graph, const GBuffer& gbuffer, const RenderScene& scene, TextureHandle noise);

        /**
         * Gets the resources that are used in the lighting pass
         */
        void get_resource_usages(BufferUsageList& buffers, TextureUsageList& textures) const;

        /**
         * Renders this light's contribution to the scene, using a fullscreen triangle and additive blending
         */
        void render(CommandBuffer& commands, const SceneView& view) const;

        TextureHandle get_shadowmap_handle() const;

    private:
        bool sun_buffer_dirty = true;

        SunLightConstants constants = {};

        BufferHandle sun_buffer = {};

        /**
         * Angular size of the sun, in degrees
         */
        float angular_size = 0.545;

        eastl::vector<glm::mat4> world_to_ndc_matrices;
        BufferHandle world_to_ndc_matrices_buffer = nullptr;

        GraphicsPipelineHandle pipeline = {};

        TextureHandle shadowmap_handle = nullptr;
        VkSampler shadowmap_sampler;

        GraphicsPipelineHandle shadow_mask_shadowmap_pso = nullptr;

        TextureHandle shadow_mask_texture = nullptr;

        RayTracingPipelineHandle rt_shadow_pipeline = nullptr;

        /**
         * Rasterizes the cascaded shadow maps for this light, and then samples them for the shadow mask texture
         */
        void render_shadowmaps(
            RenderGraph& graph, const GBuffer& gbuffer, const RenderScene& scene
        );

        /**
         * Ray traces shadows for this light, writing the results to the shadow mask texture
         */
        void ray_trace_shadows(
            RenderGraph& graph, const GBuffer& gbuffer, const RenderScene& scene, TextureHandle noise
        );
    };
}

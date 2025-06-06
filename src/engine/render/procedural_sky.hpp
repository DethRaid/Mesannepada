#pragma once

#include "glm/vec3.hpp"
#include "render/backend/compute_shader.hpp"
#include "render/backend/graphics_pipeline.hpp"
#include "render/backend/handles.hpp"

namespace render {
    struct DescriptorSet;
    class CommandBuffer;

    class RenderGraph;
    class SarahRenderer;

    /**
     * \brief Renders the sky!
     *
     * There's two important methods - `generate_luts` and `add_passes`. `generate_luts` will prepare any LUTs needed for
     * the sky. `add_passes` adds a pass to render the sky. The sky is added to the lit world render target, and is
     * based on the depth buffer, so it must be called at the end of the lighting stage
     */
    class ProceduralSky {
    public:
        explicit ProceduralSky();

        void update_sky_luts(RenderGraph& graph, const glm::vec3& light_vector) const;

        void render_sky(
            CommandBuffer& commands, BufferHandle view_buffer, BufferHandle sun_buffer, TextureHandle gbuffer_depth
        ) const;

        TextureHandle get_sky_view_lut() const;

        TextureHandle get_transmittance_lut() const;

        VkSampler get_sampler() const;

    private:
        ComputePipelineHandle transmittance_lut_pso;
        ComputePipelineHandle multiscattering_lut_pso;
        ComputePipelineHandle sky_view_lut_pso;

        GraphicsPipelineHandle sky_application_pso;

        TextureHandle transmittance_lut;
        TextureHandle multiscattering_lut;
        TextureHandle sky_view_lut;

        VkSampler linear_sampler;
    };
}

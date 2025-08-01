#pragma once

#include <glm/vec2.hpp>

#include "render/backend/graphics_pipeline.hpp"
#include "render/backend/handles.hpp"

namespace render {
    struct IndirectDrawingBuffers;
    class RenderGraph;
    class RenderWorld;

    class MotionVectorsPhase {
    public:
        static bool render_full_res();

        MotionVectorsPhase();

        void set_render_resolution(const glm::uvec2& resolution, const glm::uvec2& output_resolution);

        void render(
            RenderGraph& graph, const RenderWorld& world, BufferHandle view_data_buffer, TextureHandle depth_buffer,
            const IndirectDrawingBuffers& buffers, const IndirectDrawingBuffers& masked_buffers
        );

        TextureHandle get_motion_vectors() const;

    private:
        GraphicsPipelineHandle motion_vectors_opaque_pso = {};
        GraphicsPipelineHandle motion_vectors_masked_pso = {};

        TextureHandle motion_vectors = nullptr;
        ComputePipelineHandle motion_vectors_pso;
    };
}

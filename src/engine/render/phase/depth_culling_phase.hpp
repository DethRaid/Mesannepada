#pragma once

#include <EASTL/optional.h>
#include <glm/vec2.hpp>

#include "render/mip_chain_generator.hpp"
#include "render/backend/compute_shader.hpp"
#include "render/backend/descriptor_set_builder.hpp"
#include "render/backend/graphics_pipeline.hpp"
#include "render/backend/handles.hpp"

namespace render {
    class SceneView;
    class RenderWorld;
    class MaterialStorage;
    class TextureDescriptorPool;
    class RenderGraph;
    class ResourceAllocator;
    /**
     * \brief Render phase that culls visible objects and produces a depth buffer in the process
     *
     * This implements a two-pass culling algorithm. First, we draw the objects that were visible last frame. Second, we
     * build a HiZ depth pyramid from the depth buffer. Third, we cull all scene objects against that pyramid. Fourth, we
     * draw objects that were visible this frame but not visible last frame
     *
     * This class is stateful. It owns its depth buffer and the list of visible objects
     */
    class DepthCullingPhase {
    public:
        explicit DepthCullingPhase();

        ~DepthCullingPhase();

        void set_render_resolution(const glm::uvec2& resolution);

        void render(
            RenderGraph& graph, const RenderWorld& world, MaterialStorage& materials, SceneView& view
        );

        TextureHandle get_depth_buffer() const;

    private:
        TextureHandle depth_buffer = nullptr;

        TextureHandle hi_z_buffer = nullptr;
        VkSampler max_reduction_sampler;

        // Index of the hi-z descriptor in the texture descriptor array
        uint32_t hi_z_index = UINT32_MAX;

        MipChainGenerator downsampler;

        ComputePipelineHandle hi_z_culling_shader;

        VkIndirectCommandsLayoutNV command_signature = VK_NULL_HANDLE;

        void create_command_signature();

        eastl::optional<BufferHandle> create_preprocess_buffer(GraphicsPipelineHandle pipeline, uint32_t num_primitives);

        /**
         * Draws visible objects, using a different draw command for each material type. Uses the visible_objects buffer
         *
         * @see visible_objects
         */
        void draw_visible_objects(
            RenderGraph& graph, const RenderWorld& world, const DescriptorSet& view_descriptor,
            const DescriptorSet& masked_view_descriptor, BufferHandle primitive_buffer,
            BufferHandle visible_objects, uint32_t num_primitives
        ) const;
    };
}

#pragma once

#include <glm/vec2.hpp>

#include "render/backend/compute_shader.hpp"
#include "render/backend/handles.hpp"

namespace render {
    class RenderGraph;
    class RenderBackend;

    class Bloomer {
    public:
        explicit Bloomer();

        void fill_bloom_tex(RenderGraph& graph, TextureHandle scene_color);

        TextureHandle get_bloom_tex() const;

    private:
        TextureHandle bloom_tex = nullptr;

        ComputePipelineHandle downsample_shader;
        ComputePipelineHandle upsample_shader;

        VkSampler bilinear_sampler;

        glm::uvec2 bloom_tex_resolution = {};

        void create_bloom_tex(TextureHandle scene_color);
    };
}

#pragma once

#include <plf_colony.h>
#include <EASTL/span.h>
#include <EASTL/optional.h>

#include "render/backend/ray_tracing_pipeline.hpp"
#include "render/backend/hit_group_builder.hpp"
#include "render/backend/compute_shader.hpp"
#include "render/backend/graphics_pipeline.hpp"
#include "render/backend/pipeline_builder.hpp"

namespace render {
    class RenderBackend;

    class PipelineCache {
    public:
        explicit PipelineCache(RenderBackend& backend_in);

        ~PipelineCache();

        GraphicsPipelineHandle create_pipeline(const GraphicsPipelineBuilder& pipeline_builder);

        ComputePipelineHandle create_pipeline(const std::filesystem::path& shader_file_path);

        ComputePipelineHandle create_pipeline(std::string_view pipeline_name, eastl::span<const std::byte> instructions);

        GraphicsPipelineHandle create_pipeline_group(eastl::span<GraphicsPipelineHandle> pipelines_in);

        VkPipeline get_pipeline(
            GraphicsPipelineHandle pipeline,
            eastl::span<const VkFormat> color_attachment_formats,
            eastl::optional<VkFormat> depth_format = eastl::nullopt,
            uint32_t view_mask = 0xFF,
            bool use_fragment_shading_rate_attachment = false
        ) const;

        /**
         * Registers global miss shaders, to be used for all RT pipelines
         */
        void add_miss_shaders(eastl::span<const std::byte> occlusion_miss, eastl::span<const std::byte> gi_miss);

        /**
         * Adds a shader group to the cache. All shader groups will be added to every ray tracing pipeline. This should be
         * fine since we'll have very few shader groups, but it's worth keeping in mind
         */
        HitGroupHandle add_hit_group(const HitGroupBuilder& shader_group);

        RayTracingPipelineHandle create_ray_tracing_pipeline(const std::filesystem::path& raygen_shader_path, bool skip_gi_miss_shader = false);

        /**
         * Destroys all the pipelines in the cache
         *
         * This should be called explicitly on shutdown. The render backend global pointer gets destroyed on shutdown,
         * but the pipeline destructors try to access the backend. This tried to re-initialize the render backend,
         * which causes issues. Thus, we need to explicitly destroy pipelines before destructing the render backend
         */
        void destroy_all_pipelines();

    private:
        RenderBackend& backend;

        plf::colony<GraphicsPipeline> pipelines;

        plf::colony<ComputePipeline> compute_pipelines;

        plf::colony<HitGroup> shader_groups;

        eastl::vector<std::byte> occlusion_miss_shader;

        eastl::vector<std::byte> gi_miss_shader;

        plf::colony<RayTracingPipeline> ray_tracing_pipelines;
    };
}

#pragma once

#include "render/backend/descriptor_set_builder.hpp"
#include "render/backend/vk_descriptors.hpp"

namespace render {
    class DescriptorSetAllocator : public vkutil::DescriptorAllocator {
    public:
        explicit DescriptorSetAllocator(RenderBackend& backend_in);

        DescriptorSetBuilder build_set(GraphicsPipelineHandle pipeline, uint32_t set_index);

        DescriptorSetBuilder build_set(ComputePipelineHandle pipeline, uint32_t set_index);

        DescriptorSetBuilder build_set(const DescriptorSetInfo& info, std::string_view name);

        DescriptorSetBuilder build_set(RayTracingPipelineHandle pipeline, uint32_t set_index);

    private:
        RenderBackend* backend;
    };
}

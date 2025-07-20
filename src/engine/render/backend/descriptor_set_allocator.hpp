#pragma once

#include "render/backend/descriptor_set_builder.hpp"
#include "render/backend/vk_descriptors.hpp"

namespace render {
    struct PipelineBase;

    class DescriptorSetAllocator : public vkutil::DescriptorAllocator {
    public:
        explicit DescriptorSetAllocator(RenderBackend& backend_in);

        DescriptorSetBuilder build_set(const PipelineBase* pipeline, uint32_t set_index);

        DescriptorSetBuilder build_set(const DescriptorSetInfo& info, std::string_view name);;

    private:
        RenderBackend* backend;
    };
}

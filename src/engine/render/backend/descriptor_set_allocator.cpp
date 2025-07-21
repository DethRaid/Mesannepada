#include "descriptor_set_allocator.hpp"

#include "render/backend/render_backend.hpp"
#include "render/backend/pipeline_interface.hpp"

namespace render {
    DescriptorSetAllocator::DescriptorSetAllocator(RenderBackend& backend_in) :
        vkutil::DescriptorAllocator{backend_in.supports_ray_tracing()}, backend{&backend_in} {
    }

    DescriptorSetBuilder DescriptorSetAllocator::build_set(const PipelineBase* pipeline,
                                                           const uint32_t set_index) {
        const auto name = fmt::format("{} set {}", pipeline->name.c_str(), set_index);
        return build_set(pipeline->descriptor_sets.at(set_index), name);
    }

    DescriptorSetBuilder DescriptorSetAllocator::build_set(const DescriptorSetInfo& info, const std::string_view name) {
        return DescriptorSetBuilder{*backend, *this, info, name};
    }
} // namespace render

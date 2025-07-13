#pragma once
#include "handles.hpp"
#include "resource_allocator.hpp"
#include "core/string_utils.hpp"

namespace render {
    class RenderGraph;
    class ResourceAllocator;

    /**
     * A buffer that can be reset to an initial value. Resides entirely on the GPU
     */
    struct ResettableBuffer {
        template <typename DataType>
        static ResettableBuffer create(eastl::string_view name, ResourceAllocator& allocator, DataType initial_data);

        BufferHandle buffer;

        BufferHandle initial_value_buffer;

        uint32_t data_size;

        void reset(RenderGraph& graph) const;
    };

    template <typename DataType>
    ResettableBuffer ResettableBuffer::create(
        const eastl::string_view name, ResourceAllocator& allocator, DataType initial_data
    ) {
        const auto result = ResettableBuffer{
            .buffer = allocator.create_buffer(name, sizeof(DataType), BufferUsage::StorageBuffer),
            .initial_value_buffer = allocator.create_buffer(
                format("%s initial value", name),
                sizeof(DataType),
                BufferUsage::StagingBuffer),
            .data_size = sizeof(DataType)
        };

        auto* write_ptr = allocator.map_buffer(result.initial_value_buffer);
        std::memcpy(write_ptr, &initial_data, sizeof(DataType));

        return result;
    }
}

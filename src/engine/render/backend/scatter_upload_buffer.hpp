#pragma once

#include <cstdint>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "core/issue_breakpoint.hpp"
#include "render/backend/handles.hpp"
#include "render/backend/render_backend.hpp"
#include "render/backend/render_graph.hpp"
#include "core/system_interface.hpp"

namespace render {
    constexpr inline auto SCATTER_BUFFER_SIZE = 1024u;

    template <typename DataType>
    class ScatterUploadBuffer {
    public:
        void add_data(uint32_t destination_index, const DataType& data_in);

        void flush_to_buffer(RenderGraph& graph, BufferHandle destination_buffer);

        uint32_t get_size() const;

        bool is_full() const;

    private:
        uint32_t scatter_buffer_count = 0;

        BufferHandle scatter_indices = {};
        BufferHandle scatter_data = {};

        eastl::span<uint32_t> indices = {};
        eastl::span<DataType> data = {};
    };

    ComputePipelineHandle get_scatter_upload_shader();

    template <typename DataType>
    void ScatterUploadBuffer<DataType>::add_data(const uint32_t destination_index, const DataType& data_in) {
        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        if (!scatter_indices) {
            scatter_indices = allocator.create_buffer(
                "Primitive scatter indices",
                SCATTER_BUFFER_SIZE * sizeof(uint32_t),
                BufferUsage::StagingBuffer
            );
            indices = eastl::span{
                static_cast<uint32_t*>(scatter_indices->allocation_info.pMappedData), SCATTER_BUFFER_SIZE
            };
            eastl::fill(indices.begin(), indices.end(), eastl::numeric_limits<uint32_t>::max());
        }

        // Check if we already have this index in the buffer. if so, overwrite the data for that index. If not, increment
        // the count because we're adding to the buffer
        auto write_index = scatter_buffer_count;
        if (auto itr = eastl::find(indices.begin(), indices.end(), destination_index); itr != indices.end()) {
            write_index = itr - indices.begin();
        }
        else {
            scatter_buffer_count++;
        }
        indices[write_index] = destination_index;

        if (!scatter_data) {
            scatter_data = allocator.create_buffer(
                "Primitive scatter data",
                SCATTER_BUFFER_SIZE * sizeof(DataType),
                BufferUsage::StagingBuffer
            );
            data = eastl::span{ static_cast<DataType*>(scatter_data->allocation_info.pMappedData), SCATTER_BUFFER_SIZE };
        }

        data[write_index] = data_in;
    }

    template <typename DataType>
    uint32_t ScatterUploadBuffer<DataType>::get_size() const {
        return scatter_buffer_count;
    }

    template <typename DataType>
    bool ScatterUploadBuffer<DataType>::is_full() const {
        return scatter_buffer_count >= SCATTER_BUFFER_SIZE;
    }

    template <typename DataType>
    void ScatterUploadBuffer<DataType>::flush_to_buffer(RenderGraph& graph, BufferHandle destination_buffer) {
        if (!scatter_indices || !scatter_data) {
            return;
        }

        graph.add_pass(
            ComputePass{
                .name = "Flush scatter buffer",
                .buffers = {
                    {scatter_indices, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT},
                    {scatter_data, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT},
                    {destination_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT},
                },
                .execute = [destination_buffer, indices = scatter_indices, data = scatter_data, count = scatter_buffer_count
                ](CommandBuffer& commands) {
                    commands.flush_buffer(indices);
                    commands.flush_buffer(data);

                    commands.bind_buffer_reference(0, indices);
                    commands.bind_buffer_reference(2, data);
                    commands.bind_buffer_reference(4, destination_buffer);
                    commands.set_push_constant(6, count);
                    const auto data_size = static_cast<uint32_t>(sizeof(DataType) / sizeof(uint32_t));
                    commands.set_push_constant(7, data_size);

                    commands.bind_pipeline(get_scatter_upload_shader());

                    commands.dispatch((count + 63) / 64, 1, 1);

                    // Free the existing scatter upload buffers. We'll allocate new ones when/if we need them

                    auto& backend = RenderBackend::get();
                    auto& resources = backend.get_global_allocator();

                    resources.destroy_buffer(indices);
                    resources.destroy_buffer(data);
                }
            }
        );

        scatter_indices = {};
        scatter_data = {};
        scatter_buffer_count = 0;
    }
}

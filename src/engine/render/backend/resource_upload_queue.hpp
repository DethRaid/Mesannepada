#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <EASTL/vector.h>
#include <EASTL/span.h>

#include "render/backend/handles.hpp"
#include "render/backend/buffer.hpp"

struct ktxTexture;

namespace render {
    class RenderBackend;

    /**
     * Uploads some raw data to a texture
     */
    struct TextureUploadJob {
        TextureHandle destination;
        uint32_t mip;
        eastl::vector<uint8_t> data;
    };

    struct BufferUploadJob {
        BufferHandle buffer;
        eastl::vector<uint8_t> data;
        uint32_t dest_offset;
    };

    /**
     * Queues up resource uploads, then submits them
     *
     *
     */
    class ResourceUploadQueue {
    public:
        explicit ResourceUploadQueue(RenderBackend& backend_in);

        ResourceUploadQueue(const ResourceUploadQueue& other) = delete;
        ResourceUploadQueue& operator=(const ResourceUploadQueue& other) = delete;

        template <typename DataType>
        void upload_to_buffer(BufferHandle buffer, const DataType& data, uint32_t dest_offset = 0);

        template <typename DataType>
        void upload_to_buffer(BufferHandle buffer, eastl::span<const DataType> data, uint32_t dest_offset = 0);

        template <typename DataType>
        void upload_to_buffer(BufferHandle buffer, eastl::span<DataType> data, uint32_t dest_offset = 0);

        /**
         * Enqueues a job to upload data to one mip of a texture
         *
         * The job is batched until the backend calls flush_pending_uploads
         *
         * @param job A job representing the upload to perform
         */
        void enqueue(TextureUploadJob&& job);

        void enqueue(BufferUploadJob&& job);

        /**
         * Flushes all pending uploads. Records them to a command list and submits it to the backend. Also issues barriers
         * to transition the uploaded-to mips to be shader readable
         */
        void flush_pending_uploads();

    private:
        RenderBackend& backend;

        eastl::vector<TextureUploadJob> texture_uploads;

        eastl::vector<BufferUploadJob> buffer_uploads;
    };

    template <typename DataType>
    void ResourceUploadQueue::upload_to_buffer(BufferHandle buffer, const DataType& data, uint32_t dest_offset) {
        upload_to_buffer(buffer, eastl::span{ &data, 1 }, dest_offset);
    }

    template <typename DataType>
    void ResourceUploadQueue::upload_to_buffer(
        const BufferHandle buffer, eastl::span<const DataType> data, const uint32_t dest_offset
    ) {
        auto job = BufferUploadJob{
            .buffer = buffer,
            .data = eastl::vector<uint8_t>(data.size() * sizeof(DataType)),
            .dest_offset = dest_offset,
        };
        memcpy(job.data.data(), data.data(), data.size() * sizeof(DataType));

        enqueue(std::move(job));
    }

    template <typename DataType>
    void ResourceUploadQueue::upload_to_buffer(
        const BufferHandle buffer, eastl::span<DataType> data, const uint32_t dest_offset
    ) {
        auto job = BufferUploadJob{
            .buffer = buffer,
            .data = eastl::vector<uint8_t>(data.size() * sizeof(DataType)),
            .dest_offset = dest_offset,
        };
        memcpy(job.data.data(), data.data(), data.size() * sizeof(DataType));

        enqueue(std::move(job));
    }
}

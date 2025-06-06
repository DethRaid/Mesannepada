#pragma once

#include <string>
#include <EASTL/vector.h>

#include <volk.h>

namespace render {
    class RenderBackend;

    class CommandAllocator {
    public:
        CommandAllocator() = default;

        CommandAllocator(RenderBackend& backend_in, uint32_t queue_index);

        CommandAllocator(const CommandAllocator& other) = delete;
        CommandAllocator& operator=(const CommandAllocator& other) = delete;

        CommandAllocator(CommandAllocator&& old) noexcept;
        CommandAllocator& operator=(CommandAllocator&& old) noexcept;

        ~CommandAllocator();

        /**
         * Allocates a command buffer
         *
         * If there's free command buffers available, returns one of those. If not, allocates a new one
         */
        VkCommandBuffer allocate_command_buffer(const std::string& name);

        /**
         * Returns a command buffer to the pool
         */
        void return_command_buffer(VkCommandBuffer buffer);

        void reset();

    private:
        RenderBackend* backend;

        VkCommandPool command_pool = VK_NULL_HANDLE;

        eastl::vector<VkCommandBuffer> command_buffers;

        eastl::vector<VkCommandBuffer> available_command_buffers;
    };
}



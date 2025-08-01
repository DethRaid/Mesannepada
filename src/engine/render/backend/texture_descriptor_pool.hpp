#pragma once

#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

#include "render/backend/descriptor_set_builder.hpp"
#include "render/backend/handles.hpp"

namespace render {
    class ResourceAllocator;
    class RenderBackend;
    /**
     * \brief A pool for texture descriptors
     */
    class TextureDescriptorPool {
    public:
        explicit TextureDescriptorPool(RenderBackend& backend_in);

        ~TextureDescriptorPool();

        uint32_t create_texture_srv(TextureHandle texture, VkSampler sampler);

        void free_descriptor(uint32_t handle);

        /**
         * \brief Commits pending descriptor writes
         *
         * Should be called at start of frame
         */
        void commit_descriptors();

        const DescriptorSet& get_descriptor_set() const;

    private:
        RenderBackend& backend;

        VkDescriptorPool descriptor_pool = {};

        DescriptorSet descriptor_set;

        eastl::vector<uint32_t> available_handles;

        eastl::vector<eastl::unique_ptr<VkDescriptorImageInfo>> image_infos;
        eastl::vector<VkWriteDescriptorSet> pending_writes;
    };
}

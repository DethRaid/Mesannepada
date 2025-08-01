﻿#pragma once

#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>

#include <volk.h>

#include "render/backend/acceleration_structure.hpp"
#include "render/backend/handles.hpp"

namespace render {
    class RenderBackend;

    namespace vkutil {
        class DescriptorAllocator {
        public:
            struct PoolSizes {
                eastl::vector<std::pair<VkDescriptorType, float>> sizes;
            };

            DescriptorAllocator(bool supports_rt);

            void reset_pools();

            bool allocate(
                VkDescriptorSet* set, VkDescriptorSetLayout layout,
                const VkDescriptorSetVariableDescriptorCountAllocateInfo* variable_count_info = nullptr
            );

            void init(VkDevice newDevice);

            void cleanup();

            VkDevice device;

        private:
            VkDescriptorPool grab_pool();

            VkDescriptorPool currentPool{ VK_NULL_HANDLE };
            PoolSizes descriptorSizes;
            eastl::vector<VkDescriptorPool> usedPools;
            eastl::vector<VkDescriptorPool> freePools;
        };

        class DescriptorLayoutCache {
        public:
            void init(VkDevice newDevice);

            void cleanup();

            VkDescriptorSetLayout create_descriptor_layout(VkDescriptorSetLayoutCreateInfo* info);

            struct DescriptorLayoutInfo {
                //good idea to turn this into a inlined array
                eastl::vector<VkDescriptorSetLayoutBinding> bindings;

                bool operator==(const DescriptorLayoutInfo& other) const;

                size_t hash() const;
            };

        private:
            struct DescriptorLayoutHash {
                std::size_t operator()(const DescriptorLayoutInfo& k) const {
                    return k.hash();
                }
            };

            eastl::unordered_map<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> layoutCache;
            VkDevice device;
        };

        class DescriptorBuilder {
        public:
            struct BufferInfo {
                BufferHandle buffer = {};
                VkDeviceSize offset = {};
                VkDeviceSize range = {};
            };

            struct ImageInfo {
                VkSampler sampler = {};
                TextureHandle image = {};
                VkImageLayout image_layout = {};
                eastl::optional<uint32_t> mip_level = eastl::nullopt;
            };

            struct AccelerationStructureInfo {
                AccelerationStructureHandle as = {};
            };

            static DescriptorBuilder begin(RenderBackend& backend, vkutil::DescriptorAllocator& allocator);

            DescriptorBuilder& bind_buffer(
                uint32_t binding, const BufferInfo& info, VkDescriptorType type, VkShaderStageFlags stage_flags
            );

            DescriptorBuilder& bind_buffer_array(
                uint32_t binding, eastl::span<BufferInfo> infos, VkDescriptorType type,
                VkShaderStageFlags stage_flags
            );

            DescriptorBuilder& bind_image(
                uint32_t binding, const ImageInfo& info, VkDescriptorType type, VkShaderStageFlags stage_flags
            );

            DescriptorBuilder& bind_image_array(
                uint32_t binding, eastl::span<ImageInfo> infos,
                VkDescriptorType type, VkShaderStageFlags stage_flags
            );

            DescriptorBuilder& bind_acceleration_structure(
                uint32_t binding, const AccelerationStructureInfo& info, VkShaderStageFlags stage_flags
            );

            DescriptorBuilder& bind_buffer(
                uint32_t binding, const VkDescriptorBufferInfo* buffer_infos, VkDescriptorType type,
                VkShaderStageFlags stageFlags, uint32_t count = 1
            );

            DescriptorBuilder& bind_image(
                uint32_t binding, const VkDescriptorImageInfo* image_info, VkDescriptorType type, VkShaderStageFlags stageFlags,
                uint32_t count = 1
            );

            eastl::optional<VkDescriptorSet> build(VkDescriptorSetLayout& layout);

            eastl::optional<VkDescriptorSet> build();

        private:
            RenderBackend& backend;

            eastl::vector<VkWriteDescriptorSetAccelerationStructureKHR> as_writes;
            eastl::vector<VkWriteDescriptorSet> writes;
            eastl::vector<VkDescriptorSetLayoutBinding> bindings;

            DescriptorLayoutCache& cache;
            DescriptorAllocator& alloc;

            eastl::vector<eastl::vector<VkDescriptorBufferInfo>> buffer_infos_to_delete;
            eastl::vector<eastl::vector<VkDescriptorImageInfo>> image_infos_to_delete;
            eastl::vector<eastl::vector<VkDescriptorBufferInfo>> as_infos_to_delete;

            DescriptorBuilder(RenderBackend& backend_in, vkutil::DescriptorAllocator& allocator_in);
        };
    }
}

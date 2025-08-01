#include "resource_allocator.hpp"

#include "render/backend/utils.hpp"
#include "render/backend/render_backend.hpp"

#include <spdlog/fmt/bundled/format.h>
#include <tracy/Tracy.hpp>
#include <vulkan/vk_enum_string_helper.h>

#include "core/system_interface.hpp"
#include "render/backend/acceleration_structure.hpp"
#include "render/backend/gpu_texture.hpp"

namespace render {
    static std::shared_ptr<spdlog::logger> logger;

    ResourceAllocator::ResourceAllocator(RenderBackend& backend_in) :
        backend{backend_in} {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("ResourceAllocator");
            logger->set_level(spdlog::level::info);
        }
        auto create_info = VmaAllocatorCreateInfo{
            .flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .physicalDevice = backend.get_physical_device(),
            .device = backend.get_device(),
            .instance = backend.get_instance(),
            .vulkanApiVersion = VK_API_VERSION_1_3
        };
        auto functions = VmaVulkanFunctions{};
        auto result = vmaImportVulkanFunctionsFromVolk(&create_info, &functions);
        if(result != VK_SUCCESS) {
            throw std::runtime_error{"Could not import functions from Volk"};
        }

        create_info.pVulkanFunctions = &functions;
        result = vmaCreateAllocator(&create_info, &vma);
        if(result != VK_SUCCESS) {
            throw std::runtime_error{"Could not create VMA instance"};
        }
    }

    ResourceAllocator::~ResourceAllocator() {
        const auto& device = backend.get_device();
        for(const auto& [info, sampler] : sampler_cache) {
            vkDestroySampler(device, sampler, nullptr);
        }
    }

    TextureHandle ResourceAllocator::create_texture(const eastl::string_view name, const TextureCreateInfo& create_info) {
        const auto& device = backend.get_device();

        VkImageUsageFlags vk_usage = VK_IMAGE_USAGE_SAMPLED_BIT | create_info.usage_flags;
        VmaAllocationCreateFlags vma_flags = {};
        VkImageAspectFlags view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        switch(create_info.usage) {
        case TextureUsage::RenderTarget:
            if(is_depth_format(create_info.format)) {
                vk_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
                view_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            } else {
                vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
            }
            vma_flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            break;

        case TextureUsage::StaticImage:
            vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            break;

        case TextureUsage::StorageImage:
            vk_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
            break;

        case TextureUsage::ShadingRateImage:
            vk_usage = VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_STORAGE_BIT;
            break;
        }

        const auto image_create_info = VkImageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags = create_info.flags,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = create_info.format,
            .extent = VkExtent3D{.width = create_info.resolution.x, .height = create_info.resolution.y, .depth = 1},
            .mipLevels = create_info.num_mips,
            .arrayLayers = create_info.num_layers,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = vk_usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        const auto allocation_info = VmaAllocationCreateInfo{
            .flags = vma_flags,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };

        auto texture = GpuTexture{
            .type = TextureAllocationType::Vma
        };

        auto result = vmaCreateImage(
            vma,
            &image_create_info,
            &allocation_info,
            &texture.image,
            &texture.vma.allocation,
            &texture.vma.allocation_info
        );
        if(result != VK_SUCCESS) {
            throw std::runtime_error{fmt::format("Could not create image {}", name)};
        }

        texture.name = name;
        texture.create_info = image_create_info;

        const auto image_view_name = fmt::format("{} View", name);

        {
            const auto view_create_info = VkImageViewCreateInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = texture.image,
                .viewType = create_info.num_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
                .format = create_info.view_format == VK_FORMAT_UNDEFINED ? create_info.format : create_info.view_format,
                .subresourceRange = {
                    .aspectMask = view_aspect,
                    .baseMipLevel = 0,
                    .levelCount = create_info.num_mips,
                    .baseArrayLayer = 0,
                    .layerCount = create_info.num_layers,
                },
            };
            result = vkCreateImageView(device, &view_create_info, nullptr, &texture.image_view);
            if(result != VK_SUCCESS) {
                throw std::runtime_error{fmt::format("Could not create image view {}", image_view_name)};
            }
        }

        if(create_info.num_mips == 1) {
            texture.attachment_view = texture.image_view;
        } else {
            const auto rtv_create_info = VkImageViewCreateInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = texture.image,
                .viewType = create_info.num_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
                .format = create_info.view_format == VK_FORMAT_UNDEFINED ? create_info.format : create_info.view_format,
                .subresourceRange = {
                    .aspectMask = view_aspect,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = create_info.num_layers,
                },
            };
            result = vkCreateImageView(device, &rtv_create_info, nullptr, &texture.attachment_view);
            if(result != VK_SUCCESS) {
                throw std::runtime_error{fmt::format("Could not create image view")};
            }

            const auto rtv_name = fmt::format("{} RTV", name);
            backend.set_object_name(texture.attachment_view, rtv_name);
        }

        backend.set_object_name(texture.image, std::string{name.data(), name.size()});
        backend.set_object_name(texture.image_view, image_view_name);

        texture.mip_views.reserve(create_info.num_mips);
        for(auto i = 0u; i < create_info.num_mips; i++) {
            const auto view_create_info = VkImageViewCreateInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = texture.image,
                .viewType = create_info.num_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
                .format = create_info.view_format == VK_FORMAT_UNDEFINED ? create_info.format : create_info.view_format,
                .subresourceRange = {
                    .aspectMask = view_aspect,
                    .baseMipLevel = i,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = create_info.num_layers,
                },
            };
            auto view = VkImageView{};
            result = vkCreateImageView(device, &view_create_info, nullptr, &view);
            if(result != VK_SUCCESS) {
                throw std::runtime_error{fmt::format("Could not create image view")};
            }

            const auto view_name = fmt::format("{} mip {}", name, i);
            backend.set_object_name(view, view_name);

            texture.mip_views.emplace_back(view);
        }

        auto handle = &*textures.emplace(std::move(texture));
        return handle;
    }

    TextureHandle ResourceAllocator::create_cubemap(const eastl::string_view name, const CubemapCreateInfo& create_info) {
        const auto& device = backend.get_device();

        VkImageUsageFlags vk_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateFlags vma_flags = {};
        VkImageAspectFlags view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        switch(create_info.usage) {
        case TextureUsage::RenderTarget:
            if(is_depth_format(create_info.format)) {
                vk_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
                view_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            } else {
                vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
            }
            vma_flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            break;

        case TextureUsage::StaticImage:
            vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            break;

        case TextureUsage::StorageImage:
            vk_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
            break;

        case TextureUsage::ShadingRateImage:
            vk_usage = VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_STORAGE_BIT;
            break;
        }

        const auto image_create_info = VkImageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = create_info.format,
            .extent = VkExtent3D{.width = create_info.resolution, .height = create_info.resolution, .depth = 1},
            .mipLevels = 1,
            .arrayLayers = 6,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = vk_usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        const auto allocation_info = VmaAllocationCreateInfo{
            .flags = vma_flags,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };

        auto texture = GpuTexture{
            .type = TextureAllocationType::Vma
        };

        auto result = vmaCreateImage(
            vma,
            &image_create_info,
            &allocation_info,
            &texture.image,
            &texture.vma.allocation,
            &texture.vma.allocation_info
        );
        if(result != VK_SUCCESS) {
            throw std::runtime_error{fmt::format("Could not create image {}", name)};
        }

        texture.name = name;
        texture.create_info = image_create_info;

        const auto image_view_name = fmt::format("{} View", name);

        {
            const auto view_create_info = VkImageViewCreateInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = texture.image,
                .viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
                .format = create_info.format,
                .subresourceRange = {
                    .aspectMask = view_aspect,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 6,
                },
            };
            result = vkCreateImageView(device, &view_create_info, nullptr, &texture.image_view);
            if(result != VK_SUCCESS) {
                throw std::runtime_error{fmt::format("Could not create image view {}", image_view_name)};
            }
        }

        texture.attachment_view = texture.image_view;

        backend.set_object_name(texture.image, std::string{name.data(), name.size()});
        backend.set_object_name(texture.image_view, image_view_name);

        auto handle = &*textures.emplace(std::move(texture));
        return handle;
    }

    TextureHandle ResourceAllocator::create_volume_texture(
        const eastl::string_view name, VkFormat format, glm::uvec3 resolution,
        uint32_t num_mips, TextureUsage usage
    ) {
        const auto& device = backend.get_device();

        VkImageUsageFlags vk_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateFlags vma_flags = {};
        VkImageAspectFlags view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageCreateFlags image_create_flags = {};

        switch(usage) {
        case TextureUsage::RenderTarget:
            if(is_depth_format(format)) {
                vk_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
                view_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            } else {
                vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
            }
            image_create_flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
            vma_flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            break;

        case TextureUsage::StaticImage:
            vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            break;

        case TextureUsage::StorageImage:
            vk_usage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            image_create_flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
            break;

        default:
            throw std::runtime_error{"Unsupported 3D image usage"};
        }

        const auto image_create_info = VkImageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags = image_create_flags,
            .imageType = VK_IMAGE_TYPE_3D,
            .format = format,
            .extent = VkExtent3D{.width = resolution.x, .height = resolution.y, .depth = resolution.z},
            .mipLevels = num_mips,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = vk_usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        const auto allocation_info = VmaAllocationCreateInfo{
            .flags = vma_flags,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };

        auto texture = GpuTexture{
            .type = TextureAllocationType::Vma
        };

        auto result = vmaCreateImage(
            vma,
            &image_create_info,
            &allocation_info,
            &texture.image,
            &texture.vma.allocation,
            &texture.vma.allocation_info
        );
        if(result != VK_SUCCESS) {
            throw std::runtime_error{fmt::format("Could not create image {}", name)};
        }

        texture.name = name;
        texture.create_info = image_create_info;

        const auto image_view_name = fmt::format("{} View", name);

        const auto view_create_info = VkImageViewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = texture.image,
            .viewType = VK_IMAGE_VIEW_TYPE_3D,
            .format = format,
            .subresourceRange = {
                .aspectMask = view_aspect,
                .baseMipLevel = 0,
                .levelCount = num_mips,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        result = vkCreateImageView(device, &view_create_info, nullptr, &texture.image_view);
        if(result != VK_SUCCESS) {
            throw std::runtime_error{fmt::format("Could not create image view {}", image_view_name)};
        }

        const auto rtv_create_info = VkImageViewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = texture.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format = format,
            .subresourceRange = {
                .aspectMask = view_aspect,
                .baseMipLevel = 0,
                .levelCount = num_mips,
                .baseArrayLayer = 0,
                .layerCount = resolution.z,
            },
        };
        result = vkCreateImageView(device, &rtv_create_info, nullptr, &texture.attachment_view);
        if(result != VK_SUCCESS) {
            throw std::runtime_error{fmt::format("Could not create image view {} RTV", name)};
        }

        backend.set_object_name(texture.image, std::string{name.data(), name.size()});
        backend.set_object_name(texture.image_view, image_view_name);
        const auto rtv_name = fmt::format("{} RTV", name);
        backend.set_object_name(texture.attachment_view, rtv_name);

        auto handle = &*textures.emplace(std::move(texture));
        return handle;
    }

    TextureHandle ResourceAllocator::emplace_texture(GpuTexture&& new_texture) {
        if(new_texture.attachment_view == VK_NULL_HANDLE) {
            new_texture.attachment_view = new_texture.image_view;
        }

        const auto image_view_name = fmt::format("{} View", new_texture.name);
        backend.set_object_name(new_texture.image, new_texture.name.c_str());
        backend.set_object_name(new_texture.image_view, std::string{image_view_name.c_str()});

        const auto handle = &*textures.emplace(std::move(new_texture));
        return handle;
    }

    void ResourceAllocator::destroy_texture(TextureHandle handle) {
        if(handle == nullptr) {
            return;
        }
        auto& cur_frame_zombies = texture_zombie_lists[backend.get_current_gpu_frame()];
        cur_frame_zombies.emplace_back(handle);
    }

    BufferHandle ResourceAllocator::create_buffer(const eastl::string_view name, const size_t size, const BufferUsage usage) {
        logger->trace("Creating buffer {} with size {} and usage {}", name, size, to_string(usage));

        const auto& device = backend.get_device();

        VkBufferUsageFlags vk_usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VmaAllocationCreateFlags vma_flags = {};
        VmaMemoryUsage memory_usage = {};

        switch(usage) {
        case BufferUsage::StagingBuffer:
            vk_usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            if(backend.supports_ray_tracing()) {
                vk_usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            }
            vma_flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            break;

        case BufferUsage::VertexBuffer:
            vk_usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            if(backend.supports_ray_tracing()) {
                vk_usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            }
            memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;

        case BufferUsage::IndexBuffer:
            vk_usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            if(backend.supports_ray_tracing()) {
                vk_usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            }
            memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;

        case BufferUsage::IndirectBuffer:
            vk_usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;

        case BufferUsage::UniformBuffer:
            vk_usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            vma_flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;

        case BufferUsage::StorageBuffer:
            vk_usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;

        case BufferUsage::AccelerationStructure:
            vk_usage |= VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
            memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;

        case BufferUsage::ShaderBindingTable:
            vk_usage |= VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR
                |
                VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
            memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        }
        const auto create_info = VkBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = std::max(size, static_cast<size_t>(256)),
            .usage = vk_usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        const auto vma_create_info = VmaAllocationCreateInfo{
            .flags = vma_flags,
            .usage = memory_usage,
        };

        GpuBuffer buffer;
        const auto result = vmaCreateBuffer(
            vma,
            &create_info,
            &vma_create_info,
            &buffer.buffer,
            &buffer.allocation,
            &buffer.allocation_info
        );
        if(result != VK_SUCCESS) {
            throw std::runtime_error{fmt::format("Could not create buffer {}", name)};
        }

        backend.set_object_name(buffer.buffer, std::string{name.data(), name.size()});

        buffer.name = name;
        buffer.create_info = create_info;

        const auto info = VkBufferDeviceAddressInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer.buffer
        };
        buffer.address = vkGetBufferDeviceAddress(device, &info);

        const auto handle = &*buffers.emplace(std::move(buffer));
        return handle;
    }

    void* ResourceAllocator::map_buffer(const BufferHandle buffer_handle) const {
        if(buffer_handle->allocation_info.pMappedData == nullptr) {
            vmaMapMemory(vma, buffer_handle->allocation, &buffer_handle->allocation_info.pMappedData);
        }

        assert(buffer_handle->allocation_info.pMappedData != nullptr);

        return buffer_handle->allocation_info.pMappedData;
    }

    AccelerationStructureHandle ResourceAllocator::create_acceleration_structure(
        const uint64_t acceleration_structure_size, const VkAccelerationStructureTypeKHR type
    ) {
        ZoneScoped;
        AccelerationStructure as;

        as.buffer = create_buffer(
            "Acceleration structure",
            acceleration_structure_size,
            BufferUsage::AccelerationStructure);

        const auto acceleration_structure_create_info = VkAccelerationStructureCreateInfoKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = as.buffer->buffer,
            .size = acceleration_structure_size,
            .type = type,
        };
        vkCreateAccelerationStructureKHR(
            backend.get_device(),
            &acceleration_structure_create_info,
            nullptr,
            &as.acceleration_structure);

        const auto acceleration_device_address_info = VkAccelerationStructureDeviceAddressInfoKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .accelerationStructure = as.acceleration_structure,
        };
        as.as_address = vkGetAccelerationStructureDeviceAddressKHR(
            backend.get_device(),
            &acceleration_device_address_info);

        const auto handle = &*acceleration_structures.emplace(as);
        return handle;
    }

    void ResourceAllocator::destroy_acceleration_structure(AccelerationStructureHandle handle) {
        as_zombie_lists[backend.get_current_gpu_frame()].emplace_back(handle);
        destroy_buffer(handle->buffer);
    }

    void* ResourceAllocator::map_buffer(const BufferHandle buffer_handle) {
        if(buffer_handle->allocation_info.pMappedData == nullptr) {
            vmaMapMemory(vma, buffer_handle->allocation, &buffer_handle->allocation_info.pMappedData);
        }

        return buffer_handle->allocation_info.pMappedData;
    }

    void ResourceAllocator::destroy_buffer(BufferHandle handle) {
        auto& cur_frame_zombies = buffer_zombie_lists[backend.get_current_gpu_frame()];
        cur_frame_zombies.emplace_back(handle);
    }

    VkSampler ResourceAllocator::get_sampler(const VkSamplerCreateInfo& info) {
        const auto& device = backend.get_device();

        auto my_info = info;
        my_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

        const auto info_hash = SamplerCreateInfoHasher{}(my_info);

        if(const auto& itr = sampler_cache.find(info_hash); itr != sampler_cache.end()) {
            return itr->second;
        }

        VkSampler sampler;
        vkCreateSampler(device, &my_info, nullptr, &sampler);

        sampler_cache.emplace(info_hash, sampler);

        return sampler;
    }

    void ResourceAllocator::free_resources_for_frame(const uint32_t frame_idx) {
        ZoneScoped;

        const auto& device = backend.get_device();

        {
            ZoneScopedN("Acceleration structures");
            auto& zombie_ases = as_zombie_lists[frame_idx];
            for(const auto& as : zombie_ases) {
                vkDestroyAccelerationStructureKHR(device, as->acceleration_structure, nullptr);

                std::erase(acceleration_structures, *as);
            }
            zombie_ases.clear();
        }

        {
            ZoneScopedN("Buffers");
            auto& zombie_buffers = buffer_zombie_lists[frame_idx];
            for(const auto handle : zombie_buffers) {
                vmaDestroyBuffer(vma, handle->buffer, handle->allocation);

                buffers.erase(buffers.get_iterator(handle));
            }

            zombie_buffers.clear();
        }
        {
            ZoneScopedN("textures");
            auto& zombie_textures = texture_zombie_lists[frame_idx];
            for(const auto handle : zombie_textures) {
                vkDestroyImageView(device, handle->image_view, nullptr);

                switch(handle->type) {
                case TextureAllocationType::Vma:
                    vmaDestroyImage(vma, handle->image, handle->vma.allocation);
                    break;

                case TextureAllocationType::Swapchain:
                    // We just need to destroy the image view
                    vkDestroyImageView(device, handle->image_view, nullptr);
                    break;

                default:
                    throw std::runtime_error{"Unknown texture allocation type"};
                }

                textures.erase(textures.get_iterator(handle));
            }

            zombie_textures.clear();
        }

        vmaSetCurrentFrameIndex(vma, frame_idx);
    }

    void ResourceAllocator::report_memory_usage() const {
        auto budgets = eastl::array<VmaBudget, 32>{};
        vmaGetHeapBudgets(vma, budgets.data());

        /*
         * Google Pixel 7:
         * Memory usage report
         * Index | Block Count | Allocation Count | Block Bytes | Allocation Bytes | Total
         *     0 |          23 |               50 |   302570368 |        212183360 | 302570368 out of 6229288550
         *     1 |           0 |                0 |           0 |                0 | 0 out of 83886080
         *
         * RTX 2080 Super:
         * Memory usage report
         * Index | Block Count | Allocation Count | Block Bytes | Allocation Bytes | Total
         *     0 |          24 |               43 |   251064576 |        191568192 | 251064576 out of 6711725260
         *     1 |           1 |                4 |    33554432 |           215040 | 33554432 out of 27450340147
         *     2 |           1 |                3 |     3506176 |             1984 | 3506176 out of 179516211
         */

        logger->debug("Memory usage report");
        logger->debug("Index | Block Count | Allocation Count | Block Bytes | Allocation Bytes | Total");

        auto budget_index = 0;
        for(const auto& budget : budgets) {
            if(budget.budget == 0) {
                continue;
            }

            logger->debug(
                "{:>5} | {:>11} | {:>16} | {:>11} | {:>16} | {} out of {}",
                budget_index,
                budget.statistics.blockCount,
                budget.statistics.allocationCount,
                budget.statistics.blockBytes,
                budget.statistics.allocationBytes,
                budget.usage,
                budget.budget
            );

            budget_index++;
        }
    }

    VmaAllocator ResourceAllocator::get_vma() const {
        return vma;
    }
}

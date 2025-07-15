#include "texture_loader.hpp"

#include <stb_image.h>
#include <magic_enum.hpp>

#include "core/system_interface.hpp"
#include "render/backend/render_backend.hpp"
#include "render/backend/resource_upload_queue.hpp"

namespace render {
    struct LoadedTexture {
        int width = 0;
        int height = 0;
        eastl::vector<uint8_t> data = {};
    };

    TextureLoader::TextureLoader() {
        logger = SystemInterface::get().get_logger("TextureLoader");
    }

    eastl::optional<TextureHandle> TextureLoader::load_texture(const ResourcePath& filepath, const TextureType type,
                                                               const VkImageUsageFlags usage_flags
        ) {
        // Check if we already have the texture
        if(const auto itr = loaded_textures.find(filepath); itr != loaded_textures.end()) {
            return itr->second;
        }

        ZoneScoped;

        return SystemInterface::get()
               .load_file(filepath)
               .and_then(
                   [&](const eastl::vector<std::byte>& data) {
                       return upload_texture_stbi(filepath, data, type, usage_flags);
                   });
    }

    eastl::optional<TextureHandle> TextureLoader::upload_texture_stbi(
        const ResourcePath& filepath, const eastl::vector<std::byte>& data, const TextureType type,
        const VkImageUsageFlags usage_flags
        ) {
        ZoneScoped;

        auto& backend = RenderBackend::get();

        LoadedTexture loaded_texture;
        int num_components;
        const auto decoded_data = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(data.data()),
            static_cast<int>(data.size()),
            &loaded_texture.width,
            &loaded_texture.height,
            &num_components,
            4
            );
        if(decoded_data == nullptr) {
            logger->error("Cannot decode texture {}: {}", filepath, stbi_failure_reason());
            return eastl::nullopt;
        }

        loaded_texture.data = eastl::vector<uint8_t>(
            decoded_data,
            reinterpret_cast<uint8_t*>(decoded_data) +
            loaded_texture.width * loaded_texture.height * 4
            );

        stbi_image_free(decoded_data);

        const auto format = [&] {
            switch(type) {
            case TextureType::Color:
                return VK_FORMAT_R8G8B8A8_SRGB;

            case TextureType::Data:
                return VK_FORMAT_R8G8B8A8_UNORM;
            }

            return VK_FORMAT_R8G8B8A8_UNORM;
        }();
        auto& allocator = backend.get_global_allocator();
        const auto handle = allocator.create_texture(
            filepath.to_string(),
            {
                .format = format,
                .resolution = glm::uvec2{loaded_texture.width, loaded_texture.height},
                .num_mips = 1,
                .usage = TextureUsage::StaticImage,
                .usage_flags = usage_flags
            }
            );
        loaded_textures.emplace(filepath, handle);

        auto& upload_queue = backend.get_upload_queue();
        upload_queue.enqueue(
            TextureUploadJob{
                .destination = handle,
                .mip = 0,
                .data = loaded_texture.data,
            }
            );

        if(backend.has_separate_transfer_queue()) {
            backend.add_transfer_barrier(
                VkImageMemoryBarrier2{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = backend.get_transfer_queue_family_index(),
                    .dstQueueFamilyIndex = backend.get_graphics_queue_family_index(),
                    .image = handle->image,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                    }
                }
                );

            logger->info(
                "Added queue transfer barrier for image {} (Vulkan handle {})",
                filepath,
                static_cast<void*>(handle->image));
        }

        return handle;
    }
}

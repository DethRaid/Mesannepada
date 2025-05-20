#pragma once

// Un-comment if you use the debug library
// #define KHRONOS_STATIC

#include <filesystem>
#include <unordered_map>

#include <EASTL/optional.h>
#include <volk.h>
#include <ktxvulkan.h>

#include "render/backend/handles.hpp"
#include "render/texture_type.hpp"
#include "render/backend/resource_allocator.hpp"
#include <spdlog/logger.h>

namespace render {
    class RenderBackend;

    /**
     * Loads textures and uploads them to the GPU
     */
    class TextureLoader {
    public:
        explicit TextureLoader();

        ~TextureLoader();

        /**
         * Loads a texture from disk
         *
         * @param filepath The path to the texture
         * @param type The type of the texture
         */
        eastl::optional<TextureHandle> load_texture(const std::filesystem::path& filepath, TextureType type);

        /**
         * Uploads a KTX texture from memory
         *
         * @param filepath The filepath the texture data came from. Useful for logging and naming
         * @param data The raw data for the texture
         */
        eastl::optional<TextureHandle> upload_texture_ktx(
            const std::filesystem::path& filepath, const eastl::vector<std::byte>& data
        );

        /**
         * Uploads a PNG file from memory
         *
         * @param filepath The filepath the texture data came from. Useful for logging and naming
         * @param data The raw data for the PNG image
         * @param type The type of the texture
         */
        eastl::optional<TextureHandle> upload_texture_stbi(
            const std::filesystem::path& filepath, const eastl::vector<std::byte>& data, TextureType type
        );

    private:
        VkCommandPool ktx_command_pool;
        ktxVulkanDeviceInfo ktx;

        std::shared_ptr<spdlog::logger> logger;

        std::unordered_map<std::string, TextureHandle> loaded_textures;

        eastl::optional<TextureHandle> load_texture_ktx(const std::filesystem::path& filepath, TextureType type);

        eastl::optional<TextureHandle> load_texture_stbi(const std::filesystem::path& filepath, TextureType type);
    };
}

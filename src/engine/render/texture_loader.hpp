#pragma once

#include <filesystem>

#include <EASTL/optional.h>

#include "render/texture_type.hpp"
#include "render/backend/handles.hpp"
#include "render/backend/resource_allocator.hpp"
#include "resources/resource_path.hpp"

namespace render {
    class RenderBackend;

    /**
     * Loads textures and uploads them to the GPU
     */
    class TextureLoader {
    public:
        explicit TextureLoader();

        /**
         * Loads a texture from disk
         *
         * @param filepath The path to the texture
         * @param type The type of the texture
         */
        eastl::optional<TextureHandle> load_texture(const ResourcePath& filepath, TextureType type);

        /**
         * Uploads a KTX texture from memory
         *
         * @param filepath The filepath the texture data came from. Useful for logging and naming
         * @param data The raw data for the texture
         */
        eastl::optional<TextureHandle> upload_texture_ktx(
            const ResourcePath& filepath, const eastl::vector<std::byte>& data
        );

        /**
         * Uploads a PNG file from memory
         *
         * @param filepath The filepath the texture data came from. Useful for logging and naming
         * @param data The raw data for the PNG image
         * @param type The type of the texture
         */
        eastl::optional<TextureHandle> upload_texture_stbi(
            const ResourcePath& filepath, const eastl::vector<std::byte>& data, TextureType type
        );

    private:
        std::shared_ptr<spdlog::logger> logger;

        eastl::unordered_map<ResourcePath, TextureHandle> loaded_textures;
    };
}

#pragma once

#include <cstdint>

#include <EASTL/array.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <volk.h>

namespace render {
    struct KtxTextureHeader {
        eastl::array<uint8_t, 12> identifier;
        VkFormat format;
        uint32_t type_size;
        uint32_t pixel_width;
        uint32_t pixel_height;
        uint32_t pixel_depth;
        uint32_t layer_count;
        uint32_t face_count;
        uint32_t level_count;
        uint32_t supercompression_scheme;
    };

    struct KtxTextureIndex {
        uint32_t dfd_byte_offset;
        uint32_t dfd_byte_length;
        uint32_t kvd_byte_offset;
        uint32_t kvd_byte_length;
        uint64_t sgd_byte_offset;
        uint64_t sgd_byte_length;
    };

    struct KtxTextureLevelIndex {
        uint64_t byte_offset;
        uint64_t byte_length;
        uint64_t uncompressed_byte_length;
    };

    struct KtxTextureDataFormatDescriptor {
        uint32_t dfd_total_size;
        // Then some data for the data format descriptor
    };

    struct KtxTextureKeyValueData {
        uint32_t key_and_value_byte_length;
        eastl::vector<uint8_t> key_and_value;
    };

    /**
     * Represents a KTX Texture
     */
    struct KtxTexture {
        /**
         * Header data
         */
        KtxTextureHeader header;

        /**
         * Index into the original data buffer
         */
        KtxTextureIndex index;

        /**
         * Information about each mip level's data
         */
        eastl::vector<KtxTextureLevelIndex> levels;

        /**
         * Information about the data format
         */
        KtxTextureDataFormatDescriptor data_format_descriptor;

        /**
         * Some key/value data
         */
        eastl::optional<KtxTextureKeyValueData> key_value_data;

        /**
         * The original KTX file data
         */
        eastl::vector<uint8_t> original_data;

        /**
         * Global data about supercompression. May have size 0 if this file doesn't use supercompression. Views the
         * original data
         */
        eastl::span<uint8_t> supercompression_global_data;

        /**
         * The data for each mip level. Views into the original data
         */
        eastl::vector<eastl::span<uint8_t>> level_images;
    };
}

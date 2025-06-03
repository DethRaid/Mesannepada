#pragma once

#include <cstdint>

#include <vk_mem_alloc.h>

#include "backend/acceleration_structure.hpp"
#include "core/box.hpp"
#include "render/backend/handles.hpp"

namespace render {
    struct Mesh {
        VmaVirtualAllocation vertex_allocation = {};

        VmaVirtualAllocation index_allocation = {};

        VkDeviceSize first_index = 0;

        uint32_t num_indices = 0;

        VkDeviceSize first_vertex = 0;

        uint32_t num_vertices = 0;

        /**
         * Worldspace bounds of the mesh
         */
        Box bounds = {};

        AccelerationStructureHandle blas = {};
    };

    struct SkeletalMesh : Mesh {
        /**
         * Allocation for the weights and joints that affect each vertex - this data is stored in parallel arrays
         */
        VmaVirtualAllocation weights_allocation = {};

        VkDeviceSize weights_offset = 0;

        uint32_t num_weights = 0;
    };
}

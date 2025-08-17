#pragma once

#include "render/mesh_handle.hpp"
#include "core/object_pool.hpp"
#include "material_proxy.hpp"
#include "shared/primitive_data.hpp"

namespace render {
    struct MeshPrimitiveProxy {
        PrimitiveDataGPU data = {};

        MeshHandle mesh;

        PooledObject<BasicPbrMaterialProxy> material;

        BufferHandle emissive_points_buffer = {};

        bool visible_to_ray_tracing = true;

        AccelerationStructureHandle blas = {};

        size_t placed_blas_index = eastl::numeric_limits<size_t>::max();

        void calculate_worldspace_bounds();
    };

    inline void MeshPrimitiveProxy::calculate_worldspace_bounds() {
        const auto& bounds = mesh->bounds;

        // Transform the corners of the bounds
        auto corners = eastl::array{
            float3{data.model * float4{bounds.min.x, bounds.min.y, bounds.min.z, 1}},
            float3{data.model * float4{bounds.min.x, bounds.min.y, bounds.max.z, 1}},
            float3{data.model * float4{bounds.min.x, bounds.max.y, bounds.min.z, 1}},
            float3{data.model * float4{bounds.min.x, bounds.max.y, bounds.max.z, 1}},
            float3{data.model * float4{bounds.max.x, bounds.min.y, bounds.min.z, 1}},
            float3{data.model * float4{bounds.max.x, bounds.min.y, bounds.max.z, 1}},
            float3{data.model * float4{bounds.max.x, bounds.max.y, bounds.min.z, 1}},
            float3{data.model * float4{bounds.max.x, bounds.max.y, bounds.max.z, 1}},
        };

        auto worldspace_min = float3{eastl::numeric_limits<float>::max()};
        auto worldspace_max = float3{eastl::numeric_limits<float>::lowest()};
        for(const auto& corner : corners) {
            worldspace_max = glm::max(worldspace_max, corner);
            worldspace_min = glm::min(worldspace_min, corner);
        }

        const auto radius = glm::max(
            glm::max(worldspace_max.x - worldspace_min.x, worldspace_max.y - worldspace_min.y),
            worldspace_max.z - worldspace_min.z
            );

        data.bounds_min_and_radius = float4{worldspace_min, radius};
        data.bounds_max = float4{worldspace_max, 0};
    }

    using MeshPrimitiveProxyHandle = PooledObject<MeshPrimitiveProxy>;
}

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

        size_t placed_blas_index = eastl::numeric_limits<size_t>::max();
    };

    using MeshPrimitiveProxyHandle = PooledObject<MeshPrimitiveProxy>;
}

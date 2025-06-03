#pragma once

#include <EASTL/numeric_limits.h>

#include "render/material_proxy.hpp"
#include "render/mesh_handle.hpp"
#include "shared/primitive_data.hpp"

namespace render {

    class SkeletalMeshPrimitiveProxy {

        PrimitiveDataGPU data = {};

        MeshHandle mesh;

        PooledObject<BasicPbrMaterialProxy> material;

        BufferHandle emissive_points_buffer = {};

        bool visible_to_ray_tracing = true;

        size_t placed_blas_index = eastl::numeric_limits<size_t>::max();
    };

    using MeshPrimitiveProxyHandle = PooledObject<SkeletalMeshPrimitiveProxy>;
}

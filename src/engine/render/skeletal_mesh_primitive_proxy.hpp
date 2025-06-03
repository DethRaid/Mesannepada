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

        bool visible_to_ray_tracing = true;

        size_t placed_blas_index = eastl::numeric_limits<size_t>::max();

        BufferHandle transformed_vertices = nullptr;

        BufferHandle transformed_data = nullptr;
    };

    using SkeletalMeshPrimitiveProxyHandle = PooledObject<SkeletalMeshPrimitiveProxy>;
}

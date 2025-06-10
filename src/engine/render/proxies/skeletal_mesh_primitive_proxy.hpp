#pragma once

#include <EASTL/numeric_limits.h>

#include "animation/skeleton.hpp"
#include "material_proxy.hpp"
#include "render/mesh_handle.hpp"
#include "render/proxies/mesh_primitive_proxy.hpp"
#include "shared/primitive_data.hpp"

namespace render {
    struct SkeletalMeshPrimitiveProxy {
        MeshPrimitiveProxyHandle mesh_proxy;

        SkeletalPrimitiveDataGPU skeletal_data = {};

        BufferHandle bone_transforms = nullptr;

        BufferHandle transformed_vertices = nullptr;

        BufferHandle transformed_data = nullptr;
    };

    using SkeletalMeshPrimitiveProxyHandle = PooledObject<SkeletalMeshPrimitiveProxy>;
}

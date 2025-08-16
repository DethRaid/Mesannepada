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

        BufferHandle previous_bone_transforms = nullptr;

        BufferHandle skinned_vertices = nullptr;

        BufferHandle skinned_data = nullptr;

        BufferHandle previous_skinned_vertices = nullptr;
    };

    using SkeletalMeshPrimitiveProxyHandle = PooledObject<SkeletalMeshPrimitiveProxy>;
}

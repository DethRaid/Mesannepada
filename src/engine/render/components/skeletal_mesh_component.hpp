#pragma once

#include <EASTL/fixed_vector.h>
#include <entt/entt.hpp>

#include "../proxies/material_proxy.hpp"
#include "../proxies/skeletal_mesh_primitive_proxy.hpp"

namespace render {
    /**
     * A mesh primitive is a mesh with a material
     */
    struct SkeletalMeshPrimitive {
        MeshHandle mesh;

        PooledObject<BasicPbrMaterialProxy> material;

        SkeletalMeshPrimitiveProxyHandle proxy;

        bool visible_to_ray_tracing = true;
    };

    struct SkeletalMeshComponent {
        eastl::fixed_vector<SkeletalMeshPrimitive, 8> primitives;

        SkeletonHandle skeleton;
    };
} // render

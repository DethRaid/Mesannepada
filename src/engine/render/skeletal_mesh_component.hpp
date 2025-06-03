#pragma once

#include <entt/entt.hpp>
#include <EASTL/fixed_vector.h>

#include "render/material_proxy.hpp"
#include "render/mesh_handle.hpp"
#include "render/mesh_primitive_proxy.hpp"

namespace render {
    /**
     * A mesh primitive is a mesh with a material
     */
    struct SkeletalMeshPrimitive {
        MeshHandle mesh;

        BufferHandle joints;

        BufferHandle weights;

        PooledObject<BasicPbrMaterialProxy> material;

        SkeletalMeshPrimitiveProxyHandle proxy;

        bool visible_to_ray_tracing = true;
    };

    struct SkeletalMeshComponent {
        entt::handle animating_entity;

        eastl::fixed_vector<SkeletalMeshPrimitive, 8> primitives;
    };
} // render

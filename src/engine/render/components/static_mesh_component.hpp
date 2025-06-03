#pragma once

#include <EASTL/fixed_vector.h>

#include "render/material_proxy.hpp"
#include "render/mesh_handle.hpp"
#include "render/mesh_primitive_proxy.hpp"

namespace render {
    /**
     * A mesh primitive is a mesh with a material
     */
    struct MeshPrimitive {
        MeshHandle mesh;

        PooledObject<BasicPbrMaterialProxy> material;

        MeshPrimitiveProxyHandle proxy;

        bool visible_to_ray_tracing = true;
    };

    /**
     * Contains all the information needed to render a static mesh
     */
    struct StaticMeshComponent {
        /**
         * Many meshes will only have a handful of primitives, more than 8 is probably excessive - but may happen
         */
        eastl::fixed_vector<MeshPrimitive, 8> primitives;
    };
}

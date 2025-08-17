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

        SkeletonHandle skeleton = nullptr;

        // A copy of the skeleton's bones, able to be transformed by animations
        eastl::vector<Bone> bones = {};

        /**
         * Bone matrices with parent transforms and inverse bind poses applied
         */
        eastl::vector<float4x4> worldspace_bone_matrices;

        /**
         * Bone matrices with parent transforms and inverse bind poses applied - from the last frame
         */
        eastl::vector<float4x4> previous_worldspace_bone_matrices;

        /**
         * GPU buffer for all the bone matrices. Updated at start of render frame. May eventually be replaced with a full
         * proxy
         */
        BufferHandle bone_matrices_buffer = nullptr;

        /**
         * GPU buffer for all the bone matrices. Updated at start of render frame. May eventually be replaced with a full
         * proxy
         */
        BufferHandle previous_bone_matrices_buffer = nullptr;

        void propagate_bone_transforms();

    private:
        void propagate_bone_transform(size_t bone_idx, const float4x4& parent_matrix);
    };
} // render

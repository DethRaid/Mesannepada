#pragma once

#include "render/mesh.hpp"
#include "core/object_pool.hpp"

namespace render {
    using MeshHandle = PooledObject<Mesh>;

    using SkeletalMeshHandle = PooledObject<SkeletalMesh>;
}

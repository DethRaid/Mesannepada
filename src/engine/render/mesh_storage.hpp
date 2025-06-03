#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>

#include "render/backend/scatter_upload_buffer.hpp"
#include "render/mesh_handle.hpp"
#include "core/object_pool.hpp"
#include "render/backend/handles.hpp"
#include "render/mesh.hpp"
#include "shared/vertex_data.hpp"
#include "shared/mesh_point.hpp"

namespace render {
    class RenderBackend;
    class ResourceUploadQueue;

    /**
     * Stores meshes
     */
    class MeshStorage {
    public:
        explicit MeshStorage();

        ~MeshStorage();

        eastl::optional<MeshHandle> add_mesh(eastl::span<const StandardVertex> vertices,
                                             eastl::span<const uint32_t> indices, const Box& bounds
            );

        eastl::optional<SkeletalMeshHandle> add_skeletal_mesh(eastl::span<const StandardVertex> vertices,
                                                              eastl::span<const uint32_t> indices, const Box& bounds,
                                                              eastl::span<const u16vec4> bone_ids,
                                                              eastl::span<const float4> weights
            );

        void free_mesh(MeshHandle mesh);

        void flush_mesh_draw_arg_uploads(RenderGraph& graph);

        BufferHandle get_vertex_position_buffer() const;

        BufferHandle get_vertex_data_buffer() const;

        BufferHandle get_index_buffer() const;

        BufferHandle get_draw_args_buffer() const;

        void bind_to_commands(const CommandBuffer& commands) const;

    private:
        ObjectPool<Mesh> static_meshes;

        ObjectPool<SkeletalMesh> skeletal_meshes;

        ScatterUploadBuffer<VkDrawIndexedIndirectCommand> mesh_draw_args_upload_buffer;
        BufferHandle mesh_draw_args_buffer = {};

        // vertex_block and index_block measure vertices and indices, respectively

        VmaVirtualBlock vertex_block = {};
        BufferHandle vertex_position_buffer = {};
        BufferHandle vertex_data_buffer = {};

        VmaVirtualBlock index_block = {};
        BufferHandle index_buffer = {};

        VmaVirtualBlock weights_block = {};
        BufferHandle weights_buffer = {};
        BufferHandle joints_buffer = {};

        eastl::optional<Mesh> add_mesh_internal(
            eastl::span<const StandardVertex> vertices, eastl::span<const uint32_t> indices, const Box& bounds
            );

        AccelerationStructureHandle create_blas_for_mesh(
            uint32_t first_vertex, uint32_t num_vertices, uint32_t first_index, uint num_triangles
            ) const;
    };
}

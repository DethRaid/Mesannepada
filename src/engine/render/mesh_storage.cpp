#include "mesh_storage.hpp"

#include <random>
#include <tracy/Tracy.hpp>

#include "backend/blas_build_queue.hpp"
#include "console/cvars.hpp"
#include "core/system_interface.hpp"
#include "render/raytracing_scene.hpp"
#include "render/backend/render_graph.hpp"
#include "render/backend/resource_allocator.hpp"
#include "render/backend/resource_upload_queue.hpp"
#include "shared/vertex_data.hpp"
#include "spdlog/cfg/helpers-inl.h"

namespace render {
    constexpr uint32_t max_num_meshes = 65536;

    constexpr uint32_t max_num_vertices = 10000000;
    constexpr uint32_t max_num_indices = 10000000;

    static std::shared_ptr<spdlog::logger> logger;

    MeshStorage::MeshStorage() {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("MeshStorage");
            logger->set_level(spdlog::level::info);
        }

        const auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();
        vertex_position_buffer = allocator.create_buffer(
            "Vertex position buffer",
            max_num_vertices * sizeof(StandardVertexPosition),
            BufferUsage::VertexBuffer
            );
        vertex_data_buffer = allocator.create_buffer(
            "Vertex data buffer",
            max_num_vertices * sizeof(StandardVertexData),
            BufferUsage::VertexBuffer
            );
        index_buffer = allocator.create_buffer(
            "Index buffer",
            max_num_indices * sizeof(uint32_t),
            BufferUsage::IndexBuffer
            );

        weights_buffer = allocator.create_buffer(
            "weights_buffer",
            max_num_vertices * sizeof(u16vec4),
            BufferUsage::StorageBuffer);
        bone_ids_buffer = allocator.create_buffer(
            "joints_buffer",
            max_num_vertices * sizeof(float4),
            BufferUsage::StorageBuffer);

        mesh_draw_args_buffer = allocator.create_buffer(
            "Mesh draw args buffer",
            sizeof(VkDrawIndexedIndirectCommand) * max_num_meshes,
            BufferUsage::StorageBuffer);

        constexpr auto vertex_block_create_info = VmaVirtualBlockCreateInfo{
            .size = max_num_vertices,
        };
        vmaCreateVirtualBlock(&vertex_block_create_info, &vertex_block);

        vmaCreateVirtualBlock(&vertex_block_create_info, &weights_block);

        constexpr auto index_block_create_info = VmaVirtualBlockCreateInfo{
            .size = max_num_indices,
        };
        vmaCreateVirtualBlock(&index_block_create_info, &index_block);
    }

    MeshStorage::~MeshStorage() {
        const auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();
        allocator.destroy_buffer(vertex_position_buffer);
        allocator.destroy_buffer(vertex_data_buffer);
        allocator.destroy_buffer(index_buffer);
        allocator.destroy_buffer(mesh_draw_args_buffer);

        // Yeet all the meshes, even if not explicitly destroyed
        vmaClearVirtualBlock(vertex_block);
        vmaClearVirtualBlock(index_block);
        vmaDestroyVirtualBlock(vertex_block);
        vmaDestroyVirtualBlock(index_block);
    }

    eastl::optional<MeshHandle> MeshStorage::add_mesh(
        const eastl::span<const StandardVertex> vertices, const eastl::span<const uint32_t> indices, const Box& bounds
        ) {
        return add_mesh_internal(vertices, indices, bounds, false)
            .and_then([&](Mesh mesh) {
                const auto handle = meshes.emplace(eastl::move(mesh));

                upload_mesh_draw_args(handle);

                return eastl::make_optional(handle);
            });
    }

    eastl::optional<MeshHandle> MeshStorage::add_skeletal_mesh(
        const eastl::span<const StandardVertex> vertices, const eastl::span<const uint32_t> indices, const Box& bounds,
        const eastl::span<const u16vec4> bone_ids, const eastl::span<const float4> weights
        ) {
        if(vertices.size() != bone_ids.size() || vertices.size() != weights.size()) {
            return eastl::nullopt;
        }
        return add_mesh_internal(vertices, indices, bounds, true)
            .and_then([&](Mesh mesh) -> eastl::optional<MeshHandle> {
                const auto allocate_info = VmaVirtualAllocationCreateInfo{
                    .size = weights.size(),
                };
                const auto result = vmaVirtualAllocate(weights_block,
                                                       &allocate_info,
                                                       &mesh.weights_allocation,
                                                       &mesh.weights_offset);
                if(result != VK_SUCCESS) {
                    return eastl::nullopt;
                }

                mesh.num_weights = static_cast<uint32_t>(weights.size());

                const auto& backend = RenderBackend::get();
                auto& upload_queue = backend.get_upload_queue();
                upload_queue.upload_to_buffer<u16vec4>(
                    bone_ids_buffer,
                    bone_ids,
                    static_cast<uint32_t>(mesh.weights_offset * sizeof(u16vec4)));
                upload_queue.upload_to_buffer<float4>(
                    weights_buffer,
                    weights,
                    static_cast<uint32_t>(mesh.weights_offset * sizeof(float4)));

                const auto handle = meshes.emplace(eastl::move(mesh));

                upload_mesh_draw_args(handle);

                return handle;
            });
    }

    void MeshStorage::free_mesh(const MeshHandle mesh) {
        vmaVirtualFree(vertex_block, mesh->vertex_allocation);
        vmaVirtualFree(index_block, mesh->index_allocation);

        meshes.free_object(mesh);
    }

    void MeshStorage::flush_mesh_draw_arg_uploads(RenderGraph& graph) {
        if(mesh_draw_args_upload_buffer.get_size() > 0) {
            mesh_draw_args_upload_buffer.flush_to_buffer(graph, mesh_draw_args_buffer);
        }
    }

    BufferHandle MeshStorage::get_vertex_position_buffer() const {
        return vertex_position_buffer;
    }

    BufferHandle MeshStorage::get_vertex_data_buffer() const {
        return vertex_data_buffer;
    }

    BufferHandle MeshStorage::get_weights_buffer() const {
        return weights_buffer;
    }

    BufferHandle MeshStorage::get_bone_ids_buffer() const {
        return bone_ids_buffer;
    }

    BufferHandle MeshStorage::get_index_buffer() const {
        return index_buffer;
    }

    BufferHandle MeshStorage::get_draw_args_buffer() const {
        return mesh_draw_args_buffer;
    }

    void MeshStorage::bind_to_commands(const CommandBuffer& commands) const {
        commands.bind_vertex_buffer(0, vertex_position_buffer);
        commands.bind_vertex_buffer(1, vertex_data_buffer);
        commands.bind_index_buffer(index_buffer);
    }

    eastl::optional<Mesh> MeshStorage::add_mesh_internal(
        const eastl::span<const StandardVertex> vertices, const eastl::span<const uint32_t> indices, const Box& bounds,
        const bool is_dynamic
        ) const {
        auto mesh = Mesh{};

        const auto vertex_allocate_info = VmaVirtualAllocationCreateInfo{
            .size = vertices.size(),
        };
        auto result = vmaVirtualAllocate(vertex_block,
                                         &vertex_allocate_info,
                                         &mesh.vertex_allocation,
                                         &mesh.first_vertex);
        if(result != VK_SUCCESS) {
            return eastl::nullopt;
        }

        const auto index_allocate_info = VmaVirtualAllocationCreateInfo{
            .size = indices.size(),
        };
        result = vmaVirtualAllocate(index_block, &index_allocate_info, &mesh.index_allocation, &mesh.first_index);
        if(result != VK_SUCCESS) {
            vmaVirtualFree(vertex_block, mesh.vertex_allocation);
            return eastl::nullopt;
        }

        mesh.num_vertices = static_cast<uint32_t>(vertices.size());
        mesh.num_indices = static_cast<uint32_t>(indices.size());
        mesh.bounds = bounds;

        auto positions = eastl::vector<StandardVertexPosition>{};
        auto data = eastl::vector<StandardVertexData>{};
        positions.reserve(vertices.size());
        data.reserve(vertices.size());

        for(const auto& vertex : vertices) {
            positions.push_back(vertex.position);
            data.push_back(
                StandardVertexData{
                    .normal = vertex.normal,
                    .tangent = vertex.tangent,
                    .texcoord = vertex.texcoord,
                    .color = vertex.color,
                });
        }

        const auto& backend = RenderBackend::get();
        auto& upload_queue = backend.get_upload_queue();
        upload_queue.upload_to_buffer<StandardVertexPosition>(
            vertex_position_buffer,
            positions,
            static_cast<uint32_t>(mesh.first_vertex * sizeof(StandardVertexPosition))
            );
        upload_queue.upload_to_buffer<StandardVertexData>(
            vertex_data_buffer,
            data,
            static_cast<uint32_t>(mesh.first_vertex * sizeof(StandardVertexData))
            );
        upload_queue.upload_to_buffer(index_buffer,
                                      indices,
                                      static_cast<uint32_t>(mesh.first_index * sizeof(uint32_t)));

        if(backend.supports_ray_tracing()) {
            mesh.blas = create_blas_for_mesh(
                static_cast<uint32_t>(mesh.first_vertex),
                mesh.num_vertices,
                static_cast<uint32_t>(mesh.first_index),
                mesh.num_indices / 3,
                is_dynamic
                );
        }

        return mesh;
    }

    void MeshStorage::upload_mesh_draw_args(const MeshHandle handle) {
        if(mesh_draw_args_upload_buffer.is_full()) {
            auto& backend = RenderBackend::get();
            auto graph = RenderGraph{backend};
            flush_mesh_draw_arg_uploads(graph);
            graph.finish();
            backend.execute_graph(graph);
        }

        mesh_draw_args_upload_buffer.add_data(
            handle.index,
            {
                .indexCount = handle->num_indices,
                .instanceCount = 1,
                .firstIndex = static_cast<uint32_t>(handle->first_index),
                .vertexOffset = 0,
                // We use BDA for vertices, the pointers point to the start of the vertex allocation
                .firstInstance = 0
            });
    }

    AccelerationStructureHandle MeshStorage::create_blas_for_mesh(
        const uint32_t first_vertex, const uint32_t num_vertices, const uint32_t first_index, const uint num_triangles,
        const bool is_dynamic
        ) const {
        return RaytracingScene::create_blas(
            vertex_position_buffer->address + first_vertex * sizeof(glm::vec3),
            num_vertices,
            index_buffer->address + first_index * sizeof(uint32_t),
            num_triangles,
            is_dynamic
            );
    }
}

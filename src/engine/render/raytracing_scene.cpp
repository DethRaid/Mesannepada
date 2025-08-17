#include "raytracing_scene.hpp"

#include "console/cvars.hpp"
#include "render/mesh_storage.hpp"
#include "render/render_scene.hpp"
#include "render/backend/render_backend.hpp"

namespace render {
    [[maybe_unused]] static auto cvar_enable_raytracing = AutoCVar_Int{
        "r.Raytracing.Enable", "Whether or not to enable raytracing", 1
    };

    RaytracingScene::RaytracingScene(RenderWorld& world_in)
        : world{ world_in } {
        placed_blases.reserve(4096);
    }

    void RaytracingScene::add_primitive(const MeshPrimitiveProxyHandle primitive) {
        ZoneScoped;

        primitive->placed_blas_index = get_next_blas_index();
        if (primitive->placed_blas_index == placed_blases.size()) {
            placed_blases.emplace_back();
        }

        active_blases.emplace_back(primitive->placed_blas_index);

        update_primitive(primitive);
    }

    void RaytracingScene::update_primitive(const MeshPrimitiveProxyHandle primitive) {
        ZoneScoped;

        const auto& model_matrix = primitive->data.model;
        const auto blas_flags = primitive->material->first.transparency_mode == TransparencyMode::Solid
            ? VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR
            : VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;

        // Multiply by two because each shader group has a GI and occlusion variant
        const auto sbt_offset = static_cast<uint32_t>(primitive->material->first.transparency_mode) * 2;
        const auto instance = VkAccelerationStructureInstanceKHR{
            .transform = {
                .matrix = {
                    {model_matrix[0][0], model_matrix[1][0], model_matrix[2][0], model_matrix[3][0]},
                    {model_matrix[0][1], model_matrix[1][1], model_matrix[2][1], model_matrix[3][1]},
                    {model_matrix[0][2], model_matrix[1][2], model_matrix[2][2], model_matrix[3][2]}
                }
            },
            .instanceCustomIndex = primitive.index,
            .mask = 0xFF,
            .instanceShaderBindingTableRecordOffset = sbt_offset,
            .flags = static_cast<VkGeometryInstanceFlagsKHR>(blas_flags),
            .accelerationStructureReference = primitive->blas->as_address
        };

        placed_blases[primitive->placed_blas_index] = instance;

        set_dirty();
    }

    void RaytracingScene::remove_primitive(const MeshPrimitiveProxyHandle primitive) {
        if (primitive->placed_blas_index != eastl::numeric_limits<size_t>::max()) {
            inactive_blases.emplace_back(primitive->placed_blas_index);
            active_blases.erase_first_unsorted(primitive->placed_blas_index);

            set_dirty();
        }
    }

    void RaytracingScene::finalize(RenderGraph& graph) {
        commit_tlas_builds(graph);
    }

    AccelerationStructureHandle RaytracingScene::get_acceleration_structure() const {
        return acceleration_structure;
    }

    void RaytracingScene::set_dirty() {
        is_dirty = true;
    }

    AccelerationStructureHandle RaytracingScene::create_blas(
        const VkDeviceAddress vertices, const uint32_t num_vertices, const VkDeviceAddress indices, const uint num_triangles,
        const bool is_dynamic
        ) {
        ZoneScoped;

        const auto& backend = RenderBackend::get();

        const auto geometry = VkAccelerationStructureGeometryKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry = {
                .triangles = {
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                    .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                    .vertexData = {.deviceAddress = vertices},
                    .vertexStride = sizeof(glm::vec3),
                    .maxVertex = num_vertices - 1,
                    .indexType = VK_INDEX_TYPE_UINT32,
                    .indexData = {.deviceAddress = indices}
                }
            },
        };

        VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        if(is_dynamic) {
            flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        }

        const auto build_info = VkAccelerationStructureBuildGeometryInfoKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = flags,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .geometryCount = 1,
            .pGeometries = &geometry
        };
        auto size_info = VkAccelerationStructureBuildSizesInfoKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
        };
        vkGetAccelerationStructureBuildSizesKHR(
            backend.get_device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info,
            &num_triangles,
            &size_info);

        const auto as = backend.get_global_allocator()
                               .create_acceleration_structure(
                                   static_cast<uint32_t>(size_info.accelerationStructureSize),
                                   VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);

        as->scratch_buffer_size = eastl::max(size_info.buildScratchSize, size_info.updateScratchSize);
        as->num_triangles = num_triangles;

        backend.get_blas_build_queue().enqueue(as, geometry, build_info);

        return as;
    }

    AccelerationStructureHandle RaytracingScene::update_blas(const VkDeviceAddress vertices, const uint32_t num_vertices,
        const VkDeviceAddress indices, const uint num_triangles, const AccelerationStructureHandle as
        ) {
          ZoneScoped;

        const auto& backend = RenderBackend::get();

        const auto geometry = VkAccelerationStructureGeometryKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry = {
                .triangles = {
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                    .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                    .vertexData = {.deviceAddress = vertices},
                    .vertexStride = sizeof(glm::vec3),
                    .maxVertex = num_vertices - 1,
                    .indexType = VK_INDEX_TYPE_UINT32,
                    .indexData = {.deviceAddress = indices}
                }
            },
        };

          constexpr VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
                                                                 VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

        const auto build_info = VkAccelerationStructureBuildGeometryInfoKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = flags,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
            .srcAccelerationStructure = as->acceleration_structure,
            .dstAccelerationStructure = as->acceleration_structure,
            .geometryCount = 1,
            .pGeometries = &geometry,
        };
        auto size_info = VkAccelerationStructureBuildSizesInfoKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
        };
        vkGetAccelerationStructureBuildSizesKHR(
            backend.get_device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info,
            &num_triangles,
            &size_info);

        as->scratch_buffer_size = eastl::max(size_info.buildScratchSize, size_info.updateScratchSize);
        as->num_triangles = num_triangles;

        backend.get_blas_build_queue().enqueue(as, geometry, build_info);

        return as;
    }

    void RaytracingScene::commit_tlas_builds(RenderGraph& graph) {
        if (!is_dirty) {
            return;
        }

        ZoneScoped;

        const auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        const auto instances_buffer = allocator.create_buffer(
            "RT instances buffer",
            sizeof(VkAccelerationStructureInstanceKHR) * active_blases.size(),
            BufferUsage::StagingBuffer);
        allocator.destroy_buffer(instances_buffer);

        eastl::vector<VkAccelerationStructureInstanceKHR> instances;
        instances.reserve(active_blases.size());
        for (const auto& index : active_blases) {
            instances.emplace_back(placed_blases[index]);
        }

        auto* write_ptr = allocator.map_buffer(instances_buffer);
        std::memcpy(write_ptr, instances.data(), instances.size() * sizeof(VkAccelerationStructureInstanceKHR));

        graph.add_transition_pass(
            {
                .buffers = {
                    {
                        .buffer = instances_buffer,
                        .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        .access = VK_ACCESS_2_MEMORY_WRITE_BIT
                    }
                }
            });

        // Put the above into a VkAccelerationStructureGeometryKHR. We need to put the instances struct in a union and label it as instance data.
        const auto tlas_geometry = VkAccelerationStructureGeometryKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
            .geometry = {
                .instances = {
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                    .data = {.deviceAddress = instances_buffer->address}
                }
            }
        };

        // Find sizes
        auto build_info = VkAccelerationStructureBuildGeometryInfoKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .geometryCount = 1,
            .pGeometries = &tlas_geometry
        };

        const auto count_instance = static_cast<uint32_t>(instances.size());
        auto size_info = VkAccelerationStructureBuildSizesInfoKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
        };
        vkGetAccelerationStructureBuildSizesKHR(
            backend.get_device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info,
            &count_instance,
            &size_info);

        if(acceleration_structure) {
            allocator.destroy_acceleration_structure(acceleration_structure);
            acceleration_structure = nullptr;
        }
        acceleration_structure = allocator.create_acceleration_structure(
            size_info.accelerationStructureSize,
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);

        const auto scratch_buffer = allocator.create_buffer(
            "TLAS build scratch buffer",
            size_info.buildScratchSize,
            BufferUsage::AccelerationStructure);
        allocator.destroy_buffer(scratch_buffer);

        // Update build information
        build_info.srcAccelerationStructure = VK_NULL_HANDLE;
        build_info.dstAccelerationStructure = acceleration_structure->acceleration_structure;
        build_info.scratchData.deviceAddress = scratch_buffer->address;

        graph.add_pass(
            {
                .name = "Build TLAS",
                .buffers = {
                    {
                        .buffer = instances_buffer,
                        .stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        .access = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
                    },
                    {
                        .buffer = scratch_buffer,
                        .stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        .access = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
                    },
                    {
                        .buffer = acceleration_structure->buffer,
                        .stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        .access = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
                    }
                },
                .execute = [=](const CommandBuffer& commands) {
                // Build Offsets info: n instances
                const VkAccelerationStructureBuildRangeInfoKHR build_offset_info{count_instance, 0, 0, 0};
                const VkAccelerationStructureBuildRangeInfoKHR* p_build_offset_info = &build_offset_info;

                // Build the TLAS
                vkCmdBuildAccelerationStructuresKHR(commands.get_vk_commands(), 1, &build_info, &p_build_offset_info);
            }
            });

        // Make sure the TLAS is ready for anything
        graph.add_transition_pass(
            TransitionPass{
                .buffers = {
                    BufferUsageToken{
                        .buffer = acceleration_structure->buffer,
                        .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        .access = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                    }
                }
            });

        is_dirty = false;
    }

    size_t RaytracingScene::get_next_blas_index() {
        if (!inactive_blases.empty()) {
            const auto value = inactive_blases.back();
            inactive_blases.pop_back();
            return value;
        }
        else {
            return placed_blases.size();
        }
    }
}

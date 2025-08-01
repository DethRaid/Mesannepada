#include "blas_build_queue.hpp"

#include <tracy/Tracy.hpp>

#include "command_buffer.hpp"
#include "render_backend.hpp"
#include "render/backend/render_graph.hpp"
#include "console/cvars.hpp"

namespace render {
    static auto cvar_max_concurrent_builds = AutoCVar_Int{
        "r.RHI.BlasBuildBatchSize",
        "Size of each batch of BLAS builds. Larger builds allow more overlap on the GPU, but use more memory", 8
    };

    BlasBuildQueue::BlasBuildQueue() {
        pending_jobs.reserve(128);
    }

    void BlasBuildQueue::enqueue(AccelerationStructureHandle blas, const VkAccelerationStructureGeometryKHR& create_info,
                                 const VkAccelerationStructureBuildGeometryInfoKHR& build_info) {
        pending_jobs.emplace_back(blas, create_info, build_info);
    }

    void BlasBuildQueue::flush_pending_builds(RenderGraph& graph) {
        ZoneScoped;

        if (pending_jobs.empty()) {
            return;
        }

        graph.begin_label("BLAS builds");

        uint64_t max_scratch_buffer_size = 0;
        for (const auto& job : pending_jobs) {
            max_scratch_buffer_size = std::max(max_scratch_buffer_size, job.handle->scratch_buffer_size);
        }

        const auto batch_size = cvar_max_concurrent_builds.get();

        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();
        const auto scratch_buffer = allocator.create_buffer(
            "Scratch buffer",
            max_scratch_buffer_size * batch_size,
            BufferUsage::StorageBuffer);

        allocator.destroy_buffer(scratch_buffer);

        for (auto i = 0u; i < pending_jobs.size(); i += batch_size) {
            auto barriers = BufferUsageList{
                {
                    .buffer = scratch_buffer,
                    .stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    .access = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
                }
            };

            barriers.reserve(batch_size);

            auto build_geometry_infos = eastl::vector<VkAccelerationStructureBuildGeometryInfoKHR>{};
            auto build_range_infos = eastl::vector<VkAccelerationStructureBuildRangeInfoKHR>{};
            auto build_range_info_ptrs = eastl::vector<VkAccelerationStructureBuildRangeInfoKHR*>{};
            build_geometry_infos.reserve(batch_size);
            build_range_infos.reserve(batch_size);
            build_range_info_ptrs.reserve(batch_size);

            auto scratch_buffer_address = scratch_buffer->address;

            for (auto job_idx = i; job_idx < i + batch_size; job_idx++) {
                if (job_idx >= pending_jobs.size()) {
                    break;
                }

                auto& job = pending_jobs[job_idx];

                barriers.emplace_back(
                    BufferUsageToken{
                        .buffer = pending_jobs[job_idx].handle->buffer,
                        .stage = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        .access = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
                    });

                auto& info = job.build_info;
                info.dstAccelerationStructure = job.handle->acceleration_structure;
                info.pGeometries = &job.create_info;
                info.scratchData = {.deviceAddress = scratch_buffer_address};
                build_geometry_infos.emplace_back(info);

                scratch_buffer_address += job.handle->scratch_buffer_size;

                build_range_infos.emplace_back(
                    VkAccelerationStructureBuildRangeInfoKHR{
                        .primitiveCount = job.handle->num_triangles,
                        .primitiveOffset = 0,
                        .firstVertex = 0,
                        .transformOffset = 0,
                    });
                build_range_info_ptrs.emplace_back(&build_range_infos.back());
            }

            graph.add_pass(
                {
                    .name = "BLAS builds",
                    .buffers = barriers,
                    .execute = [
                        &backend,
                        build_geometry_infos = eastl::move(build_geometry_infos),
                        build_range_infos = eastl::move(build_range_infos),
                        build_range_info_ptrs = eastl::move(build_range_info_ptrs)]
                (const CommandBuffer& commands) {
                        TracyVkZone(backend.get_tracy_context(), commands.get_vk_commands(), "BLAS Build");

                        commands.build_acceleration_structures(build_geometry_infos, build_range_info_ptrs);
                    }
                });
        }

        graph.end_label();

        pending_jobs.clear();
    }
}

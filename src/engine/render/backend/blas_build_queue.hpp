#pragma once

#include <EASTL/vector.h>

#include "render/backend/acceleration_structure.hpp"

namespace render {
    class RenderGraph;

    struct BlasBuildJob {
        AccelerationStructureHandle handle;
        VkAccelerationStructureGeometryKHR create_info;
        VkAccelerationStructureBuildGeometryInfoKHR build_info;
    };

    class BlasBuildQueue {
    public:
        explicit BlasBuildQueue();

        void enqueue(AccelerationStructureHandle blas, const VkAccelerationStructureGeometryKHR& create_info,
            const VkAccelerationStructureBuildGeometryInfoKHR& build_info);

        void flush_pending_builds(RenderGraph& graph);

    private:
        eastl::vector<BlasBuildJob> pending_jobs;
    };
}

#pragma once

#include "render/mesh_primitive_proxy.hpp"

namespace render {
    class RenderGraph;
    class RenderScene;

    class RaytracingScene {
    public:
        explicit RaytracingScene(RenderScene& scene_in);

        void add_primitive(StaticMeshPrimitiveProxyHandle primitive);

        void update_primitive(StaticMeshPrimitiveProxyHandle primitive);

        void remove_primitive(StaticMeshPrimitiveProxyHandle primitive);

        /**
         * \brief Make the raytracing scene ready for raytracing by making sure that all raytraing acceleration structure
         * changes are submitted to the GPU
         *
         * This is basically a barrier from raytracing acceleration structure build commands submit -> raytracing
         * acceleration structures available for raytracing
         */
        void finalize(RenderGraph& graph);

        AccelerationStructureHandle get_acceleration_structure() const;

        void set_dirty();

    private:
        RenderScene& scene;

        /**
         * List of all the placed blasses from all primitives
         */
        eastl::vector<VkAccelerationStructureInstanceKHR> placed_blases;
        /**
         * List of the blases that are active and should be added to the tlas
         */
        eastl::vector<size_t> active_blases;

        /**
         * List of blases which are inactive, and thus may be reused
         */
        eastl::vector<size_t> inactive_blases;

        bool is_dirty = false;
        AccelerationStructureHandle acceleration_structure = {};

        /**
         * \brief Finishes the raytracing scene by committing pending TLAS builds. Called by finalize()
         */
        void commit_tlas_builds(RenderGraph& graph);

        size_t get_next_blas_index();
    };
}

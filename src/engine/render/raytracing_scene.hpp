#pragma once

#include "proxies/mesh_primitive_proxy.hpp"

namespace render {
    class RenderGraph;
    class RenderWorld;

    class RaytracingScene {
    public:
        explicit RaytracingScene(RenderWorld& world_in);

        void add_primitive(MeshPrimitiveProxyHandle primitive);

        void update_primitive(MeshPrimitiveProxyHandle primitive);

        void remove_primitive(MeshPrimitiveProxyHandle primitive);

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

        static AccelerationStructureHandle create_blas(
            VkDeviceAddress vertices, uint32_t num_vertices, VkDeviceAddress indices, uint num_triangles,
            bool is_dynamic
            );

        static AccelerationStructureHandle update_blas(
            VkDeviceAddress vertices, uint32_t num_vertices, VkDeviceAddress indices, uint num_triangles,
            AccelerationStructureHandle as
            );

    private:
        RenderWorld& world;

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

        bool is_dirty = true;
        AccelerationStructureHandle acceleration_structure = {};

        /**
         * \brief Finishes the raytracing scene by committing pending TLAS builds. Called by finalize()
         */
        void commit_tlas_builds(RenderGraph& graph);

        size_t get_next_blas_index();
    };
}

#pragma once

#include <plf_colony.h>
#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRenderer.h>
#endif

#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <entt/entity/entity.hpp>

#include "shared/prelude.h"
#include "physics/physics_body.hpp"
#include "physics/broadphase_layer_implementation.hpp"

class World;

namespace physics {
    struct MeshBodyCreateInfo {
        eastl::vector<float3> vertices;

        eastl::vector<uint32_t> indices;

        bool is_convex_hull;

        float static_friction;

        float restitution;

        float gravity_factor;

        bool is_static;
    };

    /**
     * Scene that holds physics-relevant information. Ticking this scene updates the physics simulation
     */
    class PhysicsWorld {
    public:
        PhysicsWorld(World& world);

        ~PhysicsWorld();

        void finalize();

        void tick(float delta_time, World& world);

        bool cast_ray(const JPH::RRayCast& ray, JPH::RayCastResult& result) const;

        JPH::BodyInterface& get_body_interface() const;

        JPH::PhysicsSystem* get_physics_system() const;

        JPH::TempAllocator& get_temp_allocator() const;

    private:
        eastl::unique_ptr<JPH::TempAllocator> temp_allocator;

        eastl::unique_ptr<JPH::JobSystemThreadPool> thread_pool;

        BpLayerInterfaceImpl broad_phase_layer_interface;

        ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter;

        ObjectLayerPairFilterImpl object_vs_object_layer_filter;

        eastl::unique_ptr<JPH::PhysicsSystem> physics_system;

#ifdef JPH_DEBUG_RENDERER
        using ShapeToGeometryMap = JPH::UnorderedMap<JPH::RefConst<JPH::Shape>, JPH::DebugRenderer::GeometryRef>;

        ShapeToGeometryMap cached_shape_to_geometry;
#endif

        void debug_draw_physics();

        void on_transform_update(const entt::registry& registry, entt::entity entity) const;
    };
}

#include "physics_scene.hpp"

#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <tracy/Tracy.hpp>

#include "collider_component.hpp"
#include "core/engine.hpp"
#include "core/system_interface.hpp"
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaleHelpers.h>

#include "core/glm_jph_conversions.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "Jolt/Physics/Body/BodyActivationListener.h"
#include "scene/scene.hpp"
#include "scene/transform_component.hpp"

namespace physics {
    static std::shared_ptr<spdlog::logger> logger;

    // This is the max amount of rigid bodies that you can add to the physics system. If you try to add more you'll get an error.
    constexpr uint32_t MAX_BODIES = 65536;

    // This determines how many mutexes to allocate to protect rigid bodies from concurrent access. Set it to 0 for the default settings.
    constexpr uint32_t NUM_BODY_MUTEXES = 0;

    // This is the max amount of body pairs that can be queued at any time (the broad phase will detect overlapping
    // body pairs based on their bounding boxes and will insert them into a queue for the narrowphase). If you make this buffer
    // too small the queue will fill up and the broad phase jobs will start to do narrow phase work. This is slightly less efficient.
    constexpr uint32_t MAX_BODY_PAIRS = 65536;

    // This is the maximum size of the contact constraint buffer. If more contacts (collisions between bodies) are detected than this
    // number then these contacts will be ignored and bodies will start interpenetrating / fall through the world.
    constexpr uint32_t MAX_CONTACT_CONSTRAINTS = 10240;

    static void trace_impl(const char* fmt, ...) {
        // Format the message
        va_list list;
        va_start(list, fmt);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, list);
        va_end(list);

        logger->info(buffer);
    }

    PhysicsWorld::PhysicsWorld(World& world) {
        ZoneScoped;

        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("PhysicsScene");
        }

        JPH::RegisterDefaultAllocator();

        JPH::Trace = trace_impl;

        JPH::Factory::sInstance = new JPH::Factory();

        JPH::RegisterTypes();

        // Pre-allocate memory for the physics update
        temp_allocator = eastl::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        // Jolt needs a thread pool. This should be fine? But we'll want to use our own thread pool if we ever make one
        thread_pool = eastl::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            std::thread::hardware_concurrency() - 1);

        physics_system = eastl::make_unique<JPH::PhysicsSystem>();
        physics_system->Init(
            MAX_BODIES,
            NUM_BODY_MUTEXES,
            MAX_BODY_PAIRS,
            MAX_CONTACT_CONSTRAINTS,
            broad_phase_layer_interface,
            object_vs_broadphase_layer_filter,
            object_vs_object_layer_filter);

        // TODO: Set a contact listener? I think we want one?

        // Register transform change listeners
        auto& registry = world.get_registry();
        registry.on_update<TransformComponent>().connect<&PhysicsWorld::on_transform_update>(this);
    }

    PhysicsWorld::~PhysicsWorld() {
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    void PhysicsWorld::finalize() {
        physics_system->OptimizeBroadPhase();
    }

    void PhysicsWorld::tick(const float delta_time, World& world) {
        ZoneScoped;

        physics_system->Update(delta_time, 1, temp_allocator.get(), thread_pool.get());

        auto& body_interface = get_body_interface();

        // Sync physics -> transforms
        auto& registry = world.get_registry();
        registry.view<TransformComponent, CollisionComponent>().each(
            [&](entt::entity entity, const TransformComponent& transform, const CollisionComponent& collision) {
                const auto layer = body_interface.GetObjectLayer(collision.body_id);
                if(layer != layers::MOVING) {
                    return;
                }

                JPH::Vec3 position;
                JPH::Quat orientation;
                body_interface.GetPositionAndRotation(collision.body_id, position, orientation);

                auto new_matrix = glm::translate(float4x4{1.f}, to_glm(position)) *
                    glm::mat4_cast(to_glm(orientation));
                const auto inverse_parent = glm::inverse(transform.cached_parent_to_world);
                registry.patch<TransformComponent>(
                    entity,
                    [&](TransformComponent& trans) {
                        trans.set_local_transform(inverse_parent * new_matrix);
                    });

            });

        const auto visualizer = Engine::get().get_renderer().get_active_visualizer();
        if(visualizer == render::RenderVisualization::Physics) {
            debug_draw_physics();
        }
    }

    bool PhysicsWorld::cast_ray(const JPH::RRayCast& ray, JPH::RayCastResult& result) const {
        return physics_system->GetNarrowPhaseQuery().CastRay(ray, result);
    }

    JPH::BodyInterface& PhysicsWorld::get_body_interface() const {
        return physics_system->GetBodyInterface();
    }

    JPH::PhysicsSystem* PhysicsWorld::get_physics_system() const {
        return physics_system.get();
    }

    JPH::TempAllocator& PhysicsWorld::get_temp_allocator() const {
        return *temp_allocator;
    }

    // from https://github.com/jrouwe/JoltPhysics/blob/master/Samples/SamplesApp.cpp#L2367C2-L2501C50
    void PhysicsWorld::debug_draw_physics() {
#ifdef JPH_DEBUG_RENDERER
        ShapeToGeometryMap shape_to_geometry;

        ZoneScoped;

        auto* debug_renderer = JPH::DebugRenderer::sInstance;

        // Iterate through all active bodies
        JPH::BodyIDVector bodies;
        physics_system->GetBodies(bodies);
        const JPH::BodyLockInterface& bli = physics_system->GetBodyLockInterface();
        for(JPH::BodyID b : bodies) {
            // Get the body
            JPH::BodyLockRead lock(bli, b);
            if(lock.SucceededAndIsInBroadPhase()) {
                // Collect all leaf shapes for the body and their transforms
                const JPH::Body& body = lock.GetBody();
                JPH::AllHitCollisionCollector<JPH::TransformedShapeCollector> collector;
                body.GetTransformedShape().CollectTransformedShapes(body.GetWorldSpaceBounds(), collector);

                // Draw all leaf shapes
                for(const JPH::TransformedShape& transformed_shape : collector.mHits) {
                    JPH::DebugRenderer::GeometryRef geometry;

                    // Find geometry from previous frame
                    ShapeToGeometryMap::iterator map_iterator = cached_shape_to_geometry.find(transformed_shape.mShape);
                    if(map_iterator != cached_shape_to_geometry.end()) {
                        geometry = map_iterator->second;
                    }

                    if(geometry == nullptr) {
                        // Find geometry from this frame
                        map_iterator = shape_to_geometry.find(transformed_shape.mShape);
                        if(map_iterator != shape_to_geometry.end())
                            geometry = map_iterator->second;
                    }

                    if(geometry == nullptr) {
                        // Geometry not cached
                        JPH::Array<JPH::DebugRenderer::Triangle> triangles;

                        // Start iterating all triangles of the shape
                        JPH::Shape::GetTrianglesContext context;
                        transformed_shape.mShape->GetTrianglesStart(
                            context,
                            JPH::AABox::sBiggest(),
                            JPH::Vec3::sZero(),
                            JPH::Quat::sIdentity(),
                            JPH::Vec3::sOne());
                        for(;;) {
                            // Get the next batch of vertices
                            constexpr int cMaxTriangles = 1000;
                            JPH::Float3 vertices[3 * cMaxTriangles];
                            int triangle_count = transformed_shape.mShape->GetTrianglesNext(
                                context,
                                cMaxTriangles,
                                vertices);
                            if(triangle_count == 0)
                                break;

                            // Allocate space for triangles
                            size_t output_index = triangles.size();
                            triangles.resize(triangles.size() + triangle_count);
                            JPH::DebugRenderer::Triangle* triangle = &triangles[output_index];

                            // Convert to a renderable triangle
                            for(int vertex = 0, vertex_max = 3 * triangle_count; vertex < vertex_max; vertex += 3, ++
                                triangle) {
                                // Get the vertices
                                JPH::Vec3 v1(vertices[vertex + 0]);
                                JPH::Vec3 v2(vertices[vertex + 1]);
                                JPH::Vec3 v3(vertices[vertex + 2]);

                                // Calculate the normal
                                JPH::Float3 normal;
                                (v2 - v1).Cross(v3 - v1).NormalizedOr(JPH::Vec3::sZero()).StoreFloat3(&normal);

                                v1.StoreFloat3(&triangle->mV[0].mPosition);
                                triangle->mV[0].mNormal = normal;
                                triangle->mV[0].mColor = JPH::Color::sWhite;
                                triangle->mV[0].mUV = JPH::Float2(0, 0);

                                v2.StoreFloat3(&triangle->mV[1].mPosition);
                                triangle->mV[1].mNormal = normal;
                                triangle->mV[1].mColor = JPH::Color::sWhite;
                                triangle->mV[1].mUV = JPH::Float2(0, 0);

                                v3.StoreFloat3(&triangle->mV[2].mPosition);
                                triangle->mV[2].mNormal = normal;
                                triangle->mV[2].mColor = JPH::Color::sWhite;
                                triangle->mV[2].mUV = JPH::Float2(0, 0);
                            }
                        }

                        // Convert to geometry
                        geometry = new JPH::DebugRenderer::Geometry(
                            debug_renderer->CreateTriangleBatch(triangles),
                            transformed_shape.mShape->GetLocalBounds());
                    }

                    // Ensure that we cache the geometry for next frame
                    // Don't cache soft bodies as their shape changes every frame
                    if(!body.IsSoftBody())
                        shape_to_geometry[transformed_shape.mShape] = geometry;

                    // Determine color
                    JPH::Color color;
                    switch(body.GetMotionType()) {
                    case JPH::EMotionType::Static:
                        //color = JPH::Color::sGrey;
                        color = JPH::Color::sGetDistinctColor(body.GetID().GetIndex());
                        break;

                    case JPH::EMotionType::Kinematic:
                        color = JPH::Color::sGreen;
                        break;

                    case JPH::EMotionType::Dynamic:
                        color = JPH::Color::sGetDistinctColor(body.GetID().GetIndex());
                        break;

                    default:
                        JPH_ASSERT(false);
                        color = JPH::Color::sBlack;
                        break;
                    }

                    // Draw the geometry
                    JPH::Vec3 scale = transformed_shape.GetShapeScale();
                    bool inside_out = JPH::ScaleHelpers::IsInsideOut(scale);
                    JPH::RMat44 matrix = transformed_shape.GetCenterOfMassTransform().PreScaled(scale);
                    debug_renderer->DrawGeometry(
                        matrix,
                        color,
                        geometry,
                        inside_out
                        ? JPH::DebugRenderer::ECullMode::CullFrontFace
                        : JPH::DebugRenderer::ECullMode::CullBackFace,
                        JPH::DebugRenderer::ECastShadow::On,
                        body.IsSensor()
                        ? JPH::DebugRenderer::EDrawMode::Wireframe
                        : JPH::DebugRenderer::EDrawMode::Solid);
                }
            }
        }

        // Replace the map with the newly created map so that shapes that we don't draw / were removed are released
        cached_shape_to_geometry = eastl::move(shape_to_geometry);
#endif
    }

    void PhysicsWorld::on_transform_update(const entt::registry& registry, const entt::entity entity) const {
        if(registry.all_of<TransformComponent, CollisionComponent>(entity)) {
            const auto& transform = registry.get<TransformComponent>(entity);
            const auto& collision = registry.get<CollisionComponent>(entity);

            auto& body_interface = get_body_interface();

            const auto matrix = transform.get_local_to_world();

            glm::vec3 scale;
            glm::quat orientation;
            glm::vec3 translation;
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(matrix, scale, orientation, translation, skew, perspective);

            body_interface.SetPositionAndRotation(
                collision.body_id,
                to_jolt(translation),
                to_jolt(orientation),
                JPH::EActivation::Activate);
        }
    }
}

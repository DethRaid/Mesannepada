#include "reflection_subsystem.hpp"

#include "ai/behavior_tree_component.hpp"
#include "ai/waypoint_component.hpp"
#include "animation/animator_component.hpp"
#include "behavior/game_object.hpp"
#include "core/system_interface.hpp"
#include "physics/collider_component.hpp"
#include "reflection/editor_ui.hpp"
#include "reflection/reflection_macros.hpp"
#include "render/components/light_component.hpp"
#include "resources/model_components.hpp"
#include "scene/camera_component.hpp"
#include "scene/entity_info_component.hpp"
#include "scene/game_object_component.hpp"
#include "scene/transform_component.hpp"

using namespace entt::literals;

namespace reflection {
    static std::shared_ptr<spdlog::logger> logger;

    void ReflectionSubsystem::register_types() {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("ReflectionSubsystem");
        }

        // Reset the system (just in case)
        entt::meta_reset();

        // Register our basic types
        REFLECT_SCALAR(bool);
        REFLECT_SCALAR(float);
        REFLECT_SCALAR(double);
        REFLECT_SCALAR(int8_t);
        REFLECT_SCALAR(uint8_t);
        REFLECT_SCALAR(int16_t);
        REFLECT_SCALAR(uint16_t);
        REFLECT_SCALAR(int32_t);
        REFLECT_SCALAR(uint32_t);

        entt::meta_factory<float3>()
            .func<&editor_write_float3>("editor_write"_hs)
            .func<&editor_read_float3>("editor_read"_hs)
            TRAITS(Trivial)
            DATA(float3, x)
            DATA(float3, y)
            DATA(float3, z);
        entt::meta_factory<glm::quat>()
            .func<&editor_write_quat>("editor_write"_hs)
            .func<&editor_read_quat>("editor_read"_hs)
            TRAITS(Trivial)
            DATA(glm::quat, x)
            DATA(glm::quat, y)
            DATA(glm::quat, z)
            DATA(glm::quat, w);

        entt::meta_factory<eastl::string>()
            .func<&editor_write_string>("editor_write"_hs)
            .func<&editor_read_string>("editor_read"_hs);

        entt::meta_factory<entt::entity>()
            .func<&editor_write_entity>("editor_write"_hs)
            .func<&editor_read_entity>("editor_read"_hs)
            TRAITS(Trivial);

        entt::meta_factory<entt::handle>()
            .func<&editor_write_handle>("editor_write"_hs)
            .func<&editor_read_handle>("editor_read"_hs)
            TRAITS(Trivial);

        entt::meta_factory<JPH::BodyID>()
            TRAITS(EditorReadOnly);

        entt::meta_factory<eastl::fixed_vector<entt::entity, 16> >();

        // And our component types
        REFLECT_COMPONENT(TransformComponent)
            DATA(TransformComponent, location)
            DATA(TransformComponent, rotation)
            DATA(TransformComponent, scale)
            DATA(TransformComponent, children);

        REFLECT_COMPONENT(ai::BehaviorTreeComponent);

        REFLECT_COMPONENT(ai::WaypointComponent)
            DATA(ai::WaypointComponent, next_waypoint);

        REFLECT_COMPONENT(NodeAnimationComponent);

        REFLECT_COMPONENT(SkeletalAnimatorComponent);

        REFLECT_COMPONENT(physics::CollisionComponent)
            DATA(physics::CollisionComponent, body_id);

        REFLECT_COMPONENT(render::PointLightComponent)
            DATA(render::PointLightComponent, color)
            DATA(render::PointLightComponent, range)
            DATA(render::PointLightComponent, size);

        REFLECT_COMPONENT(render::SpotLightComponent)
            DATA(render::SpotLightComponent, color)
            DATA(render::SpotLightComponent, range)
            DATA(render::SpotLightComponent, size)
            DATA(render::SpotLightComponent, inner_cone_angle)
            DATA(render::SpotLightComponent, outer_cone_angle);

        REFLECT_COMPONENT(render::DirectionalLightComponent)
            DATA(render::DirectionalLightComponent, color);

        REFLECT_COMPONENT(ImportedModelComponent)
            DATA(ImportedModelComponent, node_to_entity);

        REFLECT_COMPONENT(CameraComponent);

        REFLECT_COMPONENT(GameObjectComponent)
            DATA(GameObjectComponent, game_object);

        REFLECT_COMPONENT(EntityInfoComponent)
            DATA(EntityInfoComponent, name);
    }
}

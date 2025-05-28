#include "first_person_player.hpp"

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include "core/engine.hpp"
#include "core/glm_jph_conversions.hpp"
#include "scene/camera_component.hpp"
#include "scene/transform_component.hpp"

FirstPersonPlayer::FirstPersonPlayer(const entt::handle entity) : GameObject{entity} {
    name = "FirstPersonPlayer";
    auto& engine = Engine::get();
    auto& scene = engine.get_scene();

    standing_shape = JPH::RotatedTranslatedShapeSettings(
        JPH::Vec3(0, 0.5f * player_height_standing + player_radius_standing, 0),
        JPH::Quat::sIdentity(),
        new JPH::CapsuleShape(0.5f * player_height_standing, player_radius_standing)).Create().Get();
    crouching_shape = JPH::RotatedTranslatedShapeSettings(
        JPH::Vec3(0, 0.5f * player_height_crouching + player_radius_crouching, 0),
        JPH::Quat::sIdentity(),
        new JPH::CapsuleShape(0.5f * player_height_crouching, player_radius_crouching)).Create().Get();

    auto* physics_system = engine.get_physics_scene().get_physics_system();
    JPH::Ref player_settings = new JPH::CharacterVirtualSettings{};
    player_settings->mShape = standing_shape;
    player_settings->mSupportingVolume = JPH::Plane{
        JPH::Vec3::sAxisY(), -0.5f * player_height_standing - player_radius_standing
    };
    character = new JPH::CharacterVirtual(
        player_settings,
        JPH::RVec3::sZero(),
        JPH::Quat::sIdentity(),
        0,
        physics_system);

    // TODO: Set up collision handling
    // mCharacter->SetCharacterVsCharacterCollision(&mCharacterVsCharacterCollision);
    // mCharacterVsCharacterCollision.Add(mCharacter);
    // 
    // // Install contact listener for all characters
    // for (JPH::CharacterVirtual* character : mCharacterVsCharacterCollision.mCharacters)
    //     character->SetListener(this);

    // Player node hierarchy
    // We need an entity for the head pivot, an entity for the camera, an entity for the arms, and an entity for the hold item target
    // Might need other entities idk

    head_pivot_entity = scene.create_entity();
    head_pivot_entity.emplace<TransformComponent>(
        TransformComponent{
            .local_to_parent = glm::translate(float4x4{1.f}, float3{0, rig_height_standing, 0}),
        });
    scene.parent_entity_to_entity(head_pivot_entity, root_entity);

    auto camera_entity = scene.create_entity();
    camera_entity.emplace<TransformComponent>(
        TransformComponent{
            .local_to_parent = glm::translate(float4x4{1.f}, float3{0, 0.1, 0.05}),
        });
    camera_entity.emplace<CameraComponent>();
    scene.parent_entity_to_entity(camera_entity, head_pivot_entity);

    hold_target = scene.create_entity();
    hold_target.emplace<TransformComponent>(
        TransformComponent{
            .local_to_parent = glm::translate(float4x4{1.f}, float3{-0.411, -0.451, 1.415}),
        });
    scene.parent_entity_to_entity(hold_target, root_entity);

    engine.add_model_to_scene("data/game/SM_PlayerArms.glb", root_entity);
}

void FirstPersonPlayer::set_worldspace_location(const float3 location_in) const {
    character->SetPosition(to_jolt(location_in));
}

void FirstPersonPlayer::set_pitch_and_yaw(const float pitch_in, const float yaw_in) {
    pitch = pitch_in;
    yaw = yaw_in;
}

// based on https://github.com/jrouwe/JoltPhysics/blob/master/Samples/Tests/Character/CharacterVirtualTest.cpp
void FirstPersonPlayer::handle_input(
    const float delta_time, const float3 player_movement_input, const float delta_pitch, const float delta_yaw,
    const bool jump
) {
    const auto player_controls_horizontal_velocity = control_movement_during_jump || character->IsSupported();
    if(player_controls_horizontal_velocity) {
        // Smooth the player input
        desired_velocity = enable_character_inertia
            ? 0.25f * to_jolt(player_movement_input) * player_movement_speed + 0.75f * desired_velocity
            : to_jolt(player_movement_input) * player_movement_speed;

        // True if the player intended to move
        allow_sliding = length(player_movement_input) >= eastl::numeric_limits<float>::epsilon();
    } else {
        // While in air we allow sliding
        allow_sliding = true;
    }

    yaw += delta_yaw * player_rotation_speed * delta_time;

    const auto character_rotation = JPH::Quat::sEulerAngles({0, yaw, 0});
    character->SetRotation(character_rotation);

    character->UpdateGroundVelocity();
    const auto current_vertical_velocity = character->GetLinearVelocity().Dot(character->GetUp()) * character->GetUp();
    const auto ground_velocity = character->GetGroundVelocity();
    JPH::Vec3 new_velocity;

    const auto moving_towards_ground = (current_vertical_velocity.GetY() - ground_velocity.GetY()) < 0.1f;
    if(character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround &&
        enable_character_inertia
        ? moving_towards_ground
        : !character->IsSlopeTooSteep(character->GetGroundNormal())) {
        // If we're on the ground and moving towards the ground, assume the ground's velocity
        new_velocity = ground_velocity;

        if(jump && moving_towards_ground) {
            new_velocity += jump_speed * character->GetUp();
        }
    } else {
        new_velocity = current_vertical_velocity;
    }

    // Gravity
    const auto& physics = Engine::get().get_physics_scene();
    const auto* physics_system = physics.get_physics_system();
    new_velocity += physics_system->GetGravity() * delta_time;

    if(player_controls_horizontal_velocity) {
        new_velocity += character_rotation * desired_velocity;
    } else {
        // Preserve horizontal velocity
        const auto current_horizontal_velocity = character->GetLinearVelocity() - current_vertical_velocity;
        new_velocity += current_horizontal_velocity;
    }

    character->SetLinearVelocity(new_velocity);

    // Update head rotation
    head_pivot_entity.patch<TransformComponent>(
        [&](TransformComponent& transform) {
            transform.local_to_parent = glm::rotate(
                transform.local_to_parent,
                delta_pitch * player_rotation_speed * delta_time,
                float3{1, 0, 0});
        });

    // TODO: Swap between crouched and standing shapes on crouch input
}

void FirstPersonPlayer::tick(const float delta_time, Scene& scene) {
    GameObject::tick(delta_time, scene);

    // Update character simulation

    const auto& physics = Engine::get().get_physics_scene();
    const auto* physics_system = physics.get_physics_system();

    auto update_settings = JPH::CharacterVirtual::ExtendedUpdateSettings{};
    character->ExtendedUpdate(
        delta_time,
        -character->GetUp() * physics_system->GetGravity().Length(),
        update_settings,
        physics_system->GetDefaultBroadPhaseLayerFilter(physics::layers::MOVING),
        physics_system->GetDefaultLayerFilter(physics::layers::MOVING),
        {},
        {},
        physics.get_temp_allocator()
    );

    // Sync transform so the rest of the system knows about it

    root_entity.patch<TransformComponent>(
        [&](TransformComponent& transform) {
            const auto location = to_glm(character->GetPosition());
            const auto rotation = to_glm(character->GetRotation());
            transform.local_to_parent = glm::translate(float4x4{1.f}, location) * glm::mat4{rotation};
        });
}

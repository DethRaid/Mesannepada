#pragma once

#include <entt/entity/entity.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include "shared/prelude.h"
#include "behavior/game_object.hpp"

struct TransformComponent;
/**
 * A class representing a first-person player
 *
 * The first-person player loads a glTF file describing its camera location, hold target, etc. It sets up the root
 * entity with the physics character controller, adds the player input component, etc
 */
class FirstPersonPlayer : public GameObject {
public:
    FirstPersonPlayer(entt::handle entity);

    void set_worldspace_location(float3 location_in) const;

    void set_pitch_and_yaw(float pitch_in, float yaw_in);

    /**
     * @param delta_time Change in time since the last input was processed
     * @param player_movement_input The movement input, in normalize local space
     * @param delta_pitch Change in pitch
     * @param delta_yaw Change in yaw
     * @param jump If the player pressed jump this frame
     */
    void handle_input(float delta_time, float3 player_movement_input, float delta_pitch, float delta_yaw, bool jump);

    void tick(float delta_time, World& world) override;

private:
#pragma region Configuration
    /**
     * Height of the camera when you stand. Based on me, scaled down from 1.8m to 1.6m
     */
    static constexpr float rig_height_standing = 1.4f;

    /**
     * Height of your eye when you crouch. Will need tuning
     */
    static constexpr float eye_height_crouched = 0.46f;

    // Average person was 1.6 meters, but some were taller https://www.science.org/content/article/skeleton-closet-identified-bones-ancient-ur
    static constexpr float player_height_standing = 1.0f;
    static constexpr float player_radius_standing = 0.3f;

    static constexpr float player_height_crouching = 0.48f;
    static constexpr float player_radius_crouching = 0.3f;

    static constexpr bool control_movement_during_jump = true;

    static constexpr bool enable_character_inertia = true;

    static constexpr float player_movement_speed = 7.f;

    static constexpr float player_rotation_speed = 0.05f;

    static constexpr float jump_speed = 4.f;
#pragma endregion

    /**
     * Velocity we want the player to move at. The actual velocity may be a little more or less than that, depending on
     * inertia
     *
     * I don't think we want inertia?
     */
    JPH::Vec3 desired_velocity = JPH::Vec3::sZero();

    /**
     * Entity that the player's head pivots around
     */
    entt::handle head_pivot_entity;

    /**
     * Entity we use to hold things
     */
    entt::handle hold_target;

    /**
     * Shape to use for the player when they're standing up
     */
    JPH::RefConst<JPH::Shape> standing_shape;

    /**
     * Shape to use for the player when they're crouched
     */
    JPH::RefConst<JPH::Shape> crouching_shape;

    JPH::Ref<JPH::CharacterVirtual> character;

    float pitch = 0;

    float yaw = 0;

    bool allow_sliding = true;
};

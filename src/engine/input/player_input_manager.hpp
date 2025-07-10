#pragma once

#include <EASTL/queue.h>
#include <entt/entity/entity.hpp>

#include "shared/prelude.h"
#include "input/input_event.hpp"
#include "core/system_interface.hpp"

class World;

/**
 * Manages input
 *
 * The general idea: the platform layers will sent input events to this class, then this class will dispatch them to
 * handlers as needed
 */
class PlayerInputManager {
public:
    bool enabled = true;

    PlayerInputManager();

    /**
     * The platform layers call this to send the raw input to the engine
     *
     * This input need not be normalized
     */
    void set_player_movement(const glm::vec3& raw_axis);

    void set_player_rotation(glm::vec2 rotation_in);

    void add_input_event(const InputEvent& event);

    void set_controlled_entity(entt::entity entity);

    void tick(float delta_time, World& world);

private:
    glm::vec3 player_movement_input = glm::vec3{ 0 };

    glm::vec2 player_rotation_input = {};

    eastl::queue<InputEvent> events;

    entt::entity controlled_entity = entt::null;
};


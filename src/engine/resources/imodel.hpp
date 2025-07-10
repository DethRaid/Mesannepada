#pragma once

#include <EASTL/optional.h>
#include <entt/entity/entity.hpp>

#include "animation/animation_system.hpp"
#include "animation/animation_system.hpp"
#include "animation/animation_system.hpp"

class World;

/**
 * Interface for model-like resources, e.g. glTF models and Godot scenes
 */
class IModel {
public:
    virtual ~IModel() = default;

    /**
     * Adds this model to the provided world, optionally parenting it to the provided entity
     */
    virtual entt::handle add_to_world(World& world_in, const eastl::optional<entt::handle>& parent_node) const = 0;
};

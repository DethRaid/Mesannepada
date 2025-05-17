#pragma once

#include <EASTL/optional.h>
#include <entt/entity/entity.hpp>

class Scene;

/**
 * Interface for model-like resources, e.g. glTF models and Godot scenes
 */
class IModel {
public:
    virtual ~IModel() = default;

    /**
     * Adds this model to the provided scene, optionally parenting it to the provided entity
     */
    virtual entt::handle add_to_scene(Scene& scene_in, const eastl::optional<entt::entity>& parent_node) const = 0;
};

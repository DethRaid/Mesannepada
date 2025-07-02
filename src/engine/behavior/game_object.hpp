#pragma once

#include <EASTL/string.h>
#include <entt/entity/registry.hpp>
#include <entt/entity/handle.hpp>

class World;

/**
 * An object in the game
 *
 * These live in components on their root entities
 *
 * The GameObject's constructor is expected to create the GameObject, add it to the scene, set up references to other
 * objects, etc. The spawning code - Application::load_game_object - handles creating the top-level game object
 * component and attaching this object to it
 *
 * I'm not sorry for this class's crimes against ECS
 */
class GameObject {
public:
    eastl::string name = "GameObject";

    bool enabled = true;

    /**
     * Constructs a GameObject. Every GameObject has a TransformComponent
     */
    GameObject(entt::handle root_entity_in);

    virtual ~GameObject() = default;

    virtual void begin_play();

    virtual void tick(float delta_time, World& scene);

protected:

    entt::handle root_entity = {};
};

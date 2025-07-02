#pragma once

#include <entt/entity/entity.hpp>
#include <EASTL/shared_ptr.h>

#include "behavior/game_object.hpp"

class IModel;

/**
 * Behavior class for the environment level
 *
 * Our levels are kinda weird. We have an environment that's always loaded, then we load gameplay-specific things
 * levels as needed - for instance, load all the detail objects for the first level whe you begin a new game
 *
 * This behavior needs to control all the environmental things, such as 
 */
class UrEnvironmentGameObject : public GameObject
{
public:
    UrEnvironmentGameObject(entt::handle entity);

    void tick(float delta_time, World& scene) override;

private:
    entt::handle level_entity;

    entt::handle ur_gltf_entity;

    eastl::shared_ptr<IModel> level_scene;
};

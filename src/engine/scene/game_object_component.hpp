#pragma once

#include <EASTL/shared_ptr.h>

class GameObject;

/**
 * Behavior Components live at the top of an entity tree for a given object in the game. They have a pointer to a
 * Behavior. Behaviors work like actor Blueprints in Unreal - they can define a bunch of methods and receive events,
 * and they can perform interesting tasks in the world
 */
struct GameObjectComponent {
    eastl::shared_ptr<GameObject> game_object;

    GameObject* operator->() const;
};

#pragma once

#include <EASTL/string.h>

/**
 * Component that tells the engine to spawn a gameobject with the same transform as this node. This component is removed
 * after the spawning takes place
 */
struct SpawnGameObjectComponent {
    eastl::string game_object_name;
};

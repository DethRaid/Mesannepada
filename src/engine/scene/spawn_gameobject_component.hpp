#pragma once

#include <EASTL/string.h>

/**
 * Component that tells the engine to spawn a gameobject with the same transform as this node. This component is removed
 * after the spawning takes place
 */
struct SpwanPrefabComponent {
    /**
     * Path to the game object JSON file
     */
    eastl::string prefab_path;
};

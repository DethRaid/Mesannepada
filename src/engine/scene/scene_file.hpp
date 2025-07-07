#pragma once

#include <filesystem>

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <entt/entt.hpp>

#include "serialization/serializers.hpp"
#include "serialization/eastl/string.hpp"
#include "serialization/eastl/unordered_map.hpp"
#include "shared/prelude.h"

class World;

struct SceneObject {
    float3 location;
    glm::quat orientation;
    float3 scale;

    entt::handle entity;
};

/**
 * Represents the data that gets saved/loaded to a file
 *
 * This data contains wonderful information about what objects should be in what places
 */
class SceneFile {
public:
    /**
     * Loads a SceneFile from disk. Does not add any entities to the world
     */
    static SceneFile load_from_file(const std::filesystem::path& filepath);

    /**
     * Writes a SceneFile to disk
     */
    void write_to_file(const std::filesystem::path& filepath);

    /**
     * Adds the objects in this SceneFile to engine's global world
     */
    void add_to_world();

    void add_packaged_model(const std::filesystem::path& model_file, float3 location);

    template<typename Archive>
    void save(Archive& ar) {
        serialization::serialize<true>(ar, entt::meta_any(scene_objects));
    }

    template<typename Archive>
    void load(Archive& ar) {
        serialization::serialize<false>(ar, entt::meta_any(scene_objects));
    }

private:
    /**
     * A map from the filepath an object was loaded from to information about it in the scene
     *
     * This map doesn't contain any actual entity handles until you add the scene file to a world
     */
    eastl::unordered_map<eastl::string, SceneObject> scene_objects;
};

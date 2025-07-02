#pragma once

#include <filesystem>

#include <EASTL/unordered_map.h>
#include <entt/entt.hpp>

#include "shared/prelude.h"

class World;

struct SceneObject {
    float4x4 transform;
    entt::handle entity;

    template<typename Archive>
    void serialize(Archive& ar) {
        ar(transform);
    }
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
    void serialize(Archive& ar) {
        ar(scene_objects);
    }

private:
    /**
     * A map from the filepath an object was loaded from to information about it in the scene
     *
     * This map doesn't contain any actual entity handles until you add the scene file to a world
     */
    eastl::unordered_map<std::string, SceneObject> scene_objects;
};

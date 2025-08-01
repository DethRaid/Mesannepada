#pragma once

#include <filesystem>

#include <EASTL/vector.h>
#include <entt/entt.hpp>
#include <spdlog/logger.h>

#include "resources/resource_path.hpp"
#include "reflection/serialization/serializers.hpp"
#include "reflection/serialization/eastl/vector.hpp"
#include "shared/prelude.h"

class World;

struct SceneObject {
    ResourcePath filepath = {};

    float3 location = {0.f, 0.f, 0.f};
    glm::quat orientation = glm::quat{1.f, 0.f, 0.f, 0.f};
    float3 scale = float3{1.f, 1.f, 1.f};

    entt::handle entity = {};

    template<typename Archive>
    void serialize(Archive& ar) {
        ar(filepath, location, orientation, scale);
    }
};

/**
 * Represents the data that gets saved/loaded to a file
 *
 * This data contains wonderful information about what objects should be in what places
 */
class Scene {
public:
    /**
     * Loads a SceneFile from disk. Does not add any entities to the world
     */
    static Scene load_from_file(const ResourcePath& filepath);

    Scene();

    Scene(const Scene& other) = delete;

    Scene& operator=(const Scene& other) = delete;

    Scene(Scene&& old) noexcept;

    Scene& operator=(Scene&& old) noexcept;

    ~Scene();

    /**
     * Writes a SceneFile to disk
     */
    void write_to_file(const ResourcePath& path);

    /**
     * Adds the objects in this SceneFile to engine's global world
     */
    void add_new_objects_to_world();

    /**
     * Adds an object to the scene, optionally adding it to the world
     *
     * \param filepath Path to the file with data for this scene object
     * \param location Where to place the model in the world
     * \param add_to_world Whether to immediately add the model to the engine's world, or not
     * \return A reference to the newly-added scene object
     */
    SceneObject& add_object(const ResourcePath& filepath, float3 location, bool add_to_world = false);

    const eastl::vector<SceneObject>& get_objects() const;

    template<typename Archive>
    void save(Archive& ar) {
        sync_transforms_from_world();
        logger->info("Saving {} objects", scene_objects.size());
        ar(scene_objects);
        dirty = false;
    }

    template<typename Archive>
    void load(Archive& ar) {
        ar(scene_objects);
        logger->info("Loaded {} objects", scene_objects.size());
        dirty = true;
    }

    /**
     * Tries to find a SceneObject with the given name. Returns nullptr on failure
     */
    SceneObject* find_object(const ResourcePath& name);

    bool is_dirty() const {
        return dirty;
    }

    /**
     * Deletes all the entities that represent the SceneObjects
     */
    void remove_from_world();

private:
    inline static std::shared_ptr<spdlog::logger> logger = {};

    /**
     * Whether or not the scene has been modified since last being saved
     */
    bool dirty = false;

    /**
     * A map from the filepath an object was loaded from to information about it in the scene
     *
     * This map doesn't contain any actual entity handles until you add the scene file to a world
     */
    eastl::vector<SceneObject> scene_objects = {};

    /**
     * Updates the transforms of all SceneObjects with the transform in the ECS
     */
    void sync_transforms_from_world();

    static void add_object_to_world(SceneObject& object);
};

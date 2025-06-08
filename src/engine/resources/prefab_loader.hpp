#pragma once

#include <filesystem>

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/functional.h>
#include <entt/entity/registry.hpp>
#include <EASTL/unordered_map.h>

#include <simdjson.h>
#include "shared/prelude.h"

class Scene;

/**
 * Loads prefabs from disk. ez
 *
 * This class mostly exists so I don't put all my loading code in Scene
 *
 * Prefabs have a root entity. This may be a glTF or Godot model, or it may just be an empty entity. Either way, the
 * prefab file describes which components to add to that root entity
 */
class PrefabLoader {
public:
    using ComponentFactory = eastl::function<void(entt::handle,
                                                  simdjson::simdjson_result<simdjson::ondemand::value>)>;
    /**
     * Loads a prefab from disk and places it into the scene, using the provided transform for its initial position
     *
     * @param prefab_file The JSON file which describes the prefab
     * @param scene The scene to add the prefab to
     * @param transform Initial transformation matrix
     */
    entt::handle load_prefab(const std::filesystem::path& prefab_file, Scene& scene,
                             const float4x4& transform = float4x4{1.f});

    /**
     * Registers a function used to initialize a component from JSON. A basic creator might simply deserialize the JSON
     * into fields directly, a more advanced on might do... well, more advanced things
     *
     * The creator must add the component to the provided entity
     *
     * @param component_name The name of the component type, including any namespace
     * @param creator The factory that creates the component. Must have the signature of ComponentFactory
     * @see ComponentFactory
     */
    void register_component_creator(const eastl::string& component_name, ComponentFactory&& creator);

private:
    eastl::unordered_map<eastl::string, ComponentFactory> component_creators;
};

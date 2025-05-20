#pragma once

#include <EASTL/shared_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/string.h>
#include <fastgltf/core.hpp>

class IModel;

/**
 * Allows one to load all kinds of resources. Caches resources that have already been loaded
 *
 * Uses shared pointers for references to resources
 */
class ResourceLoader {
public:
    ResourceLoader();

    eastl::shared_ptr<IModel> get_model(const std::filesystem::path& model_path);

private:
    fastgltf::Parser parser;

    eastl::unordered_map<eastl::string, eastl::shared_ptr<IModel>> loaded_models;

    /**
     * Loads a glTF model, and returns a pointer to that model
     */
    void load_gltf_model(const std::filesystem::path& model_path);

    void load_godot_scene(const std::filesystem::path& scene_path);
};

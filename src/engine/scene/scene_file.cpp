#include "scene_file.hpp"

#include <fstream>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <tracy/Tracy.hpp>

#include "core/engine.hpp"
#include "scene/transform_component.hpp"

SceneFile SceneFile::load_from_file(const std::filesystem::path& filepath) {
    ZoneScoped;

    auto stream = std::ifstream{filepath, std::ios::binary};
    auto archive = cereal::BinaryInputArchive{stream};

    auto scene = SceneFile{};
    scene.serialize(archive);

    return scene;
}

void SceneFile::write_to_file(const std::filesystem::path& filepath) {
    ZoneScoped;

    auto stream = std::ofstream{filepath, std::ios::binary};
    auto archive = cereal::BinaryOutputArchive{stream};

    serialize(archive);
}

void SceneFile::add_to_world() {
    auto& engine = Engine::get();
    for(auto& [filepath, object] : scene_objects) {
        if(filepath.ends_with("glb")) {
            object.entity = engine.add_model_to_scene(filepath.c_str());
            object.entity.patch<TransformComponent>([&](TransformComponent& transform) {
                transform.set_local_transform(object.transform);
            });
            
        } else if(filepath.ends_with("sprefab")) {
            object.entity = engine.add_prefab_to_scene(filepath.c_str(), object.transform);
        }
    }
}

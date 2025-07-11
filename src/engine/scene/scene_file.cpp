#include "scene_file.hpp"

#include <fstream>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <glm/gtx/transform.hpp>
#include <tracy/Tracy.hpp>

#include "core/engine.hpp"
#include "glm/detail/_noise.hpp"
#include "glm/detail/_noise.hpp"
#include "scene/transform_component.hpp"

Scene Scene::load_from_file(const std::filesystem::path& filepath) {
    ZoneScoped;

    auto stream = std::ifstream{filepath, std::ios::binary};
    auto archive = cereal::BinaryInputArchive{stream};

    auto scene = Scene{};
    serialization::serialize<false>(archive, entt::forward_as_meta(scene));

    return scene;
}

void Scene::write_to_file(const std::filesystem::path& filepath) {
    ZoneScoped;

    auto stream = std::ofstream{filepath, std::ios::binary};
    auto archive = cereal::BinaryOutputArchive{stream};

    serialization::serialize<true>(archive, entt::forward_as_meta(*this));
}

void Scene::add_new_objects_to_world() {
    for(auto& object : scene_objects) {
        add_object_to_world(object);
    }
}

SceneObject* Scene::add_object(const std::filesystem::path& model_file, const float3 location, const bool add_to_world) {
    const auto obj = scene_objects.emplace(SceneObject{
        .filepath = model_file.string().c_str(),
        .location = location
    });

    if(add_to_world) {
        add_object_to_world(*obj);
    }

    return &*obj;
}

const plf::colony<SceneObject>& Scene::get_objects() const {
    return scene_objects;
}

SceneObject* Scene::find_object(eastl::string_view name) {
    if(const auto itr = eastl::find_if(scene_objects.begin(),
                              scene_objects.end(),
                              [&](const auto& obj) {
                                  return obj.filepath == name;
                              }); itr != scene_objects.end()) {
        return &*itr;
    }

    return nullptr;
}

void Scene::add_object_to_world(SceneObject& object) {
    if(object.entity.valid()) {
        return;
    }

    auto& engine = Engine::get();

    const auto filepath = eastl::string_view{object.filepath};
    if(filepath.ends_with(".glb") || filepath.ends_with(".gltf") || filepath.ends_with(".tscn")) {
        object.entity = engine.add_model_to_world(object.filepath.c_str());
        object.entity.patch<TransformComponent>([&](TransformComponent& transform) {
            transform.location = object.location;
            transform.rotation = object.orientation;
            transform.scale = object.scale;
        });

    } else if(filepath.ends_with(".sprefab")) {
        const auto transform_mat = glm::translate(float4x4{1.f}, object.location) *
                                   glm::mat4{object.orientation} *
                                   glm::scale(object.scale);
        object.entity = engine.add_prefab_to_world(object.filepath.c_str(), transform_mat);
    }
}

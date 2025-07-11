#include "scene_file.hpp"

#include <fstream>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <glm/gtx/transform.hpp>
#include <tracy/Tracy.hpp>

#include "core/engine.hpp"
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

void Scene::add_to_world() {
    auto& engine = Engine::get();
    for(auto& object : scene_objects) {
        if(object.entity.valid()) {
            continue;
        }

        if(eastl::string_view{object.filepath}.ends_with("glb")) {
            object.entity = engine.add_model_to_world(object.filepath.c_str());
            object.entity.patch<TransformComponent>([&](TransformComponent& transform) {
                transform.location = object.location;
                transform.rotation = object.orientation;
                transform.scale = object.scale;
            });

        } else if(eastl::string_view{object.filepath}.ends_with("sprefab")) {
            const auto transform_mat = glm::translate(float4x4{1.f}, object.location) *
                                       glm::mat4{object.orientation} *
                                       glm::scale(object.scale);
            object.entity = engine.add_prefab_to_world(object.filepath.c_str(), transform_mat);
        }
    }
}

const eastl::vector<SceneObject>& Scene::get_objects() const {
    return scene_objects;
}

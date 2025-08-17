#include "scene_file.hpp"

#include <fstream>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <glm/gtx/transform.hpp>
#include <spdlog/fmt/std.h>
#include <tracy/Tracy.hpp>

#include "core/engine.hpp"
#include "scene/transform_component.hpp"
#include "reflection/serialization/glm.hpp"

Scene Scene::load_from_file(const ResourcePath& path) {
    ZoneScoped;

    const auto filepath = path.to_filepath();
    auto stream = std::ifstream{filepath, std::ios::binary};
    auto archive = cereal::BinaryInputArchive{stream};

    auto scene = Scene{};
    scene.load(archive);

    return scene;
}

Scene::Scene() {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("Scene");
    }
}

Scene::Scene(Scene&& old) noexcept :
    scene_objects{eastl::move(old.scene_objects)} {
    old.scene_objects.clear();
}

Scene& Scene::operator=(Scene&& old) noexcept {
    this->~Scene();

    scene_objects = eastl::move(old.scene_objects);
    old.scene_objects = {};

    return *this;
}

Scene::~Scene() {
    remove_from_world();
}

void Scene::write_to_file(const ResourcePath& path) {
    ZoneScoped;

    const auto os_path = path.to_filepath();
    if(!std::filesystem::exists(os_path.parent_path())) {
        std::filesystem::create_directories(os_path.parent_path());
    }

    auto stream = std::ofstream{os_path, std::ios::binary};
    auto archive = cereal::BinaryOutputArchive{stream};

    save(archive);
}

void Scene::add_new_objects_to_world() {
    for(auto& object : scene_objects) {
        add_object_to_world(object);
    }
}

SceneObject& Scene::add_object(const ResourcePath& filepath, const float3 location, const bool add_to_world
    ) {
    logger->info("Adding object {} to the scene", filepath);
    auto& obj = scene_objects.emplace_back(SceneObject{
        .filepath = filepath,
        .location = location
    });

    if(add_to_world) {
        add_object_to_world(obj);
    }

    dirty = true;

    return obj;
}

const eastl::vector<SceneObject>& Scene::get_objects() const {
    return scene_objects;
}

void Scene::delete_object_by_entity(const entt::handle entity) {
    const auto itr = eastl::remove_if(
        scene_objects.begin(),
        scene_objects.end(),
        [&](const SceneObject& obj) {
            return obj.entity == entity;
        });
    if(itr != scene_objects.end()) {
        scene_objects.erase(itr);
    }
}

SceneObject* Scene::find_object(const ResourcePath& name) {
    ZoneScoped;

    if(const auto itr = eastl::find_if(scene_objects.begin(),
                                       scene_objects.end(),
                                       [&](const auto& obj) {
                                           return obj.filepath == name;
                                       }); itr != scene_objects.end()) {
        return &*itr;
    }

    return nullptr;
}

void Scene::remove_from_world() {
    ZoneScoped;

    auto& world = Engine::get().get_world();
    for(auto& obj : scene_objects) {
        if(obj.entity) {
            world.destroy_entity(obj.entity);
            obj.entity = {};
        }
    }
}

void Scene::sync_transforms_from_world() {
    for(auto& obj : scene_objects) {
        if(obj.entity) {
            if(const auto* trans = obj.entity.try_get<TransformComponent>()) {
                obj.location = trans->location;
                obj.orientation = trans->rotation;
                obj.scale = trans->scale;
            }
        }
    }
}

void Scene::add_object_to_world(SceneObject& object) {
    ZoneScoped;

    if(object.entity.valid()) {
        return;
    }

    logger->info("Adding object {} to the world", object.filepath);

    auto& engine = Engine::get();

    if(object.filepath.ends_with(".glb")
       || object.filepath.ends_with(".gltf")
       || object.filepath.ends_with(".tscn")) {
        object.entity = engine.add_model_to_world(object.filepath);
        object.entity.patch<TransformComponent>([&](TransformComponent& transform) {
            transform.location = object.location;
            transform.rotation = object.orientation;
            transform.scale = object.scale;
        });

    } else if(object.filepath.ends_with(".sprefab")) {
        const auto transform_mat = glm::translate(float4x4{1.f}, object.location) *
                                   glm::mat4{object.orientation} *
                                   glm::scale(object.scale);
        object.entity = engine.add_prefab_to_world(object.filepath, transform_mat);
    }
}

#include "scene_file.hpp"

#include <fstream>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <glm/gtx/transform.hpp>
#include <spdlog/fmt/std.h>
#include <tracy/Tracy.hpp>

#include "core/engine.hpp"
#include "scene/transform_component.hpp"

static std::shared_ptr<spdlog::logger> logger;

Scene Scene::load_from_file(const std::filesystem::path& filepath) {
    ZoneScoped;

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

Scene::Scene(Scene&& old) noexcept : scene_objects{old.scene_objects} {
    old.scene_objects.clear();
}

Scene& Scene::operator=(Scene&& old) noexcept {
    this->~Scene();

    scene_objects = old.scene_objects;
    old.scene_objects = {};

    return *this;
}

Scene::~Scene() {
    remove_from_world();
}

void Scene::write_to_file(const std::filesystem::path& filepath) {
    ZoneScoped;

    if(!std::filesystem::exists(filepath.parent_path())) {
        std::filesystem::create_directories(filepath.parent_path());
    }

    auto stream = std::ofstream{filepath, std::ios::binary};
    auto archive = cereal::BinaryOutputArchive{stream};

    save(archive);
}

void Scene::add_new_objects_to_world() {
    for(auto& object : scene_objects) {
        add_object_to_world(object);
    }
}

SceneObject& Scene::add_object(const std::filesystem::path& filepath, const float3 location, const bool add_to_world
    ) {
    logger->info("Adding object {} to the scene", filepath);
    auto& obj = scene_objects.emplace_back(SceneObject{
        .filepath = filepath.string().c_str(),
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

SceneObject* Scene::find_object(eastl::string_view name) {
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

void Scene::add_object_to_world(SceneObject& object) {
    ZoneScoped;

    if(object.entity.valid()) {
        return;
    }

    logger->info("Adding object {} to the world", object.filepath.c_str());

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

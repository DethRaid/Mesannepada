#include "prefab_loader.hpp"

#include <simdjson.h>

#include "core/system_interface.hpp"
#include "scene/scene.hpp"
#include "scene/transform_component.hpp"

static std::shared_ptr<spdlog::logger> logger;

entt::handle PrefabLoader::load_prefab(const std::filesystem::path& prefab_file, Scene& scene,
                                       const float4x4& transform) {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("PrefabLoader");
    }

    simdjson::ondemand::parser parser;
    auto json = simdjson::padded_string::load(prefab_file.c_str());
    auto prefab = parser.iterate(json);
    if(prefab.error() != simdjson::error_code::SUCCESS) {
        logger->error("Could not load file {}: {}", prefab_file.c_str(), simdjson::error_message(prefab.error()));
    }

    auto entity = scene.create_entity();

    auto components = prefab["components"];
    for(auto component_definition : components) {
        auto type = component_definition["type"];
        std::string_view type_name_view;
        type.get_string(type_name_view);

        const auto type_name = eastl::string{type_name_view.begin(), type_name_view.size()};

        if(auto itr = component_creators.find(type_name); itr != component_creators.end()) {
            itr->second(entity, component_definition);
        } else {
            logger->warn(
                    "Prefab {} wants a {} component, but no factory was registered for that compoent type! Skippping unknown component",
                    prefab_file.string(), type_name.c_str());
        }
    }

    entity.patch<TransformComponent>([&](auto& transform_comp) {
        transform_comp.local_to_parent = transform;
    });

    scene.add_top_level_entities(eastl::array{entity.entity()});

    return entity;
}

void PrefabLoader::register_component_creator(const eastl::string& component_name, ComponentFactory&& creator) {
    component_creators.emplace(component_name, eastl::forward<ComponentFactory>(creator));
}

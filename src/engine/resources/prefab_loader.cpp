#include "prefab_loader.hpp"

#include <simdjson.h>

#include "core/engine.hpp"
#include "core/system_interface.hpp"
#include "resources/imodel.hpp"
#include "scene/world.hpp"
#include "scene/transform_component.hpp"

static std::shared_ptr<spdlog::logger> logger;

using namespace entt::literals;

entt::handle PrefabLoader::load_prefab(const ResourcePath& prefab_file, World& world,
                                       const float4x4& transform) {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("PrefabLoader");
    }

    simdjson::ondemand::parser parser;
    const auto absolute_prefab_path = prefab_file.to_filepath();
    const auto json = simdjson::padded_string::load(absolute_prefab_path.string());
    auto prefab = parser.iterate(json);
    if(prefab.error() != simdjson::error_code::SUCCESS) {
        logger->error("Could not load file {}: {}", prefab_file, simdjson::error_message(prefab.error()));
    }

    auto& resource_loader = Engine::get().get_resource_loader();

    entt::handle entity;
    std::string_view root_entity_name;
    if(prefab["root_entity"].get_string(root_entity_name) == simdjson::SUCCESS) {
        const auto& model = resource_loader.get_model(ResourcePath{root_entity_name});
        entity = model->add_to_world(world, eastl::nullopt);
    } else {
        entity = world.create_entity();
        entity.emplace<TransformComponent>();
    }

    auto components = prefab["components"];
    for(auto component_definition : components) {
        auto type = component_definition["type"];
        std::string_view type_name_view;
        type.get_string(type_name_view);

        const auto type_name = eastl::string{type_name_view.data(), type_name_view.size()};

        if(auto itr = component_creators.find(type_name); itr != component_creators.end()) {
            itr->second(entity, component_definition);
        } else {
            const auto component_type_id = entt::id_type{entt::hashed_string{type_name.c_str()}};
            auto meta = entt::resolve(component_type_id);
            auto value = meta.construct();
            assert(value && "Component does not have a default constructor!");

            for(auto [id, data] : value.type().data()) {
                // If there's a value in the JSON with the name of one of the component's members, deserialize it
                const auto& custom = data.custom();
                reflection::PropertiesMap properties = {};
                if(auto* mp = static_cast<const reflection::PropertiesMap*>(custom)) {
                    properties = *mp;
                }
                if(const auto it = properties.find("name"_hs); it != properties.end()) {
                    const auto member_name = it->second.cast<const char*>();
                    auto json_member_data = component_definition[member_name];
                    if(json_member_data.error() == simdjson::SUCCESS) {
                     // TODO
                    }
                }
            }

            if(auto emplace_move = meta.func("emplace_move"_hs)) {
                emplace_move.invoke({}, entity.registry(), entity.entity(), value.as_ref());
            }
        }
    }

    entity.patch<TransformComponent>([&](auto& transform_comp) {
        transform_comp.set_local_transform(transform);
    });

    world.add_top_level_entities(eastl::array{entity});

    return entity;
}

void PrefabLoader::register_component_creator(const eastl::string& component_name, ComponentFactory&& creator) {
    component_creators.emplace(component_name, eastl::forward<ComponentFactory>(creator));
}

#include "world.hpp"

#include <simdjson.h>

#include "core/engine.hpp"
#include "tracy/Tracy.hpp"

#include "core/issue_breakpoint.hpp"
#include "core/system_interface.hpp"
#include "scene/entity_info_component.hpp"
#include "scene/transform_component.hpp"

static std::shared_ptr<spdlog::logger> logger;

World::World() {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("World");
    }

    registry.on_update<TransformComponent>().connect<&World::on_transform_update>(this);
}

entt::handle World::make_handle(const entt::entity entity) {
    return entt::handle{registry, entity};
}

entt::handle World::create_entity() {
    return entt::handle{registry, registry.create()};
}

entt::handle World::find_entity(const eastl::string_view name) {
    // Search the top-level entities first, to avoid delving into deep node trees for e.g. skeletons
    for(const auto& entity : top_level_entities) {
        if(const auto* info = registry.try_get<EntityInfoComponent>(entity); info) {
            if(info->name == name) {
                return entt::handle{registry, entity};
            }
        }
    }

    // Search through children
    for(const auto& entity : top_level_entities) {
        if(const auto result = find_child(entt::handle{registry, entity}, name)) {
            return result;
        }
    }

    // Didn't find it? rip
    return {};
}

void World::destroy_entity(const entt::entity entity) {
    if(!registry.valid(entity)) {
        return;
    }

    if(auto* transform = registry.try_get<TransformComponent>(entity)) {
        for(const auto child_entity : transform->children) {
            destroy_entity(child_entity);
        }
    }

    auto& scenes = Engine::get().get_loaded_scenes();
    for(auto& [name, scene] : scenes) {
        scene.delete_object_by_entity(entt::handle{registry, entity});
    }

    top_level_entities.erase(entity);

    registry.destroy(entity);
}

void World::tick(float delta_time) {
}

entt::registry& World::get_registry() {
    return registry;
}

const entt::registry& World::get_registry() const {
    return registry;
}

void World::parent_entity_to_entity(const entt::entity child, const entt::entity parent) {
    if(!registry.valid(child)) {
        logger->error("No child set, unable to parent!");
        return;
    }

    if(!registry.valid(parent)) {
        logger->error("Parent entity not valid, unable to parent!");
        return;
    }

    if(registry.try_get<TransformComponent>(child) == nullptr) {
        throw std::runtime_error{"Child does not have a transform!"};
    }

    registry.patch<TransformComponent>(
        child,
        [&](TransformComponent& transform) {
            transform.parent = parent;
        });

    registry.patch<TransformComponent>(
        parent,
        [&](TransformComponent& transform) {
            transform.children.emplace_back(child);
        });

    top_level_entities.erase(child);
}

void World::add_top_level_entities(const eastl::span<const entt::handle> entities) {
    for(const auto& entity : entities) {
        top_level_entities.insert(entity.entity());
    }
}

const eastl::unordered_set<entt::entity>& World::get_top_level_entities() const {
    return top_level_entities;
}

entt::handle World::find_child(const entt::handle entity, const eastl::string_view child_name) {
    // Search the entity and all its children for a node with the specified name

    if(const auto* transform = entity.try_get<TransformComponent>()) {
        for(const auto& child : transform->children) {
            if(const auto* info = entity.registry()->try_get<EntityInfoComponent>(child)) {
                if(info->name == child_name) {
                    return entt::handle{*entity.registry(), child};
                }
            }
        }

        for(const auto& child : transform->children) {
            if(const auto child_maybe = find_child(entt::handle{*entity.registry(), child}, child_name)) {
                return child_maybe;
            }
        }
    }

    return {};
}

void World::on_transform_update(entt::registry& registry, const entt::entity entity) {
    propagate_transform(entity);
}

void World::propagate_transform(const entt::entity entity) {
    const auto& transform = registry.get<TransformComponent>(entity);
    const auto local_to_world = transform.get_local_to_world();

    eastl::fixed_vector<entt::entity, 4> invalid_entities;
    for(const auto child : transform.children) {
        if(registry.valid(child)) {
            registry.patch<TransformComponent>(
                child,
                [&](TransformComponent& trans) {
                    trans.cached_parent_to_world = local_to_world;
                });
        } else {
            invalid_entities.emplace_back(child);
        }
    }

    if(!invalid_entities.empty()) {
        registry.patch<TransformComponent>(
            entity,
            [&](TransformComponent& transform_to_modify) {
                for(auto invalid_child : invalid_entities) {
                    transform_to_modify.children.erase_first(invalid_child);
                }
            });
    }
}

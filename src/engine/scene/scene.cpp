#include "scene.hpp"

#include <tracy/Tracy.hpp>

#include "simdjson.h"
#include "RmlUi/Core/Transform.h"
#include "ai/behavior_tree_component.hpp"
#include "core/issue_breakpoint.hpp"
#include "scene/transform_component.hpp"
#include "scene/transform_parent_component.hpp"
#include "core/system_interface.hpp"

static std::shared_ptr<spdlog::logger> logger;

World::World() {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("Scene");
    }
}

entt::handle World::create_entity() {
    return entt::handle{registry, registry.create()};
}

void World::destroy_entity(const entt::entity entity) {
    if(auto* transform = registry.try_get<TransformComponent>(entity)) {
        for(const auto child_entity : transform->children) {
            destroy_entity(child_entity);
        }
    }

    registry.destroy(entity);
}

entt::registry& World::get_registry() {
    return registry;
}

const entt::registry& World::get_registry() const {
    return registry;
}

void World::parent_entity_to_entity(const entt::handle child, const entt::handle parent) {
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
            propagate_transform(child, transform.get_local_to_world());
        });

    top_level_entities.erase(child.entity());
}

void World::add_top_level_entities(const eastl::span<const entt::handle> entities) {
    for(const auto& entity : entities) {
        top_level_entities.insert(entity.entity());
    }
}

void World::propagate_transforms(float delta_time) {
    ZoneScoped;

    for(const auto root_entity : top_level_entities) {
        const auto& transform = registry.get<TransformComponent>(root_entity);

        eastl::fixed_vector<entt::entity, 4> invalid_entities;
        for(const auto child : transform.children) {
            if(registry.valid(child)) {
                propagate_transform(child, transform.get_local_to_world());
            } else {
                invalid_entities.emplace_back(child);
            }
        }

        if(!invalid_entities.empty()) {
            registry.patch<TransformComponent>(
                root_entity,
                [&](TransformComponent& transform_to_modify) {
                    for(auto entity : invalid_entities) {
                        transform_to_modify.children.erase_first(entity);
                    }
                });
        }
    }
}

const eastl::unordered_set<entt::entity>& World::get_top_level_entities() const {
    return top_level_entities;
}

void World::propagate_transform(const entt::entity entity, const float4x4& parent_to_world) {
    const auto& transform = registry.get<TransformComponent>(entity);
    if(transform.cached_parent_to_world != parent_to_world) {
        registry.patch<TransformComponent>(
            entity,
            [&](TransformComponent& trans) {
                trans.cached_parent_to_world = parent_to_world;
            });
    }

    const auto local_to_world = transform.get_local_to_world();

    eastl::fixed_vector<entt::entity, 4> invalid_entities;
    for(const auto child : transform.children) {
        if(registry.valid(child)) {
            propagate_transform(child, local_to_world);
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

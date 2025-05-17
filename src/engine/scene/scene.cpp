#include "scene.hpp"

#include <tracy/Tracy.hpp>

#include "scene/transform_component.hpp"
#include "scene/transform_parent_component.hpp"
#include "core/system_interface.hpp"

static std::shared_ptr<spdlog::logger> logger;

Scene::Scene() {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("Scene");
    }
}

entt::handle Scene::create_entity() {
    return entt::handle{registry, registry.create()};
}

void Scene::destroy_entity(const entt::entity entity) {
    auto* transform = registry.try_get<TransformComponent>(entity);
    if(transform) {
        for(const auto child_entity : transform->children) {
            destroy_entity(child_entity);
        }
    }

    registry.destroy(entity);
}

entt::registry& Scene::get_registry() {
    return registry;
}

const entt::registry& Scene::get_registry() const {
    return registry;
}

void Scene::parent_entity_to_entity(const entt::entity child, const entt::entity parent) {
    if(!registry.valid(child)) {
        logger->error("No child set, unable to parent!");
        return;
    }

    if(!registry.valid(parent)) {
        logger->error("Parent entity not valid, unable to parent!");
        return;
    }

    registry.patch<TransformComponent>(
        parent,
        [&](TransformComponent& transform) {
            transform.children.emplace_back(child);
        });
    registry.patch<TransformComponent>(
        child,
        [&](TransformComponent& transform) {
            transform.parent = parent;
        });

    top_level_entities.erase_first_unsorted(child);
}

void Scene::add_top_level_entities(const eastl::span<const entt::entity> entities) {
    top_level_entities.insert(top_level_entities.end(), entities.begin(), entities.end());
}

void Scene::propagate_transforms(float delta_time) {
    ZoneScoped;

    for(const auto root_entity : top_level_entities) {
        const auto& transform = registry.get<TransformComponent>(root_entity);

        for(const auto child : transform.children) {
            propagate_transform(child, transform.local_to_parent);
        }
    }
}

const eastl::vector<entt::entity>& Scene::get_top_level_entities() const {
    return top_level_entities;
}

void Scene::propagate_transform(const entt::entity entity, const float4x4& parent_to_world) {
    const auto& transform = registry.get<TransformComponent>(entity);
    if(transform.cached_parent_to_world != parent_to_world) {
        registry.patch<TransformComponent>(
            entity,
            [&](TransformComponent& trans) {
                trans.cached_parent_to_world = parent_to_world;
            });
    }

    const auto local_to_world = parent_to_world * transform.local_to_parent;
    for(const auto child : transform.children) {
        propagate_transform(child, local_to_world);
    }
}

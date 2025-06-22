#pragma once

#include <EASTL/optional.h>
#include <EASTL/unordered_set.h>
#include <EASTL/span.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <entt/entt.hpp>

#include "behavior/game_object.hpp"
#include "scene/game_object_component.hpp"
#include "shared/prelude.h"

/**
 * Represents the scene of the game world
 *
 * Various subsystems such as rendering or physics may maintain their own scenes. In those cases, this scene stores
 * handles to objects in the subsystem's scenes
 */
class Scene {
public:
    Scene();

    /**
     * Creates a generic entity
     */
    entt::handle create_entity();

    /**
     * Creates a game object with the given name and optionally a parent node
     */
    template <typename GameObjectType = GameObject>
    entt::handle create_game_object(
        eastl::string_view name, const eastl::optional<entt::handle>& parent_node = eastl::nullopt
    );

    void destroy_entity(entt::entity entity);

    /**
     * Adds a component of the given type to the entity if it does not exist. Returns a reference to the component
     */
    template <typename ComponentType>
    ComponentType& add_component(entt::entity entity, ComponentType component = {});

    entt::registry& get_registry();

    const entt::registry& get_registry() const;

    void parent_entity_to_entity(entt::handle child, entt::handle parent);

    void add_top_level_entities(eastl::span<const entt::handle> entities);

    void propagate_transforms(float delta_time);

    const eastl::unordered_set<entt::entity>& get_top_level_entities() const;

private:
    entt::registry registry;

    /**
     * List of all the top-level entities from each model we load. The transform update logic starts at these and
     * propagates transforms down the child chain
     */
    eastl::unordered_set<entt::entity> top_level_entities;

    void propagate_transform(entt::entity entity, const float4x4& parent_to_world);
};

template <typename GameObjectType>
entt::handle Scene::create_game_object(
    const eastl::string_view name, const eastl::optional<entt::handle>& parent_node
) {
    const auto entity = create_entity();
    const auto& game_object = add_component(
        entity,
        GameObjectComponent{
            .game_object = eastl::make_shared<GameObjectType>(entity)
        });
    game_object->name = name;

    if(parent_node) {
        parent_entity_to_entity(entity, *parent_node);
    } else {
        add_top_level_entities(eastl::array{entity});
    }

    return entity;
}

template <typename ComponentType>
ComponentType& Scene::add_component(entt::entity entity, ComponentType component) {
    return registry.get_or_emplace<ComponentType>(entity, eastl::move(component));
}

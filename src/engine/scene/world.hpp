#pragma once

#include <EASTL/optional.h>
#include <EASTL/unordered_set.h>
#include <EASTL/span.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>

#include "transform_component.hpp"
#include "animation/animation_system.hpp"
#include "behavior/game_object.hpp"
#include "scene/game_object_component.hpp"
#include "shared/prelude.h"

/**
 * Represents the scene of the game world
 *
 * Various subsystems such as rendering or physics may maintain their own scenes. In those cases, this scene stores
 * handles to objects in the subsystem's scenes
 */
class World {
public:
    /**
     * Tried to find an entity with the provided name within the children tree of the provided entity
     *
     * Does NOT check the given entity. If that's the one you want - well, you already have it!
     *
     * @param entity Entity to search the children of
     * @param child_name Name to search for. Must match exactly
     * @return Handle to the found entity, or an empty handle if the child can't be found
     */
    static entt::handle find_child(entt::handle entity, eastl::string_view child_name);

    template<typename ComponentType>
    static entt::handle find_component_in_children(entt::handle entity);

    World();

    entt::handle make_handle(entt::entity entity);

    /**
     * Creates a generic entity
     */
    entt::handle create_entity();

    /**
     * Creates a game object with the given name and optionally a parent node
     */
    template<typename GameObjectType = GameObject>
    entt::handle create_game_object(
        eastl::string_view name, const eastl::optional<entt::handle>& parent_node = eastl::nullopt
        );

    /**
     * Attempts to find an entity with the provided name in the world
     *
     * This is relatively expensive, it should be called sparingly
     */
    entt::handle find_entity(eastl::string_view name);

    void destroy_entity(entt::entity entity);

    /**
     * Adds a component of the given type to the entity if it does not exist. Returns a reference to the component
     */
    template<typename ComponentType>
    ComponentType& add_component(entt::entity entity, ComponentType component = {});

    void tick(float delta_time);

    entt::registry& get_registry();

    const entt::registry& get_registry() const;

    void parent_entity_to_entity(entt::entity child, entt::entity parent);

    void add_top_level_entities(eastl::span<const entt::handle> entities);

    const eastl::unordered_set<entt::entity>& get_top_level_entities() const;

private:
    entt::registry registry;

    /**
     * List of all the top-level entities from each model we load. The transform update logic starts at these and
     * propagates transforms down the child chain
     */
    eastl::unordered_set<entt::entity> top_level_entities;

    void on_transform_update(entt::registry& registry,  entt::entity entity);

    /**
     * Propagates the entity's transform to its children
     */
    void propagate_transform(entt::entity entity);
};

template<typename ComponentType>
entt::handle World::find_component_in_children(const entt::handle entity) {
    if(const auto* trans = entity.try_get<TransformComponent>(); trans != nullptr) {
        for(const auto child : trans->children) {
            auto child_handle = entt::handle{*entity.registry(), child};
            if(child_handle.try_get<ComponentType>() != nullptr) {
                return child_handle;
            }
        }

        for(const auto child : trans->children) {
            auto child_handle = entt::handle{*entity.registry(), child};
            const auto found_child = find_component_in_children<ComponentType>(child_handle);
            if(found_child.valid()) {
                return found_child;
            }
        }
    }

    return {};
}

template<typename GameObjectType>
entt::handle World::create_game_object(
    const eastl::string_view name, const eastl::optional<entt::handle>& parent_node
    ) {
    try {
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
    } catch(const std::exception& e) {
        spdlog::error("Could not create entity {}: {}", name, e.what());
        return {};
    }
}

template<typename ComponentType>
ComponentType& World::add_component(entt::entity entity, ComponentType component) {
    return registry.get_or_emplace<ComponentType>(entity, eastl::move(component));
}

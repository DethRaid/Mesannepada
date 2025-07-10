#include "game_object.hpp"

#include "core/engine.hpp"
#include "scene/transform_component.hpp"

void GameObject::begin_play() {}

void GameObject::tick(const float delta_time, World& world) {}

GameObject::GameObject(const entt::handle root_entity_in) : root_entity{ root_entity_in } {
    auto& world = Engine::get().get_world();
    world.add_component(root_entity, TransformComponent{});
}

#include "game_object.hpp"

#include "core/engine.hpp"
#include "scene/transform_component.hpp"

void GameObject::tick(const float delta_time, Scene& scene) {}

GameObject::GameObject(const entt::handle root_entity_in) : root_entity{ root_entity_in } {
    auto& scene = Engine::get().get_scene();
    scene.add_component(root_entity, TransformComponent{});
}

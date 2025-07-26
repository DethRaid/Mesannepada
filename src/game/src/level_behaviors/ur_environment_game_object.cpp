#include "ur_environment_game_object.hpp"

#include "animation/animation_event_component.hpp"
#include "core/engine.hpp"
#include "resources/godot_scene.hpp"
#include "resources/model_components.hpp"
#include "scene/transform_component.hpp"

UrEnvironmentGameObject::UrEnvironmentGameObject(const entt::handle entity) :
    GameObject{entity} {
    auto& engine = Engine::get();

    const auto gltf_entity = engine.get_world().find_entity("SM_UrEnvironment");
    if(!gltf_entity) {
        throw std::runtime_error{"SM_UrEnvironment not found, are you loading the level correctly?"};
    }

    // The entity from the scene spawns the glTF model, so we need to go one level deeper
    ur_gltf_entity = entt::handle{*gltf_entity.registry(), gltf_entity.get<TransformComponent>().children[0]};

    // Add our animation event track. We KNOW that this will only be instantiated once, right?????
    auto& animations = engine.get_animation_system();
    auto& animation = animations.get_animation(nullptr, "FlyIn_Level1");
    animation.add_event(
        4.0,
        [] {
            Engine::get().give_player_full_control();
        });

    engine.get_physics_world().finalize();
}

void UrEnvironmentGameObject::tick(const float delta_time, World& world) {
    GameObject::tick(delta_time, world);
}

void UrEnvironmentGameObject::play_flyin_animation() {
    auto& engine = Engine::get();
    auto& animation_system = engine.get_animation_system();
    animation_system.play_animation_on_entity(ur_gltf_entity, "FlyIn_Level1");
}

void UrEnvironmentGameObject::connect_main_menu_listeners(ui::MainMenu& main_menu) {
    main_menu.new_game_sink.connect<&UrEnvironmentGameObject::play_flyin_animation>(this);
}

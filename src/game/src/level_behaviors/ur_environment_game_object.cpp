#include "ur_environment_game_object.hpp"

#include "animation/animation_event_component.hpp"
#include "core/engine.hpp"
#include "resources/gltf_model_component.hpp"
#include "resources/godot_scene.hpp"
#include "resources/imodel.hpp"
#include "scene/transform_component.hpp"

UrEnvironmentGameObject::UrEnvironmentGameObject(const entt::handle entity) : GameObject{entity} {
    auto& engine = Engine::get();

    auto& loader = engine.get_resource_loader();
    level_scene = loader.get_model("data/game/environments/Ur.tscn");

    auto& scene = engine.get_scene();
    level_entity = level_scene->add_to_scene(scene, root_entity.entity());

    const auto* level_godot_scene = static_cast<const godot::GodotScene*>(level_scene.get());
    const auto gltf_node_idx = level_godot_scene->find_node("SM_UrEnvironment");

    const auto gltf_entity = level_entity.get<ImportedModelComponent>().node_to_entity.at(*gltf_node_idx);

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

    engine.get_physics_scene().finalize();
}

void UrEnvironmentGameObject::tick(const float delta_time, Scene& scene) {
    GameObject::tick(delta_time, scene);

    auto& registry = scene.get_registry();

    // process events
    const auto* animation_event = registry.try_get<AnimationEventComponent>(root_entity);
    if(animation_event) {
        auto& application = Engine::get();
        auto& animation_system = application.get_animation_system();
        animation_system.play_animation_on_entity(ur_gltf_entity, animation_event->animation_to_play);

        registry.remove<AnimationEventComponent>(root_entity);
    }
}


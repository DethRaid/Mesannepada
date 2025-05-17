#include "player_input_manager.hpp"

#include <glm/geometric.hpp>

#include "player/first_person_player.hpp"
#include "scene/game_object_component.hpp"
#include "scene/scene.hpp"

static std::shared_ptr<spdlog::logger> logger;

PlayerInputManager::PlayerInputManager() {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("PlayerInputManager");
    }
}

void PlayerInputManager::set_player_movement(const glm::vec3& raw_axis) {
    if(glm::length(raw_axis) > 0) {
        player_movement_input = glm::normalize(raw_axis);
    } else {
        player_movement_input = raw_axis;
    }
}

void PlayerInputManager::set_player_rotation(const glm::vec2 rotation_in) {
    player_rotation_input = rotation_in;
}

void PlayerInputManager::add_input_event(const InputEvent& event) {
    events.push(event);
}

void PlayerInputManager::set_controlled_entity(const entt::entity entity) {
    controlled_entity = entity;
}

void PlayerInputManager::tick(const float delta_time, Scene& scene) {
    if(!enabled) {
        return;
    }

    auto& registry = scene.get_registry();

    if(!registry.valid(controlled_entity)) {
        return;
    }

    bool jump = false;
    bool toggle_crouch = false;

    while(!events.empty()) {
        const auto event = events.back();
        events.pop();

        if(event.button == InputButtons::Jump && event.action == InputAction::Pressed) {
            jump = true;
        } else if(event.button == InputButtons::Crouch && event.action == InputAction::Pressed) {
            toggle_crouch = true;
        }
    }

    const auto& player_go = registry.get<GameObjectComponent>(controlled_entity);
    auto* player = dynamic_cast<FirstPersonPlayer*>(player_go.game_object.get());
    player->handle_input(delta_time, player_movement_input, player_rotation_input.y, player_rotation_input.x, jump);

    player_movement_input = {};
    player_rotation_input = {};
}

#include "mesannapada_game_instance.hpp"

#include "core/engine.hpp"
#include "level_behaviors/ur_environment_game_object.hpp"
#include "player/first_person_player.hpp"
#include "player/player_parent_component.hpp"
#include "ui/main_menu.hpp"

static std::shared_ptr<spdlog::logger> logger;

MesannepadaGameInstance::MesannepadaGameInstance() {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("MesannepadaGameInstance");
    }

    auto& engine = Engine::get();

    // The main menu for this game is a bird's eye view of Ur. When you begin a game or load your save, the camera
    // zooms in to the player's location. This method should zoom the player out if needed, then show the main menu UI
    // and un-load save-specific assets

    auto& main_menu = engine.get_ui_controller().show_screen<ui::MainMenu>();

    auto& world = engine.get_world();
    environment_entity = world.create_game_object<UrEnvironmentGameObject>("UrEnvironment");

    main_menu.set_environment_entity(environment_entity);

    // Try to parent the player to the player control entity. It might work?
    const auto player = engine.get_player();
    const auto player_parents = world.get_registry().view<PlayerParentComponent>();
    if(player_parents.size() != 1) {
        logger->error("There may only be one player parent! And there MUST be one!");
    } else {
        player_parents.each(
            [&](const entt::entity entity) {
                world.parent_entity_to_entity(player, entity);
                player.patch<FirstPersonPlayerComponent>([](FirstPersonPlayerComponent& comp) {
                    comp.enabled = false;
                });
            });
    }
}

MesannepadaGameInstance::~MesannepadaGameInstance() {
}

void MesannepadaGameInstance::tick(const float delta_time) {
}

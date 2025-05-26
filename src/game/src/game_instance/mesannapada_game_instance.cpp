#include "mesannapada_game_instance.hpp"

#include "core/engine.hpp"
#include "level_behaviors/ur_environment_game_object.hpp"
#include "player/first_person_player.hpp"
#include "ui/main_menu.hpp"

MesannepadaGameInstance::MesannepadaGameInstance() {
    auto& engine = Engine::get();

    engine.instantiate_player<FirstPersonPlayer>();

    // The main menu for this game is a bird's eye view of Ur. When you begin a game or load your save, the camera
    // zooms in to the player's location. This method should zoom the player out if needed, then show the main menu UI
    // and un-load save-specific assets

    auto& main_menu = engine.get_ui_controller().show_screen<ui::MainMenu>();

    auto& scene = engine.get_scene();
    environment_entity = scene.create_game_object<UrEnvironmentGameObject>("UrEnvironment");
    
    main_menu.set_environment_entity(environment_entity);
}

MesannepadaGameInstance::~MesannepadaGameInstance() {}

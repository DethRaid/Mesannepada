#include "main_menu.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>

#include "animation/animation_event_component.hpp"
#include "core/engine.hpp"
#include "core/system_interface.hpp"
#include "level_behaviors/ur_environment_game_object.hpp"
#include "ui/game_settings.hpp"

static std::shared_ptr<spdlog::logger> logger;

namespace ui {
    MainMenu::MainMenu(Controller& controller_in) :
        Screen{controller_in}, continue_sink{continue_signal}, new_game_sink{new_game_signal},
        load_game_sink{load_game_signal}, settings_sink{settings_signal}, exit_sink{exit_signal} {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("MainMenu");
        }

        is_blocking_screen = true;

        auto data_model_constructor = controller.get_context().CreateDataModel("main_menu");

        data_model_constructor.Bind("has_existing_save", &has_existing_save);

        data_model_constructor.BindEventCallback("on_continue", &MainMenu::on_continue, this);
        data_model_constructor.BindEventCallback("on_new_game", &MainMenu::on_new_game, this);
        data_model_constructor.BindEventCallback("on_load_game", &MainMenu::on_load_game, this);
        data_model_constructor.BindEventCallback("on_settings", &MainMenu::on_settings, this);
        data_model_constructor.BindEventCallback("on_exit", &MainMenu::on_exit, this);

        my_model = data_model_constructor.GetModelHandle();

        load_document(SystemInterface::get().get_data_folder() / "ui/main_menu/main_menu.rml");

        my_model.DirtyVariable("has_existing_save");

        Engine::get().set_player_controller_enabled(false);
    }

    void MainMenu::on_continue(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        logger->info("Continuing game");

        continue_signal.publish();

        controller.pop_screen();
    }

    void MainMenu::on_new_game(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        logger->info("Starting new game");

        SystemInterface::get().set_cursor_hidden(true);

        new_game_signal.publish();

        controller.pop_screen();
    }

    void MainMenu::on_load_game(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        logger->info("Loading game");

        load_game_signal.publish();
    }

    void MainMenu::on_settings(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        logger->info("Opening settings menu");

        settings_signal.publish();

        controller.show_screen<SettingsScreen>();
    }

    void MainMenu::on_exit(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        exit_signal.publish();

        Engine::exit();
    }
}

#pragma once

#include <entt/entt.hpp>
#include <RmlUi/Core/DataModelHandle.h>

#include "ui/ui_screen.hpp"

namespace ui {
    class MainMenu final : public Screen {
    public:
        explicit MainMenu(Controller& controller_in);

        entt::sink<entt::sigh<void()>> continue_sink;
        entt::sink<entt::sigh<void()>> new_game_sink;
        entt::sink<entt::sigh<void()>> load_game_sink;
        entt::sink<entt::sigh<void()>> settings_sink;
        entt::sink<entt::sigh<void()>> exit_sink;

    private:
        bool has_existing_save = false;

        Rml::DataModelHandle my_model;

        entt::sigh<void()> continue_signal;
        void on_continue(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&);

        entt::sigh<void()> new_game_signal;
        void on_new_game(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&);

        entt::sigh<void()> load_game_signal;
        void on_load_game(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&);

        entt::sigh<void()> settings_signal;
        void on_settings(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&);

        entt::sigh<void()> exit_signal;
        void on_exit(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&);
    };
}

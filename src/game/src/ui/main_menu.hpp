#pragma once

#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <RmlUi/Core/DataModelHandle.h>

#include "ui/ui_screen.hpp"

namespace ui {
    class MainMenu final : public Screen {
    public:
        explicit MainMenu(Controller& controller_in);

        void set_environment_entity(entt::entity entity);

    private:
        bool has_existing_save = false;

        Rml::DataModelHandle my_model;

        entt::entity environment = entt::null;

        void on_continue(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&);

        void on_new_game(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&);

        void on_load_game(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&);

        void on_settings(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&);

        void on_exit(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&);
    };
}

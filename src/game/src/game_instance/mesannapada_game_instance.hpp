#pragma once

#include <entt/entity/fwd.hpp>

#include "game_framework/game_instance.hpp"

class MesannepadaGameInstance : public GameInstance
{
public:
    MesannepadaGameInstance();

    ~MesannepadaGameInstance() override;

private:
    entt::entity environment_entity;
};


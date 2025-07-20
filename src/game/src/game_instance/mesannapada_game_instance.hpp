#pragma once

#include <entt/entt.hpp>

#include "game_framework/game_instance.hpp"

class MesannepadaGameInstance : public GameInstance
{
public:
    MesannepadaGameInstance();

    ~MesannepadaGameInstance() override;

    void tick(float delta_time) override;

private:
    entt::entity environment_entity;
};


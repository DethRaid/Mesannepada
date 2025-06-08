#pragma once

#include "behavior/game_object.hpp"

class PatrollingGuard final : public GameObject {
public:
    PatrollingGuard(entt::handle root_entity_in);

    void begin_play() override;
};

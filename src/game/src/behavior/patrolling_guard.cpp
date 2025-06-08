#include "patrolling_guard.hpp"

PatrollingGuard::PatrollingGuard(const entt::handle root_entity_in) : GameObject{root_entity_in} {
    name = "PatrollingGuard";
}

void PatrollingGuard::begin_play() {
    GameObject::begin_play();
}



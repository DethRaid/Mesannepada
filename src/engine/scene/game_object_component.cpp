#include "game_object_component.hpp"

GameObject* GameObjectComponent::operator->() const { return game_object.get(); }

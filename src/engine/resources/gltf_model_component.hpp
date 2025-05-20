#pragma once

#include <entt/entity/entity.hpp>
#include <EASTL/vector.h>

/**
 * Marks the root node of an imported model. Contains a map from node ID to entity, letting tools that work with the
 * original data work with the entity tree
 */
struct ImportedModelComponent {
    eastl::vector<entt::handle> node_to_entity;
};

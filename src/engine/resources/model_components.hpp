#pragma once

#include <entt/entity/entity.hpp>
#include <EASTL/vector.h>

#include "animation/skeleton.hpp"
#include "render/backend/handles.hpp"

/**
 * Marks the root node of an imported model. Contains a map from node ID to entity, letting tools that work with the
 * original data work with the entity tree
 */
struct ImportedModelComponent {
    /**
     * File path that this model was imported from
     */
    eastl::string filepath;

    /**
     * Mapping from node index from the file to enTT handle in-engine
     */
    eastl::vector<entt::handle> node_to_entity;
};

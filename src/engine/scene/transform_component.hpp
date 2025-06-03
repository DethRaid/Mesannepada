#pragma once

#include <entt/entity/entity.hpp>
#include <EASTL/fixed_vector.h>

#include "simdjson.h"
#include "shared/prelude.h"

struct TransformComponent {
    /**
     * Local-to-parent model matrix
     */
    float4x4 local_to_parent = float4x4{1.f};

    /**
     * Cached parent-to-world model matrix. Updated at the start of every frame
     */
    float4x4 cached_parent_to_world = float4x4{ 1.f };

    entt::entity parent = entt::null;

    eastl::fixed_vector<entt::entity, 16> children;

    static TransformComponent from_json(const simdjson::simdjson_result<simdjson::ondemand::value>& component_definition);
};


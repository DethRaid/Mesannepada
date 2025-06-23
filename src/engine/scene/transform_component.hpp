#pragma once

#include <entt/entity/entity.hpp>
#include <EASTL/fixed_vector.h>

#include "simdjson.h"
#include "shared/prelude.h"

struct TransformComponent {
    float3 location{};

    glm::quat rotation{1.f, 0.f, 0.f, 0.f};

    float3 scale{1.f};

    /**
     * Cached parent-to-world model matrix. Updated at the start of every frame
     */
    float4x4 cached_parent_to_world = float4x4{ 1.f };

    entt::entity parent = entt::null;

    eastl::fixed_vector<entt::entity, 16> children;

    float4x4 get_local_to_world() const;

    float4x4 get_local_to_parent() const;

    void set_local_transform(const float4x4& transform);

    static TransformComponent from_json(const simdjson::simdjson_result<simdjson::ondemand::value>& component_definition);
};


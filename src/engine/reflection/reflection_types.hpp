#pragma once

#include <EASTL/unordered_map.h>
#include <entt/entt.hpp>

enum Traits : uint16_t {
    Editor      = 1 << 0,   // Draw this member in the editor
    Serialize   = 1 << 1,   // Does not contain derived data
};

using PropertiesMap = eastl::unordered_map<entt::id_type, entt::meta_any>;

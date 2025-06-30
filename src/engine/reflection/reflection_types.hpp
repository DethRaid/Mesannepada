#pragma once

#include <EASTL/array.h>
#include <EASTL/unordered_map.h>
#include <entt/entt.hpp>

namespace reflection {
    /**
     * Various traits we can apply in reflection
     */
    enum Traits : uint16_t {
        Trivial = 1 << 0,           // Trivial type, like a float
        EditorReadOnly = 1 << 1,    // Can't be changed manually
        NoEditor = 1 << 2,          // Don't show in the editor
        Component = 1 << 3,         // This is a component type
        Transient = 1 << 4,         // This thing is generated at runtime and should not be saved
    };

    using PropertiesMap = eastl::unordered_map<entt::id_type, entt::meta_any>;
}

namespace entt {
    template<typename... Args>
    struct meta_sequence_container_traits<eastl::array<Args...> >
        : basic_meta_sequence_container_traits<eastl::array<Args...> > {
    };

    template<typename... Args>
    struct meta_sequence_container_traits<eastl::vector<Args...> >
        : basic_meta_sequence_container_traits<eastl::vector<Args...> > {
    };

    template<>
    struct meta_sequence_container_traits<eastl::fixed_vector<entt::entity, 16> >
        : basic_meta_sequence_container_traits<eastl::fixed_vector<entt::entity, 16> > {
    };

    template<typename... Args>
    struct meta_associative_container_traits<eastl::hash_map<Args...> >
        : basic_meta_associative_container_traits<eastl::hash_map<Args...> > {
    };
}

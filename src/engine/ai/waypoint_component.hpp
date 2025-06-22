#pragma once

#include <entt/entt.hpp>

namespace ai {
    /**
     * A component that marks a waypoint. A waypoint is just a point in a path
     */
    struct WaypointComponent {
        entt::handle next_waypoint;
    };
} // ai

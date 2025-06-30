#pragma once

#include <EASTL/vector.h>

#include "shared/prelude.h"

namespace ai {
    struct Waypoint {
        float3 location{};
        glm::quat orientation{};
        float wait_time = 0;
    };
    /**
     * A component that marks a waypoint. A waypoint is just a point in a path
     */
    struct PatrolPathComponent {
        eastl::vector<Waypoint> waypoints;
    };
} // ai

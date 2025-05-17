#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace physics {
    /**
     * A simple collider
     */
    struct CollisionComponent {
        JPH::BodyID body_id;
    };
}

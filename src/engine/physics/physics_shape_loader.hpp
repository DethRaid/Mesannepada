#pragma once

#include <filesystem>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace physics {
    JPH::Ref<JPH::Shape> load_shape(const std::filesystem::path& filepath);

    void save_shape(const JPH::Shape& shape, const std::filesystem::path& filepath);
}

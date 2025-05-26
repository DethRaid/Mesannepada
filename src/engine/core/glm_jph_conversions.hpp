#pragma once

// Assorted conversions between GLM and JPH vector types

#include <Jolt/Jolt.h>
#include <Jolt/Math/Float2.h>

#include "shared/prelude.h"

inline JPH::Vec3 to_jolt(const float3 vec) {
    return {vec.x, vec.y, vec.z};
}

inline JPH::Quat to_jolt(const glm::quat quat) {
    return {quat.x, quat.y, quat.z, quat.w};
}

inline float3 to_glm(const JPH::Vec3 vec) {
    return {vec.GetX(), vec.GetY(), vec.GetZ()};
}

inline float4 to_glm(const JPH::Vec4 vec) {
    return {vec.GetX(), vec.GetY(), vec.GetZ(), vec.GetW()};
}

inline float2 to_glm(const JPH::Float2 vec) {
    return {vec.x, vec.y};
}

inline float3 to_glm(const JPH::Float3 vec) {
    return {vec.x, vec.y, vec.z};
}

inline glm::quat to_glm(const JPH::Quat quat) {
    return {quat.GetW(), quat.GetX(), quat.GetY(), quat.GetZ()};
}

inline float4x4 to_glm(const JPH::Mat44& mat) {
    return {
        to_glm(mat.GetColumn4(0)),
        to_glm(mat.GetColumn4(1)),
        to_glm(mat.GetColumn4(2)),
        to_glm(mat.GetColumn4(3)),
    };
}

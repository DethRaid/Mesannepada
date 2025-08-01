#pragma once

#include <cereal/cereal.hpp>

#include "reflection/serialization/serializers.hpp"
#include "shared/prelude.h"

namespace glm {
    template<typename Archive>
    void serialize(Archive& ar, float3& value) {
        ar(value.x, value.y, value.z);
    }

    template<typename Archive>
    void serialize(Archive& ar, quat& value) {
        ar(value.w, value.x, value.y, value.z);
    }

    template<typename Archive>
    void serialize(Archive& ar, float4x4& value);

    template<typename Archive>
    void serialize(Archive& ar, float4x4& value) {
        for(auto column = 0; column < 4; column++) {
            for(auto row = 0; row < 4; row++) {
                serialization::serialize_scalar(ar, value[column][row]);
            }
        }
    }
} // namespace glm

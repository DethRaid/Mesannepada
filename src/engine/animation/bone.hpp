#pragma once

#include <EASTL/fixed_vector.h>

#include "shared/prelude.h"

struct Bone {
    float4x4 local_transform;
    eastl::fixed_vector<size_t, 4> children;
};

#pragma once

#include <EASTL/vector.h>

#include "animation/bone.hpp"

/**
 * Information about a skin, imported from the glTF file
 */
struct Skeleton {
    /**
     * The inverse bind matrices for the skeleton
     */
    eastl::vector<float4x4> inverse_bind_matrices;

    /**
     * The bones in the skeleton
     *
     * These are directly from the glTF file's joints array. This is potentially Very Bad. A glTF skeleton can use any
     * nodes it wants, in any order. Blender happens to export glTF so that the skeleton is the first nodes, and
     * non-skeleton nodes come after. Blender is my main DCC tool, but... this WILL cause problems
     */
    eastl::vector<Bone> bones;

    eastl::vector<size_t> root_bones;
};

using SkeletonHandle = Skeleton*;

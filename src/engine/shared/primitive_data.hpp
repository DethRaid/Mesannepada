#ifndef PRIMITIVE_DATA_HPP
#define PRIMITIVE_DATA_HPP

#include "shared/prelude.h"

/*
 * Primitive info flags - not expected to change after the primitive is created
 */

// This is a completely opaque primitive
#define PRIMITIVE_TYPE_SOLID            1 << 0

// This primitive has an alpha mask, either from a texture or calculated in a shader
#define PRIMITIVE_TYPE_CUTOUT           1 << 1

// This primitive has an alpha texture and should be partially translucent
#define PRIMITIVE_TYPE_TRANSPARENT      1 << 2

// This primitive is a skinned mesh
#define PRIMITIVE_TYPE_SKINNED          1 << 4

/**
 * Runtime primitive flags - these may change when various things happen
 */

// This primitive is currently active and should be drawn
#define PRIMITIVE_RUNTIME_FLAG_ENABLED          1 << 0

#if defined(__cplusplus)
using MaterialPointer = uint64_t;
using IndexPointer = uint64_t;
using VertexPositionPointer = uint64_t;
using VertexDataPointer = uint64_t;

using BoneIdsPointer = uint64_t;
using WeightsPointer = uint64_t;

using BoneTransformsPointer = uint64_t;

#elif defined(GL_core_profile)
#define MaterialPointer uvec2
#define IndexPointer uvec2
#define VertexPositionPointer uvec2
#define VertexDataPointer uvec2
#define BoneIdsPointer uvec2
#define WeightsPointer uvec2
#define BoneTransformsPointer uvec2

#else
#include "shared/basic_pbr_material.hpp"
#include "shared/vertex_data.hpp"

#define MaterialPointer BasicPbrMaterialGpu*
#define IndexPointer uint*
#define VertexPositionPointer float3*
#define VertexDataPointer StandardVertexData*

#define BoneIdsPointer u16vec4*
#define WeightsPointer float4*

#define BoneTransformsPointer float4x4*
#endif

// Size 200
struct PrimitiveDataGPU {
    float4x4 model;
    float4x4 inverse_model;

    // Bounds min (xyz) and radius (w) of the mesh
    float4 bounds_min_and_radius;
    float4 bounds_max;

    MaterialPointer material;

    uint mesh_id;
    uint16_t type_flags;  // See the PRIMITIVE_TYPE_ defines above
    uint16_t runtime_flags; // See the PRIMITIVE_RUNTIME_FLAG defines, above

    IndexPointer indices;
    VertexPositionPointer vertex_positions;
    VertexDataPointer vertex_data;
};

struct SkeletalPrimitiveDataGPU {
    VertexPositionPointer original_positions;
    VertexDataPointer original_data;

    BoneIdsPointer bone_ids;
    WeightsPointer weights;

    BoneTransformsPointer bone_transforms;
};

#if defined(__cplusplus)
static_assert(sizeof(PrimitiveDataGPU) == 200);
static_assert(200 % alignof(PrimitiveDataGPU) == 0);
#endif

#endif

#ifndef PRIMITIVE_DATA_HPP
#define PRIMITIVE_DATA_HPP

#include "shared/prelude.h"

// Various flags that can apply to primitives

// This is a completely opaque primitive
#define PRIMITIVE_FLAG_SOLID            1 << 0

// This primitive has an alpha mask, either from a texture or calculated in a shader
#define PRIMITIVE_FLAG_CUTOUT           1 << 1

// This primitive has an alpha texture and should be partially translucent
#define PRIMITIVE_FLAG_TRANSPARENT      1 << 2

// This primitive is currently active and should be drawn
#define PRIMITIVE_FLAG_ENABLED          1 << 3

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
    float4x4 model;                                         // Offset 0, size 64
    float4x4 inverse_model;                                 // Offset 64, size 64

    // Bounds min (xyz) and radius (w) of the mesh
    float4 bounds_min_and_radius;                           // Offset 128, size 16
    float4 bounds_max;                                      // Offset 144, size 16

    MaterialPointer material;                               // Offset 160, size 8

    uint mesh_id;                                           // Offset 168, size 4
    uint flags;  // See the PRIMITIVE_FLAG_ defines above   // Offset 172, size 4

    IndexPointer indices;                                   // Offset 176, size 8
    VertexPositionPointer vertex_positions;                 // Offset 184, size 8
    VertexDataPointer vertex_data;                          // Offset 192, size 8
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

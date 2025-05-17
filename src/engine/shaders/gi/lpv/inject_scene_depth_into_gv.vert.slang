/**
 * Generates a SH point cloud from a depth and normal image
 */

#include "shared/prelude.h"
#include "common/spherical_harmonics.slangi"
#include "shared/view_data.hpp"
#include "shared/lpv.hpp"

Sampler2D<half3> gbuffer_normals;
Sampler2D<float> gbuffer_depth;

ConstantBuffer<LPVCascadeMatrices[4]> cascades;

ConstantBuffer<ViewDataGPU> view_data;

[vk::push_constant]
cbuffer Constants {
    uint2 resolution;
    uint num_cascades;
};

struct VertexOut {
    float4 position : SV_Position;
    float point_size : SV_PointSize;
    uint layer : SV_RenderTargetArrayIndex;
    half4 sh;
}

[shader("vertex")]
VertexOut main(uint vertex_index: SV_VertexID, uint cascade_index : SV_InstanceID) {
    uint x = vertex_index % resolution.x;
    uint y = vertex_index / resolution.x;

    const float depth = gbuffer_depth[int2(x, y)];

    const float2 screenspace = float2(x + 0.5, y + 0.5) / float2(resolution);
    const float4 ndc_position = float4(screenspace * 2.f - 1.f, depth, 1);
    float4 viewspace_position = mul(view_data.inverse_projection, ndc_position);
    viewspace_position /= viewspace_position.w;

    const float4 worldspace_position = mul(view_data.inverse_view, viewspace_position);

    VertexOut out = (VertexOut)0;

    float4 cascadespace_position = mul(cascades[cascade_index].world_to_cascade, worldspace_position);
    if (any(cascadespace_position.xyz < 0) || any(cascadespace_position.xyz > 1)) {
        out.position = (float4)0 / 0;
        return out;
    }

    half3 normal = gbuffer_normals[int2(x, y)];
    out.sh = dir_to_cosine_lobe(normal);

    // Add a half-cell offset to the GV
    cascadespace_position.xyz += 0.5f / 32.f;
    cascadespace_position.x += cascade_index;
    cascadespace_position.x /= 4;

    out.position = float4(cascadespace_position.xy * 2.f - 1.f, 0.f, 1.f);
    
    out.layer = (uint)round(cascadespace_position.z * 32.f);

    out.point_size = 1;
    
    return out;
}

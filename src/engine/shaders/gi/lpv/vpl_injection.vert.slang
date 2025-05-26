#include "common/spherical_harmonics.slangi"
#include "shared/lpv.hpp"
#include "shared/vpl.hpp"
#include "shared/sun_light_constants.hpp"

ConstantBuffer<LPVCascadeMatrices[4]> cascade_matrices;

ConstantBuffer<SunLightConstants> sun_light;

StructuredBuffer<PackedVPL> vpl_list_buffer;

[vk::push_constant]
cbuffer Constants {
    uint cascade_index;
    uint num_cascades;
};

static const float NORMAL_DEPTH_BIAS = 0.05;
static const float LIGHT_DEPTH_BIAS = 0.05;

struct VertexOut {
    float4 position : SV_Position;
    float point_size : SV_PointSize;
    int layer : SV_RenderTargetArrayIndex;
    half3 color;
    half3 normal;
    float3 pos_unsnapped;
}

VPL unpack_vpl(PackedVPL packed_vpl) {
    VPL vpl;

    half2 unpacked_x = unpackHalf2x16ToHalf(packed_vpl.data.x);
    half2 unpacked_y = unpackHalf2x16ToHalf(packed_vpl.data.y);
    half2 unpacked_z = unpackHalf2x16ToHalf(packed_vpl.data.z);
    half4 unpacked_w = unpackSnorm4x8ToHalf(packed_vpl.data.w);

    vpl.position.xy = unpacked_x.xy;
    vpl.position.z = unpacked_y.x;
    vpl.color.r = unpacked_y.y;
    vpl.color.gb = unpacked_z.xy;
    vpl.normal = normalize(unpacked_w.xyz);

    return vpl;
}

[shader("vertex")]
VertexOut main(uint vertex_index : SV_VertexID, uint instance_index : SV_InstanceID) {
    PackedVPL packed_vpl = vpl_list_buffer[vertex_index];
    VPL vpl = unpack_vpl(packed_vpl);

    float3 position = mul(cascade_matrices[cascade_index].world_to_cascade, float4(vpl.position, 1.f)).xyz;

    VertexOut out = (VertexOut)0;

    out.color = vpl.color;
    out.normal = vpl.normal;
    
	position += out.normal * NORMAL_DEPTH_BIAS;
	position += sun_light.direction_and_tan_size.xyz * LIGHT_DEPTH_BIAS;

    // Snap the position for trilinear VPL injection
    out.pos_unsnapped = position.xyz * 32.0;
	position = float3(round(out.pos_unsnapped.xy), floor(out.pos_unsnapped.z)) / 32.0;

    // Adjust position to be in the correct part of the cascade
    position.x += float(cascade_index);
    position.x /= float(num_cascades);

    // Map from UV space to NDC
    out.position = float4(position.xy * 2.f - 1.f, 0.f, 1.f);
    out.layer = int(position.z * 32.f) + instance_index;
    out.point_size = 2.f;

    if(length(out.normal) < 0.999 || length(out.color) < 0.001) {
        out.point_size = 0;
    }

    if (out.layer < 0 || out.layer >= 32) {
        out.point_size = 0;
    }

    return out;
}

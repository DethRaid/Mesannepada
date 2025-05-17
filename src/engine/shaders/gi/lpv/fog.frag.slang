#include "shared/view_data.hpp"
#include "shared/lpv.hpp"
#include "common/spherical_harmonics.slangi"

ConstantBuffer<ViewDataGPU> view_data;

ConstantBuffer<LPVCascadeMatrices[4]> cascade_matrices;

Texture2D<float> gbuffer_depth;

Sampler3D<half4> lpv_r;
Sampler3D<half4> lpv_g;
Sampler3D<half4> lpv_b;

[vk::push_constant]
cbuffer Constants {
    float fog_strength;
};

float3 get_worldspace_position(in float2 pixel) {
    const float depth = gbuffer_depth[int2(pixel.xy)].r;
    const float2 texcoord = (pixel.xy + 0.5) / view_data.render_resolution.xy;
    const float4 ndc_position = float4(float3(texcoord * 2.0 - 1.0, depth), 1.f);
    float4 viewspace_position = mul(view_data.inverse_projection, ndc_position);
    viewspace_position /= viewspace_position.w;
    const float4 worldspace_position = mul(view_data.inverse_view, viewspace_position);

    return worldspace_position.xyz;
}

half3 sample_light_from_cascade(half4 normal_coefficients, float4 position_worldspace, uint cascade_index) {
    float4 cascade_position = mul(cascade_matrices[cascade_index].world_to_cascade, position_worldspace);

    cascade_position.x += float(cascade_index);
    cascade_position.x /= 4.0;

    half4 red_coefficients = lpv_r.SampleLevel(cascade_position.xyz, 0);
    half4 green_coefficients = lpv_g.SampleLevel(cascade_position.xyz, 0);
    half4 blue_coefficients = lpv_b.SampleLevel(cascade_position.xyz, 0);

    half red_strength = dot(red_coefficients, normal_coefficients);
    half green_strength = dot(green_coefficients, normal_coefficients);
    half blue_strength = dot(blue_coefficients, normal_coefficients);

    return half3(red_strength, green_strength, blue_strength);
}

[shader("fragment")]
float4 main(float4 fragpos: SV_Position) {
    const float3 worldspace_position = get_worldspace_position(fragpos.xy);

    const float3 view_position = float3(view_data.inverse_view[0][3], view_data.inverse_view[1][3], view_data.inverse_view[2][3]);
    const float3 worldspace_view_position = worldspace_position - view_position;
    const float3 worldspace_view_vector = normalize(worldspace_view_position);

    const half4 direction_sh = dir_to_sh((half3)worldspace_view_vector);

    float3 accumulated_light = 0;

    for (float i = 0; i < 32; i += 0.5) {
        const float3 sample_position = view_position + worldspace_view_vector * i;
        const half3 lpv_sample = sample_light_from_cascade(direction_sh, float4(sample_position, 1), 0);
        accumulated_light += lpv_sample * fog_strength;
    }

    return half4(accumulated_light, 1);
}

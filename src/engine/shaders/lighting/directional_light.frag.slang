#include "shared/sun_light_constants.hpp"
#include "shared/view_data.hpp"
#include "common/brdf.slangi"

#define PI 3.1415927

// Gbuffer textures
[vk::binding(0, 0)]
Sampler2D gbuffer_base_color;
[vk::binding(1, 0)]
Sampler2D<half4> gbuffer_normal;
[vk::binding(2, 0)]
Sampler2D gbuffer_data;
[vk::binding(3, 0)]
Sampler2D gbuffer_emission;
[vk::binding(4, 0)]
Sampler2D<float> gbuffer_depth;

[vk::binding(0, 1)]
Texture2D sun_shadowmask;
[vk::binding(1, 1)]
ConstantBuffer<SunLightConstants> sun_light;
[vk::binding(2, 1)]
ConstantBuffer<ViewDataGPU> view_data;

float3 get_viewspace_position(int2 pixel) {
    const float depth = gbuffer_depth[pixel].r;
    float2 texcoord = ((float2)pixel + 0.5) / view_data.render_resolution.xy;
    float4 ndc_position = float4(float3(texcoord * 2.0 - 1.0, depth), 1.f);
    float4 viewspace_position = mul(view_data.inverse_projection, ndc_position);
    viewspace_position /= viewspace_position.w;

    return viewspace_position.xyz;
}

[shader("fragment")]
float4 main(float2 texcoord, float2 fragcoord : SV_Position) {
    const int2 pixel = (int2)fragcoord;

    const float depth_sample = gbuffer_depth[pixel];
    if(depth_sample == 0) {
        discard;
    }
    
    const float3 base_color_sample = gbuffer_base_color[pixel].rgb;
    const float3 normal_sample = normalize(gbuffer_normal[pixel].xyz);
    const float4 data_sample = gbuffer_data[pixel];
    const float4 emission_sample =gbuffer_emission[pixel];

    const float3 viewspace_position = get_viewspace_position(pixel);
    const float4 worldspace_position = mul(view_data.inverse_view, float4(viewspace_position, 1.0));

    const float3 view_position = float3(view_data.inverse_view[0][3], view_data.inverse_view[1][3], view_data.inverse_view[2][3]);
    const float3 worldspace_view_position = worldspace_position.xyz - view_position;
    const float3 worldspace_view_vector = normalize(-worldspace_view_position);

    const float3 light_vector = normalize(-sun_light.direction_and_tan_size.xyz);

    SurfaceInfo surface;
    surface.base_color = base_color_sample;
    surface.normal = normal_sample;
    surface.roughness = data_sample.g;
    surface.metalness = data_sample.b;
    surface.emission = emission_sample.rgb;
    surface.location = worldspace_position.xyz;

    float ndotl = clamp(dot(normal_sample, light_vector), 0, 1);

    float shadow = sun_shadowmask[pixel].r;

    float3 brdf_result = brdf(surface, light_vector, worldspace_view_vector);

    float3 direct_light = view_data.exposure * shadow * ndotl * brdf_result * sun_light.color.rgb;

    if(any(isnan(direct_light))) {
        direct_light = float3(0);
    }

    return float4(direct_light, 1);

    // Shadow cascade visualization
    // TODO: A uniform buffer with debug info?
    // if(cascade_index == 0) {
    //     lighting = float4(1, 0, 0, 1);
    // } else if(cascade_index == 1) {
    //     lighting = float4(0, 1, 0, 1);
    // } else if(cascade_index == 2) {
    //     lighting = float4(0, 0, 1, 1);
    // } else if(cascade_index == 3) {
    //     lighting = float4(1, 1, 0, 1);
    // } else {
    //     lighting = float4(1, 0, 1, 1);
    // }

    // lighting = float4(ndotl.xxx, 1.0);
}

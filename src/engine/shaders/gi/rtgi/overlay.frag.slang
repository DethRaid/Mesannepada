#include "NRDEncoding.hlsli"
#include "NRD.hlsli"

#include "common/brdf.slangi"
#include "shared/view_data.hpp"

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
Texture2D<half4> ray_irradiance;
[vk::binding(1, 1)]
Texture2D<float4> ray_buffer;
[vk::binding(2, 1)]
ConstantBuffer<ViewDataGPU> view_data;

[vk::push_constant]
cbuffer Constants {
    uint denoiser_type; // See render::DenoiserType
}

float3 get_viewspace_position(int2 pixel) {
    const float depth = gbuffer_depth[pixel].r;
    float2 texcoord = ((float2)pixel + 0.5) / view_data.render_resolution.xy;
    float4 ndc_position = float4(float3(texcoord * 2.0 - 1.0, depth), 1.f);
    float4 viewspace_position = mul(view_data.inverse_projection, ndc_position);
    viewspace_position /= viewspace_position.w;

    return viewspace_position.xyz;
}

[shader("fragment")]
float4 main(float2 texcoord, float4 sv_position : SV_Position) {
    const int2 pixel = (int2)sv_position.xy;

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

    SurfaceInfo surface;
    surface.base_color = base_color_sample;
    surface.normal = normal_sample;
    surface.roughness = data_sample.g;
    surface.metalness = data_sample.b;
    surface.emission = emission_sample.rgb;
    surface.location = worldspace_position.xyz;

    const float4 ray_direction_and_distance = ray_buffer[pixel];
    const float3 light_vector = normalize(ray_direction_and_distance.xyz);

    float ndotl = clamp(dot(normal_sample, light_vector), 0, 1);

    float3 brdf_result = brdf(surface, light_vector, worldspace_view_vector);

    float4 irradiance = ray_irradiance[pixel];
    if(denoiser_type == 1) { // ReBLUR
        irradiance = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(irradiance);
    } else if(denoiser_type == 2) { // ReLAX
        irradiance = RELAX_BackEnd_UnpackRadiance(irradiance);
    } 

    return float4(irradiance.rgb * brdf_result * ndotl, 1.0);
}

#include "shared/lights.hpp"
#include "shared/view_data.hpp"
#include "common/brdf.slangi"

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

// Point light resources
[vk::binding(0, 1)]
ConstantBuffer<ViewDataGPU> view_data;
[vk::binding(1, 1)]
StructuredBuffer<PointLightGPU> point_lights;
[vk::binding(2, 1)]
RaytracingAccelerationStructure rtas;

[vk::push_constant]
cbuffer Constants {
    uint num_point_lights;
};

float3 get_viewspace_position(int2 pixel) {
    const float depth = gbuffer_depth[pixel].r;
    float2 texcoord = ((float2)pixel + 0.5) / view_data.render_resolution.xy;
    float4 ndc_position = float4(float3(texcoord * 2.0 - 1.0, depth), 1.f);
    float4 viewspace_position = mul(view_data.inverse_projection, ndc_position);
    viewspace_position /= viewspace_position.w;

    return viewspace_position.xyz;
}

float point_light_attenuation(const float distance, const float radius, const float range) {
    // Combination of https://www.cemyuksel.com/research/pointlightattenuation/ and https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual
    const float d2 = distance * distance;
    const float r2 = radius * radius;
    const float edge_attenuation = saturate(1.f - pow(distance - range, 4));
    const float a = 2.f / (d2 + r2 + distance * sqrt(d2 + r2));

    return edge_attenuation * a;
}

[shader("fragment")]
float4 main(float2 texcoord, float2 fragcoord : SV_Position) {
    const int2 pixel = (int2)fragcoord;

    // Sample the surface we're shading. The depth test ensures we don't shade the sky
    const float depth_sample = gbuffer_depth[pixel];    
    const float3 base_color_sample = gbuffer_base_color[pixel].rgb;
    const float3 normal_sample = normalize(gbuffer_normal[pixel].xyz);
    const float4 data_sample = gbuffer_data[pixel];
    const float4 emission_sample = gbuffer_emission[pixel];

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

    // Calculate each light's contribution
    float3 color = 0;
    for(uint light_idx = 0; light_idx < num_point_lights; light_idx++) {
        const PointLightGPU light = point_lights[light_idx];

        float3 light_vector = light.location - surface.location;
        const float d = length(light_vector);
        if(d > light.range) {
            continue;
        }

        light_vector /= d;

        // One shadow ray

        RayDesc ray = (RayDesc)0;
        ray.Origin = surface.location;
        ray.Direction = light_vector;
        ray.TMin = 0.01;
        ray.TMax = d;

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE> query;
        query.TraceRayInline(rtas, 0, 0xFF, ray);

        query.Proceed();

        float shadow_factor = 1.f;
        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            shadow_factor = 0;
        }

        const float a = point_light_attenuation(d, light.size, light.range);

        const float ndotl = saturate(dot(surface.normal, light_vector));
        const float3 brdf_result = brdf(surface, light_vector, worldspace_view_vector);
        color += view_data.exposure * shadow_factor * ndotl * brdf_result * light.color;
    }

    return float4(color, 1);
}


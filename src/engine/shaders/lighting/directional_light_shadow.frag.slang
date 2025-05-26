#include "shared/sun_light_constants.hpp"
#include "shared/view_data.hpp"
#include "common/brdf.slangi"

#define PI 3.1415927

// Gbuffer textures
[vk::binding(0, 0)]
Texture2D<half4> gbuffer_normal;
[vk::binding(1, 0)]
Texture2D<float> gbuffer_depth;

[vk::binding(2, 0)]
Sampler2DArrayShadow sun_shadowmap;
[vk::binding(3, 0)]
ConstantBuffer<SunLightConstants> sun_light;
[vk::binding(4, 0)]
ConstantBuffer<ViewDataGPU> view_data;

float3 get_viewspace_position(int2 pixel) {
    const float depth = gbuffer_depth[pixel].r;
    float2 texcoord = ((float2)pixel + 0.5) / view_data.render_resolution.xy;
    float4 ndc_position = float4(float3(texcoord * 2.0 - 1.0, depth), 1.f);
    float4 viewspace_position = mul(view_data.inverse_projection, ndc_position);
    viewspace_position /= viewspace_position.w;

    return viewspace_position.xyz;
}

static const float4x4 biasMat = float4x4( 
	0.5, 0.0, 0.0, 0.5,
	0.0, 0.5, 0.0, 0.5,
	0.0, 0.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0 
);

half get_shadow_factor(float3 worldspace_position, uint cascade_index, float bias) {
    float4 shadow_lookup = float4(-1);
    
    float4 shadowspace_position = mul(biasMat, mul(sun_light.cascade_matrices[cascade_index], float4(worldspace_position, 1.0)));
    shadowspace_position /= shadowspace_position.w;

    if(any(shadowspace_position.xyz < float3(0)) || any(shadowspace_position.xyz > float3(1))) {
        return 1;
    }

    // Use this cascade
    shadow_lookup.xy = shadowspace_position.xy;
    shadow_lookup.z = cascade_index;
    shadow_lookup.w = shadowspace_position.z - bias;
        
    return (half)sun_shadowmap.SampleCmpLevel(shadow_lookup.xyz, shadow_lookup.w, 0);
}

half sample_csm(float3 worldspace_position, float viewspace_depth, float ndotl) {
    uint cascade_index = 0;
    for(uint i = 0; i < 4; i++) {
        if(viewspace_depth < sun_light.data[i].x) {
            cascade_index = i + 1;
        }
    }

    half shadow = 1;
    if(cascade_index <= 3) {
        shadow = get_shadow_factor(worldspace_position, cascade_index, 0.0005 * sqrt(1 - ndotl * ndotl) / ndotl);
    }

    return shadow;
}

[shader("fragment")]
float main(float2 texcoord, float2 fragcoord : SV_Position) {
    const int2 pixel = (int2)fragcoord;
    
    const half3 normal_sample = normalize(gbuffer_normal[pixel].xyz);

    const float3 viewspace_position = get_viewspace_position(pixel);
    const float4 worldspace_position = mul(view_data.inverse_view, float4(viewspace_position, 1.0));

    const half3 light_vector = normalize(-sun_light.direction_and_tan_size.xyz);

    const half ndotl = dot(normal_sample, light_vector);

    half shadow = 0;

    if(ndotl > 0) {
        shadow = sample_csm(worldspace_position.xyz, viewspace_position.z, ndotl);    
    }

    return shadow;
}

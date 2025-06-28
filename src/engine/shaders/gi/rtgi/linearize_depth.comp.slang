#include "shared/view_data.hpp"

// NRD wants linear depth, so we gotta conform ugh

ConstantBuffer<ViewDataGPU> view_data;

Texture2D<float> gbuffer_depth;

RWTexture2D<float> viewspace_depth;

float3 get_viewspace_location(const uint2 pixel) {
    const float depth = gbuffer_depth[pixel];
    const float2 texcoord = ((float2)pixel + 0.5) / view_data.render_resolution.xy;
    const float4 ndc_position = float4(texcoord * 2.f - 1.f, depth, 1.f);
    float4 viewspace_position = mul(view_data.inverse_projection, ndc_position);
    viewspace_position /= viewspace_position.w;

    return viewspace_position.xyz;
}

[numthreads(8, 8, 1)]
[shader("compute")]
void main(uint2 thread_id : SV_DispatchThreadID) {
    if(any(thread_id >= view_data.render_resolution)) {
        return;
    }

    const float3 viewspace_location = get_viewspace_location(thread_id);
    
    viewspace_depth[thread_id] = viewspace_location.z;
}

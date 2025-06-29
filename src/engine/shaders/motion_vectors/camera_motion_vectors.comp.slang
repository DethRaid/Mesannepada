#include "shared/view_data.hpp"

ConstantBuffer<ViewDataGPU> view_data;

Texture2D<float> gbuffer_depth;

RWTexture2D<half2> motion_vectors;

[numthreads(8, 8, 1)]
[shader("compute")]
void main(uint2 thread_id : SV_DispatchThreadID) {
    const float depth = gbuffer_depth[thread_id];
    const float2 texcoord = ((float2)thread_id + 0.5) / view_data.render_resolution.xy;
    const float4 ndc_position = float4(texcoord * 2.f - 1.f, depth, 1.f);
    float4 viewspace_position = mul(view_data.inverse_projection, ndc_position);
    viewspace_position /= viewspace_position.w;
    const float4 worldspace_position = mul(view_data.inverse_view, viewspace_position);

    const float4 previous_viewspace_position = mul(view_data.last_frame_view, worldspace_position);
    float4 previous_ndc_position = mul(view_data.last_frame_projection, previous_viewspace_position);
    previous_ndc_position /= previous_ndc_position.w;
    const float2 previous_texcoord = previous_viewspace_position.xy * 0.5 + 0.5;
    const float2 previous_pixel = (previous_texcoord * view_data.render_resolution.xy) - 0.5;
    
    motion_vectors[thread_id] = half2(previous_pixel - (float2)thread_id);
}

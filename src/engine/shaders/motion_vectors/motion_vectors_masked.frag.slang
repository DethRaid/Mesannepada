#include "shared/primitive_data.hpp"
#include "shared/view_data.hpp"

[[vk::binding(0, 0)]]
ConstantBuffer<ViewDataGPU> camera_data;

[[vk::binding(1, 0)]]
StructuredBuffer<PrimitiveDataGPU> primitive_datas;

[vk::binding(0, 1)]
Sampler2D textures[];

struct VertexOutput {
    float4 position : SV_Position;
    float3 previous_clipspace_location : PREVIOUS_CLIPSPACE_LOCATION;
    uint primitive_id : PRIMITIVE_ID;
    float2 texcoord;
};

[shader("fragment")]
float2 main(VertexOutput vertex) {
    const float2 previous_clipspace_location = vertex.previous_clipspace_location.xy / vertex.previous_clipspace_location.z;
    const float2 previous_uv_location = previous_clipspace_location * float2(0.5, 0.5) + 0.5;
    const float2 previous_pixel_location = previous_uv_location * camera_data.render_resolution;

    const float2 pixel_motion_vectors = previous_pixel_location - vertex.position.xy;

    const PrimitiveDataGPU data = primitive_datas[vertex.primitive_id];
    const Sampler2D colortex = textures[data.material.base_color_texture_index];

    const float4 texcolor = colortex.SampleBias(vertex.texcoord, camera_data.material_texture_mip_bias);
    const float opacity = texcolor.a * data.material.base_color_tint.a;
    if(opacity <= data.material.opacity_threshold) {
        discard;
    }

    return pixel_motion_vectors;
}

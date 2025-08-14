#include "shared/primitive_data.hpp"
#include "shared/view_data.hpp"
#include "shared/vertex_data.hpp"

[[vk::binding(0, 0)]]
ConstantBuffer<ViewDataGPU> camera_data;

[[vk::binding(1, 0)]]
StructuredBuffer<PrimitiveDataGPU> primitive_datas;

struct VertexOutput {
    float4 position : SV_Position;
    float3 previous_clipspace_location : PREVIOUS_CLIPSPACE_LOCATION;
    uint primitive_id : PRIMITIVE_ID;
};

[shader("vertex")]
VertexOutput main(
    const uint vertex_id : SV_VertexID,
    const uint primitive_id : SV_StartInstanceLocation
) {
    VertexOutput output;

    output.primitive_id = primitive_id;
    PrimitiveDataGPU data = primitive_datas[output.primitive_id];
    
    const float3 vertex_position = data.vertex_positions[vertex_id];

    output.position = mul(camera_data.projection, mul(camera_data.view, mul(data.model, float4(vertex_position, 1.f))));

    const float4 last_frame_position = mul(camera_data.last_frame_projection, mul(camera_data.last_frame_view, mul(data.model, float4(vertex_position, 1.f))));

    output.previous_clipspace_location = last_frame_position.xyw;

    return output;
}

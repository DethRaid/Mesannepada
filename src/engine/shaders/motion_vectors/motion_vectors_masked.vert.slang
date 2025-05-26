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
    float2 texcoord;
};

[shader("vertex")]
VertexOutput main(
    const float3 position_in,
    const uint primitive_id_in,
    const uint vertex_id : SV_VertexID
) {
    VertexOutput output;

    output.primitive_id = primitive_id_in;
    const PrimitiveDataGPU data = primitive_datas[output.primitive_id];

    output.position = mul(camera_data.projection, mul(camera_data.view, mul(data.model, float4(position_in, 1.f))));

    const float4 last_frame_position = mul(camera_data.last_frame_projection, mul(camera_data.last_frame_view, mul(data.model, float4(position_in, 1.f))));

    output.previous_clipspace_location = last_frame_position.xyw;

    output.texcoord = data.vertex_data[vertex_id].texcoord;

    return output;
}

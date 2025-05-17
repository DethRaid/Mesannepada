#include "shared/view_data.hpp"
#include "shared/vertex_data.hpp"
#include "shared/sun_light_constants.hpp"

struct DrawcallDataGPU {
    float4x4 model_matrix;
    float4 tint_color;
    float3* vertex_position_buffer;
    StandardVertexData* vertex_data_buffer;
};

[vk::push_constant]
cbuffer Constants {
    ViewDataGPU* view_data;
    SunLightConstants* sun_light;
    DrawcallDataGPU* drawcall_data;
    uint drawcall_id;
};

struct VertexOut {
    float4 location : SV_Position;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    half4 color : COLOR;
};

[shader("vertex")]
VertexOut main(uint vertex_id : SV_VertexID) {
    VertexOut output = (VertexOut)0;

    const DrawcallDataGPU drawcall = drawcall_data[drawcall_id];
    const float3 vertex_position = drawcall.vertex_position_buffer[vertex_id];
    
    output.location = mul(view_data.projection, mul(view_data.view, mul(drawcall.model_matrix, float4(vertex_position, 1.f))));

    const StandardVertexData data = drawcall.vertex_data_buffer[vertex_id];
    output.normal = data.normal;
    output.texcoord = data.texcoord;
    output.color = unpackUnorm4x8ToHalf(data.color);

    return output;
}

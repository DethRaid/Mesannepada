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

[shader("fragment")]
float4 main(VertexOut vertex) {
    const DrawcallDataGPU drawcall = drawcall_data[drawcall_id];

    const float ndotl = dot(vertex.normal, -sun_light.direction_and_tan_size.xyz) * 0.5 + 0.5;
    const float3 sun_light = sun_light.color.rgb * ndotl;
    
    const float3 color = vertex.color.rgb * drawcall.tint_color.rgb * sun_light * view_data.exposure;

    return float4(color, vertex.color.a * drawcall.tint_color.a);
}

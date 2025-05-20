#include "shared/view_data.hpp"
#include "shared/vertex_data.hpp"
#include "shared/sun_light_constants.hpp"

[vk::push_constant]
cbuffer Constants {
    ViewDataGPU* view_data;
    SunLightConstants* sun_light;
    JoltDebugVertex* vertices;
};

struct VertexOut {
    float4 location : SV_Position;
    half4 color : COLOR;
};

[shader("vertex")]
VertexOut main(uint vertex_id : SV_VertexID) {
    VertexOut output = (VertexOut)0;

    const JoltDebugVertex vertex = vertices[vertex_id];
    
    float4 viewspace_position = mul(view_data.view, float4(vertex.position, 1.f));
    output.location = mul(view_data.projection, viewspace_position);

    output.color = unpackUnorm4x8ToHalf(vertex.color);

    return output;
}

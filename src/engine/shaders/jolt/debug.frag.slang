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

[shader("fragment")]
float4 main(VertexOut vertex) {
    return vertex.color;
}

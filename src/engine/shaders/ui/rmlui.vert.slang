#include "common/packing.slangi"
#include "common/srgb_conversions.slangi"

struct VertexOutput {
    float4 position : SV_Position;
    half4 color;
    float2 texcoord;
};

[vk::push_constant]
cbuffer Constants {
    float2 resolution;
    float2 translation;
    uint texture_index;
};

[shader("vertex")]
VertexOutput main(float2 position_in, half4 color_in, float2 texcoord_in) {
    VertexOutput output = (VertexOutput)0;
    output.position.xy = (position_in + translation) / resolution;
    output.position.xy = output.position.xy * 2.f - 1.f;
    output.position.zw = float2(0, 1);

    output.color = color_in;
    output.color.rgb = srgb_to_linear(output.color.rgb);

    output.texcoord = texcoord_in;

    return output;
}

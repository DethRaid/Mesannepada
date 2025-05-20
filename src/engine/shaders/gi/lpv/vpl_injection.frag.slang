#include "common/spherical_harmonics.slangi"

[vk::push_constant]
cbuffer Constants {
    uint cascade_index;
    uint num_cascades;
};

struct VertexOut {
    int layer : SV_RenderTargetArrayIndex;
    half3 color;
    half3 normal;
    float3 pos_unsnapped;
};

struct PixelOut {
    half4 red;
    half4 green;
    half4 blue;
};

half3 rgb2hsv(half3 c) {
    half4 K = half4(0.h, -1.h / 3.h, 2.h / 3.h, -1.h);
    half4 p = lerp(half4(c.bg, K.wz), half4(c.gb, K.xy), step(c.b, c.g));
    half4 q = lerp(half4(p.xyw, c.r), half4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return half3(abs(q.z + (q.w - q.y) / (6.h * d + e)), d / (q.x + e), q.x);
}

half3 hsv2rgb(half3 c) {
    half4 K = half4(1.h, 2.h / 3.h, 1.h / 3.h, 3.h);
    half3 p = abs(fract(c.xxx + K.xyz) * 6.h - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.h, 1.h), c.y);
}

[shader("fragment")]
PixelOut main(VertexOut vertex, float4 position : SV_Position) {
    half3 scaled_color = vertex.color * (32 * 32) / (128 * 128);

    float2 local_position = position.xy;
    local_position.x -= (32.f * cascade_index);

    float3 d = 1.f - min(abs(vertex.pos_unsnapped - float3(local_position, float(vertex.layer) + 0.5)), 1.0);
    float w = d.x * d.y * d.z;

    half3 normal = vertex.normal;
    // normal.z *= -1;

    half4 sh = dir_to_cosine_lobe(normal);

    PixelOut output = (PixelOut)0;
    output.red = sh * scaled_color.r * w / PI;
    output.green = sh * scaled_color.g * w / PI;
    output.blue = sh * scaled_color.b * w / PI;

    return output;
}

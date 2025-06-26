[vk::binding(0, 1)]
Texture2D<half4> ray_irradiance;

[shader("fragment")]
float4 main(float4 sv_position : SV_Position) {
    return ray_irradiance[(int2)sv_position.xy];
}

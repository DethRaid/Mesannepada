/*
 * Packs our data into the format DLSS-RR needs
 */

Texture2D gbuffer_color;
Texture2D gbuffer_data;

struct Output {
    half4 diffuse_albedo;
    half4 specular_albedo;
};

[shader("fragment")]
Output main(float4 sv_position: SV_Position) {
    const int2 pixel = (int2)sv_position.xy;

    const half3 base_color = (half3)gbuffer_color[pixel].rgb;
    const half4 data_sample = (half4)gbuffer_data[pixel];

    const half metalness = data_sample.b;
    
    // Remapping from https://google.github.io/filament/Filament.html#materialsystem/parameterization/remapping
    const half dielectric_f0 = 0.04h; // TODO: Get this from a texture

    Output output = (Output)0;
    output.diffuse_albedo.rgb = base_color * (1.h - dielectric_f0) * (1.h - metalness);
    output.specular_albedo.rgb = lerp(dielectric_f0.xxx, base_color, metalness);

    return output;
}

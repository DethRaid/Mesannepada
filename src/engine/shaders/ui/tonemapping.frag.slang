#include "tonemap.slangi"

Sampler2D<half3> scene_color_texture;

Sampler2D<half3> bloom_chain;

Texture2D<half2> exposure_factor;

[vk::push_constant]
cbuffer Constants {
    uint tonemapping_mode;
};

half3 blur(const float2 uv, const float mip_level) {
    const float delta = 1.f;

    float2 source_size;

    float num_levels;
    bloom_chain.GetDimensions((uint)mip_level, source_size.x, source_size.y, num_levels);
    float2 inv_source_size = float2(1.0) / source_size;

    float4 o = inv_source_size.xyxy * float2(-delta, delta).xxyy;
    half3 s = 
        bloom_chain.SampleLevel(uv, mip_level).rgb * 4.0h +
        bloom_chain.SampleLevel(uv + float2(o.x, 0), mip_level).rgb * 2.0h +
        bloom_chain.SampleLevel(uv + float2(o.y, 0), mip_level).rgb * 2.0h +
        bloom_chain.SampleLevel(uv + float2(0, o.z), mip_level).rgb * 2.0h +
        bloom_chain.SampleLevel(uv + float2(0, o.w), mip_level).rgb * 2.0h +
        bloom_chain.SampleLevel(uv + o.xy, mip_level).rgb * 1.0h +
        bloom_chain.SampleLevel(uv + o.zy, mip_level).rgb * 1.0h +
        bloom_chain.SampleLevel(uv + o.xw, mip_level).rgb * 1.0h +
        bloom_chain.SampleLevel(uv + o.zw, mip_level).rgb * 1.0h;

    return s / 16.h;
}

half3 sample_bloom_chain(float2 texcoord) {
    half3 result = 0.h;

    for(float mip_level = 0; mip_level < 6.0; mip_level += 1) {
        half3 bloom_sample = blur(texcoord, mip_level);
        result += bloom_sample;
    }

    return result;
}

half to_luminance(const half3 color) { return color.r * 0.2126h + color.g * 0.7152h + color.b * 0.0722h; }

[shader("fragment")]
half4 main(float2 texcoord) {
    half3 bloom = sample_bloom_chain(texcoord);

    half3 scene_color = scene_color_texture.SampleLevel(texcoord, 0);

    scene_color.rgb += bloom * 0.014159h;

    const half exposure = exposure_factor[int2(0, 0)].x;

    scene_color *= exposure;

    half3 mapped_color;
    switch (tonemapping_mode) {
    case 0:
        mapped_color = Tonemap_Uchimura(scene_color);
        break;
    case 1:
        mapped_color = ungamma(Uncharted2ToneMapping(scene_color));
        break;
    case 2:
        mapped_color = Tonemap_Filmic(scene_color);
        break;
    case 3:
        mapped_color = PBRNeutralToneMapping(scene_color);
        break;
    case 4:
        mapped_color = Tonemap_Reinhart(scene_color);
        break;
    case 5:
        mapped_color = reinhard_jodie(scene_color);
        break;
    default:
        mapped_color = scene_color;
    }

    return half4(mapped_color.rgb, 1.h);
}

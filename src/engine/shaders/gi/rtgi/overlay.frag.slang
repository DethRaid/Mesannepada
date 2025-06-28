#include "NRDEncoding.hlsli"
#include "NRD.hlsli"

Texture2D<half4> ray_irradiance;

[vk::push_constant]
cbuffer Constants {
    uint denoiser_type; // See render::DenoiserType
}

[shader("fragment")]
float4 main(float4 sv_position : SV_Position) {
    float4 irradiance = ray_irradiance[(int2)sv_position.xy];
    if(denoiser_type == 1) { // ReBLUR
        irradiance = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(irradiance);
    } else if(denoiser_type == 2) { // ReLAX
        irradiance = RELAX_BackEnd_UnpackRadiance(irradiance);
    } 

    // Boost our GI for totally cis purposes
    return 3.h * irradiance;
}

#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "common/spherical_harmonics.glsl"
#include "common/brdf.glsl"
#include "shared/lpv.hpp"
#include "shared/view_data.hpp"
#include "shared/sun_light_constants.hpp"

// Gbuffer textures

layout(set = 0, binding = 0) uniform sampler2D gbuffer_base_color;
layout(set = 0, binding = 1) uniform sampler2D gbuffer_normal;
layout(set = 0, binding = 2) uniform sampler2D gbuffer_data;
layout(set = 0, binding = 3) uniform sampler2D gbuffer_emission;
layout(set = 0, binding = 4) uniform sampler2D gbuffer_depth;

// LPV textures
layout(set = 1, binding = 0) uniform sampler3D lpv_red;
layout(set = 1, binding = 1) uniform sampler3D lpv_green;
layout(set = 1, binding = 2) uniform sampler3D lpv_blue;
layout(set = 1, binding = 3) uniform LpvCascadeBuffer {
    LPVCascadeMatrices cascade_matrices[4];
};

layout(set = 1, binding = 4) uniform ViewUniformBuffer {
    ViewDataGPU view_info;
};

layout(set = 1, binding = 5) uniform sampler2D ao_texture;

layout(set = 1, binding = 6) uniform SunUniformBuffer {
    SunLightConstants sun_light;
};

layout(set = 1, binding = 7, rgba16f) uniform readonly image2D noise;

layout(push_constant) uniform Constants {
    uint num_cascades;
    float exposure;
};

// Texcoord from the vertex shader
layout(location = 0) in vec2 texcoord;

// Lighting output
layout(location = 0) out mediump vec4 lighting;

vec3 get_viewspace_position() {
    float depth = texelFetch(gbuffer_depth, ivec2(gl_FragCoord.xy), 0).r;
    vec2 texcoord = (gl_FragCoord.xy + 0.5) / view_info.render_resolution.xy;
    vec4 ndc_position = vec4(vec3(texcoord * 2.0 - 1.0, depth), 1.f);
    vec4 viewspace_position = view_info.inverse_projection * ndc_position;
    viewspace_position /= viewspace_position.w;

    return viewspace_position.xyz;
}

mediump vec3 sample_light_from_cascade(mediump vec4 normal_coefficients, vec4 position_worldspace, uint cascade_index) {
    vec4 cascade_position = cascade_matrices[cascade_index].world_to_cascade * position_worldspace;

    cascade_position.x += float(cascade_index);
    cascade_position.x /= num_cascades;

    mediump vec4 red_coefficients = texture(lpv_red, cascade_position.xyz);
    mediump vec4 green_coefficients = texture(lpv_green, cascade_position.xyz);
    mediump vec4 blue_coefficients = texture(lpv_blue, cascade_position.xyz);

    mediump float red_strength = dot(red_coefficients, normal_coefficients);
    mediump float green_strength = dot(green_coefficients, normal_coefficients);
    mediump float blue_strength = dot(blue_coefficients, normal_coefficients);

    return vec3(red_strength, green_strength, blue_strength);
}

float map(float value, float inMin, float inMax, float outMin, float outMax) {
  return outMin + (outMax - outMin) * (value - inMin) / (inMax - inMin);
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);

    const float depth = texelFetch(gbuffer_depth, pixel, 0).r;
    if(depth == 0) {
        discard;
    }

    mediump vec3 base_color_sample = texelFetch(gbuffer_base_color, pixel, 0).rgb;
    mediump vec3 normal_sample = normalize(texelFetch(gbuffer_normal, pixel, 0).xyz);
    mediump vec4 data_sample = texelFetch(gbuffer_data, pixel, 0);
    mediump vec4 emission_sample = texelFetch(gbuffer_emission, pixel, 0);

    vec3 viewspace_position = get_viewspace_position();
    vec4 worldspace_position = view_info.inverse_view * vec4(viewspace_position, 1.0);

    vec3 view_position = vec3(view_info.inverse_view[0][3], view_info.inverse_view[1][3], view_info.inverse_view[2][3]);
    vec3 worldspace_view_vector = normalize(worldspace_position.xyz - view_position);

    SurfaceInfo surface;
    surface.base_color = base_color_sample;
    surface.normal = normal_sample;
    surface.metalness = data_sample.b;
    surface.roughness = data_sample.g;
    surface.emission = emission_sample.rgb;
    surface.location = worldspace_position.xyz;

    // Dither for blending purposes
    const vec3 cascade_selection_position = worldspace_position.xyz - worldspace_view_vector * imageLoad(noise, pixel % 128).rrr;

    vec3 lpv_normal = -surface.normal;
    lpv_normal.x *= -1;
    mediump vec4 normal_coefficients = dir_to_sh(lpv_normal);

    float cascade_weights[4] = float[](0, 0, 0, 0);
    mediump vec3[4] cascade_samples;

    uint selected_cascade = 0;
    for(int i = int(num_cascades) - 1; i >= 0; i--) {
        mediump vec4 cascade_position = cascade_matrices[i].world_to_cascade * vec4(cascade_selection_position, 1);
        if(all(greaterThan(cascade_position.xyz, vec3(0))) && all(lessThan(cascade_position.xyz, vec3(1)))) {
            selected_cascade = i;

            // Base weight for this cascade. We want to fade out when we're four cells from the cascade boundary
            float distance_from_center = length(cascade_position.xyz - 0.5);
            const float fade_factor = clamp(map(distance_from_center, 0.25, 0.375, 0, 1), 0, 1);
            const float base_weight = 1.f - sqrt(fade_factor);

            cascade_weights[i] = base_weight / pow(8, i);
            //cascade_weights[i] *= cascade_weights[i];
            mediump vec4 offset = vec4(surface.normal + float(i) * 0.01f, 0);
            cascade_samples[i] = sample_light_from_cascade(normal_coefficients, worldspace_position + offset, i);
        }
    }
     
    float total_weight = 0;
    mediump vec3 indirect_light = vec3(0);
    for(uint i = 0; i < num_cascades; i++) {
        total_weight += cascade_weights[i];
        indirect_light += cascade_samples[i] * cascade_weights[i] ;
    }

    indirect_light /= total_weight;

    vec3 reflection_vector = reflect(-worldspace_view_vector, surface.normal);
    mediump vec3 specular_light = vec3(0);
    if(selected_cascade == 0) {
        vec4 cascade_position = cascade_matrices[0].world_to_cascade * worldspace_position;
    
        mediump vec4 red_coefficients = texture(lpv_red, cascade_position.xyz);
        mediump vec4 green_coefficients = texture(lpv_green, cascade_position.xyz);
        mediump vec4 blue_coefficients = texture(lpv_blue, cascade_position.xyz);
    
        mediump vec4 reflection_coefficients = dir_to_sh(reflection_vector);
    
        mediump float red_strength = dot(red_coefficients, reflection_coefficients);
        mediump float green_strength = dot(green_coefficients, reflection_coefficients);
        mediump float blue_strength = dot(blue_coefficients, reflection_coefficients);
    
        specular_light = vec3(red_strength, green_strength, blue_strength);
    
        const uint num_additional_specular_samples = 1;
        for(uint sample_index = 1; sample_index <= num_additional_specular_samples; sample_index++) {
            vec3 sample_location = worldspace_position.xyz + reflection_vector * sample_index;
            
            cascade_position = cascade_matrices[0].world_to_cascade * vec4(sample_location, 1.f);
            red_coefficients = texture(lpv_red, cascade_position.xyz);
            green_coefficients = texture(lpv_green, cascade_position.xyz);
            blue_coefficients = texture(lpv_blue, cascade_position.xyz);
    
            red_strength = dot(red_coefficients, reflection_coefficients);
            green_strength = dot(green_coefficients, reflection_coefficients);
            blue_strength = dot(blue_coefficients, reflection_coefficients);
    
            specular_light += vec3(red_strength, green_strength, blue_strength);
        }
    
        specular_light /= vec3(num_additional_specular_samples + 1);
    }

    const mediump vec3 diffuse_factor = Fd(surface, surface.normal, surface.normal);

    const mediump vec3 specular_factor = Fr(surface, surface.normal, reflection_vector);

    const mediump float ao = texelFetch(ao_texture, pixel, 0).r;

    mediump vec3 total_lighting = indirect_light * diffuse_factor * ao + specular_light * specular_factor;

    if(any(isnan(total_lighting))) {
        total_lighting = vec3(0);
    }

    lighting = vec4(total_lighting * view_info.exposure * exposure, 1.f);
    //lighting = vec4(ao.xxx, 1.f);

    // if(selected_cascade == 0) {
    //     lighting = vec4(1, 0, 0, 1);
    // } else if(selected_cascade == 1) {
    //     lighting = vec4(0, 1, 0, 1);
    // } else if(selected_cascade == 2) {
    //     lighting = vec4(0, 0, 1, 1);
    // } else if(selected_cascade == 3) {
    //     lighting = vec4(1, 1, 0, 1);
    // }
}

#version 460

/**
 * Generates a SH point cloud from a depth and normal image
 */

#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_ARB_shader_viewport_layer_array : enable

#include "common/spherical_harmonics.glsl"
#include "shared/lpv.hpp"
#include "shared/view_info.hpp"

layout(set = 0, binding = 0) uniform sampler2DArray normal_target;
layout(set = 0, binding = 1) uniform sampler2DArray depth_target;

layout(set = 0, binding = 2, scalar) uniform LpvCascadeBuffer {
    LPVCascadeMatrices cascade_matrices[4];
};

layout(set = 0, binding = 3) uniform ViewUniformBuffer {
    ViewInfo view_info;
};

layout(push_constant) uniform Constants {
    uint cascade_index;
    uint resolution_x;
    uint resolution_y;
    uint num_cascades;
};

layout(location = 0) out mediump vec4 sh;

void main() {
    uint x = (gl_VertexIndex * 2) % resolution_x;
    uint y = (gl_VertexIndex * 2) / resolution_x;

    if(x >= resolution_x || y >= resolution_y) {
        gl_Position = vec4(0) / 0.f;
        return;
    }

    float texcoord_x = (0.5 + float(x)) / float(resolution_x);
    float texcoord_y = (0.5 + float(y)) / float(resolution_y);

    float depth = texture(depth_target, vec3(texcoord_x, texcoord_y, cascade_index)).x;

    vec2 screenspace = vec2(x, y) / vec2(resolution_x, resolution_y);
    vec4 ndc_position = vec4(screenspace * 2.f - 1.f, depth, 1);
    vec4 viewspace_position = view_info.inverse_projection * ndc_position;
    viewspace_position /= viewspace_position.w;

    vec4 worldspace_position = view_info.inverse_view * viewspace_position;

    vec4 cascade_position = cascade_matrices[cascade_index].world_to_cascade * worldspace_position;

    if(any(lessThan(cascade_position.xyz, vec3(0))) || any(greaterThan(cascade_position.xyz, vec3(1)))) {
        gl_Position = vec4(0) / 0.f;
        return;
    }

    mediump vec3 normal = normalize(texture(normal_target, vec3(texcoord_x, texcoord_y, cascade_index)).xyz * 2.f - 1.f);
    sh = dir_to_cosine_lobe(normal);

    // Add a half-cell offset to the GV
    cascade_position += 0.5f / 32.f;
   
    cascade_position.x += cascade_index;
    cascade_position.x /= num_cascades;

    gl_Position = vec4(cascade_position.xy * 2.f - 1.f, 0.f, 1.f);
    gl_Layer = int(cascade_position.z * 32.f);
    gl_PointSize = 1;
}

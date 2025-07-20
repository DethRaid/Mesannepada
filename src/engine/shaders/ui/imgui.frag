#version 460

layout(location = 0) in mediump vec2 texcoord_in;
layout(location = 1) in lowp vec4 color_in;

layout(location = 0) out vec4 color_out;

layout(set = 0, binding = 0) uniform sampler2D mask_texture;

layout(push_constant) uniform Constants {
    uvec2 render_resolution;
    uint has_texture;
};

void main() {
    vec4 color = color_in;
    if (has_texture != 0) {
        color.a *= texture(mask_texture, texcoord_in).r;
    }

    color_out = color;
}

#version 460 core

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out vec2 texcoord;

void main() {
    float       x = (gl_VertexIndex & 2);
    float       y = (gl_VertexIndex & 1);
    gl_Position = vec4 (2, 4, 0, 1) * vec4 (x, y, 0, 1) - vec4 (1, 1, 0, 0);
    texcoord = vec2(1, 2) * vec2(x, y);
}

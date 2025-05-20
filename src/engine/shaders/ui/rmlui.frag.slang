struct VertexOutput {
    half4 color;
    float2 texcoord;
};

Sampler2D<half4> textures[];

[vk::push_constant]
cbuffer Constants {
    float2 resolution;
    float2 translation;
    uint texture_index;
};

[shader("fragment")]
half4 main(VertexOutput vertex) {
    half4 color = textures[texture_index].Sample(vertex.texcoord);
    color *= vertex.color;

    return color;
}

#include "shared/primitive_data.hpp"

StructuredBuffer<PrimitiveDataGPU> primitive_data_buffer;

StructuredBuffer<SkeletalPrimitiveDataGPU> skeletal_data_buffer;

[vk::push_constant]
cbuffer Constants {
    uint skeletal_primitive_index;
    uint primitive_index;
    uint num_vertices;
};

[numthreads(64, 1, 1)]
[shader("compute")]
void main(uint vertex_id : SV_DispatchThreadID) {
    if(vertex_id >= num_vertices) {
        return;
    }

    const PrimitiveDataGPU primitive_data = primitive_data_buffer[primitive_index];
    const SkeletalPrimitiveDataGPU skeletal_primitive_data = skeletal_data_buffer[skeletal_primitive_index];

    const float4 bone_weights = skeletal_primitive_data.weights[vertex_id];
    const u16vec4 bones = skeletal_primitive_data.bone_ids[vertex_id];

    const float4 original_position = float4(skeletal_primitive_data.original_positions[vertex_id], 1);
    const float4 original_normal = float4(skeletal_primitive_data.original_data[vertex_id].normal, 0);

    float4 position = 0;
    float4 normal = 0;
    for(int i = 0; i < 4; i++) {
        const uint16_t bone_id = bones[i];
        const float4x4 bone_transform = skeletal_primitive_data.bone_transforms[bone_id];
        
        position += bone_weights[i] * mul(bone_transform, original_position);
        normal += bone_weights[i] * mul(bone_transform, original_normal);
    }

    normal.xyz = normalize(normal.xyz);

    primitive_data.vertex_positions[vertex_id] = position.xyz;
    primitive_data.vertex_data[vertex_id] = skeletal_primitive_data.original_data[vertex_id];
    primitive_data.vertex_data[vertex_id].normal = normal.xyz;
}

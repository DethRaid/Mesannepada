/**
 * Simple shader to write out drawcalls for skinned meshes. These differ from normals drawcalls in that the start 
 * instance is the skinned mesh primitive index, not the regular primitive index
 *
 * Currently the only thing using these is the motion vectors pass, the other passes can use the single set of vertex 
 * buffers in the PrimitiveDataGPU
 */

#include "shared/primitive_data.hpp"

struct DrawCommand {
    uint index_count;
    uint instance_count;
    uint first_index;
    int  vertex_offset;
    uint first_instance;
};

StructuredBuffer<uint> visibility_list;
StructuredBuffer<DrawCommand> meshes;
StructuredBuffer<SkeletalPrimitiveDataGPU> skeletal_data_buffer;
StructuredBuffer<PrimitiveDataGPU> primitive_data_buffer;

RWStructuredBuffer<uint> draw_count;
RWStructuredBuffer<DrawCommand> draw_commands;

[vk::push_constants]
cbuffer Constants {
    uint num_skeletal_primitives;
};

[shader("compute")]
[numthread[64, 1, 1]]
void main(uint thread_id : SV_DispatchThreadID) {
    if(thread_id >= num_skeletal_primitives) {
        return;
    }

    const SkeletalPrimitiveDataGPU skeletal_data = skeletal_data_buffer[thread_id];
    const uint primitive_id = skeletal_data.primitive_id;
    const uint visibility = visibility_list[primitive_id];
    if(visibility == 0) {
        return;
    }

    uint draw_id;
    InterlockedAdd(draw_count[0], 1, draw_id);

    const PrimitiveDataGPU primitive_data = primitive_daa_buffer[primitive_id];
    draw_commands[draw_id] = meshes[primitive_data.mesh_id];
    draw_commands[draw_id].first_instance = thread_id;
}
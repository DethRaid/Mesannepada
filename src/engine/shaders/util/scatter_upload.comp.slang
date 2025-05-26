#include "shared/primitive_data.hpp"

[vk::push_constant]
cbuffer Constants {
    uint* scatter_indices;
    uint* src_data;
    uint* dest_data;
    uint num_data;
    uint data_size; // Size of the data to copy in uints
};

[numthreads(64, 1, 1)]
[shader("compute")]
void main(uint index : SV_DispatchThreadID) {
    if(index >= num_data) {
        return;
    }

    uint dest_index = scatter_indices[index] * data_size;

    uint read_index = index * data_size;
    for(uint offset = 0; offset < data_size; offset++) {
        dest_data[dest_index + offset] = src_data[read_index + offset];
    }
}

#include "common/octahedral.slangi"
#include "probe_update.slangi"
#include "shared/gi_probe.hpp"

ConstantBuffer<IrradianceProbeVolume> probe_volume;

StructuredBuffer<uint3> probes_to_update;

Texture2DArray<half4> trace_results_texture;

RWTexture2DArray<half3> light_cache_dest;

[numthreads(11, 11, 1)]
[shader("compute")]
void main(uint3 group_id: SV_GroupID, uint3 thread_id: SV_GroupThreadID) {
    const uint probe_index = group_id.x;
    const uint3 probe_id = probes_to_update[probe_index];

    // This thread updates one pixel in the depth texture. We find a cone that represents the depth texel, then sample
    // from the trace results to get the average depth and depth squared

    const float2 octant_coords = get_normalized_octahedral_coordinates(thread_id.xy, probe_volume.light_cache_probe_resolution);
    const half3 direction = (half3)get_octahedral_direction(octant_coords);

    // The source texture is 20x20, we're 11x11. We can sample a 2x2 region of pixels
    const uint filter_size = (uint)ceil((float)probe_volume.trace_resolution.x / (float)probe_volume.light_cache_probe_resolution.x);
    const uint2 source_base_pixel = (uint2)floor((float2)thread_id.xy * filter_size);

    half3 light = (half3)0;
    half num_valid_samples = 0;
    for (uint i = 0; i < filter_size * filter_size; i++) {
        const uint2 offset = uint2(i % filter_size, i / filter_size);
        const uint2 read_pixel = source_base_pixel + offset;
        const half4 irradiance = trace_results_texture[uint3(read_pixel, probe_index)];
        
        if (irradiance.w > 0) {
            const float2 octant_ray_coord = get_normalized_octahedral_coordinates(read_pixel, 20);
            const float3 ray_direction = get_octahedral_direction(octant_ray_coord);
            
            const half weight = dot(direction, ray_direction);
            light += irradiance.rgb * weight;
            num_valid_samples += 1.h;
        }
    }

    if (num_valid_samples > 0) {
        light /= num_valid_samples;
    } else {
        light = (half3)0;
    }

    write_probe_texel_with_border(light_cache_dest, probe_volume.light_cache_probe_resolution, probe_id, thread_id.xy, light);
}

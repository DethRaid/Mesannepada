#include "shared/view_data.hpp"
#include "shared/gi_probe.hpp"
#include "gi/rtgi/rtgi_payload.slangi"
#include "probe_sampling.slangi"
#include "common/octahedral.slangi"

[vk::binding(2, 0)]
StructuredBuffer<uint3> probes_to_update;

[vk::binding(3, 0)]
RaytracingAccelerationStructure rtas;

[vk::binding(4, 0)]
ConstantBuffer<IrradianceProbeVolume> probe_volume;

[vk::binding(5, 0)]
RWTexture2DArray<half4> trace_results_texture;

[vk::binding(6, 0)]
Texture2D<float3> noisetex;

[vk::binding(7, 0)]
Sampler2DArray<half3> rtgi;

[vk::binding(8, 0)]
Sampler2DArray<half2> depth;

[vk::binding(11, 0)]
Texture2DArray<half> validity;

[vk::binding(12, 0)]
ConstantBuffer<ViewDataGPU> view_data;

/**
 * Yeet 400 rays into the aether. Convolve them into our textures. Party
 *
 * This shader doesn't update the irradiance cache itself. Rather, we write the new probe data to the new_probe_data
 * buffer. A later compute shader will take that information, generate the RTGI and Average data, and write it all to 
 * irradiance textures. This helps with sync - the RT shaders can sample the irradiance textures as needed, without
 * race conditions between this shader
 */
[shader("raygen")]
void main_raygen() {
    const uint3 thread_id = DispatchRaysIndex();
    const uint probe_index = thread_id.z;

    const uint3 probe_id = probes_to_update[probe_index];

    const uint cascade_index = probe_id.y / 8;
    const ProbeCascade cascade = probe_volume.cascades[cascade_index];

    const float3 local_probe_id = (float3)uint3(probe_id.x, probe_id.y % 8, probe_id.z);

    const float2 octant_ray_coord = get_normalized_octahedral_coordinates(thread_id.xy, probe_volume.trace_resolution);
    const float3 ray_direction = get_octahedral_direction(octant_ray_coord);

    float ray_distance = 8192;
    if (cascade_index < 3) {
        ray_distance = probe_volume.cascades[cascade_index + 1].probe_spacing * 4;
    }

    RayDesc ray = (RayDesc)0;
    ray.Origin = cascade.min + local_probe_id * cascade.probe_spacing;
    ray.Direction = ray_direction;
    ray.TMin = 0.05;
    ray.TMax = ray_distance;

    IrradiancePayload payload = (IrradiancePayload)0;
    payload.remaining_bounces = 0;

    TraceRay(
        rtas,
        RAY_FLAG_NONE,
        0xFF,
        RADIANCE_HIT_GROUP_OFFSET,
        1,
        RADIANCE_HIT_GROUP_OFFSET,
        ray,
        payload);

    if (payload.ray_distance == 0) {
        // If we didn't hit anything and there's a next-largest cascade to sample, sample from it. Otherwise we just 
        // keep the sky irradiance in the payload
        if (cascade_index + 1 < 4) {
            payload.irradiance = sample_cascade(
                ray.Origin + ray.Direction * ray.TMax,
                ray.Direction,
                cascade_index + 1,
                probe_volume.cascades[cascade_index + 1],
                rtgi,
                probe_volume.rgti_probe_resolution,
                depth,
                validity);
        } else {
            payload.irradiance *= 10.f;
        }

        payload.ray_distance = ray_distance;

    } else if (payload.ray_distance < 0) {
        // We hit the back of a surface. Set this ray to black so we don't mess up our results
        payload.irradiance = 0;
    }

    trace_results_texture[thread_id] = half4((half3)payload.irradiance * view_data.exposure, (half)payload.ray_distance);
}

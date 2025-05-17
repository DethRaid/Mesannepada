// from https://bruop.github.io/exposure/

#define GROUP_SIZE 256
#define THREADS_X 16
#define THREADS_Y 16

#define EPSILON 0.005
// Taken from RTR vol 4 pg. 278
#define RGB_TO_LUM float3(0.2125, 0.7154, 0.0721)

Texture2D<half3> tex_color;

RWStructuredBuffer<uint> histogram;

[vk::push_constant]
cbuffer Constants {
    float min_luminance;
    float max_luminance;
    uint2 dim;
}

// Shared histogram buffer used for storing intermediate sums for each work group
groupshared uint histogramShared[GROUP_SIZE];

// For a given color and luminance range, return the histogram bin index
uint colorToBin(half3 hdrColor, float minLogLum, float inverseLogLumRange) {
    // Convert our RGB value to Luminance, see note for RGB_TO_LUM macro above
    float lum = dot(hdrColor, RGB_TO_LUM);
  
    // Avoid taking the log of zero
    if (lum < EPSILON) {
      return 0;
    }
  
    // Calculate the log_2 luminance and express it as a value in [0.0, 1.0]
    // where 0.0 represents the minimum luminance, and 1.0 represents the max.
    float logLum = clamp((log2(lum) - minLogLum) * inverseLogLumRange, 0.0, 1.0);
  
    // Map [0, 1] to [1, 255]. The zeroth bin is handled by the epsilon check above.
    return uint(logLum * 254.0 + 1.0);
}

// 16 * 16 * 1 threads per group
[numthreads(THREADS_X, THREADS_Y, 1)]
[shader("compute")]
void main(uint local_invocation_index: SV_GroupIndex, uint2 thread_id: SV_DispatchThreadID) {
    // Initialize the bin for this thread to 0
    histogramShared[local_invocation_index] = 0;
    GroupMemoryBarrierWithGroupSync();

    // Ignore threads that map to areas beyond the bounds of our HDR image
    if (thread_id.x < dim.x && thread_id.y < dim.y) {
        half3 hdrColor = tex_color[thread_id];
        uint binIndex = colorToBin(hdrColor, min_luminance, max_luminance);
        // We use an atomic add to ensure we don't write to the same bin in our
        // histogram from two different threads at the same time.
        InterlockedAdd(histogramShared[binIndex], 1);
    }

    // Wait for all threads in the work group to reach this point before adding our
    // local histogram to the global one
    GroupMemoryBarrierWithGroupSync();

    // Technically there's no chance that two threads write to the same bin here,
    // but different work groups might! So we still need the atomic add.
    InterlockedAdd(histogram[local_invocation_index], histogramShared[local_invocation_index]);
}

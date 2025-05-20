#define GROUP_SIZE 256
#define THREADS_X 256
#define THREADS_Y 1

[vk::push_constant]
cbuffer Constants {
    float minLogLum;
    float logLumRange;
    float timeCoeff;
    float numPixels;
};

RWStructuredBuffer<uint> histogram;

RWTexture2D<float> s_target;

RWTexture2D<float> exposure_image;

// Shared
groupshared uint histogramShared[GROUP_SIZE];

[numthreads(THREADS_X, THREADS_Y, 1)] 
[shader("compute")]
void main(uint local_invocation_index: SV_GroupIndex, uint2 thread_id: SV_DispatchThreadID) {
    // Get the count from the histogram buffer
    uint countForThisBin = histogram[local_invocation_index];
    histogramShared[local_invocation_index] = countForThisBin * local_invocation_index;

    GroupMemoryBarrierWithGroupSync();

    // Reset the count stored in the buffer in anticipation of the next pass
    histogram[local_invocation_index] = 0;
  
    // This loop will perform a weighted count of the luminance range
    for (uint cutoff = (GROUP_SIZE >> 1); cutoff > 0; cutoff >>= 1) {
        if (uint(local_invocation_index) < cutoff) {
            histogramShared[local_invocation_index] += histogramShared[local_invocation_index + cutoff];
        }

        GroupMemoryBarrierWithGroupSync();
    }

    // We only need to calculate this once, so only a single thread is needed.
    if (local_invocation_index == 0) {
        // Here we take our weighted sum and divide it by the number of pixels
        // that had luminance greater than zero (since the index == 0, we can
        // use countForThisBin to find the number of black pixels)
        float weightedLogAverage = (histogramShared[0] / max(numPixels - float(countForThisBin), 1.0)) - 1.0;
  
        // Map from our histogram space to actual luminance
        float weightedAvgLum = exp2(((weightedLogAverage / 254.0) * logLumRange) + minLogLum);
  
        // The new stored value will be interpolated using the last frames value
        // to prevent sudden shifts in the exposure.
        float lumLastFrame = s_target[int2(0, 0)].x;
        float adaptedLum = lumLastFrame + (weightedAvgLum - lumLastFrame) * timeCoeff;
        if (isnan(adaptedLum)) {
            adaptedLum = 0;
        }
        s_target[int2(0, 0)] = adaptedLum;

        exposure_image[int2(0, 0)] = 0.18 / (adaptedLum * (1.0 - 0.18));
    }
}

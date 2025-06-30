#pragma once

#include <EASTL/bonus/fixed_ring_buffer.h>

class PerformanceTracker {
public:
    PerformanceTracker();

    void add_frame_time_sample(float sample);

    void add_memory_sample(uint64_t sample);

    float get_average_frame_time() const;

    uint64_t get_average_gpu_memory() const;

private:
    static constexpr auto max_num_samples = 300;

    eastl::fixed_ring_buffer<float, max_num_samples> frame_time_samples;

    eastl::fixed_ring_buffer<uint64_t, max_num_samples> gpu_memory_samples;
};

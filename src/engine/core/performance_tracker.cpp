#include "performance_tracker.hpp"

PerformanceTracker::PerformanceTracker() : frame_time_samples(max_num_samples), gpu_memory_samples(max_num_samples) {
}

void PerformanceTracker::add_frame_time_sample(const float sample) {
    if(frame_time_samples.size() >= max_num_samples) {
        frame_time_samples.pop_front();
    }

    frame_time_samples.push_back(sample);
}

void PerformanceTracker::add_memory_sample(const uint64_t sample) {
    if(gpu_memory_samples.size() >= max_num_samples) {
        gpu_memory_samples.pop_front();
    }

    gpu_memory_samples.push_back(sample);
}

float PerformanceTracker::get_average_frame_time() const {
    auto average = 0.f;
    auto count = 0.f;
    for(const auto sample : frame_time_samples) {
        average += sample;
        count++;
    }

    return average / count;
}

uint64_t PerformanceTracker::get_average_gpu_memory() const {
    if(gpu_memory_samples.empty()) {
        return 0;
    }

    uint64_t average = 0;
    uint64_t count = 0;
    for(const auto sample : gpu_memory_samples) {
        average += sample;
        count++;
    }

    return average / count;
}

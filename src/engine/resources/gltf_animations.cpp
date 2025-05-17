#include "gltf_animations.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

void AnimationEventSampler::tick(const float time) {
    const auto local_time = time - start_time;
    auto index = current_index;

    while (index + 1 < timeline->timestamps.size() && local_time > timeline->timestamps[index + 1]) {
        index++;
    }

    // If the index has changed, fire off the event and save the new index
    if(index != current_index) {
        timeline->values.at(index)();
        current_index = index;
    }
}

bool AnimationEventSampler::is_ended(const float time) const {
    const auto local_time = time - start_time;
    return local_time > timeline->timestamps.back();
}

bool NodeAnimator::has_animation_ended(const float time) const {
    const auto local_time = time - start_time;

    auto position_ended = true;
    if(position_sampler) {
        if(local_time < position_sampler->timeline->timestamps.back()) {
            position_ended = false;
        }
    }
    auto rotation_ended = true;
    if (rotation_sampler) {
        if (local_time < rotation_sampler->timeline->timestamps.back()) {
            rotation_ended = false;
        }
    }
    auto scale_ended = true;
    if (scale_sampler) {
        if (local_time < scale_sampler->timeline->timestamps.back()) {
            scale_ended = false;
        }
    }
    return position_ended && rotation_ended && scale_ended;
}

float4x4 NodeAnimator::sample(const float time) {
    const auto local_time = time - start_time;

    float4x4 transform{1.f};
    if(position_sampler) {
        const auto position = position_sampler->sample(local_time);
        transform = glm::translate(transform, position);
        // spdlog::debug("Position: {}, {}, {}", position.x, position.y, position.z);
    }
    if(rotation_sampler) {
        const auto rotation = rotation_sampler->sample(local_time);
        const auto euler_rotation = glm::eulerAngles(rotation);
        transform = transform * glm::mat4_cast(rotation);
    }
    if(scale_sampler) {
        const auto scale = scale_sampler->sample(local_time);
        transform = glm::scale(transform, scale);
    }

    return transform;
}

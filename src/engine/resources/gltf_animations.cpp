#include "gltf_animations.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <tracy/Tracy.hpp>

void AnimationEventSampler::tick(const float time) {
    ZoneScopedN("AnimationEventSampler::tick");
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

float NodeAnimator::get_duration() const {
    float duration = 0;

    if(position_sampler) {
        duration = eastl::max(duration, position_sampler->timeline->timestamps.back());
    }
    if (rotation_sampler) {
        duration = eastl::max(duration,  rotation_sampler->timeline->timestamps.back());
    }
    if (scale_sampler) {
        duration = eastl::max(duration,  scale_sampler->timeline->timestamps.back());
    }

    return duration;
}

bool NodeAnimator::has_animation_ended(const float time) const {
    auto position_ended = true;
    if(position_sampler) {
        if(time < position_sampler->timeline->timestamps.back()) {
            position_ended = false;
        }
    }
    auto rotation_ended = true;
    if (rotation_sampler) {
        if (time < rotation_sampler->timeline->timestamps.back()) {
            rotation_ended = false;
        }
    }
    auto scale_ended = true;
    if (scale_sampler) {
        if (time < scale_sampler->timeline->timestamps.back()) {
            scale_ended = false;
        }
    }
    return position_ended && rotation_ended && scale_ended;
}

float4x4 NodeAnimator::sample(const float time) {
    float4x4 transform{1.f};
    if(position_sampler) {
        const auto position = position_sampler->sample(time);
        transform = glm::translate(transform, position);
        // spdlog::debug("Position: {}, {}, {}", position.x, position.y, position.z);
    }
    if(rotation_sampler) {
        const auto rotation = rotation_sampler->sample(time);
        transform = transform * glm::mat4_cast(rotation);
    }
    if(scale_sampler) {
        const auto scale = scale_sampler->sample(time);
        transform = glm::scale(transform, scale);
    }

    return transform;
}

bool SkeletonAnimator::has_animation_ended(const float time) const {
    auto ended = true;
    for(const auto& animator : joint_animators) {
        ended &= animator.has_animation_ended(time);
    }
    return ended;
}

void SkeletonAnimator::update_bones(const eastl::span<Bone> bones, const float time) {
    for(auto& animator : joint_animators) {
        auto& target_bone = bones[animator.target_node];
        target_bone.local_transform = animator.sample(time);
    }
}

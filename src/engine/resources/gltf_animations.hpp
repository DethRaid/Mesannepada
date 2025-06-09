#pragma once

#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <glm/gtx/compatibility.hpp>
#include <glm/gtx/norm.hpp>

#include "animation/bone.hpp"
#include "shared/prelude.h"

/*
 * Basic animation support, based on https://lisyarus.github.io/blog/posts/gltf-animation.html
 */

template <typename T>
struct AnimationTimeline {
    eastl::vector<float> timestamps;
    eastl::vector<T> values;
};

struct TransformAnimation {
    eastl::optional<AnimationTimeline<float3>> position = eastl::nullopt;
    eastl::optional<AnimationTimeline<glm::quat>> rotation = eastl::nullopt;
    eastl::optional<AnimationTimeline<float3>> scale = eastl::nullopt;
};

struct Animation {
    /**
     * Animations of each node. Uses the glTF node ID as the key, and the node's animations are the values
     */
    eastl::unordered_map<size_t, TransformAnimation> channels;

    /**
     * Events for the animation. These fire when the animation evaluator reaches the keyframes they're attached to
     */
    AnimationTimeline<eastl::function<void()>> events;

    /**
     * Adds an event to this animation's events timeline, inserting it so that the keyframes stay sorted
     *
     * It's not recommended to call this while the animation is playing, this should only be called in init code when possible
     *
     * @param time Time for the event to fire at. Unfortunately glTF doesn't give us raw keyframes
     * @param func Function to execute for the event. Be careful about what you capture! Stack variables are a no-no
     */
    template<typename FuncType>
    void add_event(float time, FuncType func);
};

template <typename T, typename InterpolationFunc>
struct AnimationSampler {
    /**
     * Samples the value of a spline at the given time. The time is relative to the start of the spline
     */
    T sample(float time);

    size_t current_index = 0;

    const AnimationTimeline<T>* timeline;
};

struct AnimationEventSampler {
    void tick(float time);

    bool is_ended(float time) const;

    int32_t current_index = -1;

    float start_time = 0;

    const AnimationTimeline<eastl::function<void()>>* timeline;
};

template <typename T>
struct LerpFunctor {
    T operator()(const T& a, const T& b, float t);
};

template <typename T>
struct SlerpFunctor {
    T operator()(const T& a, const T& b, float t);
};

using PositionAnimationSampler = AnimationSampler<float3, LerpFunctor<float3>>;
using RotationAnimationSampler = AnimationSampler<glm::quat, SlerpFunctor<glm::quat>>;
using ScaleAnimationSampler = AnimationSampler<float3, LerpFunctor<float3>>;

struct NodeAnimator {
    eastl::optional<PositionAnimationSampler> position_sampler = eastl::nullopt;
    eastl::optional<RotationAnimationSampler> rotation_sampler = eastl::nullopt;
    eastl::optional<ScaleAnimationSampler> scale_sampler = eastl::nullopt;

    float start_time;

    /**
     * Checks if the animation has ended at the given time, e.g. is the given time past the end of all the samplers
     */
    bool has_animation_ended(float time) const;

    /**
     * Samples the value of this transform at the given time. The time is absolute
     */
    float4x4 sample(float time);
};

struct SkeletonAnimator {
    eastl::vector<NodeAnimator> joint_animators;

    float start_time;

    bool has_animation_ended(float time) const;

    void update_bones(eastl::span<Bone> bones, float time);
};

template <typename FuncType>
void Animation::add_event(float time, FuncType func) {
    const auto event_add_itr = eastl::upper_bound(events.timestamps.begin(), events.timestamps.end(), time);

    const auto add_index = event_add_itr - events.timestamps.begin();

    events.timestamps.emplace(event_add_itr, time);

    events.values.emplace(events.values.begin() + add_index, eastl::function{ func });
}

template <typename T, typename InterpolationFunc>
T AnimationSampler<T, InterpolationFunc>::sample(float time) {
    while(current_index + 1 < timeline->timestamps.size() && time > timeline->timestamps[current_index + 1]) {
        current_index++;
    }

    if(current_index + 1 >= timeline->timestamps.size()) {
        current_index = 0;
    }

    const auto t = (time - timeline->timestamps[current_index]) / (timeline->timestamps[current_index + 1] - timeline->
        timestamps
        [current_index]);
    return InterpolationFunc{}(timeline->values[current_index], timeline->values[current_index + 1], t);
}

template <typename T>
T LerpFunctor<T>::operator()(const T& a, const T& b, float t) {
    return glm::lerp(a, b, t);
}

template <typename T>
T SlerpFunctor<T>::operator()(const T& a, const T& b, float t) {
    return glm::slerp(a, b, t);
}

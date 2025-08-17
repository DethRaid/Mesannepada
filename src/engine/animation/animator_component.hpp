#pragma once

#include "core/engine.hpp"
#include "resources/gltf_animations.hpp"

struct NodeAnimationComponent {
    NodeAnimator animator;

    float start_time = 0;
};

struct SkeletalAnimatorComponent {
    SkeletonAnimator animator;

    bool looping = true;

    float start_time = 0;

    float duration = 0;
};

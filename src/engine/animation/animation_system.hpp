#pragma once

#include <EASTL/string.h>
#include <entt/entity/entity.hpp>
#include <EASTL/unique_ptr.h>

#include "plf_colony.h"
#include "animation/skeleton.hpp"
#include "resources/gltf_animations.hpp"

class Scene;

class AnimationSystem {
public:
    AnimationSystem(Scene& scene_in);

    void tick(float delta_time);

    void add_animation(const eastl::string& name, Animation&& animation);

    /**
     * Adds an animation to a specific skeleton
     *
     * Animations are grouped by skeleton, and some animations have no skeleton. Think of skeletons as a namespace
     */
    void add_animation(SkeletonHandle skeleton, const eastl::string& name, Animation&& animation);

    Animation& get_animation(const eastl::string& animation_name);

    void play_animation_on_entity(entt::entity entity, const eastl::string& animation_name);

    void remove_animation(const eastl::string& animation_name);

    SkeletonHandle add_skeleton(Skeleton&& skeleton);

private:
    Scene& scene;

    using AnimationMap = eastl::unordered_map<eastl::string, eastl::unique_ptr<Animation>>;

    /**
     * Animations on skeletons. If the skeleton is nullptr, the animations only applies to nodes
     */
    eastl::unordered_map<SkeletonHandle, AnimationMap> skeletal_animations;

    eastl::vector<AnimationEventSampler> active_event_timelines;

    plf::colony<Skeleton> skeletons;
};

#pragma once

#include <EASTL/string.h>
#include <entt/entity/entity.hpp>
#include <EASTL/unique_ptr.h>

#include "resources/gltf_animations.hpp"

class Scene;

class AnimationSystem {
public:
    AnimationSystem(Scene& scene_in);

    void tick(float delta_time);

    void add_animation(const eastl::string& name, Animation&& animation);

    Animation& get_animation(const eastl::string& animation_name);

    void play_animation_on_entity(entt::entity entity, const eastl::string& animation_name);

    void remove_animation(const eastl::string& animation_name);

private:
    Scene& scene;

    eastl::unordered_map<eastl::string, eastl::unique_ptr<Animation>> animations;

    eastl::vector<AnimationEventSampler> active_event_timelines;
};

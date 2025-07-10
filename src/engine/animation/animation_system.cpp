#include "animation_system.hpp"

#include <EASTL/numeric.h>

#include "animation/animator_component.hpp"
#include "core/engine.hpp"
#include "render/components/skeletal_mesh_component.hpp"
#include "resources/model_components.hpp"
#include "scene/scene.hpp"
#include "scene/transform_component.hpp"

static std::shared_ptr<spdlog::logger> logger;

AnimationSystem::AnimationSystem(World& world_in) :
    world{world_in} {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("AnimationSystem");
    }
}

void AnimationSystem::tick(float delta_time) {
    auto& registry = world.get_registry();

    const auto current_time = Engine::get().get_current_time();

    // Tick node animators

    registry.view<TransformComponent, NodeAnimationComponent>().each(
        [&](const entt::entity entity, const TransformComponent& transform, NodeAnimationComponent& animator) {
            if(animator.animator.has_animation_ended(current_time)) {
                registry.remove<NodeAnimationComponent>(entity);
            } else {
                registry.patch<TransformComponent>(
                    entity,
                    [&](TransformComponent& trans) {
                        trans.set_local_transform(animator.animator.sample(current_time));
                    });
            }
        });

    // Tick skeletal animators

    registry.view<render::SkeletalMeshComponent, SkeletalAnimatorComponent>().each(
        [&](const entt::entity entity, render::SkeletalMeshComponent& skelly, SkeletalAnimatorComponent& animator) {
            if(animator.animator.has_animation_ended(current_time)) {
                registry.remove<SkeletalAnimatorComponent>(entity);
            } else {
                animator.animator.update_bones(skelly.bones, current_time);
            }
        });

    registry.view<render::SkeletalMeshComponent>().each(
        [&](render::SkeletalMeshComponent& skelly) {
            skelly.propagate_bone_transforms();
        });

    // Clean up

    auto itr = active_event_timelines.begin();
    while(itr != active_event_timelines.end()) {
        itr->tick(current_time);
        if(itr->is_ended(current_time)) {
            itr = active_event_timelines.erase(itr);
        } else {
            ++itr;
        }
    }
}

void AnimationSystem::add_animation(SkeletonHandle skeleton, const eastl::string& name, Animation&& animation) {
    logger->info("Adding animation {}", name.c_str());
    if(animations.find(skeleton) == animations.end()) {
        animations.emplace(skeleton, AnimationMap{});
    }

    auto& animation_map = animations.at(skeleton);
    if(animation_map.find(name) != animation_map.end()) {
        throw std::runtime_error{"Duplicate animation names are not allowed!"};
    }

    animation_map.emplace(name, eastl::make_unique<Animation>(eastl::forward<Animation>(animation)));
}

Animation& AnimationSystem::get_animation(SkeletonHandle skeleton, const eastl::string& animation_name) {
    return *animations.at(skeleton).at(animation_name);
}

void AnimationSystem::play_animation_on_entity(const entt::handle entity, const eastl::string& animation_name) {
    SkeletonHandle skeleton = nullptr;
    if(const auto* skinned_component = entity.try_get<render::SkeletalMeshComponent>()) {
        // TODO: Handle skinned meshes _better_
        skeleton = skinned_component->skeleton;
    }
    const auto& skeleton_animations = animations.at(skeleton);
    const auto itr = skeleton_animations.find(animation_name);
    if(itr == skeleton_animations.end()) {
        logger->error("Could not find an animation named {}, unable to play!", animation_name.c_str());
    }

    auto& registry = world.get_registry();
    const auto& gltf_component = registry.get<ImportedModelComponent>(entity);

    const auto start_time = Engine::get().get_current_time();

    // Add animator components to all the nodes referenced by the animation
    for(const auto& [node, node_animation] : itr->second->channels) {
        auto animator = NodeAnimator{
            .start_time = start_time
        };

        if(node_animation.position) {
            animator.position_sampler = PositionAnimationSampler{.timeline = &*node_animation.position};
        }
        if(node_animation.rotation) {
            animator.rotation_sampler = RotationAnimationSampler{.timeline = &*node_animation.rotation};
        }
        if(node_animation.scale) {
            animator.scale_sampler = ScaleAnimationSampler{.timeline = &*node_animation.scale};
        }

        const auto node_entity = gltf_component.node_to_entity.at(node);
        registry.emplace<NodeAnimationComponent>(node_entity, NodeAnimationComponent{.animator = animator});
    }

    if(!itr->second->events.timestamps.empty()) {
        active_event_timelines.emplace_back(
            AnimationEventSampler{.start_time = start_time, .timeline = &itr->second->events});
    }
}

void AnimationSystem::remove_animation(const SkeletonHandle skeleton, const eastl::string& animation_name) {
    animations.at(skeleton).erase(animation_name);
}

SkeletonHandle AnimationSystem::add_skeleton(Skeleton&& skeleton) {
    const auto handle = &*skeletons.emplace(eastl::forward<Skeleton&&>(skeleton));

    // Find the root bone
    auto root_bones = eastl::vector<size_t>(handle->bones.size());
    eastl::iota(root_bones.begin(), root_bones.end(), 0);

    for(const auto& bone : handle->bones) {
        for(const auto& child : bone.children) {
            root_bones.erase_first_unsorted(child);
        }
    }

    handle->root_bones = root_bones;

    return handle;
}

void AnimationSystem::destroy_skeleton(SkeletonHandle skeleton) {
    if(skeleton != nullptr) {
        animations.erase(skeleton);
        skeletons.erase(skeletons.get_iterator(skeleton));
    }
}

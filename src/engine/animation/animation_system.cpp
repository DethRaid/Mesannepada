#include "animation_system.hpp"

#include <EASTL/numeric.h>

#include "animation_event_component.hpp"
#include "animation/animator_component.hpp"
#include "core/engine.hpp"
#include "render/components/skeletal_mesh_component.hpp"
#include "resources/model_components.hpp"
#include "scene/world.hpp"
#include "scene/transform_component.hpp"

static std::shared_ptr<spdlog::logger> logger;

AnimationSystem::AnimationSystem(World& world_in) :
    world{world_in} {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("AnimationSystem");
    }
}

void AnimationSystem::tick(float delta_time) {
    ZoneScopedN("AnimationSystem::tick");

    auto& registry = world.get_registry();

    const auto current_time = Engine::get().get_current_time();

    // Process animation events

    registry.view<PlayAnimationComponent>().each(
        [&](const entt::entity entity, const PlayAnimationComponent& comp) {
            logger->debug("Playing a animation on entity {}", static_cast<uint32_t>(entity));
            const auto handle = world.make_handle(entity);
            if(const auto skelly = World::find_component_in_children<render::SkeletalMeshComponent>(handle);
                skelly.valid()) {
                play_animation_on_entity(skelly, comp.animation_to_play);

            } else {
                play_animation_on_entity(handle, comp.animation_to_play);
            }
            handle.remove<PlayAnimationComponent>();
        }
        );

    // Tick node animators

    registry.view<TransformComponent, NodeAnimationComponent>().each(
        [&](const entt::entity entity, const TransformComponent& transform, NodeAnimationComponent& animator) {
            const auto local_time = current_time - animator.start_time;

            if(animator.animator.has_animation_ended(local_time)) {
                registry.remove<NodeAnimationComponent>(entity);
            } else {
                registry.patch<TransformComponent>(
                    entity,
                    [&](TransformComponent& trans) {
                        trans.set_local_transform(animator.animator.sample(local_time));
                    });
            }
        });

    // Tick skeletal animators

    registry.view<render::SkeletalMeshComponent, SkeletalAnimatorComponent>().each(
        [&](const entt::entity entity, render::SkeletalMeshComponent& skelly, SkeletalAnimatorComponent& animator) {
            auto local_time = current_time - animator.start_time;
            const auto num_iterations = floor(local_time / animator.duration);
            local_time -= num_iterations * animator.duration;

            if(!animator.looping && animator.animator.has_animation_ended(local_time)) {
                registry.remove<SkeletalAnimatorComponent>(entity);
            } else {
                animator.animator.update_bones(skelly.bones, local_time);
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

Animation& AnimationSystem::get_animation(const SkeletonHandle skeleton, const eastl::string& animation_name) {
    return *animations.at(skeleton).at(animation_name);
}

void AnimationSystem::play_animation_on_entity(const entt::handle entity, const eastl::string& animation_name) {
    // Check if the entity has a skeleton. If so, add a skeletal mesh animator. If not, add node animators
    SkeletonHandle skeleton = nullptr;
    if(const auto* skinned_component = entity.try_get<render::SkeletalMeshComponent>()) {
        // TODO: Handle skinned meshes _better_
        skeleton = skinned_component->skeleton;
    }
    if(skeleton == nullptr) {
        play_node_animation_on_entity(entity, animation_name);
    } else {
        play_skeletal_animation_on_entity(entity, skeleton, animation_name);
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

void AnimationSystem::play_node_animation_on_entity(const entt::handle entity, const eastl::string& animation_name) {
    const auto& skeleton_animations = animations.at(nullptr);
    const auto itr = skeleton_animations.find(animation_name);
    if(itr == skeleton_animations.end()) {
        logger->error("Could not find an animation named {}, unable to play!", animation_name.c_str());
    }

    const auto& model_component = entity.get<ImportedModelComponent>();

    const auto start_time = Engine::get().get_current_time();

    // Add animator components to all the nodes referenced by the animation
    for(const auto& [node, node_animation] : itr->second->channels) {
        auto animator = NodeAnimator{};

        if(node_animation.position) {
            animator.position_sampler = PositionAnimationSampler{.timeline = &*node_animation.position};
        }
        if(node_animation.rotation) {
            animator.rotation_sampler = RotationAnimationSampler{.timeline = &*node_animation.rotation};
        }
        if(node_animation.scale) {
            animator.scale_sampler = ScaleAnimationSampler{.timeline = &*node_animation.scale};
        }

        const auto node_entity = model_component.node_to_entity.at(node);
        node_entity.emplace<NodeAnimationComponent>(NodeAnimationComponent{
            .animator = animator,
            .start_time = start_time
        });
    }

    if(!itr->second->events.timestamps.empty()) {
        active_event_timelines.emplace_back(
            AnimationEventSampler{.start_time = start_time, .timeline = &itr->second->events});
    }
}

void AnimationSystem::play_skeletal_animation_on_entity(entt::handle entity, const SkeletonHandle skeleton,
                                                        const eastl::string& animation_name
    ) {
    const auto& skeleton_animations = animations.at(skeleton);
    const auto itr = skeleton_animations.find(animation_name);
    if(itr == skeleton_animations.end()) {
        logger->error("Could not find an animation named {}, unable to play!", animation_name.c_str());
    }

    const auto num_channels = itr->second->channels.size();

    auto animator = SkeletonAnimator{};
    animator.joint_animators.reserve(num_channels);

    const auto start_time = Engine::get().get_current_time();

    auto duration = 0.f;

    for(const auto& [idx, channel] : itr->second->channels) {
        auto& channel_animator = animator.joint_animators.emplace_back(NodeAnimator{
            .target_node = idx
        });

        if(channel.position) {
            channel_animator.position_sampler = PositionAnimationSampler{.timeline = &*channel.position};
        }
        if(channel.rotation) {
            channel_animator.rotation_sampler = RotationAnimationSampler{.timeline = &*channel.rotation};
        }
        if(channel.scale) {
            channel_animator.scale_sampler = ScaleAnimationSampler{.timeline = &*channel.scale};
        }

        duration = eastl::max(duration, channel_animator.get_duration());
    }

    entity.emplace<SkeletalAnimatorComponent>(SkeletalAnimatorComponent{
        .animator = animator,
        .start_time = start_time,
        .duration = duration,
    });
}

#include "animation_system.hpp"

#include "core/engine.hpp"
#include "animation/animator_component.hpp"
#include "resources/gltf_model_component.hpp"
#include "scene/scene.hpp"
#include "scene/transform_component.hpp"

static std::shared_ptr<spdlog::logger> logger;

AnimationSystem::AnimationSystem(Scene& scene_in) : scene{scene_in} {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("AnimationSystem");
    }
}

void AnimationSystem::tick(float delta_time) {
    auto& registry = scene.get_registry();

    const auto current_time = Engine::get().get_current_time();

    registry.view<TransformComponent, NodeAnimationComponent>().each(
        [&](const entt::entity entity, const TransformComponent& transform, NodeAnimationComponent& animator) {
            if(animator.animator.has_animation_ended(current_time)) {
                registry.remove<NodeAnimationComponent>(entity);
            } else {
                registry.patch<TransformComponent>(
                    entity,
                    [&](TransformComponent& trans) {
                        trans.local_to_parent = animator.animator.sample(current_time);
                    });
            }
        });

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

void AnimationSystem::add_animation(const eastl::string& name, Animation&& animation) {
    if(animations.find(name) != animations.end()) {
        throw std::runtime_error{"Duplicate animation names are not allowed!"};
    }
    animations.emplace(name, eastl::make_unique<Animation>(eastl::move(animation)));
}

Animation& AnimationSystem::get_animation(const eastl::string& animation_name) {
    return *animations.at(animation_name);
}

void AnimationSystem::play_animation_on_entity(const entt::entity entity, const eastl::string& animation_name) {
    const auto itr = animations.find(animation_name);
    if(itr == animations.end()) {
        logger->error("Could not find an animation named {}, unable to play!", animation_name.c_str());
    }

    auto& registry = scene.get_registry();
    const auto& gltf_component = registry.get<ImportedModelComponent>(entity);

    const auto start_time = Engine::get().get_current_time();

    // Add animator components to all the nodes referenced by the animation
    for(const auto& [node, node_animation] : itr->second->nodes) {
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

void AnimationSystem::remove_animation(const eastl::string& animation_name) {
    animations.erase(animation_name);
}

#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include "physics/layers.hpp"

namespace physics {
    /// Class that determines if two object layers can collide
    class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(
            const JPH::ObjectLayer in_object1, const JPH::ObjectLayer in_object2
        ) const override {
            switch(in_object1) {
            case layers::NON_MOVING:
                return in_object2 == layers::MOVING; // Non-moving only collides with moving
            case layers::MOVING:
                return true; // Moving collides with everything
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

    // Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
    // a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
    // You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
    // many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
    // your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
    namespace broad_phase_layers {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr uint32_t NUM_LAYERS(2);
    };

    // BroadPhaseLayerInterface implementation
    // This defines a mapping between object and broadphase layers.
    class BpLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
    public:
        BpLayerInterfaceImpl() {
            // Create a mapping table from object to broad phase layer
            object_to_broad_phase[layers::NON_MOVING] = broad_phase_layers::NON_MOVING;
            object_to_broad_phase[layers::MOVING] = broad_phase_layers::MOVING;
        }

        uint32_t GetNumBroadPhaseLayers() const override {
            return broad_phase_layers::NUM_LAYERS;
        }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(const JPH::ObjectLayer in_layer) const override {
            JPH_ASSERT(in_layer < layers::NUM_LAYERS);
            return object_to_broad_phase[in_layer];
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(const JPH::BroadPhaseLayer in_layer) const override {
            switch(static_cast<JPH::BroadPhaseLayer::Type>(in_layer)) {
            case static_cast<JPH::BroadPhaseLayer::Type>(broad_phase_layers::NON_MOVING):
                return "NON_MOVING";
            case static_cast<JPH::BroadPhaseLayer::Type>(broad_phase_layers::MOVING):
                return "MOVING";
            default: JPH_ASSERT(false);
                return "INVALID";
            }
        }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

    private:
        JPH::BroadPhaseLayer object_to_broad_phase[layers::NUM_LAYERS];
    };

    /// Class that determines if an object layer can collide with a broadphase layer
    class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(const JPH::ObjectLayer in_layer1, const JPH::BroadPhaseLayer in_layer2) const override {
            switch(in_layer1) {
            case layers::NON_MOVING:
                return in_layer2 == broad_phase_layers::MOVING;
            case layers::MOVING:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };
}

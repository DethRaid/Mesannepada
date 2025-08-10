#include "behavior_tree.hpp"

#include <tracy/Tracy.hpp>

namespace bt {
    void BehaviorTree::tick(const float delta_time) const {
        ZoneScopedN("BehaviorTree::tick");
        root_node->tick(delta_time);
    }
} // ai
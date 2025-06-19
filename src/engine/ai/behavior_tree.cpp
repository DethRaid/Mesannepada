#include "behavior_tree.hpp"

namespace bt {
    void BehaviorTree::tick(const float delta_time) const {
        root_node->tick(delta_time);
    }
} // ai
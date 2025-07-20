#pragma once

#include <EASTL/unique_ptr.h>

#include "ai/behavior_tree_node.hpp"

namespace bt {
    /**
     * Represents a behavior tree
     */
    class BehaviorTree {
    public:
        void tick(float delta_time) const;

    private:
        /**
         * The root node of a behavior tree. Likely a Selector, but not necessarily
         */
        eastl::unique_ptr<Node> root_node;
    };
} // ai

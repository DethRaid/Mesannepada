#include "selector_node.hpp"

namespace bt {
    Status SelectorNode::tick(float delta_time) {
        for(const auto& child : children) {
            const auto result = child->tick(delta_time);
            // If the child succeeded or is still running, return its status as our own. If it failed, let the loop
            // continue to the next child
            if(result != Status::Failed) {
                return result;
            }
        }

        return Status::Failed;
    }
} // bt
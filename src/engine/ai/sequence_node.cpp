#include "sequence_node.hpp"

namespace  bt {
    Status SequenceNode::tick(float delta_time) {
        for(const auto& child :  children) {
            const auto result = child->tick(delta_time);

            // If the child failed or is still running, return its status as our own. If it succeeded, let the loop
            // continue until we find a node that either failed or is running
            if(result != Status::Succeeded) {
                return result;
            }
        }

        return Status::Succeeded;
    }
} //  bt
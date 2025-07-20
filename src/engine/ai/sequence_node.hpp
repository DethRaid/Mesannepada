#pragma once

#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

#include "ai/behavior_tree_node.hpp"

namespace bt {
    class SequenceNode final : public Node {
    public:
        Status tick(float delta_time) override;

    private:
        eastl::vector<eastl::unique_ptr<Node> > children;
    };

} //  bt

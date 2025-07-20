#pragma once

#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

#include "ai/behavior_tree_node.hpp"

namespace bt {
    /**
     * A selector executes each of its children until it finds one that succeeds
     */
    class SelectorNode final : public Node {
    public:
        Status tick(float delta_time) override;

    private:
        eastl::vector<eastl::unique_ptr<Node> > children;

        size_t cur_child = 0;
    };

} // bt

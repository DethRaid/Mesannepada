#pragma once

#include <cstdint>

namespace bt {
    /**
     * The status of a node in a behavior tree
     */
    enum Status {
        /**
         * This node is still running. After this tick, execution should return to it
         */
        Running,

        /**
         * The node completed, but failed its execution
         */
        Failed,

        /**
         * The node completed, and succeeded
         */
        Succeeded,
    };

    class Node {
    public:
        virtual ~Node() = default;

        uint32_t get_id() const;

        virtual Status tick(float delta_time) = 0;
    };
} // ai

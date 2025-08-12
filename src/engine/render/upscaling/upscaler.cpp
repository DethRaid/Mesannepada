#include "upscaler.hpp"

namespace render {
    glm::vec2 IUpscaler::get_jitter() {
        // Halton sequence gives us a value from -1 to 1, but we want -0.5 to 0.5
        return glm::vec2{ jitter_sequence_x.get_next_value(), jitter_sequence_y.get_next_value() } * 0.5f;
    }
}

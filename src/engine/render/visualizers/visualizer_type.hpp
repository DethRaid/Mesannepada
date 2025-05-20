#pragma once

namespace render {
    enum class RenderVisualization : uint8_t {
        None,
        GIDebug,
        Physics,
    };

    inline const char* to_string(const RenderVisualization e) {
        switch (e) {
        case RenderVisualization::None: return "None";
        case RenderVisualization::GIDebug: return "Global Illumination";
        case RenderVisualization::Physics: return "Physics";
        default: return "unknown";
        }
    }
}

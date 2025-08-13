#pragma once

#include <EASTL/optional.h>

#include "render/backend/handles.hpp"

namespace render {
    struct GBuffer;
    class RenderWorld;
    class SceneView;
    struct IndirectDrawingBuffers;
    class RenderGraph;

    class GbufferPhase {
    public:
        GbufferPhase();

        void render(
            RenderGraph& graph, const RenderWorld& world, const GBuffer& gbuffer,
            eastl::optional<TextureHandle> shading_rate, const SceneView& view
            );
    };
}

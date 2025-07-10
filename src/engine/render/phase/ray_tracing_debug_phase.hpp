#pragma once
#include <cstdint>

#include "render/backend/handles.hpp"

namespace render {
    struct GBuffer;
    class RenderWorld;
    class RenderGraph;
    class SceneView;

    class RayTracingDebugPhase {
    public:
        static uint32_t get_debug_mode();

        void raytrace(
            RenderGraph& graph, const SceneView& view, const RenderWorld& world, const GBuffer& gbuffer,
            TextureHandle output_texture
        );

    private:
        RayTracingPipelineHandle pipeline = nullptr;
    };
}

#pragma once

#include <cstdint>

#include "render/backend/handles.hpp"

namespace render {
    struct NoiseTexture;
    class RenderWorld;
    class SceneView;
    class RenderGraph;

    enum class AoTechnique {
        Off,
        RTAO
    };

    /**
     * Renders ambient occlusion
     *
     * Also consider https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
     */
    class AmbientOcclusionPhase {
    public:
        AmbientOcclusionPhase();

        ~AmbientOcclusionPhase();

        void generate_ao(
            RenderGraph& graph, const SceneView& view, const RenderWorld& world, const NoiseTexture& noise,
            TextureHandle gbuffer_normals, TextureHandle gbuffer_depth, TextureHandle ao_out
        );

    private:

        ComputePipelineHandle rtao_pipeline = nullptr;

        uint32_t frame_index = 0;

        void evaluate_rtao(
            RenderGraph& graph, const SceneView& view, const RenderWorld& world, const NoiseTexture& noise,
            TextureHandle gbuffer_depth, TextureHandle gbuffer_normals, TextureHandle ao_out
        );
    };
}

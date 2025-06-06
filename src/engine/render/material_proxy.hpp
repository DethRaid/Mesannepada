#pragma once

#include <EASTL/array.h>
#include <utility>

#include "render/basic_pbr_material.hpp"
#include "render/scene_pass_type.hpp"

namespace render {
    class RenderBackend;

    /**
     * Proxy for a material
     */
    struct MaterialProxy {
        eastl::array<GraphicsPipelineHandle, ScenePassType::Count> pipelines;
    };

    using BasicPbrMaterialProxy = std::pair<BasicPbrMaterial, MaterialProxy>;
}

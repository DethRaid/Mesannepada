#include "scatter_upload_buffer.hpp"

#include "resources/resource_path.hpp"
#include "render/backend/pipeline_cache.hpp"

namespace render {
    static ComputePipelineHandle scatter_shader = nullptr;

    ComputePipelineHandle get_scatter_upload_shader() {
        if (!scatter_shader) {
            auto& backend = RenderBackend::get();
            auto& pipeline_cache = backend.get_pipeline_cache();
            scatter_shader = pipeline_cache.create_pipeline("shader://util/scatter_upload.comp.spv"_res);
        }

        return scatter_shader;
    }
}

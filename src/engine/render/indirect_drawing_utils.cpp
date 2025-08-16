#include "indirect_drawing_utils.hpp"

#include <shared/prelude.h>

#include "backend/pipeline_cache.hpp"
#include "backend/render_backend.hpp"
#include "render/backend/render_graph.hpp"
#include "resources/resource_path.hpp"

namespace render {
    static ComputePipelineHandle visibility_list_to_draw_commands = nullptr;

    BufferHandle translate_visibility_list_to_draw_commands(
        RenderGraph& graph, const BufferHandle visibility_list, const BufferHandle primitive_buffer,
        const uint32_t num_primitives, const BufferHandle mesh_draw_args_buffer, const uint16_t primitive_type,
        const eastl::string& debug_string
    ) {
        ZoneScoped;

        auto& backend = RenderBackend::get();

        auto& pipeline_cache = backend.get_pipeline_cache();

        if (!visibility_list_to_draw_commands) {
            visibility_list_to_draw_commands = pipeline_cache.create_pipeline(
                "shader://util/visibility_list_to_draw_commands.comp.spv"_res);
        }

        auto& allocator = backend.get_global_allocator();
        const auto drawcall_buffer = allocator.create_buffer(
                "Draw commands " + debug_string,
                sizeof(VkDrawIndexedIndirectCommand) * num_primitives + 16,
                BufferUsage::IndirectBuffer
        );

        graph.add_clear_pass(drawcall_buffer);

        const auto tvl_set = visibility_list_to_draw_commands->begin_building_set(0)
            .bind(primitive_buffer)
            .bind(visibility_list)
            .bind(mesh_draw_args_buffer)
            .bind(drawcall_buffer, 16)
            .bind(drawcall_buffer)
            .build();
        graph.add_compute_dispatch<glm::uvec2>(
            {
                .name = "Translate visibility list",
                .descriptor_sets = {tvl_set},
                .push_constants = glm::uvec2{num_primitives, primitive_type},
                .num_workgroups = {(num_primitives + 95) / 96, 1, 1},
                .compute_shader = visibility_list_to_draw_commands
            }
        );

        return drawcall_buffer;
    }
}

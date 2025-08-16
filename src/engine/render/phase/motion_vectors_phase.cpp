#include "motion_vectors_phase.hpp"

#include "console/cvars.hpp"
#include "render/indirect_drawing_utils.hpp"
#include "render/mesh_storage.hpp"
#include "render/render_scene.hpp"
#include "render/backend/render_backend.hpp"

namespace render {
    static auto cvar_full_res_motion_vectors = AutoCVar_Int{
        "r.MotionVectors.FullRes",
        "Whether to render motion vectors at output resolution (1), or at render resolution (0)",
        0
    };

    static auto cvar_rasterize_motion_vectors = AutoCVar_Int{
        "r.MotionVectors.Rasterize",
        "Whether to generate motion vectors by rasterizing objects (1) or by running a postprocess shader over the depth buffer (0)",
        1
    };

    bool MotionVectorsPhase::render_full_res() {
        return cvar_full_res_motion_vectors.get() == 1;
    }

    MotionVectorsPhase::MotionVectorsPhase() {
        motion_vectors_opaque_pso = RenderBackend::get()
                                    .begin_building_pipeline("motion_vectors_opaque_pso")
                                    .use_standard_vertex_layout()
                                    .set_vertex_shader("shader://motion_vectors/motion_vectors.vert.spv"_res)
                                    .set_fragment_shader("shader://motion_vectors/motion_vectors_opaque.frag.spv"_res)
                                    .set_depth_state(
                                    {
                                        .enable_depth_test = true,
                                        .enable_depth_write = false,
                                        .compare_op = VK_COMPARE_OP_EQUAL
                                    })
                                    .build();

        motion_vectors_masked_pso = RenderBackend::get()
                                    .begin_building_pipeline("motion_vectors_masked_pso")
                                    .use_standard_vertex_layout()
                                    .set_vertex_shader("shader://motion_vectors/motion_vectors_masked.vert.spv"_res)
                                    .set_fragment_shader("shader://motion_vectors/motion_vectors_masked.frag.spv"_res)
                                    .set_depth_state(
                                    {
                                        .enable_depth_test = true,
                                        .enable_depth_write = false,
                                        .compare_op = VK_COMPARE_OP_EQUAL
                                    })
                                    .build();

        motion_vectors_skinned_pso = RenderBackend::get()
                                     .begin_building_pipeline("motion_vectors_skinned_pso")
                                     .use_standard_vertex_layout()
                                     .set_vertex_shader("shader://motion_vectors/motion_vectors_skinned.vert.spv"_res)
                                     .set_fragment_shader("shader://motion_vectors/motion_vectors_opaque.frag.spv"_res)
                                     .set_depth_state(
                                     {
                                         .enable_depth_test = true,
                                         .enable_depth_write = false,
                                         .compare_op = VK_COMPARE_OP_EQUAL
                                     })
                                     .build();

        build_skinned_drawcalls_pso = RenderBackend::get()
                                      .get_pipeline_cache()
                                      .create_pipeline(
                                          "shader://motion_vectors/extract_skinned_mesh_drawcalls.comp.spv"_res);

        camera_motion_vectors_pso = RenderBackend::get()
                                    .get_pipeline_cache()
                                    .create_pipeline("shader://motion_vectors/camera_motion_vectors.comp.spv"_res);
    }

    void MotionVectorsPhase::set_render_resolution(const glm::uvec2& resolution, const glm::uvec2& output_resolution) {
        const auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        const auto mv_resolution = cvar_full_res_motion_vectors.get() == 0 ? resolution : output_resolution;

        if(motion_vectors) {
            if(motion_vectors->create_info.extent.width != mv_resolution.x || motion_vectors->create_info.extent.height
               !=
               mv_resolution.y) {
                allocator.destroy_texture(motion_vectors);
                motion_vectors = nullptr;
            }
        }

        if(motion_vectors == nullptr) {
            motion_vectors = allocator.create_texture(
                "motion_vectors",
                {
                    .format = VK_FORMAT_R16G16_SFLOAT,
                    .resolution = mv_resolution,
                    .usage = TextureUsage::RenderTarget,
                    .usage_flags = VK_IMAGE_USAGE_STORAGE_BIT
                }
                );
        }
    }

    void MotionVectorsPhase::render(
        RenderGraph& graph, const RenderWorld& world, const SceneView& view,
        const TextureHandle depth_buffer
        ) const {

        const auto view_data_buffer = view.get_constant_buffer();

        const auto skinned_draws = RenderBackend::get()
                                   .get_global_allocator()
                                   .create_buffer(
                                       "skinned_mesh_motion_vector_draws",
                                       sizeof(VkDrawIndexedIndirectCommand) * world.get_num_skinned_primitives() + 16,
                                       BufferUsage::IndirectBuffer);

        const auto skinned_set = build_skinned_drawcalls_pso->begin_building_set(0)
                                                            .bind(view.visible_objects)
                                                            .bind(world.get_mesh_storage().get_draw_args_buffer())
                                                            .bind(world.get_skeletal_primitive_buffer())
                                                            .bind(world.get_primitive_buffer())
                                                            .bind(skinned_draws)
                                                            .bind(skinned_draws, 16)
                                                            .build();

       graph.add_clear_pass(skinned_draws);

        graph.add_compute_dispatch(ComputeDispatch<uint32_t>{
            .name = "build_skinned_drawcall_buffer",
            .descriptor_sets = {skinned_set},
            .push_constants = world.get_num_skinned_primitives(),
            .num_workgroups = {world.get_num_skinned_primitives() + 63 / 64, 1, 1},
            .compute_shader = build_skinned_drawcalls_pso
        });

        if(cvar_rasterize_motion_vectors.get() == 0) {
            const auto set = camera_motion_vectors_pso->begin_building_set(0)
                                                      .bind(view_data_buffer)
                                                      .bind(depth_buffer)
                                                      .bind(motion_vectors)
                                                      .build();

            graph.add_compute_dispatch(ComputeDispatch<uint32_t>{
                .name = "motion_vectors",
                .descriptor_sets = {set},
                .num_workgroups = uint3{motion_vectors->get_resolution() + uint2{7} / uint2{8}, 1},
                .compute_shader = camera_motion_vectors_pso
            });

        } else {
            const auto set = motion_vectors_opaque_pso->begin_building_set(0)
                                                      .bind(view_data_buffer)
                                                      .bind(world.get_primitive_buffer())
                                                      .build();

            const auto masked_set = motion_vectors_masked_pso->begin_building_set(0)
                                                             .bind(view_data_buffer)
                                                             .bind(world.get_primitive_buffer())
                                                             .build();

            const auto skinned_set = motion_vectors_skinned_pso->begin_building_set(0)
                                                               .bind(view_data_buffer)
                                                               .bind(world.get_primitive_buffer())
                                                               .bind(world.get_skeletal_primitive_buffer())
                                                               .build();

            graph.add_render_pass(
            {
                .name = "motion_vectors",
                .buffers = {
                    {
                        .buffer = view.solid_drawcalls,
                        .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                        .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                    },
                    {
                        .buffer = view.cutout_drawcalls,
                        .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                        .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                    },
                    {
                        .buffer = skinned_draws,
                        .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                        .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                    },
                },
                .descriptor_sets = {masked_set, skinned_set},
                .color_attachments = {
                    {
                        .image = motion_vectors,
                        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .clear_value = {.color = {.float32 = {0, 0, 0, 0}}}
                    }
                },
                .depth_attachment = RenderingAttachmentInfo{.image = depth_buffer},
                .execute = [&](CommandBuffer& commands) {
                    commands.bind_descriptor_set(0, set);
                    world.draw_opaque(commands, view.solid_drawcalls, motion_vectors_opaque_pso);

                    commands.bind_descriptor_set(0, masked_set);
                    world.draw_masked(commands, view.cutout_drawcalls, motion_vectors_masked_pso);

                    commands.bind_descriptor_set(0, skinned_set);
                    world.draw_opaque(commands, skinned_draws, motion_vectors_skinned_pso);

                    commands.clear_descriptor_set(0);
                }
            });
        }
    }

    TextureHandle MotionVectorsPhase::get_motion_vectors() const {
        return motion_vectors;
    }
}

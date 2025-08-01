#include "depth_culling_phase.hpp"

#include <glm/common.hpp>
#include <glm/exponential.hpp>

#include "core/string_utils.hpp"
#include "core/system_interface.hpp"
#include "render/indirect_drawing_utils.hpp"
#include "render/material_pipelines.hpp"
#include "render/material_storage.hpp"
#include "render/mesh_storage.hpp"
#include "render/render_scene.hpp"
#include "render/backend/pipeline_cache.hpp"
#include "render/backend/render_backend.hpp"
#include "render/backend/resource_allocator.hpp"
#include "resources/resource_path.hpp"

namespace render {
    DepthCullingPhase::DepthCullingPhase() {
        auto& backend = RenderBackend::get();
        auto& pipeline_cache = backend.get_pipeline_cache();

        hi_z_culling_shader = pipeline_cache.create_pipeline("shader://culling/hi_z_culling.comp.spv"_res);

        //add a extension struct to enable Min mode
        VkSamplerReductionModeCreateInfoEXT create_info_reduction = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT,
            .reductionMode = VK_SAMPLER_REDUCTION_MODE_MAX,
        };

        auto& allocator = backend.get_global_allocator();
        max_reduction_sampler = allocator.get_sampler(
            {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .pNext = &create_info_reduction,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .maxLod = VK_LOD_CLAMP_NONE,
            }
            );
    }

    DepthCullingPhase::~DepthCullingPhase() {
        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();
        if(depth_buffer != nullptr) {
            allocator.destroy_texture(depth_buffer);
            depth_buffer = nullptr;
        }
        if(hi_z_buffer != nullptr) {
            allocator.destroy_texture(hi_z_buffer);
            hi_z_buffer = nullptr;

            auto& texture_descriptor_pool = backend.get_texture_descriptor_pool();
            texture_descriptor_pool.free_descriptor(hi_z_index);
            hi_z_index = 0;
        }
        if(visible_objects) {
            allocator.destroy_buffer(visible_objects);
            visible_objects = {};
        }
    }

    void DepthCullingPhase::set_render_resolution(const glm::uvec2& resolution) {
        ZoneScoped;

        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();
        auto& texture_descriptor_pool = backend.get_texture_descriptor_pool();

        if(depth_buffer != nullptr) {
            allocator.destroy_texture(depth_buffer);
            depth_buffer = nullptr;
        }
        if(hi_z_buffer != nullptr) {
            allocator.destroy_texture(hi_z_buffer);
            hi_z_buffer = nullptr;

            texture_descriptor_pool.free_descriptor(hi_z_index);
            hi_z_index = 0;
        }

        depth_buffer = allocator.create_texture(
            "Depth buffer",
            {
                .format = VK_FORMAT_D32_SFLOAT,
                .resolution = resolution,
                .num_mips = 1,
                .usage = TextureUsage::RenderTarget,
                .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT
            }
            );

        const auto hi_z_resolution = resolution / 2u;
        const auto major_dimension = glm::max(hi_z_resolution.x, hi_z_resolution.y);
        const auto num_mips = glm::round(glm::log2(static_cast<float>(major_dimension)));
        hi_z_buffer = allocator.create_texture(
            "Hi Z Buffer",
            {
                VK_FORMAT_R32_SFLOAT,
                hi_z_resolution,
                static_cast<uint32_t>(num_mips),
                TextureUsage::StorageImage
            }
            );

        hi_z_index = texture_descriptor_pool.create_texture_srv(hi_z_buffer, max_reduction_sampler);
    }

    void DepthCullingPhase::render(
        RenderGraph& graph, const RenderWorld& world,
        MaterialStorage& materials, const BufferHandle view_data_buffer
        ) {
        ZoneScoped;

        graph.begin_label("Depth/culling pass");

        auto& backend = RenderBackend::get();

        const auto primitive_buffer = world.get_primitive_buffer();

        const auto& pipelines = world.get_material_storage().get_pipelines();

        const auto depth_pso = pipelines.get_depth_pso();
        const auto view_descriptor = backend.get_transient_descriptor_allocator()
                                            .build_set(depth_pso, 0)
                                            .bind(primitive_buffer)
                                            .bind(view_data_buffer)
                                            .build();
        // Masked view needs the primitive buffer in the fragment shader ugh
        const auto masked_view_descriptor = backend.get_transient_descriptor_allocator()
                                                   .build_set(pipelines.get_depth_masked_pso(), 0)
                                                   .bind(primitive_buffer)
                                                   .bind(view_data_buffer)
                                                   .build();

        const auto num_primitives = world.get_total_num_primitives();

        auto& allocator = backend.get_global_allocator();
        if(!visible_objects) {
            visible_objects = allocator.create_buffer(
                "Visible objects list",
                sizeof(uint32_t) * num_primitives,
                BufferUsage::StorageBuffer
                );
        }

        if(backend.supports_device_generated_commands()) {
            draw_visible_objects_dgc(
                graph,
                world,
                materials,
                view_descriptor,
                primitive_buffer,
                num_primitives);
        } else {
            draw_visible_objects(graph,
                                 world,
                                 view_descriptor,
                                 masked_view_descriptor,
                                 primitive_buffer,
                                 num_primitives);
        }

        // Build Hi-Z pyramid

        downsampler.fill_mip_chain(graph, depth_buffer, hi_z_buffer);

        // Cull all objects against the pyramid, keeping track of newly visible objects

        // All the primitives that are visible this frame, whether they're newly visible or not
        const auto buffer_name = fmt::format("Frame {} visibility mask", backend.get_current_gpu_frame());
        const auto this_frame_visible_objects = allocator.create_buffer(
            buffer_name.c_str(),
            sizeof(uint32_t) * num_primitives,
            BufferUsage::StorageBuffer
            );

        // Just the primitives that are visible this frame
        const auto newly_visible_objects = allocator.create_buffer(
            "New visibility mask",
            sizeof(uint32_t) * num_primitives,
            BufferUsage::StorageBuffer
            );

        graph.add_pass(
            {
                .name = "HiZ Culling",
                .textures = {
                    {
                        hi_z_buffer,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    }
                },
                .buffers = {
                    {
                        primitive_buffer,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT
                    },
                    {
                        visible_objects,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT
                    },
                    {
                        newly_visible_objects,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT
                    },
                    {
                        this_frame_visible_objects,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT
                    },
                },
                .execute = [&](CommandBuffer& commands) {
                    auto& texture_descriptor_pool = backend.get_texture_descriptor_pool();
                    commands.bind_descriptor_set(0, texture_descriptor_pool.get_descriptor_set());

                    commands.bind_buffer_reference(0, primitive_buffer);
                    commands.bind_buffer_reference(2, visible_objects);
                    commands.bind_buffer_reference(4, newly_visible_objects);
                    commands.bind_buffer_reference(6, this_frame_visible_objects);

                    commands.bind_buffer_reference(8, view_data_buffer);

                    commands.set_push_constant(10, num_primitives);
                    commands.set_push_constant(11, hi_z_index);

                    commands.bind_pipeline(hi_z_culling_shader);

                    commands.dispatch((num_primitives + 95) / 96, 1, 1);

                    commands.clear_descriptor_set(0);
                }
            }
            );

        // The destruction will happen after this frame is complete, it's fine
        allocator.destroy_buffer(newly_visible_objects);
        allocator.destroy_buffer(visible_objects);

        // Save the list of visible objects so we can use them next frame
        visible_objects = this_frame_visible_objects;

        draw_visible_objects(graph, world, view_descriptor, masked_view_descriptor, primitive_buffer, num_primitives);

        graph.end_label();
    }

    TextureHandle DepthCullingPhase::get_depth_buffer() const {
        return depth_buffer;
    }

    BufferHandle DepthCullingPhase::get_visible_objects_buffer() const {
        return visible_objects;
    }

    void DepthCullingPhase::draw_visible_objects_dgc(
        RenderGraph& graph, const RenderWorld& world, MaterialStorage& materials,
        const DescriptorSet& descriptors,
        const BufferHandle primitive_buffer, const uint32_t num_primitives
        ) {
        /*
         * Run a compute shader over the visible objects list. Sort object IDs and draw commands by transparency
         *
         * We want to draw opaque and masked objects during the depth prepass. We can use a dual bump-point allocator for
         * this. Opaque objects start at index 0 and increment, masked objects start at index MAX and decrement
         *
         * What about transparency? We can draw them as masked, with a high threshold. That'll ensure that only pixels with
         * alpha = 1.0 get written to the buffer - but it'll still help us in a lot of situations. Later, we'll draw
         * transparent objects with depth mode = equal or less
         */

        if(command_signature == VK_NULL_HANDLE) {
            create_command_signature();
        }

        const auto pipeline_group = materials.get_pipeline_group();

        create_preprocess_buffer(pipeline_group, num_primitives);

        // Translate last frame's list of objects to indirect draw commands

        const auto& [draw_commands_buffer, draw_count_buffer, primitive_id_buffer] =
            translate_visibility_list_to_draw_commands(
                graph,
                visible_objects,
                primitive_buffer,
                num_primitives,
                world.get_mesh_storage().get_draw_args_buffer(),
                PRIMITIVE_TYPE_SOLID
                );

        BufferHandle indirect_commands_buffer;
        // graph.add_compute_dispatch({});

        graph.add_render_pass(
        {
            .name = "Depth prepass",
            .buffers = {
                {
                    {
                        .buffer = draw_commands_buffer,
                        .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                        .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                    },
                    {
                        .buffer = indirect_commands_buffer,
                        .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                        .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                    },
                }
            },
            .descriptor_sets = {},
            .depth_attachment = RenderingAttachmentInfo{depth_buffer},
            .execute = [=](CommandBuffer& commands) {
                commands.execute_commands();
            }
        });

        // cleanup
        auto& allocator = RenderBackend::get().get_global_allocator();
        allocator.destroy_buffer(draw_commands_buffer);
        allocator.destroy_buffer(draw_count_buffer);
        allocator.destroy_buffer(primitive_id_buffer);
    }

    struct DrawBatchCommand {
        VkBindShaderGroupIndirectCommandNV shader;
        VkBindVertexBufferIndirectCommandNV object_id_vb;
        VkDrawIndexedIndirectCommand draw_command;
    };

    void DepthCullingPhase::create_command_signature() {
        auto& backend = RenderBackend::get();
        const auto tokens = eastl::array{
            VkIndirectCommandsLayoutTokenNV{
                .sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV,
                .tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_SHADER_GROUP_NV,
                .stream = 0,
                .offset = 0,
            },
            VkIndirectCommandsLayoutTokenNV{
                .sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV,
                .tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV,
                .stream = 0,
                .offset = offsetof(DrawBatchCommand, object_id_vb),
                .vertexBindingUnit = 1,
                .vertexDynamicStride = VK_FALSE,
            },
            VkIndirectCommandsLayoutTokenNV{
                .sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV,
                .tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NV,
                .stream = 0,
                .offset = offsetof(DrawBatchCommand, draw_command),
                .vertexBindingUnit = 1,
                .vertexDynamicStride = VK_FALSE,
            }
        };
        constexpr auto stride = static_cast<uint32_t>(sizeof(DrawBatchCommand));
        const auto create_info = VkIndirectCommandsLayoutCreateInfoNV{
            .sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_NV,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .tokenCount = static_cast<uint32_t>(tokens.size()),
            .pTokens = tokens.data(),
            .streamCount = 1,
            .pStreamStrides = &stride
        };
        vkCreateIndirectCommandsLayoutNV(
            backend.get_device(),
            &create_info,
            nullptr,
            &command_signature);
    }

    eastl::optional<BufferHandle> DepthCullingPhase::create_preprocess_buffer(
        const GraphicsPipelineHandle pipeline, const uint32_t num_primitives
        ) {
        auto& backend = RenderBackend::get();

        const auto info = VkGeneratedCommandsMemoryRequirementsInfoNV{
            .sType = VK_STRUCTURE_TYPE_GENERATED_COMMANDS_MEMORY_REQUIREMENTS_INFO_NV,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .pipeline = pipeline->get_pipeline(),
            .indirectCommandsLayout = command_signature,
            .maxSequencesCount = num_primitives
        };
        auto requirements = VkMemoryRequirements2{.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
        vkGetGeneratedCommandsMemoryRequirementsNV(backend.get_device(), &info, &requirements);

        if(requirements.memoryRequirements.size > 0) {
            // Allocate and return a buffer
            auto& allocator = backend.get_global_allocator();
            return allocator.create_buffer(
                "Preprocess Buffer",
                requirements.memoryRequirements.size,
                BufferUsage::StorageBuffer);
        } else {
            return eastl::nullopt;
        }
    }

    void DepthCullingPhase::draw_visible_objects(
        RenderGraph& graph, const RenderWorld& world, const DescriptorSet& view_descriptor,
        const DescriptorSet& masked_view_descriptor, const BufferHandle primitive_buffer, const uint32_t num_primitives
        ) const {
        // Translate the list of objects to indirect draw commands

        const auto solid_buffers = translate_visibility_list_to_draw_commands(
            graph,
            visible_objects,
            primitive_buffer,
            num_primitives,
            world.get_mesh_storage().get_draw_args_buffer(),
            PRIMITIVE_TYPE_SOLID
            );

        const auto cutout_buffers = translate_visibility_list_to_draw_commands(
            graph,
            visible_objects,
            primitive_buffer,
            num_primitives,
            world.get_mesh_storage().get_draw_args_buffer(),
            PRIMITIVE_TYPE_CUTOUT
            );

        const auto& pipelines = world.get_material_storage().get_pipelines();
        const auto depth_pso = pipelines.get_depth_pso();
        const auto& masked_pso = pipelines.get_depth_masked_pso();

        // Draw the visible objects

        graph.add_render_pass(
        {
            .name = "Rasterize visible objects",
            .buffers = {
                {
                    solid_buffers.commands,
                    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                },
                {
                    solid_buffers.count,
                    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                },
                {
                    solid_buffers.primitive_ids,
                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT
                },
                {
                    cutout_buffers.commands,
                    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                },
                {
                    cutout_buffers.count,
                    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                },
                {
                    cutout_buffers.primitive_ids,
                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT
                },
            },
            .descriptor_sets = {view_descriptor, masked_view_descriptor},
            .depth_attachment = RenderingAttachmentInfo{
                .image = depth_buffer,
                .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .clear_value = {.depthStencil = {.depth = 0.0}}
            },
            .execute = [&](CommandBuffer& commands) {
                commands.bind_descriptor_set(0, view_descriptor);

                world.draw_opaque(commands, solid_buffers, depth_pso);

                commands.bind_descriptor_set(0, masked_view_descriptor);
                world.draw_masked(commands, cutout_buffers, masked_pso);

                commands.clear_descriptor_set(0);
            }
        });

        // cleanup
        auto& allocator = RenderBackend::get().get_global_allocator();
        allocator.destroy_buffer(solid_buffers.commands);
        allocator.destroy_buffer(solid_buffers.count);
        allocator.destroy_buffer(solid_buffers.primitive_ids);
        allocator.destroy_buffer(cutout_buffers.commands);
        allocator.destroy_buffer(cutout_buffers.count);
        allocator.destroy_buffer(cutout_buffers.primitive_ids);
    }
}

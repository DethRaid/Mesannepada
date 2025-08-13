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
        const auto& backend = RenderBackend::get();
        auto& pipeline_cache = backend.get_pipeline_cache();

        hi_z_culling_shader = pipeline_cache.create_pipeline("shader://culling/hi_z_culling.comp.spv"_res);

        // add a extension struct to enable Min mode
        VkSamplerReductionModeCreateInfoEXT create_info_reduction = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT,
            .reductionMode = VK_SAMPLER_REDUCTION_MODE_MAX,
        };

        auto& allocator = backend.get_global_allocator();
        max_reduction_sampler = allocator.get_sampler({
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = &create_info_reduction,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = VK_LOD_CLAMP_NONE,
        });
    }

    DepthCullingPhase::~DepthCullingPhase() {
        const auto& backend = RenderBackend::get();
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
    }

    void DepthCullingPhase::set_render_resolution(const glm::uvec2& resolution) {
        ZoneScoped;

        const auto& backend = RenderBackend::get();
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

        depth_buffer = allocator.create_texture("Depth buffer",
                                                {.format = VK_FORMAT_D32_SFLOAT,
                                                 .resolution = resolution,
                                                 .num_mips = 1,
                                                 .usage = TextureUsage::RenderTarget,
                                                 .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT});

        const auto hi_z_resolution = resolution / 2u;
        const auto major_dimension = glm::max(hi_z_resolution.x, hi_z_resolution.y);
        const auto num_mips = glm::round(glm::log2(static_cast<float>(major_dimension)));
        hi_z_buffer = allocator.create_texture(
            "Hi Z Buffer",
            {VK_FORMAT_R32_SFLOAT, hi_z_resolution, static_cast<uint32_t>(num_mips), TextureUsage::StorageImage});

        hi_z_index = texture_descriptor_pool.create_texture_srv(hi_z_buffer, max_reduction_sampler);
    }

    void DepthCullingPhase::render(
        RenderGraph& graph, const RenderWorld& world,
        SceneView& view
        ) {
        ZoneScoped;

        graph.begin_label("Depth/culling pass");

        auto& backend = RenderBackend::get();

        const auto primitive_buffer = world.get_primitive_buffer();

        const auto& pipelines = world.get_material_storage().get_pipelines();

        const auto view_constant_buffer = view.get_constant_buffer();

        const auto depth_pso = pipelines.get_depth_pso();
        const auto view_descriptor = backend.get_transient_descriptor_allocator()
                                            .build_set(depth_pso, 0)
                                            .bind(primitive_buffer)
                                            .bind(view_constant_buffer)
                                            .build();
        // Masked view needs the primitive buffer in the fragment shader ugh
        const auto masked_view_descriptor = backend.get_transient_descriptor_allocator()
                                                   .build_set(pipelines.get_depth_masked_pso(), 0)
                                                   .bind(primitive_buffer)
                                                   .bind(view_constant_buffer)
                                                   .build();

        const auto num_primitives = world.get_total_num_primitives();

        draw_visible_objects(
            graph,
            world,
            view_descriptor,
            masked_view_descriptor,
            view.solid_buffers,
            view.cutout_buffers);

        // Build Hi-Z pyramid

        downsampler.fill_mip_chain(graph, depth_buffer, hi_z_buffer);

        // Cull all objects against the pyramid, keeping track of newly visible objects

        auto& allocator = backend.get_global_allocator();

        // All the primitives that are visible this frame, whether they're newly visible or not
        const auto buffer_name = fmt::format("Frame {} visibility mask", backend.get_current_gpu_frame());
        const auto this_frame_visible_objects =
            allocator.create_buffer(buffer_name.c_str(), sizeof(uint32_t) * num_primitives, BufferUsage::StorageBuffer);

        // Just the primitives that are visible this frame
        const auto newly_visible_objects = allocator.create_buffer(
            "New visibility mask",
            sizeof(uint32_t) * num_primitives,
            BufferUsage::StorageBuffer);

        graph.add_pass(
        {.name = "HiZ Culling",
         .textures = {{hi_z_buffer,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_READ_BIT,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}},
         .buffers =
         {
             {primitive_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT},
             {view.visible_objects, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT},
             {newly_visible_objects, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT},
             {this_frame_visible_objects, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT},
         },
         .execute = [&](CommandBuffer& commands) {
             const auto& texture_descriptor_pool = backend.get_texture_descriptor_pool();
             commands.bind_descriptor_set(0, texture_descriptor_pool.get_descriptor_set());

             commands.bind_buffer_reference(0, primitive_buffer);
             commands.bind_buffer_reference(2, view.visible_objects);
             commands.bind_buffer_reference(4, newly_visible_objects);
             commands.bind_buffer_reference(6, this_frame_visible_objects);

             commands.bind_buffer_reference(8, view_constant_buffer);

             commands.set_push_constant(10, num_primitives);
             commands.set_push_constant(11, hi_z_index);
             commands.set_push_constant(12, static_cast<float>(hi_z_buffer->get_resolution().x));
             commands.set_push_constant(13, static_cast<float>(hi_z_buffer->get_resolution().y));
             commands.set_push_constant(14, static_cast<float>(hi_z_buffer->create_info.mipLevels));

             commands.bind_pipeline(hi_z_culling_shader);

             commands.dispatch((num_primitives + 95) / 96, 1, 1);

             commands.clear_descriptor_set(0);
         }});

        // Save the list of visible objects so we can use them next frame
        allocator.destroy_buffer(view.visible_objects);
        view.visible_objects = this_frame_visible_objects;

        // Generate drawcall buffers for all visible objects
        generate_drawcall_buffers(graph, world, view);

        // Generate drawcall buffers for newly-visible objects
        const auto new_solid_drawcalls =
            translate_visibility_list_to_draw_commands(graph,
                                                       view.visible_objects,
                                                       primitive_buffer,
                                                       num_primitives,
                                                       world.get_mesh_storage().get_draw_args_buffer(),
                                                       PRIMITIVE_FLAG_SOLID);

        const auto new_cutout_drawcalls =
            translate_visibility_list_to_draw_commands(graph,
                                                       view.visible_objects,
                                                       primitive_buffer,
                                                       num_primitives,
                                                       world.get_mesh_storage().get_draw_args_buffer(),
                                                       PRIMITIVE_FLAG_CUTOUT);

        // Draw ONLY newly-visible objects
        draw_visible_objects(
            graph,
            world,
            view_descriptor,
            masked_view_descriptor,
            new_solid_drawcalls,
            new_cutout_drawcalls
            );

        // cleanup
        allocator.destroy_buffer(new_solid_drawcalls.commands);
        allocator.destroy_buffer(new_solid_drawcalls.count);
        allocator.destroy_buffer(new_solid_drawcalls.primitive_ids);
        allocator.destroy_buffer(new_cutout_drawcalls.commands);
        allocator.destroy_buffer(new_cutout_drawcalls.count);
        allocator.destroy_buffer(new_cutout_drawcalls.primitive_ids);
        allocator.destroy_buffer(newly_visible_objects);

        graph.end_label();
    }

    TextureHandle DepthCullingPhase::get_depth_buffer() const {
        return depth_buffer;
    }

    void DepthCullingPhase::generate_drawcall_buffers(RenderGraph& graph, const RenderWorld& world, SceneView& view) {
        const auto primitive_buffer = world.get_primitive_buffer();
        const auto num_primitives = world.get_total_num_primitives();

        auto& allocator = RenderBackend::get().get_global_allocator();
        allocator.destroy_buffer(view.solid_buffers.commands);
        allocator.destroy_buffer(view.solid_buffers.count);
        allocator.destroy_buffer(view.solid_buffers.primitive_ids);
        allocator.destroy_buffer(view.cutout_buffers.commands);
        allocator.destroy_buffer(view.cutout_buffers.count);
        allocator.destroy_buffer(view.cutout_buffers.primitive_ids);

        graph.begin_label("DepthCullingPhase::generate_drawcall_buffers");

        view.solid_buffers =
            translate_visibility_list_to_draw_commands(graph,
                                                       view.visible_objects,
                                                       primitive_buffer,
                                                       num_primitives,
                                                       world.get_mesh_storage().get_draw_args_buffer(),
                                                       PRIMITIVE_FLAG_SOLID);

        view.cutout_buffers =
            translate_visibility_list_to_draw_commands(graph,
                                                       view.visible_objects,
                                                       primitive_buffer,
                                                       num_primitives,
                                                       world.get_mesh_storage().get_draw_args_buffer(),
                                                       PRIMITIVE_FLAG_CUTOUT);

        graph.end_label();
    }

    void DepthCullingPhase::draw_visible_objects(RenderGraph& graph, const RenderWorld& world,
                                                 const DescriptorSet& view_descriptor,
                                                 const DescriptorSet& masked_view_descriptor,
                                                 const IndirectDrawingBuffers& solid_drawcalls,
                                                 const IndirectDrawingBuffers& cutout_drawcalls
        ) const {
        const auto& pipelines = world.get_material_storage().get_pipelines();
        const auto depth_pso = pipelines.get_depth_pso();
        const auto& masked_pso = pipelines.get_depth_masked_pso();

        // Draw the visible objects

        auto buffers = BufferUsageList{};
        if(solid_drawcalls.commands != nullptr) {
            buffers.emplace_back(solid_drawcalls.commands,
                                 VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                                 VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
            buffers.emplace_back(solid_drawcalls.count,
                                 VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                                 VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
            buffers.emplace_back(solid_drawcalls.primitive_ids,
                                 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                                 VK_ACCESS_2_SHADER_READ_BIT);
        }
        if(cutout_drawcalls.commands != nullptr) {
            buffers.emplace_back(cutout_drawcalls.commands,
                                 VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                                 VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
            buffers.emplace_back(cutout_drawcalls.count,
                                 VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                                 VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
            buffers.emplace_back(cutout_drawcalls.primitive_ids,
                                 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                                 VK_ACCESS_2_SHADER_READ_BIT);
        }

        graph.add_render_pass(
        {
            .name = "Rasterize visible objects",
            .buffers = buffers,
            .descriptor_sets = {view_descriptor, masked_view_descriptor},
            .depth_attachment = RenderingAttachmentInfo{
                .image = depth_buffer,
                .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .clear_value = {.depthStencil = {.depth = 0.0}}},
            .execute = [&](CommandBuffer& commands) {
                if(solid_drawcalls.commands != nullptr) {
                    commands.bind_descriptor_set(0, view_descriptor);
                    world.draw_opaque(commands, solid_drawcalls, depth_pso);
                }

                if(cutout_drawcalls.commands != nullptr) {
                    commands.bind_descriptor_set(0, masked_view_descriptor);
                    world.draw_masked(commands, cutout_drawcalls, masked_pso);
                }

                commands.clear_descriptor_set(0);
            }});
    }
} // namespace render

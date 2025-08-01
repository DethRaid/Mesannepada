#include "ui_phase.hpp"

#include "console/cvars.hpp"
#include "render/backend/command_buffer.hpp"
#include "render/sarah_renderer.hpp"

namespace render {
    constexpr static uint32_t MAX_IMGUI_INDICES = 65535;
    constexpr static uint32_t MAX_IMGUI_VERTICES = 65535;

    enum class Tonemapper {
        Uchimura,
        Uncharted2,
        Filmic,
        PBRNeutral,
        Reinhart,
        Jodie,
        None
    };

    static auto cvar_tonemapper = AutoCVar_Enum{"r.Tonemapper", "Which tonemapper to use", Tonemapper::Uchimura};

    UiPhase::UiPhase() :
        scene_color{RenderBackend::get().get_white_texture_handle()} {
        create_pipelines();

        auto& allocator = RenderBackend::get().get_global_allocator();

        vertex_buffer = allocator.create_buffer(
            "ImGUI vertex buffer",
            sizeof(ImDrawVert) * MAX_IMGUI_VERTICES,
            BufferUsage::VertexBuffer
            );
        index_buffer = allocator.create_buffer(
            "ImGUI index buffer",
            sizeof(ImDrawIdx) * MAX_IMGUI_INDICES,
            BufferUsage::IndexBuffer
            );

        bilinear_sampler = allocator.get_sampler(
            {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .maxLod = VK_LOD_CLAMP_NONE,
            }
            );
    }

    void UiPhase::set_resources(const TextureHandle scene_color_in, const glm::uvec2 render_resolution_in) {
        scene_color = scene_color_in;

        render_resolution = render_resolution_in;
    }

    void UiPhase::add_data_upload_passes(ResourceUploadQueue& queue) const {
        ZoneScoped;

        if(imgui_draw_data->TotalIdxCount > MAX_IMGUI_INDICES || imgui_draw_data->TotalVtxCount > MAX_IMGUI_VERTICES) {
            throw std::runtime_error{"Too many ImGUI elements! Draw less UI please"};
        }

        if(imgui_draw_data->TotalIdxCount == 0 || imgui_draw_data->TotalVtxCount == 0) {
            return;
        }

        auto indices_byte_offset = 0u;
        auto vertices_bytes_offset = 0u;

        for(const auto* imgui_command_list : eastl::span{
                imgui_draw_data->CmdLists.begin(), static_cast<size_t>(imgui_draw_data->CmdLists.size())
            }) {
            queue.upload_to_buffer(
                index_buffer,
                eastl::span{
                    imgui_command_list->IdxBuffer.Data, static_cast<size_t>(imgui_command_list->IdxBuffer.size())
                },
                indices_byte_offset
                );
            queue.upload_to_buffer(
                vertex_buffer,
                eastl::span{
                    imgui_command_list->VtxBuffer.Data, static_cast<size_t>(imgui_command_list->VtxBuffer.size())
                },
                vertices_bytes_offset
                );

            indices_byte_offset += imgui_command_list->IdxBuffer.size_in_bytes();
            vertices_bytes_offset += imgui_command_list->VtxBuffer.size_in_bytes();
        }
    }

    void UiPhase::render(
        CommandBuffer& commands, const SceneView& view, const TextureHandle bloom_texture,
        const TextureHandle average_exposure, rmlui::Renderer& ui_renderer
        ) const {
        commands.begin_label("UiPhase::render");

        draw_scene_image(commands, bloom_texture, average_exposure);

        ui_renderer.render_and_reset(commands, {{0, 0}, {render_resolution.x, render_resolution.y}});

        render_imgui_items(commands);

        commands.end_label();
    }

    void UiPhase::set_imgui_draw_data(ImDrawData* im_draw_data) {
        imgui_draw_data = im_draw_data;
    }

    void UiPhase::draw_scene_image(
        CommandBuffer& commands, const TextureHandle bloom_texture, const TextureHandle average_exposure
        ) const {
        auto& backend = RenderBackend::get();

        const auto set = backend.get_transient_descriptor_allocator().build_set(tonemapping_pipeline, 0)
                                .bind(scene_color, bilinear_sampler)
                                .bind(bloom_texture, bilinear_sampler)
                                .bind(average_exposure)
                                .build();

        commands.bind_descriptor_set(0, set);

        commands.set_push_constant(0, static_cast<uint32_t>(cvar_tonemapper.get()));

        commands.bind_pipeline(tonemapping_pipeline);

        commands.draw_triangle();

        commands.clear_descriptor_set(0);
    }

    void UiPhase::render_imgui_items(CommandBuffer& commands) const {
        if(imgui_draw_data->TotalIdxCount == 0) {
            return;
        }

        commands.bind_vertex_buffer(0, vertex_buffer);
        commands.bind_index_buffer<ImDrawIdx>(index_buffer);

        commands.set_push_constant(0, render_resolution.x);
        commands.set_push_constant(1, render_resolution.y);

        commands.bind_pipeline(imgui_pipeline);

        auto first_vertex = 0u;
        auto first_index = 0u;

        for(const auto* imgui_command_list : eastl::span{
                imgui_draw_data->CmdLists.begin(), static_cast<size_t>(imgui_draw_data->CmdLists.size())
            }) {
            const auto display_pos = glm::vec2{imgui_draw_data->DisplayPos.x, imgui_draw_data->DisplayPos.y};

            for(const auto& cmd : imgui_command_list->CmdBuffer) {
                const auto scissor_start = glm::vec2{cmd.ClipRect.x, cmd.ClipRect.y} - display_pos;
                const auto scissor_end = glm::vec2{cmd.ClipRect.z, cmd.ClipRect.w} - display_pos;
                commands.set_scissor_rect(glm::max(scissor_start, float2{0}), scissor_end);

                if(cmd.GetTexID() != 0) {
                    commands.bind_descriptor_set(0,
                                                 reinterpret_cast<VkDescriptorSet>(static_cast<intptr_t>(cmd.
                                                     GetTexID())));
                    commands.set_push_constant(2, 1u);
                } else {
                    commands.set_push_constant(2, 0u);
                }

                if(cmd.UserCallback) {
                    cmd.UserCallback(imgui_command_list, &cmd);
                } else {
                    commands.draw_indexed(cmd.ElemCount,
                                          1,
                                          cmd.IdxOffset + first_index,
                                          cmd.VtxOffset + first_vertex,
                                          0);
                }
            }

            first_index += imgui_command_list->IdxBuffer.size();
            first_vertex += imgui_command_list->VtxBuffer.size();
        }

        commands.clear_descriptor_set(0);
    }

    void UiPhase::create_pipelines() {
        const auto& backend = RenderBackend::get();
        tonemapping_pipeline = backend
                               .begin_building_pipeline("Scene Upscale")
                               .set_vertex_shader("shader://common/fullscreen.vert.spv"_res)
                               .set_fragment_shader("shader://ui/tonemapping.frag.spv"_res)
                               .set_depth_state(
                                   DepthStencilState{
                                       .enable_depth_test = false,
                                       .enable_depth_write = false,
                                   }
                                   )
                               .build();

        imgui_pipeline = backend
                         .begin_building_pipeline("ImGUI")
                         .use_imgui_vertex_layout()
                         .set_vertex_shader("shader://ui/imgui.vert.spv"_res)
                         .set_fragment_shader("shader://ui/imgui.frag.spv"_res)
                         .set_depth_state({.enable_depth_test = false, .enable_depth_write = false})
                         .set_blend_state(
                             0,
                             {
                                 .blendEnable = VK_TRUE,
                                 .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                                 .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                 .colorBlendOp = VK_BLEND_OP_ADD,
                                 .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                                 .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                 .alphaBlendOp = VK_BLEND_OP_ADD,
                                 .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
                             }
                             )
                         .build();
    }
}

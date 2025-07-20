#include "rmlui_render_interface.hpp"

#include "core/issue_breakpoint.hpp"
#include "core/system_interface.hpp"
#include "render/texture_loader.hpp"
#include "render/backend/command_buffer.hpp"
#include "render/backend/render_backend.hpp"
#include "resources/resource_path.hpp"

namespace rmlui {
    constexpr static VkDeviceSize MAX_NUM_VERTICES = 65536;
    constexpr static VkDeviceSize MAX_NUM_INDICES = 65536;

    static std::shared_ptr<spdlog::logger> logger;

    Renderer::Renderer(render::TextureLoader& texture_loader_in) : texture_loader{texture_loader_in} {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("rmlui::Renderer");
        }

        auto& backend = render::RenderBackend::get();

        constexpr auto vertex_block_create_info = VmaVirtualBlockCreateInfo{
            .size = MAX_NUM_VERTICES,
        };
        vmaCreateVirtualBlock(&vertex_block_create_info, &vertex_block);

        constexpr auto index_block_create_info = VmaVirtualBlockCreateInfo{
            .size = MAX_NUM_INDICES,
        };
        vmaCreateVirtualBlock(&index_block_create_info, &index_block);

        auto& allocator = render::RenderBackend::get().get_global_allocator();
        vertex_buffer = allocator.create_buffer(
            "rmlui_vertices",
            sizeof(Rml::Vertex) * MAX_NUM_VERTICES,
            render::BufferUsage::VertexBuffer);
        index_buffer = allocator.create_buffer(
            "rmlui_indices",
            sizeof(uint32_t) * MAX_NUM_INDICES,
            render::BufferUsage::IndexBuffer);

        draws.reserve(1024);

        linear_sampler = render::RenderBackend::get().get_global_allocator().get_sampler(
            {
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR
            });

        ui_pipeline = backend.begin_building_pipeline("rmlui")
                             .use_rmlui_vertex_layout()
                             .set_vertex_shader("shader://ui/rmlui.vert.spv"_res)
                             .set_fragment_shader("shader://ui/rmlui.frag.spv"_res)
                             .set_depth_state({.enable_depth_test = false, .enable_depth_write = false})
                             .set_blend_state(
                                 0,
                                 {
                                     .blendEnable = VK_TRUE,
                                     .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                                     .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                     .colorBlendOp = VK_BLEND_OP_ADD,
                                     .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                     .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                     .alphaBlendOp = VK_BLEND_OP_ADD,
                                     .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
                                 })
                             .build();
    }

    Rml::CompiledGeometryHandle Renderer::CompileGeometry(
        const Rml::Span<const Rml::Vertex> vertices, const Rml::Span<const int> indices
    ) {
        auto mesh = UIMesh{
            .num_indices = static_cast<uint32_t>(indices.size())
        };

        const auto vertex_allocate_info = VmaVirtualAllocationCreateInfo{
            .size = vertices.size(),
        };
        auto result = vmaVirtualAllocate(
            vertex_block,
            &vertex_allocate_info,
            &mesh.vertex_allocation,
            &mesh.first_vertex);
        if(result != VK_SUCCESS) {
            SAH_BREAKPOINT;
            return 0;
        }

        const auto index_allocate_info = VmaVirtualAllocationCreateInfo{
            .size = indices.size(),
        };
        result = vmaVirtualAllocate(index_block, &index_allocate_info, &mesh.index_allocation, &mesh.first_index);
        if(result != VK_SUCCESS) {
            vmaVirtualFree(vertex_block, mesh.vertex_allocation);

            SAH_BREAKPOINT;
            return 0;
        }

        auto& backend = render::RenderBackend::get();
        backend.get_upload_queue().upload_to_buffer(
            vertex_buffer,
            eastl::span{vertices.data(), vertices.size()},
            static_cast<uint32_t>(mesh.first_vertex * sizeof(Rml::Vertex)));
        backend.get_upload_queue().upload_to_buffer(
            index_buffer,
            eastl::span{indices.data(), indices.size()},
            static_cast<uint32_t>(mesh.first_index * sizeof(uint32_t)));

        return reinterpret_cast<Rml::CompiledGeometryHandle>(&*meshes.insert(mesh));
    }

    void Renderer::RenderGeometry(
        const Rml::CompiledGeometryHandle geometry, const Rml::Vector2f translation, const Rml::TextureHandle texture
    ) {
        const auto texture_index = texture != 0
            ? reinterpret_cast<UITexture*>(texture)->srv_index
            : render::RenderBackend::get().get_white_texture_srv();
        draws.emplace_back(
            Drawcall{
                .mesh = reinterpret_cast<UIMesh*>(geometry),
                .translation = {translation.x, translation.y},
                .texture_index = texture_index,
                .scissor = scissor
            });
    }

    void Renderer::ReleaseGeometry(const Rml::CompiledGeometryHandle geometry) {
        auto* mesh = reinterpret_cast<UIMesh*>(geometry);
        vmaVirtualFree(vertex_block, mesh->vertex_allocation);
        vmaVirtualFree(index_block, mesh->index_allocation);

        const auto mesh_itr = meshes.get_iterator(mesh);
        if(mesh_itr != meshes.end()) {
            meshes.erase(mesh_itr);
        }
    }

    Rml::TextureHandle Renderer::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
        const auto texture_maybe = texture_loader.load_texture(ResourcePath::file(source), render::TextureType::Color);
        if(!texture_maybe) {
            logger->error("Could not load texture {}", source);
            return 0;
        }
        const auto texture = *texture_maybe;
        texture_dimensions.x = texture->get_resolution().x;
        texture_dimensions.y = texture->get_resolution().y;

        auto ui_texture = UITexture{
            .texture = texture,
            .srv_index = render::RenderBackend::get().get_texture_descriptor_pool().create_texture_srv(texture, linear_sampler)
        };

        return reinterpret_cast<Rml::TextureHandle>(&*textures.emplace(ui_texture));
    }

    Rml::TextureHandle Renderer::GenerateTexture(
        const Rml::Span<const unsigned char> source, const Rml::Vector2i source_dimensions
    ) {
        auto& backend = render::RenderBackend::get();

        const auto texture = backend.get_global_allocator().create_texture(
            "rmlui_generated_texture",
            {
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .resolution = {source_dimensions.x, source_dimensions.y},
                .usage = render::TextureUsage::StaticImage
            }
        );

        backend.get_upload_queue().enqueue(
            render::TextureUploadJob{
                .destination = texture,
                .mip = 0,
                .data = {source.begin(), source.end()}
            });

        auto ui_texture = UITexture{
            .texture = texture,
            .srv_index = backend.get_texture_descriptor_pool().create_texture_srv(texture, linear_sampler)
        };
        return reinterpret_cast<Rml::TextureHandle>(&*textures.emplace(ui_texture));
    }

    void Renderer::ReleaseTexture(const Rml::TextureHandle texture) {
        auto* my_texture = reinterpret_cast<UITexture*>(texture);
        render::RenderBackend::get().get_global_allocator().destroy_texture(my_texture->texture);
        render::RenderBackend::get().get_texture_descriptor_pool().free_descriptor(my_texture->srv_index);

        const auto texture_itr = textures.get_iterator(my_texture);
        if(texture_itr != textures.end()) {
            textures.erase(texture_itr);
        }
    }

    void Renderer::EnableScissorRegion(const bool enable) {
        if(!enable) {
            scissor = eastl::nullopt;
        }
    }

    void Renderer::SetScissorRegion(const Rml::Rectanglei region) {
        scissor = VkRect2D{
            .offset = {region.Position().x, region.Position().y},
            .extent = {static_cast<uint32_t>(region.Width()), static_cast<uint32_t>(region.Height())}
        };
    }

    void Renderer::render_and_reset(render::CommandBuffer& commands, const VkRect2D render_area) {
        commands.begin_label("rmlui");

        commands.bind_index_buffer(index_buffer);
        commands.bind_vertex_buffer(0, vertex_buffer);

        commands.set_cull_mode(VK_CULL_MODE_NONE);

        commands.bind_pipeline(ui_pipeline);

        commands.bind_descriptor_set(0, render::RenderBackend::get().get_texture_descriptor_pool().get_descriptor_set());

        commands.set_push_constant(0, static_cast<float>(render_area.extent.width));
        commands.set_push_constant(1, static_cast<float>(render_area.extent.height));

        for(const auto& drawcall : draws) {
            if(drawcall.scissor) {
                commands.set_scissor_rect(*drawcall.scissor);
            } else {
                commands.set_scissor_rect(render_area);
            }

            commands.set_push_constant(2, drawcall.translation.x);
            commands.set_push_constant(3, drawcall.translation.y);
            commands.set_push_constant(4, drawcall.texture_index);

            commands.draw_indexed(
                drawcall.mesh->num_indices,
                1,
                drawcall.mesh->first_index,
                drawcall.mesh->first_vertex,
                0);
        }

        commands.end_label();

        draws.clear();
    }

    render::BufferHandle Renderer::get_vertex_buffer() const {
        return vertex_buffer;
    }

    render::BufferHandle Renderer::get_index_buffer() const {
        return index_buffer;
    }
}

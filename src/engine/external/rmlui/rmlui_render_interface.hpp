#pragma once

#include <plf_colony.h>
#include <vk_mem_alloc.h>
#include <RmlUi/Core/RenderInterface.h>

#include <EASTL/optional.h>
#include <EASTL/vector.h>

#include "external/rmlui/rmlui_vertex.hpp"
#include "render/backend/handles.hpp"

namespace render {
    class CommandBuffer;
    class TextureLoader;
    class MeshStorage;
}

namespace rmlui {
    /**
     * A RmlUI mesh that can be rendered later
     */
    struct UIMesh {
        VkDeviceSize first_vertex;
        VkDeviceSize first_index;
        uint32_t num_indices;

        VmaVirtualAllocation vertex_allocation = {};

        VmaVirtualAllocation index_allocation = {};
    };

    struct UITexture {
        render::TextureHandle texture;
        uint32_t srv_index;
    };

    struct Drawcall {
        UIMesh* mesh;
        float2 translation;
        uint32_t texture_index;
        eastl::optional<VkRect2D> scissor = eastl::nullopt;
    };

    static_assert(sizeof(Vertex) == sizeof(Rml::Vertex));

    class Renderer : public Rml::RenderInterface {
    public:
        Renderer(render::TextureLoader& texture_loader_in);

        Rml::CompiledGeometryHandle CompileGeometry(
            Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices
        ) override;

        void RenderGeometry(
            Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture
        ) override;

        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

        Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;

        Rml::TextureHandle GenerateTexture(
            Rml::Span<const unsigned char> source, Rml::Vector2i source_dimensions
        ) override;

        void ReleaseTexture(Rml::TextureHandle texture) override;

        void EnableScissorRegion(bool enable) override;

        void SetScissorRegion(Rml::Rectanglei region) override;

        void render_and_reset(render::CommandBuffer& commands, VkRect2D render_area);

        render::BufferHandle get_vertex_buffer() const;

        render::BufferHandle get_index_buffer() const;

    private:
        render::TextureLoader& texture_loader;

        plf::colony<UIMesh> meshes;

        plf::colony<UITexture> textures;

        eastl::vector<Drawcall> draws;

        eastl::optional<VkRect2D> scissor = eastl::nullopt;

        VmaVirtualBlock vertex_block = {};
        render::BufferHandle vertex_buffer;

        VmaVirtualBlock index_block = {};
        render::BufferHandle index_buffer;

        VkSampler linear_sampler;

        render::GraphicsPipelineHandle ui_pipeline;
    };
}

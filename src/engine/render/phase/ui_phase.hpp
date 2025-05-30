#pragma once

#include <imgui.h>
#include <glm/vec2.hpp>

#include "render/backend/handles.hpp"
#include "render/backend/graphics_pipeline.hpp"
#include "render/backend/resource_upload_queue.hpp"

namespace rmlui {
    class Renderer;
}

namespace render {
    class CommandBuffer;
    class SceneView;
    class SarahRenderer;

    /**
     * Renders the upscaled scene image and full-res postprocessing effects, and also the UI
     *
     * Not a great class name
     */
    class UiPhase {
    public:
        explicit UiPhase();

        void set_resources(TextureHandle scene_color_in, glm::uvec2 render_resolution_in);

        void add_data_upload_passes(ResourceUploadQueue& queue) const;

        void render(
            CommandBuffer& commands, const SceneView& view, TextureHandle bloom_texture, TextureHandle average_exposure,
            rmlui::Renderer& ui_renderer
        ) const;

        void set_imgui_draw_data(ImDrawData* im_draw_data);

    private:
        TextureHandle scene_color = nullptr;

        glm::uvec2 render_resolution;

        VkSampler bilinear_sampler;

        ImDrawData* imgui_draw_data = nullptr;

        BufferHandle index_buffer;
        BufferHandle vertex_buffer;

        void create_pipelines();

        void draw_scene_image(CommandBuffer& commands, TextureHandle bloom_texture, TextureHandle average_exposure) const;

        void render_imgui_items(CommandBuffer& commands) const;

        GraphicsPipelineHandle tonemapping_pipeline;

        GraphicsPipelineHandle imgui_pipeline;
    };
}

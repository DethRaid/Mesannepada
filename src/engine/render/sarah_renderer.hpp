#pragma once

#include <imgui.h>

#include <EASTL/unique_ptr.h>

#include "external/rmlui/rmlui_render_interface.hpp"
#include "phase/ray_tracing_debug_phase.hpp"
#include "render/antialiasing_type.hpp"
#include "render/bloomer.hpp"
#include "render/material_storage.hpp"
#include "render/mesh_storage.hpp"
#include "render/mip_chain_generator.hpp"
#include "render/noise_texture.hpp"
#include "render/procedural_sky.hpp"
#include "render/texture_loader.hpp"
#include "render/denoiser/nvidia_realtime_denoiser.hpp"
#include "render/gi/light_propagation_volume.hpp"
#include "render/phase/ambient_occlusion_phase.hpp"
#include "render/phase/depth_culling_phase.hpp"
#include "render/phase/gbuffer_phase.hpp"
#include "render/phase/lighting_phase.hpp"
#include "render/phase/motion_vectors_phase.hpp"
#include "render/phase/sampling_rate_calculator.hpp"
#include "render/phase/ui_phase.hpp"
#include "render/upscaling/upscaler.hpp"
#include "ui/debug_menu.hpp"
#include "visualizers/jolt_debug_renderer.hpp"
#include "visualizers/visualizer_type.hpp"

namespace render {
    /**
     * How to calculate global illumination
     */
    enum class GIMode {
        /**
         * No GI
         */
        Off,
        /**
         * Light Propagation Volume, a la Crytek 2009. This leaks a ton because I don't have the geometry volume working, but it's vaguely plausible and very fast
         */
        LPV,
        /**
         * Ray traced global illumination - per-pixel short-range RT, with irradiance probes for longer-range light
         */
        RT
    };

    /**
     * Sarah's artisanal handcrafted renderer
     */
    class SarahRenderer {
    public:
        explicit SarahRenderer();

        /**
         * Sets the internal render resolution of the scene, recreating the internal render targets (and framebuffers)
         *
         * @param new_output_resolution Resolution to render at
         */
        void set_output_resolution(const glm::uvec2& new_output_resolution);

        void set_scene(RenderScene& scene_in);

        /**
         * Do the thing!
         */
        void render();

        TextureLoader& get_texture_loader();

        MaterialStorage& get_material_storage();

        MeshStorage& get_mesh_storage();

        void set_imgui_commands(ImDrawData* im_draw_data);

        RenderVisualization get_active_visualizer() const;

        void set_active_visualizer(RenderVisualization visualizer);

        Rml::RenderInterface* get_rmlui_renderer();

#ifdef JPH_DEBUG_RENDERER
        JoltDebugRenderer* get_jolt_debug_renderer() const;
#endif

    private:
        TextureLoader texture_loader;

        rmlui::Renderer rmlui_renderer;

        MaterialStorage material_storage;

        MeshStorage meshes;

        MipChainGenerator mip_chain_generator;

        Bloomer bloomer;

        RenderScene* scene = nullptr;

        glm::uvec2 output_resolution = {};

        glm::uvec2 scene_render_resolution = glm::uvec2{};

        /**
         * Spatio-temporal blue noise texture, containing 3D vectors in a unit sphere
         */
        NoiseTexture stbn_3d_unitvec;

        /**
         * Spatio-temporal blue noise texture, pairs of random scalars
         */
        NoiseTexture stbn_2d_scalar;

        GIMode cached_gi_mode = GIMode::Off;

        eastl::unique_ptr<IGlobalIlluminator> gi;

        GBuffer gbuffer = {};

        TextureHandle ao_handle = nullptr;

        TextureHandle lit_scene_handle = nullptr;

        TextureHandle antialiased_scene_handle = nullptr;

        eastl::vector<TextureHandle> swapchain_images;

        /**
         * \brief Screen-space camera jitter applied to this frame
         */
        glm::vec2 jitter = {};
        glm::vec2 previous_jitter = {};

        DepthCullingPhase depth_culling_phase;

        MotionVectorsPhase motion_vectors_phase;

        GbufferPhase gbuffer_phase;

        AmbientOcclusionPhase ao_phase;

        LightingPhase lighting_pass;

        RayTracingDebugPhase rt_debug_phase;

        eastl::unique_ptr<VRSAA> vrsaa;

        UiPhase ui_phase;

        RenderVisualization active_visualization = RenderVisualization::None;

        eastl::unique_ptr<IUpscaler> upscaler;

        AntiAliasingType cached_aa = AntiAliasingType::None;

        uint32_t frame_count = 0;

        void set_render_resolution(glm::uvec2 new_render_resolution);

        void create_scene_render_targets();

        void update_jitter();

#ifdef JPH_DEBUG_RENDERER
        eastl::unique_ptr<JoltDebugRenderer> jolt_debug;
#endif

        void draw_debug_visualizers(RenderGraph& render_graph);

        /**
         * Simple fullscreen triangle PSO to copy one sampled image to a render target
         */
        GraphicsPipelineHandle copy_scene_pso;

        VkSampler linear_sampler;

        BufferHandle luminance_histogram = nullptr;

        TextureHandle average_exposure = nullptr;

        TextureHandle exposure_factor = nullptr;

        ComputePipelineHandle exposure_histogram_pipeline;

        ComputePipelineHandle average_exposure_pipeline;

        void deform_skinned_meshes(RenderGraph& graph);

        /**
         * Measures the exposure of the lit scene target, saving to a 1x1 texture since that's what DLSS and friends want
         */
        void measure_exposure(RenderGraph& render_graph);

        void evaluate_antialiasing(RenderGraph& render_graph, TextureHandle gbuffer_depth_handle) const;
    };
}

#pragma once

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

#include "global_illuminator.hpp"
#include "render/backend/handles.hpp"

namespace render {
    class ResourceUploadQueue;
    struct DescriptorSet;
    class RenderGraph;
    class RenderBackend;
    class ResourceAllocator;
    class CommandBuffer;
    class RenderWorld;
    class SceneView;
    class DirectionalLight;
    class MeshStorage;

    struct CascadeData {
        /**
         * World to cascade matrix. Does not contain a NDC -> UV conversion
         */
        glm::mat4 world_to_cascade = glm::mat4{ 1 };

        /**
         * VP matrix to use when rendering the RSM
         */
        glm::mat4 rsm_vp = glm::mat4{ 1 };

        /**
         * Buffer that stores the count of the VPLs in this cascade
         *
         * The buffer is as big as a non-indexed drawcall
         */
        BufferHandle vpl_count_buffer = {};

        /**
         * VPLs in this cascade
         */
        BufferHandle vpl_buffer = {};

        glm::vec3 min_bounds;
        glm::vec3 max_bounds;
    };

    enum class GvBuildMode {
        Off,
        DepthBuffers,
    };

    /**
     * A light propagation volume, a la Crytek
     *
     * https://www.advances.realtimerendering.com/s2009/Light_Propagation_Volumes.pdf
     *
     * This is actually cascaded LPVs, but I couldn't find that paper
     *
     * Each cascade is 2x as large as the previous cascade, but has the same number of cells
     */
    class LightPropagationVolume : public IGlobalIlluminator {
    public:
        explicit LightPropagationVolume();

        ~LightPropagationVolume() override;

        void pre_render(
            RenderGraph& graph, const SceneView& view, const RenderWorld& world, TextureHandle noise_tex
        ) override;

        void post_render(
            RenderGraph& graph, const SceneView& view, const RenderWorld& world, const GBuffer& gbuffer,
             TextureHandle motion_vectors, TextureHandle noise_tex
        ) override;

        void get_lighting_resource_usages(
            TextureUsageList& textures, BufferUsageList& buffers
        ) const override;

        void render_to_lit_scene(
            CommandBuffer& commands, BufferHandle view_buffer, BufferHandle sun_buffer, TextureHandle ao_tex,
            TextureHandle noise_tex
        ) const override;

        void render_volumetrics(
            RenderGraph& render_graph, const SceneView& player_view, const RenderWorld& world, const GBuffer& gbuffer,
            TextureHandle lit_scene_handle
        ) override;

        void draw_debug_overlays(
            RenderGraph& graph, const SceneView& view, const GBuffer& gbuffer, TextureHandle lit_scene_texture
        ) override;

    private:
        // RSM render targets. Each is an array texture with one layer per cascade
        TextureHandle rsm_flux_target;
        TextureHandle rsm_normals_target;
        TextureHandle rsm_depth_target;

        // We have a A and B LPV, to allow for ping-ponging during the propagation step

        // Each LPV has a separate texture for the red, green, and blue SH coefficients
        // We store coefficients for the first two SH bands. Future work might add another band, at the cost of 2x the
        // memory

        TextureHandle lpv_a_red = nullptr;
        TextureHandle lpv_a_green = nullptr;
        TextureHandle lpv_a_blue = nullptr;

        TextureHandle lpv_b_red = nullptr;
        TextureHandle lpv_b_green = nullptr;
        TextureHandle lpv_b_blue = nullptr;

        TextureHandle geometry_volume_handle = nullptr;

        VkSampler linear_sampler = VK_NULL_HANDLE;

        ComputePipelineHandle rsm_generate_vpls_pipeline;

        ComputePipelineHandle clear_lpv_shader;

        GraphicsPipelineHandle vpl_injection_pipeline;

        ComputePipelineHandle vpl_injection_compute_pipeline;

        ComputePipelineHandle propagation_shader;

        eastl::vector<CascadeData> cascades;
        BufferHandle cascade_data_buffer = {};

        /**
         * Buffer of the cascade matrices in an array
         */
        BufferHandle vp_matrix_buffer = nullptr;

        /**
         * Renders the LPV into the lighting buffer
         */
        GraphicsPipelineHandle lpv_render_shader;

        static inline GraphicsPipelineHandle fog_pipeline = nullptr;

        static inline GraphicsPipelineHandle gv_visualization_pipeline = nullptr;

        /**
         * Renders a visualization of each VPL
         *
         * Takes in a list of VPLs. A geometry shader generates a quad for each, then the fragment shader draws a sphere
         * with the VPL's light on the surface
         */
        GraphicsPipelineHandle vpl_visualization_pipeline;

        GraphicsPipelineHandle inject_rsm_depth_into_gv_pipeline;
        GraphicsPipelineHandle inject_scene_depth_into_gv_pipeline;

        void init_resources(ResourceAllocator& allocator);

        void update_buffers() const;

        /**
         * Updates the transform of this LPV to match the scene view
         */
        void update_cascade_transforms(const SceneView& view, const DirectionalLight& light);

        void clear_volume(RenderGraph& render_graph) const;

        static GvBuildMode get_build_mode();

        /**
         * \brief Builds the geometry volume from last frame's depth buffer
         *
         * \param graph Render graph to use
         * \param depth_buffer Scene depth buffer to inject
         * \param normal_target gbuffer normals to inject
         * \param view_uniform_buffer The view uniform buffer that was used to render the depth and normals targets
         */
        void build_geometry_volume_from_scene_view(
            RenderGraph& graph, TextureHandle depth_buffer,
            TextureHandle normal_target, BufferHandle view_uniform_buffer
        ) const;

        void inject_indirect_sun_light(RenderGraph& graph, const RenderWorld& world) const;

        void dispatch_vpl_injection_pass(
            RenderGraph& graph, uint32_t cascade_index, const CascadeData& cascade, BufferHandle sun_light_buffer
        ) const;

        void propagate_lighting(RenderGraph& render_graph) const;

        /**
         * \brief Injects the RSM depth and normals buffers for a given cascade into that cascade's geometry volume
         *
         * This method dispatches one point for each pixel in the depth buffer. The vertex shader reads the depth and
         * normal targets, converts the normals into SH, and dispatches the point to the correct depth layer. The fragment
         * shader simply adds the SH into the cascade target
         *
         * \param graph Render graph to use to perform the work
         * \param cascade Cascade to inject data into
         * \param cascade_index Index of the cascade that we're injecting into
         */
        void inject_rsm_depth_into_cascade_gv(RenderGraph& graph, const CascadeData& cascade, uint32_t cascade_index) const;

        void visualize_geometry_volume(
            RenderGraph& graph, const SceneView& view, TextureHandle lit_scene_texture, TextureHandle depth
        );

        void visualize_vpls(
            RenderGraph& graph, BufferHandle scene_view_buffer, TextureHandle lit_scene, TextureHandle depth_buffer
        );
    };
}
